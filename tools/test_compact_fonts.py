import unittest
from generate_compact_fonts import glyphs,subset,lookup


def font_fixture():
    header=bytearray(23)
    header[17:19]=(0).to_bytes(2,'big');header[19:21]=(3).to_bytes(2,'big')
    ascii_data=b'A\x03\x11a\x03\x22\0\0'
    header[21:23]=len(ascii_data).to_bytes(2,'big')
    records=[]
    for cp in range(0x3000,0x30a0):records.append(cp.to_bytes(2,'big')+b'\x04'+bytes([cp%256]))
    return bytes(header)+ascii_data+b'\x00\x04\xff\xff'+b''.join(records)+b'\0\0'


class CompactFontTests(unittest.TestCase):
    def test_subset_lookup_crosses_unicode_blocks_and_removes_only_requested_glyphs(self):
        original=font_fixture();records=glyphs(original)
        keep=set(records)-{65,0x3010,0x307f}
        compact=subset(original,keep)
        self.assertEqual(original[1:17],compact[1:17])
        for cp in range(65536):
            self.assertEqual(lookup(compact,cp),records.get(cp) if cp in keep else None)

    def test_empty_subset_is_a_valid_missing_glyph_font(self):
        compact=subset(font_fixture(),set())
        for cp in (0,32,65,97,255,256,0x3000,0x7fff,0xffff):
            self.assertIsNone(lookup(compact,cp))

    def test_ascii_only_and_unicode_only_subsets_preserve_payload(self):
        data=font_fixture();records=glyphs(data)
        for keep in ({65,97},{0x3000,0x309f}):
            compact=subset(data,keep)
            for cp in keep:self.assertEqual(lookup(compact,cp),records[cp])
            self.assertIsNone(lookup(compact,0x4000))


if __name__=='__main__':unittest.main()
