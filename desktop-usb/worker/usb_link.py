from __future__ import annotations
import base64
from collections import deque
import json
import re
import os
import time
import zlib
import serial
from serial.tools import list_ports

TARGET = os.environ.get("DESKHUB_DEVICE_MAC", "").upper()


class UsbLink:
    def __init__(self, device):
        self.device = device
        self.pending = b""
        self.events = deque()
        self.sequence = 0

    def close(self): self.device.close()

    def write(self, line):
        data = (line+"\n").encode("utf-8")
        if len(data)>510: raise ValueError("USB line exceeds device limit")
        self.device.write(data);self.device.flush()

    def wait(self, prefix, timeout=5):
        end = time.monotonic()+timeout
        while time.monotonic()<end:
            while b"\n" in self.pending:
                raw,self.pending = self.pending.split(b"\n",1)
                line = raw.strip().decode("utf-8",errors="replace")
                if line.startswith("USB_EVENT "):
                    try:self.events.append(json.loads(line[10:]))
                    except ValueError:pass
                if line.startswith(prefix): return line
                if line.startswith("USB_ERROR "): raise RuntimeError("Device rejected USB packet")
            self.pending += self.device.read(self.device.in_waiting or 1)
        raise TimeoutError(prefix)

    def command(self, command, response, timeout=5):
        self.write(command);return self.wait(response,timeout)

    def status(self):
        line=self.command("HUB_STATUS","HUB_STATUS ")
        match=re.search(r"app=(\w+).*heap=(\d+)",line)
        if not match:raise ValueError("Invalid device status")
        uptime=re.search(r"uptime_ms=(\d+)",line)
        return {"app":match[1],"heap":int(match[2]),"uptime":int(uptime[1]) if uptime else 0}

    def packet(self, kind, value):
        data=value if isinstance(value,bytes) else json.dumps(value,ensure_ascii=False,separators=(",",":"),allow_nan=False).encode()
        self.sequence=(self.sequence+1)&0xffffffff
        request=self.sequence
        def send(**fields):
            self.write("HUB_USB "+json.dumps({"id":request,**fields},separators=(",",":")))
        send(op="begin",kind=kind,length=len(data),crc=zlib.crc32(data)&0xffffffff)
        self.wait(f"USB_ACK {request} 0")
        for offset in range(0,len(data),256):
            part=data[offset:offset+256]
            send(op="chunk",offset=offset,data=base64.b64encode(part).decode())
            ack=self.wait(f"USB_ACK {request} ")
            if int(ack.rsplit(" ",1)[1])!=offset+len(part):raise RuntimeError("USB offset mismatch")
        send(op="end")
        return self.wait(f"USB_DONE {request} ",30).endswith(" OK")

    def clock(self,payload):
        self.command("HUB_CLOCK "+json.dumps(payload,separators=(",",":"),allow_nan=False),"CLOCK_OK")

    @classmethod
    def connect(cls):
        ports=[p for p in list_ports.comports() if (p.vid,p.pid) in ((0x303a,0x1001),(0x1a86,0x7523),(0x1a86,0x7522),(0x10c4,0xea60))]
        ports.sort(key=lambda p:p.vid!=0x303a)
        for port in ports:
            dev=serial.Serial(port=None,baudrate=460800,timeout=.03,write_timeout=3)
            dev.dtr=port.vid==0x303a;dev.rts=False;dev.port=port.device
            try:
                dev.open();dev.reset_input_buffer();link=cls(dev)
                response=link.command("HUB_CLOCK_STATUS","CLOCK_STATUS ",3)
                status=json.loads(response[13:])
                mac=str(status.get("device_mac","")).upper()
                if not re.fullmatch(r"(?:[0-9A-F]{2}:){5}[0-9A-F]{2}",mac):raise ValueError("Invalid device identity")
                if TARGET and mac!=TARGET:raise ValueError("Different configured device")
                identity=link.command("HUB_STATUS","HUB_STATUS ",3)
                if "version=1.4." in identity:return link
            except (OSError,ValueError,TimeoutError,serial.SerialException):pass
            if dev.is_open:dev.close()
        raise RuntimeError("No matching DeskHub connected")
