import json,hashlib,tempfile,shutil,unittest
from pathlib import Path
from flash_firmware import validate

class FirmwareSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.folder=Path(self.temp.name)/'firmware'
        shutil.copytree(Path(__file__).resolve().parents[1]/'prebuilt/firmware-s3',self.folder)
    def tearDown(self):self.temp.cleanup()
    def test_known_bundle(self):
        manifest,images=validate(self.folder)
        self.assertEqual(manifest['chip'],'esp32s3');self.assertEqual(len(images),len(manifest['images']))
    def test_modified_image_rejected(self):
        p=self.folder/'firmware.bin';data=bytearray(p.read_bytes());data[100]^=1;p.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'Checksum'):validate(self.folder)
    def test_wrong_chip_rejected_even_with_matching_checksum(self):
        p=self.folder/'firmware.bin';data=bytearray(p.read_bytes());data[12]=0;p.write_bytes(data)
        m=self.folder/'manifest.json';d=json.loads(m.read_text())
        for item in d['images']:
            if item['name']=='firmware.bin':item['sha256']=hashlib.sha256(data).hexdigest()
        m.write_text(json.dumps(d))
        with self.assertRaisesRegex(ValueError,'not ESP32-S3'):validate(self.folder)
    def test_unexpected_address_rejected(self):
        p=self.folder/'manifest.json';d=json.loads(p.read_text());d['images'][0]['address']=0x9000;p.write_text(json.dumps(d))
        with self.assertRaisesRegex(ValueError,'layout'):validate(self.folder)
    def test_video_partition_change_rejected_with_updated_checksum(self):
        p=self.folder/'partitions.bin';data=bytearray(p.read_bytes());data[3*32+4]^=0x10;p.write_bytes(data)
        m=self.folder/'manifest.json';d=json.loads(m.read_text())
        for item in d['images']:
            if item['name']=='partitions.bin':item['sha256']=hashlib.sha256(data).hexdigest()
        m.write_text(json.dumps(d))
        with self.assertRaisesRegex(ValueError,'partition table'):validate(self.folder)
    def test_video_corruption_rejected_with_updated_checksum(self):
        p=self.folder/'video.bin';data=bytearray(p.read_bytes());data[-100]^=1;p.write_bytes(data)
        m=self.folder/'manifest.json';d=json.loads(m.read_text())
        for item in d['images']:
            if item['name']=='video.bin':item['sha256']=hashlib.sha256(data).hexdigest()
        m.write_text(json.dumps(d))
        with self.assertRaisesRegex(ValueError,'CRC'):validate(self.folder)

if __name__=='__main__':unittest.main()
