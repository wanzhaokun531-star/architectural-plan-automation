import json
from pathlib import Path
import struct
import unittest
import zlib

from ezdxf.addons import acadctb
from owned_plan_host import file_hash


class TemplateResourceTests(unittest.TestCase):
    resources = Path(__file__).resolve().parents[1] / 'assets'

    def test_template_donors_are_real_native_objects_in_verified_dwg(self):
        standard = json.loads((self.resources / 'annotation-template/standard.json').read_text('utf-8'))
        observed = json.loads((self.resources / 'annotation-template/native-readback.json').read_text('utf-8'))
        self.assertEqual(file_hash(self.resources / 'annotation-template' / standard['dwg']), standard['sha256'].upper())
        actual = {(row['handle'], row['entity']['type']) for row in observed['objects']}
        self.assertTrue(all((row['handle'], row['type']) in actual for row in standard['nativeDonors']))
        self.assertFalse(standard['operations']['allowSourceStyleOverwrite'])
        self.assertFalse(standard['operations']['allowExplodedReplacement'])

    def test_pdf_configuration_converts_all_fonts_and_has_valid_compression(self):
        data = (self.resources / 'plotters/FY-XS-2026.9-PDF.pc3').read_bytes()
        checksum, original_length, compressed_length = struct.unpack('<III', data[48:60])
        content = zlib.decompress(data[60:])
        self.assertEqual(zlib.adler32(data[60:]), checksum)
        self.assertEqual(len(content), original_length)
        self.assertEqual(len(data) - 60, compressed_length)
        self.assertIn(b'name="All_As_Geometry\n  value=TRUE', content)
        self.assertIn(b'truetype_as_text=FALSE', content)
        self.assertIn(b'name="View_New_File\n  value=FALSE', content)

    def test_ctb_preserves_object_weights_and_separates_gray_furniture(self):
        styles = acadctb.load(self.resources / 'plotters/FY-XS-2026.9-A3 A4.ctb')
        self.assertEqual(len(styles.lineweights), 27)
        self.assertEqual(styles[254].screen, 70)
        self.assertAlmostEqual(styles.lineweights[styles[7].lineweight - 1], .10)
        self.assertAlmostEqual(styles.lineweights[styles[4].lineweight - 1], .30)
        self.assertEqual(styles[254].lineweight, 0)
        for index in range(1, 256):
            if index != 254:
                self.assertEqual(styles[index].screen, 100)

    def test_a1_keeps_shared_text_color_by_object_and_uses_larger_wall_pen(self):
        styles = acadctb.load(self.resources / 'plotters/FY-XS-2026.9-A1.ctb')
        self.assertEqual(styles[7].lineweight, 0)
        self.assertAlmostEqual(styles.lineweights[styles[4].lineweight - 1], .35)
        self.assertEqual(styles[254].screen, 70)

    def test_template_base_and_ratio_styles_remain_distinct(self):
        standard = json.loads((self.resources / 'annotation-template/standard.json').read_text('utf-8'))
        styles = {row['name']: row['parameters'] for row in standard['nativeDimensionStyles']}
        self.assertEqual(styles['_TCH_ARCH']['textStyle'], '_TCH_DIM')
        self.assertEqual(styles['_TCH_ARCH&&100']['textStyle'], '_TCH_DIM_T3')
        self.assertEqual(styles['_TCH_ARCH&&100']['textHeight'], 297.5)


if __name__ == '__main__':
    unittest.main()
