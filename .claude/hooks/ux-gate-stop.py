#!/usr/bin/env python3
"""Claude Code SessionStart/Stop hook: require UX evidence for UI edits *in this session*.

Session-scoped snapshots avoid blocking unrelated conversations on a branch
which already has Studio changes. Stop blocks once; stop_hook_active prevents
a feedback loop if the local environment cannot execute Studio.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(os.environ.get("CLAUDE_PROJECT_DIR") or Path(__file__).resolve().parents[2])
REPORT = ROOT / "build" / "ux-gate" / "latest" / "report.json"
SESSIONS = ROOT / "build" / "ux-gate" / "sessions"
PREFIXES = ("apps/desktop_v2/studio/", "apps/desktop_v2/core/", "resources/themes/")


def git(*args: str) -> str:
    try:
        return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True,
                                       stderr=subprocess.DEVNULL, timeout=12)
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return ""


def changed_ui_files() -> list[str]:
    names = set()
    for args in (("diff", "--name-only", "origin/main...HEAD"),
                 ("diff", "--name-only", "main...HEAD"),
                 ("diff", "--name-only", "HEAD"),
                 ("diff", "--cached", "--name-only"),
                 ("ls-files", "--others", "--exclude-standard")):
        names.update(git(*args).splitlines())
    return sorted(n for n in names if n.replace("\\", "/").startswith(PREFIXES))


def snapshot() -> dict[str, str]:
    state = {}
    for relative in changed_ui_files():
        path = ROOT / relative
        if path.is_file():
            state[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
        else:
            state[relative] = "DELETED"
    return state


def record_path(session_id: str) -> Path:
    safe = hashlib.sha256(session_id.encode("utf-8")).hexdigest()[:32]
    return SESSIONS / f"{safe}.json"


def hook_block(reason: str) -> None:
    print(json.dumps({"decision": "block", "reason": reason}, ensure_ascii=False))


def main() -> int:
    try:
        event = json.load(sys.stdin)
    except (ValueError, OSError):
        return 0
    session_id = event.get("session_id")
    if not isinstance(session_id, str) or not session_id:
        return 0
    record = record_path(session_id)
    action = sys.argv[1] if len(sys.argv) > 1 else "stop"

    if action == "start":
        try:
            record.parent.mkdir(parents=True, exist_ok=True)
            record.write_text(json.dumps(snapshot()), encoding="utf-8")
        except OSError:
            pass
        return 0
    if action != "stop" or event.get("stop_hook_active"):
        return 0
    if not record.is_file():
        return 0  # SessionStart not installed/previous session: do not hijack it.
    try:
        before = json.loads(record.read_text(encoding="utf-8"))
        after = snapshot()
    except (ValueError, OSError):
        return 0
    if before == after:
        return 0

    # Import the same executable verifier used by CI. stdout reserved for hook JSON.
    if not REPORT.is_file():
        hook_block(
            "Des fichiers UI OpenStitch ont change durant cette session, mais la recette "
            "visuelle manque. Executer scripts/ux/verify.py capture --studio <openstitch.exe> "
            "--project <fixture.osp> --out build/ux-gate/latest; faire relire les captures "
            "par l'agent os-ux-reviewer (independant), consigner sa revue via "
            "scripts/ux/verify.py review, puis verifier avec check-report --require-review. "
            "Si l'environnement ne peut pas afficher Studio, signaler explicitement BLOCKED."
        )
        return 0
    result = subprocess.run(
        [sys.executable, str(ROOT / "scripts" / "ux" / "verify.py"), "check-report",
         "--report", str(REPORT), "--require-review"],
        cwd=ROOT, capture_output=True, text=True, timeout=20,
    )
    if result.returncode:
        hook_block(
            "UX Gate incomplet ou obsolete: "
            + (result.stderr or result.stdout)[-1100:]
            + " Refaites les captures et l'audit independant avant de livrer la PR."
        )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.TimeoutExpired):
        # A broken hook must not trap Claude forever; state the block in its report.
        sys.exit(0)
