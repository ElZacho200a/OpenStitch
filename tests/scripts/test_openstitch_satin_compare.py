"""Contrats d'une comparaison locale de satins, sans OpenStitch installe."""
import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/openstitch_satin_compare.py'
spec = importlib.util.spec_from_file_location('satin_compare', SCRIPT)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class SatinCompareTest(unittest.TestCase):
    def test_pareto_tradeoff(self):
        samples = [
            {'strategy': 'skeleton', 'status': 'ok', 'coverage': 0.99,
             'overflow': 0.2, 'jumps': 2, 'stitches': 100},
            {'strategy': 'parallel_sweep', 'status': 'ok', 'coverage': 0.99,
             'overflow': 0.2, 'jumps': 2, 'stitches': 95},
            {'strategy': 'medial_axis', 'status': 'ok', 'coverage': 0.5,
             'overflow': 0.8, 'jumps': 5, 'stitches': 160},
        ]
        self.assertEqual(mod.pareto(samples), ['parallel_sweep'])

    def test_rejected_or_zero_not_ranked(self):
        samples = [{'strategy': 'skeleton', 'status': 'rejected', 'coverage': 1.0,
                    'overflow': 0, 'jumps': 0, 'stitches': 120},
                   {'strategy': 'medial_axis', 'status': 'ok', 'coverage': 1.0,
                    'overflow': 0, 'jumps': 0, 'stitches': 0}]
        self.assertEqual(mod.pareto(samples), [])

    def test_batch_uses_native_auto_satin_and_dryrun(self):
        with tempfile.TemporaryDirectory() as root:
            out = Path(root) / 'outputs'
            calls = []
            def mock_call(cli, *args, **kwargs):
                calls.append(args)
                if 'inspect' in args:
                    if '--object' in args and str(args[2]) == 'initial.osp':
                        return {'schema': 'openstitch.object-inspect/1', 'object': {'id': 42, 'type': 'tatami'}}
                    return {'object': {'stitches': {'total': 80, 'jumps': 3}}}
                if 'check' in args:
                    return {'summary': {'errors': 0}, 'objects': [{'id': 42, 'covered_ratio': 0.97, 'overflow_ratio': 0.01}], 'findings': []}
                return {'ok': True}
            with patch.object(mod, 'call', side_effect=mock_call):
                res = mod.compare('/cli', Path('initial.osp'), 42, out, 0.4, False)
            self.assertEqual(len(res['trials']), 3)
            self.assertEqual([x['status'] for x in res['trials']], ['ok'] * 3)
            self.assertTrue(any('--dry-run' in args for args in calls))
            batch = (out / 'skeleton.batch.json').read_text()
            self.assertIn('"op": "auto_satin"', batch)
            self.assertIn('"strategy": "skeleton"', batch)
            self.assertTrue((out / 'comparison.json').is_file())

    def test_existing_output_refused(self):
        with tempfile.TemporaryDirectory() as root:
            out = Path(root)
            (out / 'skeleton.osp').write_text('do not overwrite')
            with patch.object(mod, 'call', return_value={'schema': 'openstitch.object-inspect/1',
                                                         'object': {'id': 3, 'type': 'auto_satin'}}):
                with self.assertRaisesRegex(RuntimeError, 'aucun ecrasement'):
                    mod.compare('/cli', Path('initial.osp'), 3, out, 0.4, False)
            self.assertEqual((out / 'skeleton.osp').read_text(), 'do not overwrite')


if __name__ == '__main__':
    unittest.main()
