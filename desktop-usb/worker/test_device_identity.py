import unittest
from types import SimpleNamespace
from unittest.mock import patch
from usb_link import UsbLink

class Device:
    def __init__(self,*a,**k):self.is_open=False;self.closed=False
    def open(self):self.is_open=True
    def close(self):self.is_open=False;self.closed=True
    def reset_input_buffer(self):pass

class IdentityTests(unittest.TestCase):
    def connect(self,version='1.4.4-touch-poll',target=''):
        device=Device()
        def command(instance,cmd,*args):
            return 'CLOCK_STATUS {"device_mac":"AA:BB:CC:DD:EE:FF"}' if cmd=='HUB_CLOCK_STATUS' else 'HUB_STATUS app=clock version='+version
        port=SimpleNamespace(device='COM42',vid=0x1a86,pid=0x7523)
        with patch('usb_link.TARGET',target),patch('usb_link.list_ports.comports',return_value=[port]),patch('usb_link.serial.Serial',return_value=device),patch.object(UsbLink,'command',command):
            result=UsbLink.connect()
        return result,device
    def test_public_release_accepts_a_different_board(self):
        link,device=self.connect();self.assertIs(link.device,device);link.close()
    def test_optional_mac_restriction(self):
        with self.assertRaises(RuntimeError):self.connect(target='00:11:22:33:44:55')
    def test_unrecognized_firmware_rejected(self):
        with self.assertRaises(RuntimeError):self.connect(version='unrelated-device')

if __name__=='__main__':unittest.main()
