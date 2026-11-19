import csv
import copy
import json
from pathlib import Path
import tempfile
import unittest

from owned_plan_host import JobError, file_hash
from template_revision import (missing_paper_segments, publish_template_result,
                               typography_evidence_issues, verify_final_panel_coverage,
                               omission_evidence_issues, risk_glyph_evidence_issues,
                               unit_glyph_repair_evidence_issues,
                               title_paper_evidence_issues,
                               font_dependency_evidence_issues, final_rule_evidence, publication_coverage)


class TemplateRevisionTests(unittest.TestCase):
    def title_coordinates(self):
        return {'units': 1, 'useStandardScale': False, 'standardScale': .01, 'customScale': [1, 1],
                'rotation': 0, 'plotType': 4, 'centered': True, 'paperSize': [594, 420],
                'window': [0, 0, 594, 420], 'margins': [0, 0, 0, 0], 'origin': [0, 0]}

    def test_title_paper_actual_extent_overrides_font_pass_and_native_pass(self):
        pages = [{'panel': 2, 'layout': '平面图幅-2', 'paperCoordinates': self.title_coordinates()}]
        audit = {'contract': 'plan.title-paper-bounds.v1', 'outputSHA256': 'HASH', 'savedReopened': True,
                 'expectedCoverageKnown': True, 'layouts': [{'panel': 2, 'layout': '平面图幅-2', 'textLeafCount': 1, 'reason': ''}],
                 'objects': [{'object': 'FRAME/LONG', 'panel': 2, 'layout': '平面图幅-2', 'verified': True,
                              'paperBounds': [0, 0, 594, 420], 'bounds': [550, 25, 590, 30], 'overrunMM': [0, 0, 0, 0]}]}
        audit['layouts'][0]['paperCoordinates'] = self.title_coordinates()
        self.assertEqual(title_paper_evidence_issues(audit, 'HASH', pages), [])
        audit['objects'][0]['bounds'][2] = 612
        issues = title_paper_evidence_issues(audit, 'HASH', pages)
        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0]['object'], 'FRAME/LONG')
        self.assertIn('18.000', issues[0]['reason'])

    def test_title_paper_unknown_transform_or_missing_layout_is_not_table_pass(self):
        pages = [{'panel': 1, 'layout': 'A'}, {'panel': 2, 'layout': 'B'}]
        audit = {'contract': 'plan.title-paper-bounds.v1', 'outputSHA256': 'HASH', 'savedReopened': True,
                 'expectedCoverageKnown': False, 'layouts': [{'panel': 1, 'layout': 'A', 'textLeafCount': 0,
                                                           'reason': 'unresolved paper origin'}], 'objects': []}
        self.assertTrue(title_paper_evidence_issues(audit, 'HASH', pages))
        self.assertTrue(title_paper_evidence_issues(audit, 'OTHER', pages))
        self.assertTrue(title_paper_evidence_issues(None, 'HASH', pages))

    def test_title_empty_attributes_are_not_missing_ink_and_unknown_is_once_per_layout(self):
        coords = self.title_coordinates()
        pages = [{'panel': 1, 'layout': 'A', 'paperCoordinates': coords}]
        audit = {'contract': 'plan.title-paper-bounds.v1', 'outputSHA256': 'HASH', 'savedReopened': True,
            'expectedCoverageKnown': True, 'layouts': [{'panel': 1, 'layout': 'A', 'textLeafCount': 154,
                'inkLeafCount': 137, 'emptyLeafCount': 17, 'reason': '', 'paperCoordinates': coords}],
            'objects': [{'object': str(i), 'panel': 1, 'layout': 'A', 'text': '' if i < 17 else '文字',
                'noInk': i < 17, 'applicable': i >= 17, 'verified': None if i < 17 else True,
                'paperBounds': [0, 0, 594, 420], 'bounds': None if i < 17 else [10, 10, 20, 20]}
                for i in range(154)]}
        self.assertEqual(title_paper_evidence_issues(audit, 'HASH', pages), [])
        audit['layouts'][0]['reason'] = 'unknown transform'; audit['expectedCoverageKnown'] = False
        issues = title_paper_evidence_issues(audit, 'HASH', pages)
        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0]['object'], 'layout:1')

    def test_title_saved_and_print_active_scales_must_match(self):
        saved = self.title_coordinates(); effective = copy.deepcopy(saved)
        pages = [{'panel': 1, 'layout': 'A', 'paperCoordinates': effective}]
        audit = {'contract': 'plan.title-paper-bounds.v1', 'outputSHA256': 'HASH', 'savedReopened': True,
            'expectedCoverageKnown': True, 'layouts': [{'panel': 1, 'layout': 'A', 'textLeafCount': 1,
                'reason': '', 'paperCoordinates': saved}], 'objects': [{'object': 'X', 'panel': 1, 'layout': 'A',
                'verified': True, 'paperBounds': [0, 0, 594, 420], 'bounds': [10, 10, 20, 20]}]}
        self.assertEqual(title_paper_evidence_issues(audit, 'HASH', pages), [])
        # A latent standard ratio is irrelevant while custom scale is active.
        effective['standardScale'] = .5
        self.assertEqual(title_paper_evidence_issues(audit, 'HASH', pages), [])
        saved['useStandardScale'] = True
        self.assertEqual(len(title_paper_evidence_issues(audit, 'HASH', pages)), 1)

    def area_superscript_fixture(self):
        policy = {'styles': [{'name': 'FY_HZ', 'font': 'tssdeng.shx', 'bigFont': 'hztxt.shx',
                              'heightMM': 4, 'width': .7, 'fixedHeight': 0}]}
        base = {'style': 'FY_HZ', 'font': 'tssdeng.shx', 'bigFont': 'hztxt.shx',
                'height': 400, 'width': .7, 'styleWidth': .7, 'fixedHeight': 0,
                'oblique': 0, 'flags': 0, 'annotative': False, 'vertical': False, 'rotation': 0}
        glyphs = [{**base, 'text': '建筑面积', 'anchor': [-1600, 0, 0], 'bounds': [-1600, 0, 0, -100, 400, 0]},
                  {**base, 'text': '198.38m', 'anchor': [0, 0, 0], 'bounds': [0, 0, 0, 1800, 400, 0]},
                  {**base, 'text': '2', 'height': 200, 'expectedHeight': 200,
                   'anchor': [1840, 200, 0], 'bounds': [1840, 200, 0, 1960, 400, 0]}]
        return {'objects': [{'object': 'AREA', 'role': 'special-area', 'nativeType': 'TDbText',
                             'unitsPerPaperMM': 100, 'verified': True, 'glyphs': glyphs}]}, policy

    def test_native_area_squared_unit_requires_real_paired_superscript_geometry(self):
        audit, policy = self.area_superscript_fixture()
        self.assertEqual(typography_evidence_issues(audit, policy), [])
        audit['objects'][0]['glyphs'][2]['width'] = .65
        self.assertEqual(len(typography_evidence_issues(audit, policy)), 1)

    def test_ordinary_two_unpaired_two_and_distant_two_have_no_height_exception(self):
        original, policy = self.area_superscript_fixture()
        for failure in ('ordinary', 'unpaired', 'distant', 'no-geometry'):
            with self.subTest(failure=failure):
                audit = copy.deepcopy(original); row = audit['objects'][0]
                if failure == 'ordinary':
                    row['nativeType'] = 'AcDbText'
                elif failure == 'unpaired':
                    row['glyphs'][1]['text'] = '编号198.38'
                elif failure == 'distant':
                    row['glyphs'][2]['anchor'][0] += 3000
                    row['glyphs'][2]['bounds'][0] += 3000
                    row['glyphs'][2]['bounds'][3] += 3000
                else:
                    del row['glyphs'][2]['anchor']
                self.assertEqual(len(typography_evidence_issues(audit, policy)), 1)

    def test_superscript_readback_cannot_bypass_body_height_or_native_failure(self):
        audit, policy = self.area_superscript_fixture()
        audit['objects'][0]['glyphs'][1]['height'] = 300
        self.assertEqual(len(typography_evidence_issues(audit, policy)), 1)
        audit, policy = self.area_superscript_fixture()
        audit['objects'][0]['verified'] = False
        self.assertEqual(len(typography_evidence_issues(audit, policy)), 1)

    def unit_repair_fixture(self):
        row = {'object': '201', 'before': '二层建筑面积：151.99㎡(阳台半面积)',
               'after': '二层建筑面积：151.99m²(阳台半面积)', 'actual': '二层建筑面积：151.99m²(阳台半面积)',
               'style': 'FY_HZ', 'primary': 'tssdeng.shx', 'big': 'hztxt.shx',
               'primarySHA256': 'A' * 64, 'bigSHA256': 'B' * 64,
               'expectedProtectedParameters': 'position;height;width;style',
               'actualProtectedParameters': 'position;height;width;style',
               'originalPlaceholderContour': 'question', 'replacementMContour': 'm-strokes',
               'replacementSquaredContour': 'squared-strokes', 'verified': True, 'reason': ''}
        audit = {'contract': 'plan.unit-glyph-repairs.v1', 'outputSHA256': 'HASH',
                 'savedReopened': True, 'objects': [row]}
        risk = {'contract': 'plan.risk-glyph-contours.v1', 'outputSHA256': 'HASH',
                'savedReopened': True, 'objects': [{'object': '201', 'character': '²', 'style': 'FY_HZ',
                'reason': '', 'unitContour': 'squared-strokes', 'placeholderContours': ['question', '', ''],
                'primarySHA256': 'A' * 64, 'bigSHA256': 'B' * 64, 'contours': 3}]}
        typography = {'objects': [{'object': '201', 'glyphs': [{'text': row['actual'], 'style': 'FY_HZ',
                                   'font': 'tssdeng.shx', 'bigFont': 'hztxt.shx'}]}]}
        return audit, risk, typography

    def test_unit_equivalent_spelling_preserves_complete_body_and_real_squared_glyph(self):
        audit, risk, typography = self.unit_repair_fixture()
        self.assertEqual(unit_glyph_repair_evidence_issues(audit, risk, typography, 'HASH'), [])
        risk['objects'][0]['unitContour'] = 'question'
        self.assertEqual(len(unit_glyph_repair_evidence_issues(audit, risk, typography, 'HASH')), 1)
        risk['objects'] = []
        self.assertEqual(len(unit_glyph_repair_evidence_issues(audit, risk, typography, 'HASH')), 1)

    def test_native_verified_unit_record_cannot_hide_changed_digits_anchor_or_font(self):
        for field, value in [('actual', '二层建筑面积：159.99m²(阳台半面积)'),
                             ('actualProtectedParameters', 'moved-anchor'), ('primarySHA256', ''),
                             ('primary', 'simplex.shx'), ('verified', False)]:
            with self.subTest(field=field):
                audit, risk, typography = self.unit_repair_fixture()
                audit['objects'][0][field] = value
                if field == 'actual':
                    audit['objects'][0]['after'] = value
                    typography['objects'][0]['glyphs'][0]['text'] = value
                self.assertEqual(len(unit_glyph_repair_evidence_issues(audit, risk, typography, 'HASH')), 1)

    def test_unit_repair_report_is_required_and_bound_to_final_reopen(self):
        audit, risk, typography = self.unit_repair_fixture()
        self.assertEqual(len(unit_glyph_repair_evidence_issues(None, risk, typography, 'HASH')), 1)
        self.assertEqual(len(unit_glyph_repair_evidence_issues(audit, risk, typography, 'OTHER')), 1)
        audit['savedReopened'] = False
        self.assertEqual(len(unit_glyph_repair_evidence_issues(audit, risk, typography, 'HASH')), 1)

    def test_publication_distinguishes_checked_rules_from_unknown_complete_coverage_and_na(self):
        evidence = {'contract': 'plan.final-rule-evidence.v1', 'outputSHA256': 'HASH',
                    'allCatalogRulesImplemented': True,
                    'rules': [{'rule': 'N06', 'expectedCoverageKnown': True, 'passed': True},
                              {'rule': 'L08', 'expectedCoverageKnown': False, 'passed': False}]}
        state = publication_coverage(evidence, 'HASH')
        self.assertTrue(state['checkedRulesPassed'])
        self.assertFalse(state['coverageComplete'])
        self.assertFalse(state['allRulesPassed'])
        self.assertEqual(state['unknownCoverageRules'], ['L08'])
        evidence['rules'][1]['applicability'] = 'not-applicable'
        self.assertTrue(publication_coverage(evidence, 'HASH')['allRulesPassed'])
        evidence['allCatalogRulesImplemented'] = False
        self.assertFalse(publication_coverage(evidence, 'HASH')['allRulesPassed'])

    def test_rule_summary_cannot_pass_unknown_coverage_or_unpublished_delivery(self):
        native = {'savedReopened': True, 'objects': []}
        text = {'savedReopened': True, 'policySHA256': 'POLICY', 'objects': []}
        omissions = {'independentInventory': True, 'scannedObjects': 5,
                     'enumeratedAnnotationRows': 5, 'evaluatedAnnotationRows': 5, 'exclusions': [],
                     'ruleCoverage': {'D23': {'coverageKnown': True}, 'S01': {'coverageKnown': True}},
                     'sourceCandidates': [], 'residuals': []}
        evidence = final_rule_evidence('DWG', native, text, omissions,
                                       {'expectedCoverageKnown': False, 'ends': []}, [],
                                       policy_digest='POLICY', printed_pages=2)
        rules = {row['rule']: row for row in evidence['rules']}
        self.assertEqual(evidence['outputSHA256'], 'DWG')
        self.assertTrue(rules['G09']['passed'])
        self.assertFalse(rules['A28']['passed'])
        self.assertFalse(rules['L08']['passed'])
        self.assertFalse(rules['P06']['passed'])
        self.assertTrue(rules['P06']['pendingPublication'])
        omissions['residuals'].append({'object': 'UNKNOWN', 'role': 'omission-coverage', 'reason': 'unreadable'})
        evidence = final_rule_evidence('DWG', native, text, omissions, {}, [],
                                       policy_digest='POLICY', printed_pages=2)
        for row in evidence['rules']:
            if row['rule'] in ('D23', 'S01'):
                self.assertFalse(row['expectedCoverageKnown'])
                self.assertFalse(row['passed'])
                self.assertEqual(row['issueCount'], 1)

    def test_missing_font_definitions_require_independent_full_scan_and_preserve_issues(self):
        audit = {'contract': 'plan.font-dependencies.v1', 'outputSHA256': 'HASH',
                 'savedReopened': True, 'allStylesEnumerated': True,
                 'retired': [{'handle': 'OLD', 'absent': True}], 'unresolved': [], 'issues': []}
        self.assertEqual(font_dependency_evidence_issues(audit, 'HASH'), [])
        audit['retired'][0]['absent'] = False
        self.assertEqual(len(font_dependency_evidence_issues(audit, 'HASH')), 1)
        audit['unresolved'].append({'handle': 'USED', 'name': 'HZTXT', 'big': 'AllInOneBig.shx'})
        self.assertEqual(len(font_dependency_evidence_issues(audit, 'HASH')), 2)
        with self.assertRaises(JobError):
            font_dependency_evidence_issues(audit, 'OTHER')

    def test_same_named_opening_style_cannot_hide_wrong_font_height_or_width(self):
        policy = {'styles': [{'name': '_TCH_WINDOW', 'font': 'simplex.shx',
                              'bigFont': 'gbcbig.shx', 'heightMM': 3.0, 'width': .65, 'fixedHeight': 3}]}
        glyph = {'style': '_TCH_WINDOW', 'font': 'simplex.shx', 'bigFont': 'gbcbig.shx',
                 'width': .65, 'height': 300, 'styleWidth': .65, 'fixedHeight': 3,
                 'oblique': 0, 'flags': 0, 'annotative': False, 'vertical': False}
        audit = {'objects': [{'object': 'OPEN', 'role': 'native-opening-label',
                              'verified': True, 'unitsPerPaperMM': 100, 'glyphs': [glyph]}]}
        self.assertEqual(typography_evidence_issues(audit, policy), [])
        for key, value in [('font', 'tssdeng.shx'), ('bigFont', 'hztxt.shx'),
                           ('width', 1), ('height', 400), ('fixedHeight', 4),
                           ('styleWidth', .7), ('vertical', True), ('annotative', True)]:
            original = glyph[key]
            glyph[key] = value
            self.assertEqual(len(typography_evidence_issues(audit, policy)), 1)
            glyph[key] = original

    def test_failed_native_row_and_its_own_expected_height_cannot_hide_current_mismatch(self):
        policy = {'styles': [{'name': 'FY_HZ', 'font': 'tssdeng.shx', 'bigFont': 'hztxt.shx',
                              'heightMM': 4, 'width': .7, 'fixedHeight': 0}]}
        row = {'object': 'A', 'role': 'general', 'verified': False, 'reason': 'native edit failed',
               'unitsPerPaperMM': 100, 'glyphs': [{'style': 'FY_HZ', 'font': 'tssdeng.shx',
               'bigFont': 'hztxt.shx', 'height': 300, 'expectedHeight': 300, 'width': .7,
               'styleWidth': .7, 'fixedHeight': 0, 'oblique': 0, 'flags': 0,
               'annotative': False, 'vertical': False}]}
        issues = typography_evidence_issues({'objects': [row]}, policy)
        self.assertEqual(len(issues), 2)
        row['verified'] = True
        self.assertEqual(len(typography_evidence_issues({'objects': [row]}, policy)), 1)
        row['glyphs'][0]['height'] = 400
        self.assertEqual(typography_evidence_issues({'objects': [row]}, policy), [])
        del row['unitsPerPaperMM']
        self.assertEqual(len(typography_evidence_issues({'objects': [row]}, policy)), 1)

    def test_final_omissions_require_whole_drawing_scan_and_report_new_residual(self):
        with self.assertRaises(JobError):
            omission_evidence_issues({'savedReopened': True, 'removed': ['OLD']}, 'HASH')
        audit = {'contract': 'plan.final-omissions.v1', 'outputSHA256': 'HASH',
                 'savedReopened': True, 'independentInventory': True, 'modelEnumerated': True,
                 'layoutsEnumerated': 2, 'scannedObjects': 20, 'sourceCandidates': [],
                 'enumeratedAnnotationRows': 20, 'evaluatedAnnotationRows': 20, 'exclusions': [],
                 'ruleCoverage': {'D23': {'coverageKnown': True}, 'S01': {'coverageKnown': True}},
                 'residuals': [{'object': 'NEW', 'role': 'D23', 'reason': 'new interior dimension'}]}
        self.assertEqual(omission_evidence_issues(audit, 'HASH')[0]['object'], 'NEW')
        with self.assertRaises(JobError):
            omission_evidence_issues(audit, 'OTHER')

    def omission_coverage_fixture(self):
        return {'contract': 'plan.final-omissions.v1', 'outputSHA256': 'HASH', 'savedReopened': True,
                'independentInventory': True, 'modelEnumerated': True, 'layoutsEnumerated': 2,
                'scannedObjects': 10, 'enumeratedAnnotationRows': 20, 'evaluatedAnnotationRows': 10,
                'sourceCandidates': [], 'residuals': [], 'exclusions': [],
                'ruleCoverage': {'D23': {'coverageKnown': True}, 'S01': {'coverageKnown': True}}}

    def omission_rules(self, omissions):
        issues = omission_evidence_issues(omissions, 'HASH')
        evidence = final_rule_evidence('HASH', {'savedReopened': True, 'objects': []},
            {'savedReopened': True, 'policySHA256': 'P', 'objects': []}, omissions, {}, issues,
            policy_digest='P', printed_pages=2)
        return issues, {row['rule']: row for row in evidence['rules']}

    def test_readable_dimension_inside_preserved_furniture_is_still_reported(self):
        audit = self.omission_coverage_fixture()
        audit['residuals'] = [{'object': 'FURN/DIM', 'role': 'D23', 'reason': 'retained protected interior dimension'}]
        audit['sourceCandidates'] = list(audit['residuals'])
        audit['exclusions'] = [{'object': 'FURN', 'root': 'FURN', 'category': 'protected-furniture',
            'reason': 'original instance preserved, annotation read-only checked', 'rules': ['D23', 'S01'],
            'writeProtected': True, 'coverageKnown': True, 'readAnnotationPaths': ['FURN/DIM'], 'unreadPaths': []}]
        issues, rules = self.omission_rules(audit)
        self.assertEqual(issues, audit['residuals'])
        self.assertTrue(rules['D23']['expectedCoverageKnown'])
        self.assertFalse(rules['D23']['passed'])
        self.assertEqual(rules['D23']['verifiedCount'], 0)
        self.assertTrue(rules['S01']['passed'])

    def test_zero_residuals_with_unread_furniture_domains_are_not_complete_coverage(self):
        audit = self.omission_coverage_fixture()
        audit['exclusions'] = [{'object': root, 'root': root, 'category': 'protected-furniture',
            'reason': 'contour-only geometry not decoded', 'rules': ['D23', 'S01'], 'writeProtected': True,
            'coverageKnown': False, 'readAnnotationPaths': [], 'unreadPaths': [root + '/contour-only-geometry']}
            for root in ('F1', 'F2', 'F3')]
        # Even a stale native "true" cannot override the explicit unread domains.
        issues, rules = self.omission_rules(audit)
        self.assertEqual(len(issues), 2)
        for rule in ('D23', 'S01'):
            self.assertFalse(rules[rule]['expectedCoverageKnown'])
            self.assertFalse(rules[rule]['passed'])
            self.assertEqual(rules[rule]['coverage']['excludedInstanceCount'], 3)

    def test_exclusion_affects_its_rule_and_missing_ledger_never_means_zero(self):
        audit = self.omission_coverage_fixture()
        audit['exclusions'] = [{'object': 'X', 'root': 'X', 'category': 'unreadable-dimension',
            'reason': 'measurement unavailable', 'rules': ['D23'], 'writeProtected': True,
            'coverageKnown': False, 'readAnnotationPaths': [], 'unreadPaths': ['X']}]
        _, rules = self.omission_rules(audit)
        self.assertFalse(rules['D23']['expectedCoverageKnown'])
        self.assertTrue(rules['S01']['passed'])
        del audit['exclusions']
        issues, rules = self.omission_rules(audit)
        self.assertEqual(len(issues), 2)
        self.assertFalse(rules['D23']['expectedCoverageKnown'])
        self.assertFalse(rules['S01']['expectedCoverageKnown'])

    def test_risk_character_requires_real_distinct_strokes_not_native_pass_flag(self):
        typography = {'objects': [{'object': 'CELL', 'glyphs': [{'style': 'FY_HZ', 'text': '面积（㎡）'}]}]}
        audit = {'contract': 'plan.risk-glyph-contours.v1', 'outputSHA256': 'HASH',
                 'savedReopened': True, 'objects': []}
        self.assertEqual(len(risk_glyph_evidence_issues(audit, typography, 'HASH')), 1)
        row = {'object': 'CELL', 'character': '㎡', 'style': 'FY_HZ', 'verified': True,
               'reason': '', 'unitContour': 'question', 'placeholderContours': ['question', '', ''],
               'primarySHA256': 'A' * 64, 'contours': 3}
        audit['objects'].append(row)
        self.assertEqual(len(risk_glyph_evidence_issues(audit, typography, 'HASH')), 1)
        row['unitContour'] = 'real-metre-square-strokes'
        self.assertEqual(risk_glyph_evidence_issues(audit, typography, 'HASH'), [])
        row['primarySHA256'] = ''
        self.assertEqual(len(risk_glyph_evidence_issues(audit, typography, 'HASH')), 1)

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
        proof = report / 'storey-wall-height-readback.json'
        proof.write_text(json.dumps({'contract': 'plan.final-wall-heights.v1', 'savedReopened': True,
                                     'outputSHA256': file_hash(drawing), 'unresolved': []}))
        (report / 'plan-final-rule-evidence.json').write_text(json.dumps({
            'contract': 'plan.final-rule-evidence.v1', 'outputSHA256': file_hash(drawing),
            'allCatalogRulesImplemented': True, 'rules': [
                {'rule': 'P06', 'expectedCoverageKnown': True, 'passed': True, 'coverage': {}}]}))
        return {'drawing': str(drawing), 'sha256': file_hash(drawing), 'report': str(report),
                'unresolved': [], 'wallHeightProof': str(proof)}, old

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
            status = json.loads((Path(result['report']) / 'publication-quality-status.json').read_text('utf-8'))
            self.assertTrue(status['processingCompleted'])
            self.assertFalse(status['allRulesPassed'])
            self.assertEqual(status['unresolvedCount'], 1)

    def test_missing_final_rule_coverage_is_published_as_unknown_not_as_all_passed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); result, _ = self.fixture(root)
            (Path(result['report']) / 'plan-final-rule-evidence.json').unlink()
            publish_template_result(result, root / 'delivery.dwg')
            state = json.loads((Path(result['report']) / 'publication-quality-status.json').read_text('utf-8'))
            self.assertTrue(state['delivered'])
            self.assertFalse(state['coverageComplete'])
            self.assertFalse(state['allRulesPassed'])
            self.assertIn('G09', state['unknownCoverageRules'])
            self.assertTrue((root / 'delivery_遗留清单.csv').is_file())

    def test_old_area_issue_closes_only_when_exact_source_is_independently_absent(self):
        for absent in (False, True):
            with self.subTest(absent=absent), tempfile.TemporaryDirectory() as directory:
                root = Path(directory); result, old = self.fixture(root)
                old.write_text('源对象,问题\nABC,房间面积仍残留\n', encoding='utf-8-sig')
                audit = {'contract': 'plan.final-omissions.v1', 'outputSHA256': result['sha256'],
                         'savedReopened': True, 'independentInventory': True, 'modelEnumerated': True,
                         'layoutsEnumerated': 2, 'scannedObjects': 20, 'residuals': [],
                         'sourceCandidates': [{'object': 'ABC', 'role': 'S01', 'reason': 'room area',
                                               'presentAfterReopen': not absent}]}
                (Path(result['report']) / 'plan-final-omissions-readback.json').write_text(json.dumps(audit))
                publish_template_result(result, root / 'delivery.dwg', prior_checklist=old)
                self.assertEqual((root / 'delivery_遗留清单.csv').exists(), not absent)

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
