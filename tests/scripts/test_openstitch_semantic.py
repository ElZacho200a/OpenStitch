"""Tests du manifeste : IDs, cycles, superpositions, PNG et chemins."""
import importlib.util
import json
import struct
import zlib
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/openstitch_semantic.py'
spec = importlib.util.spec_from_file_location('openstitch_semantic', SCRIPT)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def png_bytes(width, height):
    """Petit PNG RGBA valide pour les tests d'en-tete."""
    def chunk(kind, data):
        payload = kind + data
        return (struct.pack('>I', len(data)) + payload +
                struct.pack('>I', zlib.crc32(payload) & 0xffffffff))

    ihdr = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    pixels = (b'\x00' + b'\x00\x00\x00\x00' * width) * height
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) +
            chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))


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
        self.folder.joinpath('mask.png').write_bytes(png_bytes(2, 3))

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
        self.folder.joinpath('mask.png').write_bytes(png_bytes(10, 3))
        self.assertIn('E_MASK_SIZE', self.errors(document(region('a'))))

    def test_png_ihdr_integrity_is_checked(self):
        good = png_bytes(2, 3)
        self.assertNotIn('E_MASK_PNG', self.errors(document(region('a'))))
        bad_parameters = bytearray(good)
        bad_parameters[26] = 1  # PNG n'autorise que compression=0.
        bad_parameters[29:33] = struct.pack(
            '>I', zlib.crc32(bad_parameters[12:29]) & 0xffffffff)
        cases = {
            'truncated_ihdr': good[:30],
            'wrong_ihdr_length': good[:8] + struct.pack('>I', 12) + good[12:],
            'bad_ihdr_crc': good[:29] + b'\x00\x00\x00\x00' + good[33:],
            'invalid_ihdr_parameters': bytes(bad_parameters),
        }
        for name, contents in cases.items():
            with self.subTest(case=name):
                self.folder.joinpath('mask.png').write_bytes(contents)
                self.assertIn('E_MASK_PNG', self.errors(document(region('a'))))
                # --skip-files ne lit pas les octets du masque.
                self.assertNotIn('E_MASK_PNG', self.errors(
                    document(region('a')), skip_files=True))

    def test_png_chunk_structure_and_crc_are_checked(self):
        good = png_bytes(2, 3)

        def chunk(kind, data):
            payload = kind + data
            return (struct.pack('>I', len(data)) + payload +
                    struct.pack('>I', zlib.crc32(payload) & 0xffffffff))

        idat_length = struct.unpack('>I', good[33:37])[0]
        idat_end = 33 + 12 + idat_length
        damaged_idat = bytearray(good)
        damaged_idat[41] ^= 1  # Change the payload without updating its CRC.
        ancillary = chunk(b'tEXt', b'Comment\x00ok')
        # Ancillary chunks after IDAT are legal and must remain accepted.
        self.folder.joinpath('mask.png').write_bytes(
            good[:idat_end] + ancillary + good[idat_end:])
        self.assertNotIn('E_MASK_PNG', self.errors(document(region('a'))))

        cases = {
            'header_only': good[:33],
            'missing_idat': good[:33] + good[idat_end:],
            'truncated_idat': good[:43],
            'missing_iend': good[:idat_end],
            'bad_idat_crc': bytes(damaged_idat),
            'trailing_bytes': good + b'junk',
            'duplicate_ihdr': good[:33] + good[8:33] + good[33:],
            'noncontiguous_idat': (good[:idat_end] + ancillary +
                                  chunk(b'IDAT', b'') + good[idat_end:]),
        }
        for name, contents in cases.items():
            with self.subTest(case=name):
                self.folder.joinpath('mask.png').write_bytes(contents)
                self.assertIn('E_MASK_PNG', self.errors(document(region('a'))))
                self.assertNotIn('E_MASK_PNG', self.errors(
                    document(region('a')), skip_files=True))

    def test_png_critical_chunks_and_palette_rules(self):
        # Build complete PNG fixtures; validation deliberately does not decode IDAT.
        def chunk(kind, data):
            payload = kind + data
            return (struct.pack('>I', len(data)) + payload +
                    struct.pack('>I', zlib.crc32(payload) & 0xffffffff))

        sig = b'\x89PNG\r\n\x1a\n'
        ihdr = lambda depth, color: chunk(
            b'IHDR', struct.pack('>IIBBBBB', 2, 3, depth, color, 0, 0, 0))
        palette = chunk(b'PLTE', b'\x00\x00\x00\xff\xff\xff')
        indexed_idat = chunk(b'IDAT', zlib.compress(b'\x00\x00' * 3))
        iend = chunk(b'IEND', b'')
        indexed = sig + ihdr(1, 3) + palette + indexed_idat + iend
        self.folder.joinpath('mask.png').write_bytes(indexed)
        self.assertNotIn('E_MASK_PNG', self.errors(document(region('a'))))

        rgba = png_bytes(2, 3)
        idat_length = struct.unpack('>I', rgba[33:37])[0]
        idat_end = 33 + 12 + idat_length
        cases = {
            'unknown_critical': rgba[:33] + chunk(b'ABCD', b'') + rgba[33:],
            'reserved_bit': rgba[:33] + chunk(b'tExt', b'') + rgba[33:],
            'indexed_missing_palette': sig + ihdr(1, 3) + indexed_idat + iend,
            'indexed_too_many_entries': (sig + ihdr(1, 3) +
                                         chunk(b'PLTE', b'\x00' * 9) +
                                         indexed_idat + iend),
            'indexed_bad_palette_length': (sig + ihdr(1, 3) +
                                           chunk(b'PLTE', b'\x00' * 4) +
                                           indexed_idat + iend),
            'indexed_duplicate_palette': (sig + ihdr(1, 3) + palette + palette +
                                          indexed_idat + iend),
            'grayscale_palette': (sig + ihdr(8, 0) + palette +
                                  indexed_idat + iend),
            'palette_after_idat': rgba[:idat_end] + palette + rgba[idat_end:],
            'empty_palette': rgba[:33] + chunk(b'PLTE', b'') + rgba[33:],
            'oversized_palette': (rgba[:33] + chunk(b'PLTE', b'\x00' * 771) +
                                  rgba[33:]),
        }
        for name, contents in cases.items():
            with self.subTest(case=name):
                self.folder.joinpath('mask.png').write_bytes(contents)
                self.assertIn('E_MASK_PNG', self.errors(document(region('a'))))
                self.assertNotIn('E_MASK_PNG', self.errors(
                    document(region('a')), skip_files=True))

    def test_png_transparency_chunk_rules(self):
        # tRNS is optional, but its length, placement and palette references
        # must be validated even though IDAT pixels are not decoded here.
        def chunk(kind, data):
            payload = kind + data
            return (struct.pack('>I', len(data)) + payload +
                    struct.pack('>I', zlib.crc32(payload) & 0xffffffff))

        sig = b'\\x89PNG\\r\\n\\x1a\\n'
        def ihdr(depth, color):
            return chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 3, depth, color, 0, 0, 0))

        palette = chunk(b'PLTE', b'\\x00\\x00\\x00\\xff\\xff\\xff')
        indexed_pixels = chunk(b'IDAT', zlib.compress(b'\\x00\\x00' * 3))
        gray_pixels = chunk(b'IDAT', zlib.compress(b'\\x00\\x00\\x00' * 3))
        rgb_pixels = chunk(b'IDAT', zlib.compress(b'\\x00' * 7 * 3))
        iend = chunk(b'IEND', b'')
        indexed_transparency = chunk(b'tRNS', b'\\x00\\xff')
        gray_transparency = chunk(b'tRNS', b'\\x00\\x00')
        rgb_transparency = chunk(b'tRNS', b'\\x00\\x00' * 3)
        indexed_prefix = sig + ihdr(1, 3) + palette
        valid = {
            'indexed': indexed_prefix + indexed_transparency + indexed_pixels + iend,
            'grayscale': sig + ihdr(8, 0) + gray_transparency + gray_pixels + iend,
            'rgb': sig + ihdr(8, 2) + rgb_transparency + rgb_pixels + iend,
        }
        for name, contents in valid.items():
            with self.subTest(valid=name):
                self.folder.joinpath('mask.png').write_bytes(contents)
                self.assertNotIn('E_MASK_PNG', self.errors(document(region('a'))))

        invalid = {
            'duplicate': (indexed_prefix + indexed_transparency +
                          indexed_transparency + indexed_pixels + iend),
            'after_idat': (indexed_prefix + indexed_pixels +
                           indexed_transparency + iend),
            'indexed_without_plte': (sig + ihdr(1, 3) + indexed_transparency +
                                     indexed_pixels + iend),
            'indexed_too_long': (indexed_prefix + chunk(b'tRNS', b'\\x00' * 3) +
                                 indexed_pixels + iend),
            'indexed_empty': (indexed_prefix + chunk(b'tRNS', b'') +
                              indexed_pixels + iend),
            'grayscale_bad_length': (sig + ihdr(8, 0) + chunk(b'tRNS', b'\\x00') +
                                     gray_pixels + iend),
            'rgb_bad_length': (sig + ihdr(8, 2) + chunk(b'tRNS', b'\\x00' * 4) +
                               rgb_pixels + iend),
            'rgba_forbids_trns': (sig + ihdr(8, 6) + gray_transparency +
                                  chunk(b'IDAT', zlib.compress(b'\\x00' * 9 * 3)) + iend),
            'plte_after_trns': (sig + ihdr(8, 2) + rgb_transparency +
                                palette + rgb_pixels + iend),
        }
        for name, contents in invalid.items():
            with self.subTest(invalid=name):
                self.folder.joinpath('mask.png').write_bytes(contents)
                self.assertIn('E_MASK_PNG', self.errors(document(region('a'))))
                self.assertNotIn('E_MASK_PNG', self.errors(
                    document(region('a')), skip_files=True))

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
