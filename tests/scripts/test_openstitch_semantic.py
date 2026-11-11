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

    def test_deep_parent_hierarchy_does_not_exceed_recursion_limit(self):
        # A long valid hierarchy used to raise RecursionError in cycles().
        count = 1500
        nodes = [region(f'n{i:04}', parent_id=f'n{i + 1:04}' if i + 1 < count else None)
                 for i in range(count)]
        result = mod.validate(document(*nodes), self.folder, skip_files=True)
        self.assertTrue(result['ok'], result['errors'])

    def test_deep_parent_cycle_reports_validation_error(self):
        count = 1500
        nodes = [region(f'n{i:04}', parent_id=f'n{i + 1:04}' if i + 1 < count else 'n0000')
                 for i in range(count)]
        result = mod.validate(document(*nodes), self.folder, skip_files=True)
        self.assertFalse(result['ok'])
        self.assertIn('E_PARENT_CYCLE', [error['code'] for error in result['errors']])

    def test_foreign_reference(self):
        self.assertIn('E_REFERENCE', self.errors(document(region('a', over=['missing']))))

    def test_missing_mask_and_wrong_size(self):
        self.assertIn('E_MISSING_MASK', self.errors(document(region('a', mask_path='absent.png'))))
        self.folder.joinpath('mask.png').write_bytes(b'\x89PNG\r\n\x1a\n' +
            (13).to_bytes(4, 'big') + b'IHDR' + (10).to_bytes(4, 'big') + (3).to_bytes(4, 'big'))
        self.assertIn('E_MASK_SIZE', self.errors(document(region('a'))))

    def test_path_escape(self):
        self.assertIn('E_PATH', self.errors(document(region('a', mask_path='../outside.png'))))

    def test_skip_files_still_rejects_path_escape(self):
        self.assertIn('E_PATH', self.errors(
            document(region('a', mask_path='../outside.png')), skip_files=True))
        # Missing in-folder files are allowed only when file checks are skipped.
        self.assertNotIn('E_MISSING_MASK', self.errors(
            document(region('a', mask_path='absent.png')), skip_files=True))

    def test_skip_files_rejects_symlink_escape(self):
        outside = self.folder.parent / (self.folder.name + '-outside.png')
        outside.write_bytes(b'not a png')
        self.addCleanup(outside.unlink)
        link = self.folder / 'linked.png'
        try:
            link.symlink_to(outside)
        except (OSError, NotImplementedError):
            self.skipTest('symlinks unavailable')
        for skip in (False, True):
            with self.subTest(skip_files=skip):
                self.assertIn('E_PATH', self.errors(
                    document(region('a', mask_path='linked.png')), skip_files=skip))

    def test_malformed_mask_path_is_validation_error(self):
        for skip in (False, True):
            with self.subTest(skip_files=skip):
                self.assertIn('E_PATH', self.errors(
                    document(region('a', mask_path='bad\x00name.png')), skip_files=skip))

    def test_absolute_mask_path_is_rejected_even_inside_manifest(self):
        absolute = str(self.folder / 'mask.png')
        for skip in (False, True):
            with self.subTest(skip_files=skip):
                self.assertIn('E_PATH', self.errors(
                    document(region('a', mask_path=absolute)), skip_files=skip))

    def test_windows_mask_paths_are_rejected_on_all_hosts(self):
        # A manifest must mean the same thing on Linux and Windows.
        bad_paths = (
            'C:/temp/mask.png',   # Windows absolute path
            'C:mask.png',        # Windows drive-relative path
            '..\\\\outside.png',   # Windows-style parent traversal
            '\\\\server\\share\\mask.png',  # UNC path
            'subdir\\mask.png',  # Ambiguous path separator
        )
        for mask in bad_paths:
            for skip in (False, True):
                with self.subTest(mask=mask, skip_files=skip):
                    self.assertIn('E_PATH', self.errors(
                        document(region('a', mask_path=mask)), skip_files=skip))
        # Portable forward-slash relative paths are still accepted in skip mode.
        self.assertNotIn('E_PATH', self.errors(
            document(region('a', mask_path='subdir/mask.png')), skip_files=True))

    def test_manifest_schema_and_required_mask(self):
        self.assertIn('E_SCHEMA', self.errors({'schema': 'wrong', **{k: v for k,v in document().items() if k != 'schema'}}))
        self.assertIn('E_MASK', self.errors(document(region('a', mask_path=None))))


    def test_malformed_agent_fields_are_errors_not_exceptions(self):
        broken = document(region('a', priority=[], level={}, stitch_intent=[],
                                 decision=[], parent_id={}))
        errors = self.errors(broken)
        for code in ('E_PRIORITY', 'E_LEVEL', 'E_STITCH', 'E_DECISION', 'E_PARENT'):
            self.assertIn(code, errors)


if __name__ == '__main__':
    unittest.main()
