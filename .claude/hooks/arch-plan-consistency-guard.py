#!/usr/bin/env python3
"""PreToolUse hook: block a Write/Edit/MultiEdit on
specs/arch-plan/vision/*.md if the resulting content would fail
check_arch_plan_consistency.py's structural checks (Correspondance /
Dependency Order / Output cross-reference).

Why: this is the exact bug class that caused the arm-1 4x-rejection churn
(review_cycles_total 7-8) and, downstream, an 11h30 pipeline stall on
2026-09-29 (a malformed carrier doc nobody caught before it was
committed). Catching it at write-time, before an agent ever submits the
doc for review, is much cheaper than a review cycle or a stalled sprint.

Fail-open by design: anything unexpected (payload shape, missing
checker, can't simulate the edit, checker crashes) lets the tool call
through silently (exit 0). This hook only ever ADDS a deny for a
structural finding the checker is confident about -- it must never be
able to block unrelated file work project-wide, and it must never be
the reason a legitimate edit to this file gets stuck.
"""
import json
import os
import re
import subprocess
import sys
import tempfile

TARGET_RE = re.compile(r"specs/arch-plan/vision/.*\.md$")
PROJECT_DIR = os.environ.get("CLAUDE_PROJECT_DIR", ".")
CHECKER = os.path.join(PROJECT_DIR, "scripts", "liza", "check_arch_plan_consistency.py")


def allow():
    sys.exit(0)


def deny(reason):
    print(json.dumps({
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        }
    }))
    sys.exit(0)


def simulate_new_content(tool_name, tool_input, file_path):
    if tool_name == "Write":
        return tool_input.get("content")

    if not os.path.exists(file_path):
        return None  # can't simulate an Edit/MultiEdit against a file that doesn't exist yet

    with open(file_path, "r", encoding="utf-8") as f:
        content = f.read()

    if tool_name == "Edit":
        edits = [{
            "old_string": tool_input.get("old_string", ""),
            "new_string": tool_input.get("new_string", ""),
            "replace_all": tool_input.get("replace_all", False),
        }]
    else:  # MultiEdit
        edits = tool_input.get("edits") or []

    for e in edits:
        old, new = e.get("old_string", ""), e.get("new_string", "")
        if not old or old not in content:
            return None  # can't reliably simulate -> don't guess, fail open
        if e.get("replace_all"):
            content = content.replace(old, new)
        else:
            content = content.replace(old, new, 1)

    return content


def main():
    try:
        payload = json.load(sys.stdin)
    except Exception:
        allow()

    tool_name = payload.get("tool_name", "")
    tool_input = payload.get("tool_input", {}) or {}
    file_path = tool_input.get("file_path") or tool_input.get("path") or ""
    file_path_norm = file_path.replace("\\", "/")

    if tool_name not in ("Write", "Edit", "MultiEdit"):
        allow()
    if not TARGET_RE.search(file_path_norm):
        allow()
    if not os.path.exists(CHECKER):
        allow()

    try:
        new_content = simulate_new_content(tool_name, tool_input, file_path)
    except Exception:
        new_content = None
    if new_content is None:
        allow()

    tmp_path = None
    try:
        with tempfile.NamedTemporaryFile("w", suffix=".md", delete=False, encoding="utf-8") as tf:
            tf.write(new_content)
            tmp_path = tf.name
        result = subprocess.run(
            ["python3", CHECKER, tmp_path],
            capture_output=True, text=True, timeout=20,
        )
    except Exception:
        allow()
    finally:
        if tmp_path:
            try:
                os.unlink(tmp_path)
            except Exception:
                pass

    if result.returncode == 0:
        allow()

    out = result.stdout
    marker = "error(s) (structured-table mismatch"
    idx = out.find(marker)
    detail = out[idx:] if idx != -1 else out

    deny(
        "check_arch_plan_consistency.py found a structural mismatch between "
        "Correspondance / Dependency Order / Output in this arch-plan doc -- "
        "the exact bug class behind the arm-1 4x-rejection churn and the "
        "2026-09-29 11h30 pipeline stall. Fix it before writing this file:\n\n"
        + detail.strip()
        + "\n\nRe-run `python3 scripts/liza/check_arch_plan_consistency.py "
        + file_path + "` yourself after fixing to confirm it's clean."
    )


if __name__ == "__main__":
    main()
