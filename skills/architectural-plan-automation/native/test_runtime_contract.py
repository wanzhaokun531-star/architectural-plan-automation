from pathlib import Path
import json
import shutil
import tempfile
import unittest

from runtime_preflight import typography_contract


class RuntimeContractTests(unittest.TestCase):
    def test_native_contract_uses_current_parameters_and_rejects_drift(self):
        skill = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder = root / 'assets/annotation-template'
            folder.mkdir(parents=True)
            for name in ('standard.json', 'typography.tsv'):
                shutil.copyfile(skill / 'assets/annotation-template' / name, folder / name)
            self.assertTrue(typography_contract(root)['verified'])
            contract = (folder / 'typography.tsv').read_text('ascii')
            (folder / 'typography.tsv').write_text(contract.replace(' 3 ', ' 4 '), encoding='ascii')
            with self.assertRaises(ValueError):
                typography_contract(root)
            (folder / 'typography.tsv').write_text(contract, encoding='ascii')
            standard = json.loads((folder / 'standard.json').read_text('utf-8-sig'))
            standard['finalTypographyPolicy']['styles'][0]['heightMM'] += 1
            (folder / 'standard.json').write_text(json.dumps(standard), encoding='utf-8')
            with self.assertRaises(ValueError):
                typography_contract(root)


if __name__ == '__main__':
    unittest.main()
