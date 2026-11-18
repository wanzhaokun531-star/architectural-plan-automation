import csv
import json
from pathlib import Path
import tempfile
import unittest

from owned_plan_host import JobError, file_hash
from template_revision import (missing_paper_segments, publish_template_result,
                               typography_evidence_issues, verify_final_panel_coverage)


class TemplateRevisionTests(unittest.TestCase):
    def test_same_named_opening_style_cannot_hide_wrong_font_height_or_width(self):
        policy = {'styles': [{'name': '_TCH_WINDOW', 'font': 'simplex.shx',
                              'bigFont': 'gbcbig.shx', 'heightMM': 4.0, 'width': .65}]}
        glyph = {'style': '_TCH_WINDOW', 'font': 'simplex.shx', 'bigFont': 'gbcbig.shx',
                 'width': .65, 'height': 400}
        audit = {'objects': [{'object': 'OPEN', 'role': 'native-opening-label',
                              'verified': True, 'glyphs': [glyph]}]}
        self.assertEqual(typography_evidence_issues(audit, policy), [])
        for key, value in [('font', 'tssdeng.shx'), ('bigFont', 'hztxt.shx'),
                           ('width', 1), ('height', 280)]:
            original = glyph[key]
            glyph[key] = value
            self.assertEqual(len(typography_evidence_issues(audit, policy)), 1)
            glyph[key] = original

    def test_missing_second_floor_cannot_pass_with_matching_generated_totals(self):
        with tempfile.TemporaryDirectory() as directory:
            sheets = Path(directory) / 'sheets.tsv'
            sheets.write_text('PLANJOINT1 HASH\nSHEET 1 "一层"\nSHEET 2 "二层"\nEND\n', encoding='utf-8')
            with self.assertRaises(JobError):
                verify_final_panel_coverage(sheets, 'HASH', [{'panel': 1, 'layout': '一层'}], [{'panel': 1}])
            pages = [{'panel': 1, 'layout': '一层'}, {'panel': 2, 'layout': '二层'}]
            self.assertEqual(len(verify_final_panel_coverage(sheets, 'HASH', pages, [{'panel': 1}, {'panel': 2}])), 2)
            pages[1]['layout'] = '一层'
            with self.assertRaises(JobError):
                verify_final_panel_coverage(sheets, 'HASH', pages, [{'panel': 1}, {'panel': 2}])

    def test_duplicate_ink_panels_and_wrong_source_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            sheets = Path(directory) / 'sheets.tsv'
            sheets.write_text('PLANJOINT1 HASH\nSHEET 1 "一层"\nSHEET 2 "二层"\nEND\n', encoding='utf-8')
            pages = [{'panel': 1, 'layout': '一层'}, {'panel': 2, 'layout': '二层'}]
            with self.assertRaises(JobError):
                verify_final_panel_coverage(sheets, 'HASH', pages, [{'panel': 1}, {'panel': 1}])
            with self.assertRaises(JobError):
                verify_final_panel_coverage(sheets, 'OTHER', pages, [{'panel': 1}, {'panel': 2}])

    def test_named_style_without_actual_glyph_evidence_is_unresolved(self):
        row = {'object': 'ABC', 'role': 'general', 'verified': True, 'glyphs': []}
        self.assertEqual(len(typography_evidence_issues({'objects': [row]})), 1)
        row['glyphs'] = [{'style': 'FY_HZ', 'font': 'tssdeng.shx', 'width': .7, 'height': 400}]
        self.assertEqual(typography_evidence_issues({'objects': [row]}), [])
        row['glyphs'][0]['height'] = float('nan')
        self.assertEqual(len(typography_evidence_issues({'objects': [row]})), 1)

    def test_diagonal_short_segment_cannot_hide_behind_another_target(self):
        audit = {'objects': [{'kind': 'dimension', 'verified': True, 'targets': ['AB'],
                             'stations': [[0, 0], [200, 200], [1000, 1000]]}]}
        ink = {'frames': [{'panel': 1, 'modelCenter': [0, 0], 'modelSize': [3000, 3000],
                           'targets': [{'handle': 'CD', 'segment': 0}]}]}
        self.assertEqual(len(missing_paper_segments(audit, ink)), 1)
        ink['frames'][0]['targets'].append({'handle': 'AB', 'segment': 0})
        self.assertEqual(missing_paper_segments(audit, ink), [])

    def fixture(self, root):
        report = root / 'report'; report.mkdir()
        drawing = report / 'candidate.dwg'; drawing.write_bytes(b'verified fixture')
        (report / 'template-native-readback.json').write_text(json.dumps({'objects': []}))
        (report / 'typography-paper-input.json').write_text(json.dumps({'frames': []}))
        old = root / 'old.csv'
        old.write_text('源对象,问题,源图幅,实际部位,依据\nABC,缺少面积,2,厨房,源平面\n', encoding='utf-8-sig')
        return {'drawing': str(drawing), 'sha256': file_hash(drawing), 'report': str(report), 'unresolved': []}, old

    def test_generation_checklist_keeps_problem_and_location(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); result, old = self.fixture(root)
            output = root / 'delivery.dwg'
            publish_template_result(result, output, prior_checklist=old)
            with (root / 'delivery_遗留清单.csv').open(encoding='utf-8-sig') as stream:
                row = next(csv.DictReader(stream))
            self.assertIn('缺少面积', row['原因'])
            self.assertIn('厨房', row['原因'])
            self.assertIn('源平面', row['原因'])

    def test_conflicting_drawing_never_leaves_new_checklist(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); result, old = self.fixture(root)
            output = root / 'delivery.dwg'; output.write_bytes(b'previous delivery')
            with self.assertRaises(JobError):
                publish_template_result(result, output, prior_checklist=old)
            self.assertEqual(output.read_bytes(), b'previous delivery')
            self.assertFalse((root / 'delivery_遗留清单.csv').exists())


if __name__ == '__main__':
    unittest.main()
