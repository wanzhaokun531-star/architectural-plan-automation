import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from owned_plan_host import (JobError, RetryableHostError, file_hash, read_json, write_json,
                             finish_owned_attempt, resume_owned_plan, verify_owned_host_closure)


def write_closed_host(attempt, *, workspace=None, module=None, **changes):
    """Synthetic evidence only; never starts or inspects any process."""
    attempt = Path(attempt)
    attempt.mkdir(parents=True, exist_ok=True)
    nonce = hashlib.sha256(str(attempt).encode()).hexdigest()[:32]
    root = Path(workspace) if workspace is not None else attempt.parent
    (root / 'host/controller').mkdir(parents=True, exist_ok=True)
    (root / 'host/controller/run_quiet_job.py').touch(exist_ok=True)
    session = root / 'host/runs' / ('isolated-plan-portable-' + nonce[:8])
    session.mkdir(parents=True, exist_ok=True)
    identity = {'nonce': nonce, 'session': str(session.resolve()), 'processId': 100,
                'desktopName': 'CodexS1_' + nonce}
    write_json(attempt / 'host.json', {'session': str(session.resolve()), 'nonce': nonce,
               'moduleSHA256': file_hash(module) if module is not None else 'A' * 64})
    write_json(session / 'isolation-host-receipt.json',
               {'contract': 'stage1.isolated-host-launch.v1', **identity})
    terminal = {'contract': 'stage1.isolated-host-final.v1', **identity,
                'workerCreated': True, 'workerExited': True, 'readyIdentityConfirmed': True,
                'nativeQuitConfirmed': True, 'monitorStartedBeforeWorker': True,
                'monitorStarted': True, 'monitorStopped': True, 'eventFileWritten': True,
                'desktopClosed': True, 'processHandlesClosed': True,
                'forcedTermination': False, 'timedOut': False, 'inputDesktopChanged': False,
                'switchDesktopCalled': False, 'userCadTouched': False, 'stillRunningOwnedWorker': False,
                'workerExitCode': 0, 'inputDesktopReadErrors': 0, 'droppedEvents': 0,
                'workerForegroundEvents': 0, 'workerUserDesktopShowEvents': 0, 'workerForegroundSamples': 0,
                'monitorError': None, 'supervisorErrors': [], 'relatedCadEvents': 8,
                'relatedCadEventsNeedReview': True}
    terminal.update(changes)
    write_json(session / 'isolation-final.json', terminal)
    return session


class OwnedClosureTests(unittest.TestCase):
    def test_normal_resume_reuses_terminal_evidence_without_touching_a_process(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            module = root / 'module.arx'; module.write_bytes(b'module')
            attempt = root / 'attempt'
            write_closed_host(attempt, workspace=root, module=module)
            with patch('owned_plan_host.subprocess.check_output', side_effect=AssertionError('No process access')):
                self.assertEqual(verify_owned_host_closure(attempt, module=module, workspace=root)['workerExitCode'], 0)
                resume_owned_plan(root, module, attempt, root)

    def test_native_quit_marker_does_not_hide_crash_or_timeout(self):
        for field, value in [('workerExitCode', 3221225477), ('timedOut', True),
                             ('workerExited', False), ('forcedTermination', True),
                             ('workerUserDesktopShowEvents', 1), ('inputDesktopReadErrors', 1)]:
            with self.subTest(field=field), tempfile.TemporaryDirectory() as folder:
                attempt = Path(folder) / 'attempt'
                write_closed_host(attempt, **{field: value})
                with self.assertRaisesRegex(RetryableHostError, field):
                    verify_owned_host_closure(attempt)

    def test_nonce_session_process_and_missing_final_are_rejected(self):
        for field, value in [('nonce', 'b' * 32), ('session', 'other'), ('processId', 999)]:
            with self.subTest(field=field), tempfile.TemporaryDirectory() as folder:
                attempt = Path(folder) / 'attempt'
                session = write_closed_host(attempt, **{field: value})
                with self.assertRaises(RetryableHostError):
                    verify_owned_host_closure(attempt)
                (session / 'isolation-final.json').unlink()
                with self.assertRaises(RetryableHostError):
                    verify_owned_host_closure(attempt)

    def test_cleanup_preserves_business_error_and_still_runs_later_actions(self):
        with tempfile.TemporaryDirectory() as folder:
            attempt = Path(folder) / 'attempt'
            write_closed_host(attempt, workerExitCode=3221225477)
            actions = []
            original = JobError('original business failure')
            def bad_cleanup():
                actions.append('close')
                raise OSError('cleanup failure')
            try:
                try:
                    raise original
                finally:
                    finish_owned_attempt(attempt, [bad_cleanup, lambda: actions.append('retire')],
                                         primary_error=sys.exc_info()[1])
            except JobError as caught:
                self.assertIs(caught, original)
            self.assertEqual(actions, ['close', 'retire'])
            self.assertEqual(read_json(attempt / 'host-cleanup-errors.json')['primaryError'], str(original))
            with self.assertRaises(RetryableHostError):
                finish_owned_attempt(attempt, [])


if __name__ == '__main__':
    unittest.main()
