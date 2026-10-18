import base64
import json
import unittest
import zlib
from usb_link import UsbLink


class FakeDevice:
    def __init__(self):self.pending=b"";self.lines=[];self.data=b"";self.header={};self.bad_offset=False
    @property
    def in_waiting(self):return len(self.pending)
    def read(self,n):data=self.pending[:n];self.pending=self.pending[n:];return data
    def flush(self):pass
    def write(self,data):
        self.lines.append(data);value=json.loads(data.decode()[8:]);i=value["id"]
        if value["op"]=="begin":self.header=value;self.data=b"";reply=f"USB_ACK {i} 0\n"
        elif value["op"]=="chunk":
            assert value["offset"]==len(self.data)
            self.data+=base64.b64decode(value["data"])
            reply=f"USB_ACK {i} {len(self.data)+(1 if self.bad_offset else 0)}\n"
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


if __name__=="__main__":unittest.main()
