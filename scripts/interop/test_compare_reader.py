import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from types import SimpleNamespace

from compare_reader import compare, count_external, read_openstitch_stats


class FakeDecoder:
    STITCH = 0
    JUMP = 1
    TRIM = 2
    STOP = 3
    COLOR_CHANGE = 5
    COMMAND_MASK = 0xFF

    def __init__(self, stitches):
        self.pattern = SimpleNamespace(stitches=stitches)

    def read(self, filename):
        return self.pattern


def cli_result(data, code=0):
    return lambda *args, **kwargs: SimpleNamespace(returncode=code, stdout=json.dumps(data), stderr="bad" if code else "")


class CompareReaderTests(unittest.TestCase):
    def test_classification_and_flagged_commands(self):
        dec = FakeDecoder([(0, 0, 0), (1, 1, 0x100 | 0), (2, 2, 1), (3, 3, 2), (4, 4, 3), (5, 5, 5)])
        self.assertEqual(count_external(dec.pattern, dec), {"stitches": 2, "jumps": 1, "trims": 1, "color_changes": 1})

    def test_reject_invalid_command_record(self):
        dec = FakeDecoder([(0, 1)])
        with self.assertRaisesRegex(ValueError, "tuple"):
            count_external(dec.pattern, dec)

    def test_openstitch_stats_json_required(self):
        with self.assertRaisesRegex(ValueError, "invalid or missing"):
            read_openstitch_stats(Path("cli"), Path("example.dst"), cli_result({"stitches": True}))
        with self.assertRaisesRegex(ValueError, "returned 3"):
            read_openstitch_stats(Path("cli"), Path("example.dst"), cli_result({}, code=3))
        with self.assertRaisesRegex(ValueError, "valid JSON"):
            read_openstitch_stats(Path("cli"), Path("example.dst"), lambda *a, **kw: SimpleNamespace(returncode=0, stdout="oops", stderr=""))

    def test_matching_stats_and_independent_reader(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / "ok.DST"
            p.write_bytes(b"not a real dst: decoder is mocked")
            dec = FakeDecoder([(1, 2, 0), (2, 3, 0), (3, 4, 1), (4, 5, 2), (5, 6, 5)])
            counts = {"stitches": 2, "jumps": 1, "trims": 1, "color_changes": 1}
            output = compare(p, Path("cli"), dec, cli_result(counts))
            self.assertEqual(output["status"], "match")
            self.assertEqual(output["differences"], {})

    def test_disagreement_is_reported_not_hidden(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / "different.pes"
            p.write_bytes(b"mock")
            dec = FakeDecoder([(0, 0, 0)])
            output = compare(p, Path("cli"), dec, cli_result({"stitches": 2, "jumps": 0, "trims": 0, "color_changes": 0}))
            self.assertEqual(output["status"], "different")
            self.assertEqual(output["differences"]["stitches"], {"openstitch": 2, "third_party": 1})

    def test_unsupported_or_missing_file(self):
        dec = FakeDecoder([])
        with self.assertRaisesRegex(ValueError, "unsupported extension"):
            compare(Path("no.bmp"), Path("cli"), dec)
        with self.assertRaises(FileNotFoundError):
            compare(Path("not-found.dst"), Path("cli"), dec)


if __name__ == "__main__":
    unittest.main()
