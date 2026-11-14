"""Regression tests for the PR336 PNG integrity checker (no CLI required)."""
import ast
from pathlib import Path
import struct
import unittest
import zlib


def checker():
    # Importing the QA module deletes its output directory; isolate only the
    # pure validation function to keep this test side-effect-free.
    source = Path(__file__).resolve().parents[1] / "scripts" / "qa_pr336_gradients.py"
    tree = ast.parse(source.read_text(encoding="utf-8"))
    node = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "valid_png")
    namespace = {"Path": Path}
    exec(compile(ast.Module(body=[node], type_ignores=[]), str(source), "exec"), namespace)
    return namespace["valid_png"]


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def png():
    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
    pixels = zlib.compress(b"\\x00\\xff\\x00\\x00")
    return b"\\x89PNG\\r\\n\\x1a\\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", pixels) + chunk(b"IEND", b"")


class TestPreviewPng(unittest.TestCase):
    def test_valid_png_signature_and_chunks(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "preview.png"
            data = png()
            path.write_bytes(data)
            self.assertTrue(checker()(path))
            path.write_bytes(data[:-1])
            self.assertFalse(checker()(path))
            corrupted = bytearray(data)
            corrupted[20] ^= 1
            path.write_bytes(corrupted)
            self.assertFalse(checker()(path))


if __name__ == "__main__":
    unittest.main()
