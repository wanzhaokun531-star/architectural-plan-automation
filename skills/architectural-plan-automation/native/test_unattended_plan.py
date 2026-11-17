import tempfile
import unittest
from pathlib import Path

from unattended_plan import (
    JobError, RetryableHostError, execute_plan, file_hash, plan_resource_files, source_project_parameters,
    workspace_root, write_json,
)
from owned_plan_host import bridge_ini_text, packaged_host_inputs


class UnattendedPlanTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source.dwg'
        self.source.write_bytes(b'original')
        self.output = self.root / 'delivery' / 'formal.dwg'
        self.output.parent.mkdir()
        self.work = self.root / 'internal'
        self.options = {'moduleSHA256': 'test-module', 'resourcesSHA256': 'test-resources'}

    def evidence(self, attempt, *, formal=True, wrong_print=False, wrong_ink=False, bad_checklist=False):
        report = attempt / 'job'
        report.mkdir(parents=True)
        drawing = report / 'candidate.dwg'
        drawing.write_bytes(b'verified-dwg')
        pdf = report / 'layout.pdf'
        pdf.write_bytes(b'%PDF-1.4')
        drawing_hash = file_hash(drawing)
        binding = {'sourceSHA256': file_hash(self.source), 'outputSHA256': drawing_hash}
        checklist = report / 'candidate_遗留清单.csv'
        if not formal:
            checklist.write_text('源图幅,实际部位,源对象,适用规则,实际楼层,问题,图面保留状态,DWG SHA-256\n'
                                 f'2,东侧,23A,D23,二层,尺寸界线相交,保留画法,{drawing_hash}\n', encoding='utf-8-sig')
        write_json(report / 'product-result.json', {
            **binding, 'contract': 'bcp.product-result.v1', 'records': str(report), 'output': str(drawing),
            'completed': True, 'published': True, 'finalDocumentOpened': True, 'sourceUnchanged': True,
            'constructionDrawingComplete': formal, 'formalDelivery': True,
            'deliveryStatus': 'formal' if formal else 'formal-with-exceptions',
            'blockingChecklist': '' if formal else str(checklist),
            'checklistSHA256': '' if formal else 'wrong' if bad_checklist else file_hash(checklist),
        })
        write_json(report / 'plan-delivery-review.json', {
            **binding, 'acceptanceMode': 'architectural-plan', 'formalRequested': True, 'formalReady': formal,
            'formalDelivery': True, 'dwgReopened': True, 'plottedPages': 1,
            'gates': dict.fromkeys(('mainPlan','reopened','files'), formal),
            'blockingLocations': [] if formal else [{'panel': 2, 'source': '23A', 'findings': [{'check': 'interior-clearance'}]}],
        })
        write_json(report / 'plan-rule-ledger.json', {**binding, 'formalReady': formal})
        write_json(report / 'final-typography-readback.json', {
            'outputSHA256': drawing_hash, 'savedReopened': True, 'independentInventory': True,
            'checked': 1, 'unresolved': 0, 'allApplicableVerified': True,
            'dimensionStyleTable': {'checked': 1, 'unresolved': 0, 'allApplicableVerified': True,
                                    'styles': [{'name': '_TCH_ARCH', 'textStyle': '_TCH_DIM',
                                                'definitionVerified': True, 'verified': True, 'reason': ''}]},
            'objects': [{'object': 'TEXT1', 'role': 'general', 'expectedHeight': 400, 'verified': True,
                         'glyphs': [{'style': 'FY_HZ', 'font': 'tssdeng.shx', 'bigFont': 'hztxt.shx',
                                     'height': 400, 'definitionVerified': True}]}],
        })
        write_json(report / 'formal-layout-print-review.json', {
            'outputSHA256': drawing_hash, 'pages': [{'panel': 1, 'status': 'plotted', 'file': pdf.name,
                'sha256': 'wrong' if wrong_print else file_hash(pdf), 'paperScale': 1, 'viewportScale': .01}],
        })
        write_json(report / 'plan-printed-ink.json', {
            'contract': 'plan.printed-ink.v1', 'drawingSHA256': drawing_hash,
            'verified': formal, 'pages': [{'file': pdf.name, 'sha256': 'other' if wrong_ink else file_hash(pdf)}],
            'checked': 4, 'expected': 4, 'unresolved': 0 if formal else 1,
        })
        write_json(report / 'plan-postcheck.json', {
            **binding, 'contract': 'plan.postcheck.v1', 'drawingSHA256': drawing_hash,
            'mode': 'repair', 'readOnly': True,
        })

    def run_plan(self, attempt):
        return execute_plan(self.source, self.output, self.work, self.options, attempt)

    def test_resource_locks_do_not_invalidate_a_restart(self):
        assets = self.root / 'assets'
        assets.mkdir()
        resource = assets / 'reference.dwg'
        resource.write_bytes(b'approved resource')
        before = plan_resource_files(assets)
        (assets / 'reference.dwl').write_bytes(b'host lock')
        (assets / 'reference.DWL2').write_bytes(b'host lock metadata')
        self.assertEqual(plan_resource_files(assets), before)
        self.assertEqual(before, [resource])

    def test_installed_host_wins_over_workspace_and_binds_local_profile(self):
        package = self.root / 'portable install with spaces'
        host = package / 'host'
        controller = host / 'controller'
        controller.mkdir(parents=True)
        (controller / 'run_quiet_job.py').touch()
        source_workspace = self.root / 'checkout'
        legacy = source_workspace / '阶段1/原生接口探测'
        legacy.mkdir(parents=True)
        (legacy / 'run_quiet_job.py').touch()
        self.assertEqual(workspace_root(None, source_workspace, package), package)
        self.assertEqual(workspace_root(source_workspace, source_workspace, package), source_workspace)
        with self.assertRaisesRegex(JobError, 'Install-PlanDraw'):
            packaged_host_inputs(package)
        configuration = {'profile': 'CodexS1Native_installed', 'supportComponent': str(self.root / 'vendor.arx')}
        write_json(host / 'machine.json', configuration)
        session = host / 'runs/isolated-plan-portable-12345678'
        staged_module = self.root / 'staging/PlanNative.arx'
        text = bridge_ini_text(host, session, '12345678', configuration, staged_module)
        for line in (f'root={host}', f'session={session}', 'expectedProfile=CodexS1Native_installed',
                     f'supportComponent={configuration["supportComponent"]}', f'module={staged_module}'):
            self.assertIn(line, text.splitlines())
        self.assertIn(host / 'machine.json', packaged_host_inputs(package))

    def test_transient_host_retries_and_only_delivers_formal_dwg(self):
        attempts = []

        def execute(directory):
            attempts.append(directory)
            if len(attempts) == 1:
                raise RetryableHostError('isolated CAD exited')
            self.evidence(directory)

        self.assertEqual(self.run_plan(execute), self.output)
        self.assertEqual(len(set(attempts)), 2)
        self.assertEqual(list(self.output.parent.iterdir()), [self.output])
        self.assertEqual(self.output.read_bytes(), b'verified-dwg')

    def test_completed_receipt_is_reused_after_controller_interruption(self):
        def execute(directory):
            self.evidence(directory)
            raise KeyboardInterrupt

        with self.assertRaises(KeyboardInterrupt):
            self.run_plan(execute)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.run_plan(lambda _: self.fail('already generated')), self.output)
        self.assertEqual(self.run_plan(lambda _: self.fail('already delivered')), self.output)

    def test_running_attempt_can_be_reattached_at_the_saved_checkpoint(self):
        def interrupted(directory):
            directory.mkdir()
            (directory / 'running').touch()
            raise KeyboardInterrupt

        def resume(directory):
            self.assertTrue((directory / 'running').is_file())
            self.evidence(directory)

        with self.assertRaises(KeyboardInterrupt):
            self.run_plan(interrupted)
        self.assertEqual(execute_plan(self.source, self.output, self.work, self.options,
            lambda _: self.fail('should reattach to the first attempt'), resume_attempt=resume), self.output)

    def test_unpassed_item_delivers_current_dwg_and_bound_checklist_without_replaying(self):
        self.assertEqual(self.run_plan(lambda path: self.evidence(path, formal=False)), self.output)
        checklist = self.output.with_name('formal_遗留清单.csv')
        self.assertIn(file_hash(self.output), checklist.read_text(encoding='utf-8-sig'))
        self.assertEqual(set(self.output.parent.iterdir()), {self.output, checklist})
        checklist.unlink()
        self.assertEqual(self.run_plan(lambda _: self.fail('should reuse checked evidence')), self.output)
        self.assertTrue(checklist.is_file())

    def test_exception_checklist_hash_mismatch_blocks_publication(self):
        with self.assertRaises(JobError):
            self.run_plan(lambda path: self.evidence(path, formal=False, bad_checklist=True))
        self.assertEqual(list(self.output.parent.iterdir()), [])

    def test_automatically_reads_only_the_current_sources_project_parameters(self):
        profile = self.source.with_suffix('.plan-project.tsv')
        self.assertIsNone(source_project_parameters(self.source))
        profile.write_text(f'PLANPROJECT1 {file_hash(self.source)}\nEND\n', encoding='utf-8')
        self.assertEqual(source_project_parameters(self.source), profile)
        self.source.write_bytes(b'revised source')
        self.assertIsNone(source_project_parameters(self.source))

    def test_print_hash_mismatch_blocks_publication(self):
        for mismatch in ('wrong_print', 'wrong_ink'):
            with self.subTest(mismatch=mismatch), self.assertRaises(JobError):
                execute_plan(self.source, self.output, self.work / mismatch, self.options,
                             lambda path: self.evidence(path, **{mismatch: True}))
            self.assertFalse(self.output.exists())

    def test_repair_retries_incomplete_same_version_print_even_with_a_checklist(self):
        self.options['nativeConfiguration'] = {'postcheckMode': 'repair'}
        attempts = []

        def execute(directory):
            attempts.append(directory)
            self.evidence(directory, formal=False, wrong_print=len(attempts) == 1)

        self.assertEqual(self.run_plan(execute), self.output)
        self.assertEqual(len(attempts), 2)
        self.assertTrue(self.output.with_name('formal_遗留清单.csv').is_file())
        self.assertEqual(self.run_plan(lambda _: self.fail('must reuse the same-version proof')), self.output)

    def test_delivery_directory_is_created_only_after_formal_pass(self):
        self.output.parent.rmdir()
        self.assertEqual(self.run_plan(self.evidence), self.output)
        self.assertEqual(list(self.output.parent.iterdir()), [self.output])

    def test_transient_limit_does_not_loop_or_overwrite(self):
        attempts = []

        def execute(directory):
            attempts.append(directory)
            raise RetryableHostError('temporary CAD failure')

        with self.assertRaises(JobError):
            self.run_plan(execute)
        self.assertEqual(len(attempts), 3)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
