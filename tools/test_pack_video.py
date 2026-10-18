import unittest,struct,zlib
from pack_video import pack,validate,HEADER

def frame():
    # Minimal framing sufficient for the packer's header/bounds validation.
    return b'\xff\xd8\xff\xc0\x00\x0b\x08'+struct.pack('>HH',180,320)+b'\x01\x01\x11\x00\xff\xdaDATA\xff\xd9'

class VideoPackingTests(unittest.TestCase):
    def test_all_frames_preserved(self):
        raw=frame()*24;image=pack(raw);info=validate(image)
        self.assertEqual(info['frames'],24);self.assertEqual(info['duration_seconds'],2)
        header=HEADER.unpack_from(image);self.assertEqual(image[header[8]:],raw)
    def test_truncated_video_rejected(self):
        with self.assertRaisesRegex(ValueError,'Truncated'):pack(frame()[:-2])
    def test_corrupted_data_rejected(self):
        image=bytearray(pack(frame()));image[-4]^=1
        with self.assertRaisesRegex(ValueError,'CRC'):validate(image)
    def test_frame_index_out_of_bounds_rejected(self):
        image=bytearray(pack(frame()));struct.pack_into('<II',image,64,0,999999)
        struct.pack_into('<I',image,52,zlib.crc32(image[64:72]))
        with self.assertRaisesRegex(ValueError,'bounds'):validate(image)

if __name__=='__main__':unittest.main()
