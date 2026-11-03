import json
import struct
import unittest
from pathlib import Path
from types import SimpleNamespace
from image_corpus import SIGNATURE, chunk, png, corpus, smoke

class ImageCorpusTests(unittest.TestCase):
    def test_synthetic_samples_reproducible_and_limited(self):
        self.assertEqual(corpus(), corpus())
        self.assertEqual(len(corpus()), 7)
        self.assertEqual(sum(valid for _, valid in corpus().values()), 4)

    def test_png_signature_chunks_and_crc(self):
        for name, (data, valid) in corpus().items():
            if not valid:
                continue
            self.assertTrue(data.startswith(SIGNATURE), name)
            pos = len(SIGNATURE)
            kinds = []
            while pos < len(data):
                count, = struct.unpack_from('>I', data, pos)
                namepart = data[pos+4:pos+8]
                payload = data[pos+8:pos+8+count]
                self.assertEqual(data[pos+8+count:pos+12+count], chunk(namepart, payload)[-4:])
                kinds.append(namepart)
                pos += 12 + count
            self.assertEqual(kinds[0], b'IHDR')
            self.assertEqual(kinds[-1], b'IEND')
            self.assertEqual(pos, len(data))

    def test_validation_rejects_bad_inputs(self):
        with self.assertRaises(ValueError):
            png(1, 2, 8, 2, [b'hi'])
        with self.assertRaises(ValueError):
            png(0, 1, 8, 2, [b'hi'])

    def test_smoke_uses_real_command_contract(self):
        def fake(argv, **kwargs):
            valid = not Path(argv[2]).stem.startswith('invalid-')
            return SimpleNamespace(returncode=0 if valid else 1, stdout=json.dumps({'width_px': 2, 'height_px': 1}))
        self.assertEqual(smoke(Path('fake-cli'), runner=fake), {})

    def test_smoke_reports_broken_decoder(self):
        def broken(argv, **kwargs):
            return SimpleNamespace(returncode=0, stdout=json.dumps({'width_px': 1, 'height_px': 1}))
        errors = smoke(Path('fake-cli'), runner=broken)
        self.assertEqual(len(errors), 7)
        self.assertEqual(errors['rgb8'], 'incorrect-image-dimensions')

if __name__ == '__main__':
    unittest.main()
