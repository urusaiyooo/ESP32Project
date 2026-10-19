import base64
import json
import unittest
import zlib
from usb_link import UsbLink,PacketCancelled


class FakeDevice:
    def __init__(self):self.pending=b"";self.lines=[];self.data=b"";self.header={};self.bad_offset=False;self.outstanding=0;self.max_outstanding=0
    @property
    def in_waiting(self):return len(self.pending)
    def read(self,n):
        self.outstanding=0
        data=self.pending[:n];self.pending=self.pending[n:];return data
    def flush(self):pass
    def write(self,data):
        for line in data.splitlines(keepends=True):self.line(line)
    def line(self,data):
        self.lines.append(data);value=json.loads(data.decode()[8:]);i=value["id"]
        if value["op"]=="begin":self.header=value;self.data=b"";reply=f"USB_ACK {i} 0\n"
        elif value["op"]=="chunk":
            assert value["offset"]==len(self.data)
            self.data+=base64.b64decode(value["data"])
            reply=f"USB_ACK {i} {len(self.data)+(1 if self.bad_offset else 0)}\n"
            self.outstanding+=1;self.max_outstanding=max(self.max_outstanding,self.outstanding)
        elif value['op']=='abort':
            self.data=b'';reply=f'USB_ABORT {i}\n'
        else:
            assert len(self.data)==self.header["length"] and zlib.crc32(self.data)==self.header["crc"]
            reply=f"USB_DONE {i} OK\n"
        self.pending+=reply.encode()


class ProtocolTests(unittest.TestCase):
    def test_utf8_chunking_and_crc(self):
        device=FakeDevice();link=UsbLink(device)
        self.assertTrue(link.packet("lyrics",{"lrc":"[00:00.00]繁體中文 日本語 简体测试\n"*100}))
        self.assertTrue(all(len(line)<=510 for line in device.lines))
        self.assertIn("日本語",json.loads(device.data)["lrc"])
    def test_binary_bytes_preserved(self):
        device=FakeDevice();self.assertTrue(UsbLink(device).packet("cover",bytes(range(256))*10))
        self.assertEqual(device.data,bytes(range(256))*10)
    def test_bad_ack_is_rejected(self):
        device=FakeDevice();device.bad_offset=True
        with self.assertRaises(RuntimeError):UsbLink(device).packet("cover",b"test")
    def test_windowed_transfer_verifies_all_chunks_and_preserves_crc(self):
        device=FakeDevice();link=UsbLink(device);link.window=8
        value=bytes(range(256))*20+b'final'
        self.assertTrue(link.packet('cover',value))
        self.assertEqual(device.data,value)
        self.assertEqual(device.max_outstanding,8)
        self.assertTrue(all(len(line)<=510 for line in device.lines))
    def test_mid_window_bad_ack_is_rejected(self):
        device=FakeDevice();device.bad_offset=True;link=UsbLink(device);link.window=8
        with self.assertRaises(RuntimeError):link.packet('cover',bytes(range(256))*10)
    def test_obsolete_asset_aborts_and_next_packet_reuses_link(self):
        device=FakeDevice();link=UsbLink(device);link.window=8
        with self.assertRaises(PacketCancelled):
            link.packet('cover',b'x'*9000,cancelled=lambda:len(device.data)>=2048)
        self.assertTrue(any(b'"op":"abort"' in line for line in device.lines))
        self.assertFalse(any(b'"op":"end"' in line for line in device.lines))
        self.assertTrue(link.packet('track',{'track_id':'new'}))
    def test_legacy_firmware_uses_single_chunk_and_no_abort(self):
        device=FakeDevice();link=UsbLink(device)
        self.assertTrue(link.packet('cover',b'x'*900,cancelled=lambda:True))
        self.assertEqual(device.max_outstanding,1)
    def test_control_event_interrupts_asset_without_losing_event(self):
        class EventDevice(FakeDevice):
            def line(self,data):
                super().line(data)
                if len(self.data)==256:
                    self.pending+=b'USB_EVENT {"kind":"music_control","command":"next","id":7}\n'
        device=EventDevice();link=UsbLink(device);link.window=8
        with self.assertRaises(PacketCancelled):
            link.packet('cover',b'x'*9000,cancelled=lambda:bool(link.events))
        self.assertEqual(link.events[0]['command'],'next')
    def test_window_capability_is_bounded_and_missing_capability_is_legacy(self):
        link=UsbLink(FakeDevice())
        for suffix,expected in [(' usb_window=8',8),(' usb_window=999',8),('',1)]:
            link.command=lambda *args:'HUB_STATUS app=music heap=100 uptime_ms=1000'+suffix
            self.assertEqual(link.status()['app'],'music')
            self.assertEqual(link.window,expected)


if __name__=="__main__":unittest.main()
