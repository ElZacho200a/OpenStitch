# SPDX-License-Identifier: Apache-2.0
"""Unit tests for the E16 real-CLI reproducibility gate."""
import json
from pathlib import Path
from types import SimpleNamespace
import subprocess
import unittest

from digitize_determinism import verify


def dst(body=b"\x00\x00\xf3"):
    return b"LA:" + b" " * (512 - 3) + body


class DeterminismGateTests(unittest.TestCase):
    def test_identical_bytes_and_same_basename_are_accepted(self):
        outputs = []

        def fake(argv, **kwargs):
            path = Path(argv[3])
            self.assertEqual(argv[1], "digitize")
            self.assertEqual(argv[-1], "--json")
            self.assertEqual(path.name, "design.dst")
            outputs.append(path)
            path.write_bytes(dst())
            return SimpleNamespace(returncode=0, stdout=json.dumps({"stitches": 4}))

        report = verify(Path("fake-cli"), Path("fixture.png"), runner=fake)
        self.assertEqual(report["failures"], [])
        self.assertEqual(report["passes"], 2)
        self.assertEqual(report["stitches"], [4, 4])
        self.assertEqual(report["sha256"][0], report["sha256"][1])
        self.assertNotEqual(outputs[0].parent, outputs[1].parent)

    def test_zero_stitches_are_valid_for_empty_digitization(self):
        def fake(argv, **kwargs):
            Path(argv[3]).write_bytes(dst())
            return SimpleNamespace(returncode=0, stdout='{"stitches":0}')

        report = verify(Path("fake-cli"), Path("fixture.png"), runner=fake)
        self.assertEqual(report["failures"], [])
        self.assertEqual(report["stitches"], [0, 0])
        self.assertEqual(report["passes"], 2)

    def test_detects_byte_drift_even_when_stitch_counts_agree(self):
        counter = 0

        def fake(argv, **kwargs):
            nonlocal counter
            counter += 1
            Path(argv[3]).write_bytes(dst(bytes((counter, 0, 0xF3))))
            return SimpleNamespace(returncode=0, stdout='{"stitches":4}')

        report = verify(Path("fake-cli"), Path("fixture.png"), runner=fake)
        self.assertEqual(report["failures"], ["byte-or-stitch-drift"])
        self.assertEqual(report["passes"], 2)

    def test_missing_output_is_a_failure(self):
        def fake(argv, **kwargs):
            return SimpleNamespace(returncode=0, stdout='{"stitches":4}')

        self.assertEqual(
            verify(Path("fake-cli"), Path("fixture.png"), runner=fake)["failures"],
            ["pass-1:missing-dst"],
        )

    def test_nonzero_exit_and_invalid_json_are_failures(self):
        def bad_exit(argv, **kwargs):
            return SimpleNamespace(returncode=2, stdout="")

        def bad_json(argv, **kwargs):
            Path(argv[3]).write_bytes(dst())
            return SimpleNamespace(returncode=0, stdout='{"stitches":null}')

        self.assertEqual(
            verify(Path("fake-cli"), Path("fixture.png"), runner=bad_exit)["failures"],
            ["pass-1:exit-2"],
        )
        self.assertEqual(
            verify(Path("fake-cli"), Path("fixture.png"), runner=bad_json)["failures"],
            ["pass-1:invalid-json-stitches"],
        )

    def test_fractional_boolean_and_negative_counts_are_rejected(self):
        for value in (1.5, True, -1, "2"):
            with self.subTest(value=value):
                def fake(argv, **kwargs):
                    Path(argv[3]).write_bytes(dst())
                    return SimpleNamespace(
                        returncode=0, stdout=json.dumps({"stitches": value})
                    )

                report = verify(Path("fake-cli"), Path("fixture.png"), runner=fake)
                self.assertEqual(report["failures"], ["pass-1:invalid-json-stitches"])

    def test_timeout_and_truncated_dst_are_failures(self):
        def timed_out(argv, **kwargs):
            raise subprocess.TimeoutExpired(cmd=argv, timeout=kwargs["timeout"])

        def invalid_dst(argv, **kwargs):
            Path(argv[3]).write_bytes(b"LA:truncated")
            return SimpleNamespace(returncode=0, stdout='{"stitches":1}')

        self.assertEqual(
            verify(Path("fake-cli"), Path("fixture.png"), runner=timed_out)["failures"],
            ["pass-1:timeout"],
        )
        self.assertEqual(
            verify(Path("fake-cli"), Path("fixture.png"), runner=invalid_dst)["failures"],
            ["pass-1:invalid-dst-envelope"],
        )


if __name__ == "__main__":
    unittest.main()
