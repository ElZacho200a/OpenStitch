"""Tests du manifeste : IDs, cycles, superpositions, PNG et chemins."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/openstitch_semantic.py'
spec = importlib.util.spec_from_file_location('openstitch_semantic', SCRIPT)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def region(sid, **kwargs):
    d = {'semantic_id': sid, 'family': 'vegetation', 'role': 'feuille', 'priority': 'important',
         'stitch_intent': 'tatami', 'mask_path': 'mask.png', 'under': [], 'over': []}
    d.update(kwargs)
    return d


def document(*items):
    return {'schema': mod.SCHEMA, 'source_image': {'width_px': 2, 'height_px': 3},
            'design': {'width_mm': 300, 'height_mm': 200}, 'regions': list(items)}


class SemanticValidationTest(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.folder = Path(self.work.name)
        self.folder.joinpath('mask.png').write_bytes(b'\x89PNG\r\n\x1a\n' +
            (13).to_bytes(4, 'big') + b'IHDR' + (2).to_bytes(4, 'big') + (3).to_bytes(4, 'big'))

    def errors(self, doc, skip_files=False):
        result = mod.validate(doc, self.folder, skip_files=skip_files)
        return [e['code'] for e in result['errors']]

    def test_valid_semantic_scene(self):
        d = document(region('lierre', level='instance', parent_id=None, stitch_intent=None, mask_path=None),
                     region('lierre.feuille1', parent_id='lierre', under=['mur']),
                     region('mur', family='architecture', role='pierre', over=['lierre.feuille1']))
        self.assertTrue(mod.validate(d, self.folder)['ok'])

    def test_duplicate_id(self):
        self.assertIn('E_DUPLICATE', self.errors(document(region('a'), region('a'))))

    def test_unknown_parent(self):
        self.assertIn('E_PARENT', self.errors(document(region('a', parent_id='unknown'))))

    def test_parent_cycle(self):
        self.assertIn('E_PARENT_CYCLE', self.errors(document(region('a', parent_id='b'), region('b', parent_id='a'))))

    def test_occlusion_cycle(self):
        self.assertIn('E_OCCLUSION_CYCLE', self.errors(document(region('a', under=['b']), region('b', under=['a']))))

    def test_foreign_reference(self):
        self.assertIn('E_REFERENCE', self.errors(document(region('a', over=['missing']))))

    def test_missing_mask_and_wrong_size(self):
        self.assertIn('E_MISSING_MASK', self.errors(document(region('a', mask_path='absent.png'))))
        self.folder.joinpath('mask.png').write_bytes(b'\x89PNG\r\n\x1a\n' +
            (13).to_bytes(4, 'big') + b'IHDR' + (10).to_bytes(4, 'big') + (3).to_bytes(4, 'big'))
        self.assertIn('E_MASK_SIZE', self.errors(document(region('a'))))

    def test_path_escape(self):
        self.assertIn('E_PATH', self.errors(document(region('a', mask_path='../outside.png'))))

    def test_manifest_schema_and_required_mask(self):
        self.assertIn('E_SCHEMA', self.errors({'schema': 'wrong', **{k: v for k,v in document().items() if k != 'schema'}}))
        self.assertIn('E_MASK', self.errors(document(region('a', mask_path=None))))


if __name__ == '__main__':
    unittest.main()
