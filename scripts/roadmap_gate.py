#!/usr/bin/env python3
"""OpenStitch roadmap gate: mechanical check of the CLAUDE.md commit rule.

CLAUDE.md requires that every commit either updates docs/roadmap-parite-hatch.md
(status, new gaps, journal §28) or writes "roadmap : aucune entrée concernée" in
its message. An audit on 2026-11-21 found this was followed by only ~26% of
commits over 60 days once discipline was left to memory alone. This script makes
the rule unskippable by checking it per commit instead of trusting the habit.

No third-party Python dependencies.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys

ROADMAP_PATH = "docs/roadmap-parite-hatch.md"
OPTOUT_RE = re.compile(r"(?i)roadmap\s*:\s*aucune\s+entr[ée]e\s+concern[ée]e")


def commit_shas(base: str, head: str) -> list[str]:
    out = subprocess.check_output(
        ["git", "log", "--no-merges", "--format=%H", f"{base}..{head}"],
        text=True, stderr=subprocess.DEVNULL)
    return [sha for sha in out.splitlines() if sha.strip()]


def commit_info(sha: str) -> dict:
    message = subprocess.check_output(
        ["git", "show", "-s", "--format=%s%n%n%b", sha],
        text=True, stderr=subprocess.DEVNULL)
    files_raw = subprocess.check_output(
        ["git", "show", "--name-only", "--format=", sha],
        text=True, stderr=subprocess.DEVNULL)
    files = [f.replace("\\", "/").strip() for f in files_raw.splitlines() if f.strip()]
    return {"sha": sha, "message": message, "files": files}


def check_commit(info: dict) -> str | None:
    """Return a human-readable violation, or None if the commit complies."""
    if ROADMAP_PATH in info["files"]:
        return None
    if OPTOUT_RE.search(info["message"]):
        return None
    subject = next((line for line in info["message"].splitlines() if line.strip()), "")
    return f"{info['sha'][:10]} {subject!r}"


def check_range(base: str, head: str) -> list[str]:
    errors = []
    for sha in commit_shas(base, head):
        violation = check_commit(commit_info(sha))
        if violation:
            errors.append(violation)
    return errors


def check_pr(args: argparse.Namespace) -> int:
    errors = check_range(args.base, args.head)
    if errors:
        print("Roadmap Gate NON VALIDE : commit(s) sans mise a jour de "
              f"{ROADMAP_PATH} ni mention d'opt-out "
              "('roadmap : aucune entree concernee') :", file=sys.stderr)
        for violation in errors:
            print(f"- {violation}", file=sys.stderr)
        print("Regle : CLAUDE.md, section 'Skills du projet'.", file=sys.stderr)
        return 1
    print("Roadmap Gate: chaque commit met a jour le roadmap ou le justifie.")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    pr = sub.add_parser("check-pr", help="verifier chaque commit de la PR")
    pr.add_argument("--base", required=True, help="sha de base (exclu)")
    pr.add_argument("--head", required=True, help="sha de tete (inclus)")
    args = parser.parse_args(argv)
    try:
        return check_pr(args)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"Roadmap Gate: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
