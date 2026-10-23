import subprocess
import unittest
from pathlib import Path
from types import SimpleNamespace
from fuzz_osp import archive, invalid_corpus, check_one, run_corpus


class FuzzOspTests(unittest.TestCase):
    def test_corpus_deterministic_and_unique(self):
        self.assertEqual(invalid_corpus(), invalid_corpus())
        self.assertEqual(len(invalid_corpus()), 61)
        self.assertEqual(len(set(invalid_corpus())), 61)

    def test_zip_archive_stable(self):
        self.assertEqual(archive(b"abc"), archive(b"abc"))

    def test_clean_rejection_is_success(self):
        result = lambda *a, **k: SimpleNamespace(returncode=1)
        self.assertIsNone(check_one(Path("cli"), Path("invalid.osp"), runner=result))

    def test_reject_acceptance_crash_and_timeout(self):
        cases = [(0, "unexpectedly-accepted"), (-11, "signal: 11"), (3221225477, "abnormal-exit")]
        for code, phrase in cases:
            with self.subTest(code=code):
                runner = lambda *a, **k: SimpleNamespace(returncode=code)
                self.assertIn(phrase, check_one(Path("cli"), Path("case.osp"), runner=runner))
        def hang(*a, **k):
            raise subprocess.TimeoutExpired("cli", 4)
        self.assertEqual(check_one(Path("cli"), Path("case.osp"), runner=hang), "timeout")

    def test_runner_cleans_up_temporary_corpus(self):
        calls = []
        def reject(argv, **kwargs):
            calls.append(argv)
            self.assertTrue(Path(argv[3]).is_file())
            self.assertEqual(argv[1:3], ["project", "inspect"])
            return SimpleNamespace(returncode=1)
        self.assertEqual(run_corpus(Path("cli"), runner=reject), {})
        self.assertEqual(len(calls), 61)


if __name__ == "__main__":
    unittest.main()
