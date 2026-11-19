import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from owned_plan_host import JobError, file_hash, write_json
from wall_height_pipeline import compare_walls, frozen_text, reconcile_wall_heights, require_wall_proof, prepared_wall_basis
from wall_height_pipeline import run_stage, WallStageFailure
from test_owned_plan_host import write_closed_host


def sample(height=3000):
    return {'floors': [{'component': 1, 'floor': 1, 'targetMM': 3600,
                        'basis': 'explicit-storey-height', 'finishedMetres': 0,
                        'datumBasis': 'explicit-finished-floor',
                        'walls': [{'handle': 'A', 'nativeLowWall': False, 'height': height,
                                   'base': 0, 'left': 120, 'right': 120},
                                  {'handle': 'B', 'nativeLowWall': True, 'height': 1100,
                                   'base': 0, 'left': 100, 'right': 100}]}], 'excludedWalls': []}


class WallHeightPipelineTests(unittest.TestCase):
    def test_cached_success_from_crashed_wall_host_is_not_reused(self):
        for all_crashed in (False, True):
            with self.subTest(all_crashed=all_crashed), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                source = root / 'source.dwg'; source.write_bytes(b'source')
                module = root / 'module.arx'; module.write_bytes(b'module')
                policy = root / 'assets/plan-policy'; policy.mkdir(parents=True)
                (policy / 'rule-catalog.tsv').write_text('policy')
                work = root / 'work'
                for number in range(1, 4):
                    attempt = work / f'attempt-{number:02}'
                    write_closed_host(attempt, workspace=root, module=module,
                                      workerExitCode=3221225477 if all_crashed or number == 1 else 0)
                    (attempt / 'job').mkdir()
                    write_json(attempt / 'job/product-result.json',
                               {'completed': True, 'readOnly': True, 'sourceSHA256': file_hash(source)})
                with patch('wall_height_pipeline.run_owned_plan', side_effect=AssertionError('no CAD')):
                    if all_crashed:
                        with self.assertRaises(WallStageFailure):
                            run_stage(root, module, source, work, root, inspect=True)
                    else:
                        for _ in range(2):
                            self.assertEqual(run_stage(root, module, source, work, root, inspect=True),
                                             work / 'attempt-02/job')

    def test_generation_handoff_carries_detached_wall_pieces_and_source_bound_omissions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            drawing = root / 'prepared.dwg'
            drawing.write_bytes(b'prepared')
            write_json(root / 'product-result.json', {'outputSHA256': file_hash(drawing), 'sourceSHA256': 'SOURCE'})
            write_json(root / 'storey-wall-height-readback.json', {'sourceSHA256': 'SOURCE',
                'levels': [{'panel': 2, 'heightMM': 3600, 'finishedMetres': 0,
                            'heightBasis': 'explicit-storey-height', 'datumBasis': 'first-floor-project-zero'}],
                'walls': [{'panel': 2, 'object': h, 'actualHeight': 3600, 'actualBase': 0} for h in ('A', 'B')]})
            write_json(root / 'plan-panel-ownership.json', {'sourceSHA256': 'SOURCE', 'components': [
                {'component': 1, 'role': 'detail', 'floor': None, 'walls': ['C'],
                 'result': 'omitted-with-evidence', 'reason': 'confirmed site component'},
                {'component': 2, 'role': 'floor', 'floor': 1, 'walls': ['A']},
                {'component': 3, 'role': 'attached-floor-component', 'floor': 1, 'parentPanel': 2, 'walls': ['B']}]})
            expected = prepared_wall_basis(drawing, root)
            self.assertEqual([w['handle'] for w in expected['floors'][0]['walls']], ['A', 'B'])
            self.assertEqual(expected['excludedWalls'][0]['role'], 'confirmed-non-floor')
            text = frozen_text(expected, file_hash(drawing))
            self.assertIn('WALL 2 B', text)
            self.assertIn('OMIT C confirmed-non-floor', text)
            write_json(root / 'design-source-readback.json', {'sourceSHA256': 'SOURCE', 'savedReopened': True,
                       'boundaryWalls': ['A']})
            separated = prepared_wall_basis(drawing, root)
            self.assertEqual([w['handle'] for w in separated['floors'][0]['walls']], ['B'])
            self.assertEqual(next(w for w in separated['excludedWalls'] if w['handle'] == 'A')['role'], 'confirmed-non-floor')
            drawing.write_bytes(b'another project')
            with self.assertRaises(JobError):
                prepared_wall_basis(drawing, root)

    def test_wrong_height_detected_but_low_wall_preserved(self):
        rows, issues = compare_walls(sample(), sample())
        self.assertEqual([row['object'] for row in issues], ['A'])
        self.assertEqual(rows[1]['targetHeight'], 1100)
        self.assertEqual(compare_walls(sample(), sample(3600))[1], [])
        self.assertEqual(compare_walls(sample(0), sample(0))[1][0]['object'], 'A')

    def test_missing_extra_duplicate_and_reassigned_walls_are_not_passed(self):
        for edit in ('missing', 'extra', 'floor', 'low', 'base', 'thickness'):
            actual = sample(3600)
            floor = actual['floors'][0]
            if edit == 'missing':
                floor['walls'].pop(0)
            elif edit == 'extra':
                floor['walls'].append(dict(floor['walls'][0], handle='C'))
            elif edit == 'floor':
                floor['floor'] = 2
            elif edit == 'low':
                floor['walls'][0]['nativeLowWall'] = True
            elif edit == 'base':
                floor['walls'][0]['base'] = 100
            else:
                floor['walls'][0]['left'] = 150
            with self.subTest(edit=edit):
                self.assertTrue(compare_walls(sample(), actual)[1])
        actual = sample(3600)
        actual['floors'][0]['walls'].append(actual['floors'][0]['walls'][0])
        with self.assertRaises(JobError):
            compare_walls(sample(), actual)

    def test_conflict_cannot_become_default_or_a_silent_success(self):
        expected = sample()
        expected['floors'][0]['targetMM'] = None
        expected['floors'][0]['basis'] = 'conflicting-source'
        self.assertTrue(compare_walls(expected, sample(3300))[1])
        self.assertIn('null conflicting-source', frozen_text(expected, 'HASH'))

    def test_excluded_invalid_wall_is_reported_roof_has_explicit_disposition(self):
        expected = sample(3600)
        expected['excludedWalls'] = [{'handle': 'C', 'role': 'unreadable', 'reason': 'zero baseline'}]
        self.assertEqual(compare_walls(expected, copy.deepcopy(expected))[1][0]['object'], 'C')
        expected['excludedWalls'][0]['role'] = 'roof'
        self.assertEqual(compare_walls(expected, copy.deepcopy(expected))[1], [])
        expected['floors'] = []
        self.assertTrue(compare_walls(expected, expected)[1])

    def test_repairs_are_followed_by_independent_reads_and_bounded(self):
        for fixed in (True, False):
            with self.subTest(fixed=fixed), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                source = root / 'input.dwg'
                source.write_bytes(b'source')
                calls = []
                def native(workspace, module, drawing, work, skill, *, inspect, expected, floor_levels=None):
                    calls.append(inspect)
                    work.mkdir(parents=True)
                    if inspect:
                        height = 3600 if fixed and drawing != source else 3000
                        write_json(work / 'wall-height-preflight.json', sample(height))
                    else:
                        (work / 'candidate.dwg').write_bytes(str(len(calls)).encode())
                    return work
                with patch('wall_height_pipeline.run_stage', native):
                    result = reconcile_wall_heights(source, root / 'work', root, root, root / 'module')
                self.assertEqual(calls, [True, False, True] if fixed else [True, False, True, False, True])
                self.assertEqual(bool(result['unresolved']), not fixed)
                self.assertEqual(source.read_bytes(), b'source')
                proof = json.loads(Path(result['proof']).read_text())
                self.assertEqual(proof['outputSHA256'], file_hash(result['drawing']))

    def test_publication_requires_same_version_wall_proof_and_listed_exceptions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            drawing = root / 'final.dwg'
            drawing.write_bytes(b'final')
            result = {'drawing': str(drawing), 'sha256': file_hash(drawing), 'unresolved': [],
                      'wallHeightProof': str(root / 'proof.json')}
            write_json(root / 'proof.json', {'contract': 'plan.final-wall-heights.v1', 'savedReopened': True,
                                           'outputSHA256': result['sha256'], 'unresolved': []})
            require_wall_proof(result)
            proof = json.loads((root / 'proof.json').read_text())
            proof['unresolved'] = [{'object': 'A', 'role': 'wall-height', 'reason': 'conflict'}]
            write_json(root / 'proof.json', proof)
            with self.assertRaises(JobError):
                require_wall_proof(result)
            result['unresolved'] = proof['unresolved']
            require_wall_proof(result)
            drawing.write_bytes(b'changed')
            with self.assertRaises(JobError):
                require_wall_proof(result)

    def test_late_height_repair_rebinds_sheets_and_reprints_the_repaired_version(self):
        from template_revision import execute_template_revision
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source.dwg'
            source.write_bytes(b'original')
            module = root / 'module.arx'
            module.write_bytes(b'module')
            policy = root / 'assets/plan-policy'
            policy.mkdir(parents=True)
            (policy / 'rule-catalog.tsv').write_text('rules')
            sheets = root / 'sheets.tsv'
            sheets.write_text('PLANJOINT1 ' + file_hash(source) + '\nSHEET 1 "one"\nEND\n')
            fixed = root / 'fixed.dwg'
            fixed.write_bytes(b'height fixed')
            expected = sample(3600)
            calls = []
            def template(drawing, bound_sheets, work, *args, **kwargs):
                self.assertEqual(bound_sheets.read_text().splitlines()[0], 'PLANJOINT1 ' + file_hash(drawing))
                calls.append(file_hash(drawing))
                work.mkdir(parents=True)
                output = work / 'candidate.dwg'
                output.write_bytes(('template ' + str(len(calls))).encode())
                return {'drawing': str(output), 'sha256': file_hash(output), 'report': str(work),
                        'unresolved': [], 'formal': True}
            def reconcile(drawing, work, *args, **kwargs):
                work.mkdir(parents=True)
                if work.name == 'final-0':
                    return {'drawing': str(fixed), 'sha256': file_hash(fixed), 'expected': expected, 'unresolved': []}
                proof = work / 'proof.json'
                write_json(proof, {'contract': 'plan.final-wall-heights.v1', 'savedReopened': True,
                                   'outputSHA256': file_hash(drawing), 'unresolved': []})
                return {'drawing': str(drawing), 'sha256': file_hash(drawing), 'expected': expected,
                        'proof': str(proof), 'unresolved': []}
            with patch('template_revision._execute_template_revision', template), \
                 patch('wall_height_pipeline.reconcile_wall_heights', reconcile):
                result = execute_template_revision(source, sheets, root / 'work', root, root, module=module)
            self.assertEqual(calls, [file_hash(source), file_hash(fixed)])
            self.assertTrue(result['formal'])
            self.assertEqual(json.loads(Path(result['wallHeightProof']).read_text())['outputSHA256'], result['sha256'])


if __name__ == '__main__':
    unittest.main()
