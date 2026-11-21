#!/usr/bin/env python3
"""Pure-Python regression tests for the Studio UX gate (no Qt/CI required)."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zlib

SOURCE = Path(__file__).resolve().parents[2] / "scripts" / "ux" / "verify.py"
spec = importlib.util.spec_from_file_location("studio_ux_verify", SOURCE)
ux = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ux)

GOOD_PR = """## UX Gate
- Parcours novice : importer un motif ORA et regler les zones, puis revenir aux points.
- Clics avant/après : 8 -> 4, mesure sur le meme projet.
- Captures inspectées : CI artifact openstitch-studio-screenshots-windows.
- Revue indépendante : agent UX distinct PASS, aucune friction bloquante.
- Tests UX : test_desktop_v2_studio_space_image 14/14 PASS.
"""


def make_png(width=100, height=100, seed=b"test"):
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload)))
    # Valid RGB PNG with enough varying pixels to catch blank/truncated output.
    scanlines = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            row.extend(hashlib.sha256(seed + struct.pack(">HH", x, y)).digest()[:3])
        scanlines.append(b"\x00" + bytes(row))
    hdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (ux.PNG_HEADER + chunk(b"IHDR", hdr)
            + chunk(b"IDAT", zlib.compress(b"".join(scanlines)))
            + chunk(b"IEND", b""))


class UXContractTests(unittest.TestCase):
    def test_detects_production_ui_not_test_or_docs(self):
        paths = ["apps/desktop_v2/studio/qml/Main.qml",
                 "apps/desktop_v2/core/workspace.cpp",
                 "resources/themes/neo.json", "docs/desktop-v2/x.md",
                 "tests/unit/desktop_v2/test_studio_files.cpp"]
        self.assertEqual(ux.ui_paths(paths), sorted(paths[:3]))

    def test_non_ui_pull_request_passes_without_body(self):
        self.assertEqual(ux.validate_pr("", ["docs/source/foo.md"]), [])

    def test_complete_contract_passes(self):
        self.assertEqual(ux.validate_pr(GOOD_PR, ["apps/desktop_v2/studio/main.cpp"]), [])

    def test_each_missing_field_fails(self):
        for field in ux.REVIEW_FIELDS:
            with self.subTest(field=field):
                prefix = GOOD_PR.splitlines()
                import re
                replacement = [line for line in prefix if not re.search(ux.REVIEW_FIELDS[field], line)]
                self.assertTrue(ux.validate_pr("\n".join(replacement),
                             ["apps/desktop_v2/studio/qml/Inspector.qml"]))

    def test_placeholders_and_no_reviewer_fail(self):
        body = GOOD_PR.replace("agent UX distinct PASS", "à faire")
        self.assertTrue(ux.validate_pr(body, ["apps/desktop_v2/studio/qml/Main.qml"]))
        body = GOOD_PR.replace("8 -> 4", "beaucoup")
        self.assertTrue(ux.validate_pr(body, ["apps/desktop_v2/studio/qml/Main.qml"]))

    def test_png_valid_and_corrupt_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "test.png"
            path.write_bytes(make_png())
            w, h, digest = ux.read_png(path)
            self.assertEqual((w, h), (100, 100))
            self.assertEqual(len(digest), 64)
            broken = bytearray(path.read_bytes())
            broken[50] ^= 1
            path.write_bytes(broken)
            with self.assertRaises(ValueError):
                ux.read_png(path)

    def test_capture_matrix_records_current_outputs(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            studio, project = root / "openstitch.exe", root / "input.osp"
            studio.write_bytes(b"fake")
            project.write_bytes(b"fake")
            out = root / "shots"

            def fake_run(command, **kwargs):
                shot = Path(command[command.index("--screenshot") + 1])
                shot.write_bytes(make_png(1440, 900, shot.name.encode()))
                return subprocess.CompletedProcess(command, 0, "", "")

            argv = ["capture", "--studio", str(studio), "--project", str(project),
                    "--out", str(out), "--backend", "software"]
            with mock.patch.object(ux.subprocess, "run", side_effect=fake_run):
                self.assertEqual(ux.main(argv), 0)
            report = json.loads((out / "report.json").read_text())
            self.assertEqual(len(report["screenshots"]), 8)
            self.assertEqual(report["backend"], "software")
            self.assertEqual(report["visual_review"], "NOT_REVIEWED")

    def test_capture_rejects_stale_image(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            studio, project = root / "studio.exe", root / "project.osp"
            studio.touch()
            project.touch()
            out = root / "screens"
            out.mkdir()
            stale = out / "ux-image-1000x640.png"
            stale.write_bytes(make_png(seed=b"stale"))
            argv = ["capture", "--studio", str(studio), "--project", str(project),
                    "--out", str(out), "--spaces", "image", "--sizes", "1000x640"]
            with mock.patch.object(ux.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")):
                self.assertEqual(ux.main(argv), 1)
            self.assertFalse(stale.exists())

    def test_capture_identical_spaces_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            studio, project = root / "studio.exe", root / "project.osp"
            studio.touch()
            project.touch()

            def fake_run(command, **kwargs):
                Path(command[command.index("--screenshot") + 1]).write_bytes(
                    make_png(1440, 900, b"identical"))
                return subprocess.CompletedProcess(command, 0, "", "")

            argv = ["capture", "--studio", str(studio), "--project", str(project),
                    "--out", str(root / "screens"), "--sizes", "1440x900",
                    "--spaces", "image,points"]
            with mock.patch.object(ux.subprocess, "run", side_effect=fake_run):
                self.assertEqual(ux.main(argv), 1)
            self.assertFalse((root / "screens" / "report.json").exists())


if __name__ == "__main__":
    unittest.main()
