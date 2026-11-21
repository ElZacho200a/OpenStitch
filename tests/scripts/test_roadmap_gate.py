import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))

import roadmap_gate  # noqa: E402


def info(sha="deadbeef00", message="feat(x): chose", files=()):
    return {"sha": sha, "message": message, "files": list(files)}


class TestCheckCommit(unittest.TestCase):
    def test_roadmap_file_touched_passes(self):
        self.assertIsNone(roadmap_gate.check_commit(
            info(files=["docs/roadmap-parite-hatch.md", "libs/core/foo.cpp"])))

    def test_optout_phrase_passes(self):
        self.assertIsNone(roadmap_gate.check_commit(
            info(message="chore(ci): roadmap : aucune entree concernee")))

    def test_optout_phrase_accepts_accents_and_case(self):
        self.assertIsNone(roadmap_gate.check_commit(
            info(message="Chore: Roadmap : Aucune Entrée Concernée")))

    def test_optout_phrase_in_body_not_just_subject(self):
        self.assertIsNone(roadmap_gate.check_commit(
            info(message="chore(ci): add gate\n\nroadmap : aucune entree concernee")))

    def test_missing_both_fails(self):
        violation = roadmap_gate.check_commit(
            info(sha="0123456789abcdef", message="feat(stitch): nouvelle option\n",
                 files=["libs/stitch_generation/src/tatami.cpp"]))
        self.assertIsNotNone(violation)
        self.assertIn("0123456789", violation)
        self.assertIn("feat(stitch): nouvelle option", violation)

    def test_unrelated_file_mentioning_roadmap_word_does_not_fool_the_opt_out_regex(self):
        violation = roadmap_gate.check_commit(
            info(message="docs: reorganise la section roadmap du guide"))
        self.assertIsNotNone(violation)


if __name__ == "__main__":
    unittest.main()
