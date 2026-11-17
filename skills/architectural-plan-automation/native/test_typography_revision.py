import tempfile
import unittest
from pathlib import Path
from dimension_revision import verify_dimension_revision
from owned_plan_host import JobError, file_hash, write_json
from typography_revision import verify_revision, verify_typography

class TypographyEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = {'outputSHA256': 'CURRENT', 'savedReopened': True, 'independentInventory': True,
                     'checked': 2, 'unresolved': 0, 'allApplicableVerified': True,
                     'objects': [{'object': 'A/1', 'role': 'general', 'expectedHeight': 400, 'verified': True,
                                  'glyphs': [{'height': 400, 'definitionVerified': True}]},
                                 {'object': 'A/2', 'role': 'dimension', 'expectedHeight': 280, 'verified': True,
                                  'glyphs': [{'height': 280, 'definitionVerified': True, 'style': '_TCH_DIM'}]}],
                     'dimensionStyleTable': {'checked': 2, 'unresolved': 0, 'allApplicableVerified': True,
                                             'styles': [
                                                 {'name': '_TCH_ARCH', 'textStyle': '_TCH_DIM',
                                                  'definitionVerified': True, 'verified': True, 'reason': ''},
                                                 {'name': '_TCH_ARCH&&100', 'textStyle': '_TCH_DIM',
                                                  'definitionVerified': True, 'verified': True, 'reason': ''}]}}

    def check(self, **kwargs):
        write_json(self.root / 'final-typography-readback.json', self.data)
        return verify_typography(self.root, 'CURRENT', **kwargs)

    def test_same_version_actual_readback(self):
        self.assertEqual(self.check(formal=True), [])

    def test_visible_object_readback_without_dimension_style_table_is_stale(self):
        del self.data['dimensionStyleTable']
        with self.assertRaises(JobError):
            self.check(formal=True)

    def test_table_readback_must_include_the_base_style(self):
        table = self.data['dimensionStyleTable']
        table['styles'].pop(0)
        table['checked'] = 1
        with self.assertRaises(JobError):
            self.check(formal=True)

    def test_passing_table_style_requires_exact_text_style_and_verified_definition(self):
        style = self.data['dimensionStyleTable']['styles'][1]
        for field, wrong in (('textStyle', '_TCH_DIM_PLAN_1'), ('textStyle', '_tch_dim'),
                             ('definitionVerified', False)):
            with self.subTest(field=field), self.assertRaises(JobError):
                original = style[field]
                try:
                    style[field] = wrong
                    self.check(formal=True)
                finally:
                    style[field] = original

    def test_passing_visible_dimension_requires_exact_text_style(self):
        self.data['objects'][1]['glyphs'][0]['style'] = '_TCH_DIM_PLAN_1'
        with self.assertRaises(JobError):
            self.check(formal=True)

    def table_failure(self):
        table = self.data['dimensionStyleTable']
        table.update(unresolved=1, allApplicableVerified=False)
        table['styles'][1].update(textStyle='Standard', definitionVerified=False,
                                  verified=False, reason='different DIMTXSTY')
        return {'object': '_TCH_ARCH&&100', 'role': 'dimension-style-table',
                'verified': False, 'reason': 'different DIMTXSTY'}

    def test_table_failure_must_appear_in_the_top_level_audit(self):
        self.table_failure()
        with self.assertRaises(JobError):
            self.check()

    def test_reported_table_failure_counts_as_an_open_item(self):
        failure = self.table_failure()
        self.data['objects'].append(failure)
        self.data.update(checked=3, unresolved=1, allApplicableVerified=False)
        self.assertEqual(self.check(), [failure])
        with self.assertRaises(JobError):
            self.check(formal=True)

    def test_standalone_typography_receipt_must_carry_audited_style_failure(self):
        failure = self.table_failure()
        self.data['objects'].append(failure)
        self.data.update(checked=3, unresolved=1, allApplicableVerified=False)
        source = self.root / 'source.dwg'
        source.write_bytes(b'original')
        drawing = self.root / 'candidate.dwg'
        drawing.write_bytes(b'saved')
        digest = file_hash(drawing)
        self.data['outputSHA256'] = digest
        write_json(self.root / 'final-typography-readback.json', self.data)
        write_json(self.root / 'product-result.json', {'completed': True, 'dwgReopened': True,
                   'nonAnnotationGeometryPreserved': True, 'sourceSHA256': file_hash(source),
                   'outputSHA256': digest, 'unresolved': []})
        report = self.root / 'job'
        report.mkdir()
        for name in (drawing.name, 'product-result.json', 'final-typography-readback.json'):
            (self.root / name).rename(report / name)
        write_json(self.root / 'host.json', {'session': str(self.root)})
        write_json(self.root / 'isolation-final.json', {'nativeQuitConfirmed': True, 'workerExitCode': 0,
                   'forcedTermination': False, 'userCadTouched': False})
        pdf = report / 'layout.pdf'
        pdf.write_bytes(b'%PDF-1.4')
        write_json(report / 'formal-layout-print-review.json', {'outputSHA256': digest, 'pages': [
            {'status': 'plotted', 'file': pdf.name, 'sha256': file_hash(pdf),
             'paperScale': 1, 'viewportScale': .01}]})
        with self.assertRaises(JobError):
            verify_revision(report, source)

    def dimension_revision_proof(self):
        drawing = self.root / 'candidate.dwg'
        drawing.write_bytes(b'saved')
        pdf = self.root / 'layout.pdf'
        pdf.write_bytes(b'%PDF-1.4')
        digest = file_hash(drawing)
        write_json(self.root / 'product-result.json', {'dwgReopened': True, 'outputSHA256': digest})
        write_json(self.root / 'formal-layout-print-review.json', {'outputSHA256': digest, 'pages': [
            {'status': 'plotted', 'file': pdf.name, 'sha256': file_hash(pdf)}]})
        proof = {'savedReopened': True, 'outputSHA256': digest, 'dimensions': [
            {'source': '10', 'target': '20', 'sourceVisible': False, 'issue': '',
             'parts': [{'textStyle': '_TCH_DIM', 'textStyleDefinitionVerified': True, 'effectiveHeight': 280}]}]}
        return proof, self.root / 'required-dimension-readback.json'

    def test_dimension_only_revision_rejects_stale_table_evidence(self):
        proof, readback = self.dimension_revision_proof()
        write_json(readback, proof)
        with self.assertRaises(JobError):
            verify_dimension_revision(self.root)

    def test_dimension_only_revision_accepts_a_reported_table_failure(self):
        proof, readback = self.dimension_revision_proof()
        self.table_failure()
        proof['dimensionStyleTable'] = self.data['dimensionStyleTable']
        write_json(readback, proof)
        _, reopened = verify_dimension_revision(self.root)
        self.assertEqual(reopened['dimensionStyleTable']['unresolved'], 1)

    def test_dimension_only_revision_checks_definition_even_when_table_passes(self):
        proof, readback = self.dimension_revision_proof()
        proof['dimensionStyleTable'] = self.data['dimensionStyleTable']
        proof['dimensions'][0]['parts'][0]['textStyleDefinitionVerified'] = False
        write_json(readback, proof)
        with self.assertRaises(JobError):
            verify_dimension_revision(self.root)

    def test_old_report_cannot_certify_new_dwg(self):
        self.data['outputSHA256'] = 'PREVIOUS'
        with self.assertRaises(JobError): self.check()

    def test_pass_flag_cannot_hide_wrong_height(self):
        self.data['objects'][0]['glyphs'][0]['height'] = 300
        with self.assertRaises(JobError): self.check()

    def test_name_only_cannot_certify_definition(self):
        self.data['objects'][0]['glyphs'][0]['definitionVerified'] = False
        with self.assertRaises(JobError): self.check()

    def test_exception_can_continue_but_is_not_all_passed(self):
        self.data.update(unresolved=1, allApplicableVerified=False)
        self.data['objects'][0].update(verified=False, reason='native field cannot be read')
        self.assertEqual(len(self.check()), 1)
        with self.assertRaises(JobError): self.check(formal=True)

    def test_empty_inventory_cannot_pass(self):
        self.data.update(objects=[], checked=0)
        with self.assertRaises(JobError): self.check()
