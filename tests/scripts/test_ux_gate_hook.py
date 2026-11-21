"""Regression tests for Claude Code UX hooks: no live Git/Qt required."""
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

SOURCE = Path(__file__).resolve().parents[2] / ".claude" / "hooks" / "ux-gate-stop.py"
spec = importlib.util.spec_from_file_location("openstitch_ux_gate_hook", SOURCE)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


def invoke(payload, action):
    inp, out = io.StringIO(json.dumps(payload)), io.StringIO()
    with mock.patch.object(gate.sys, "stdin", inp), \
         mock.patch.object(gate.sys, "stdout", out), \
         mock.patch.object(gate.sys, "argv", ["ux-gate-stop.py", action]):
        assert gate.main() == 0
    return json.loads(out.getvalue()) if out.getvalue() else {}


class HookTests(unittest.TestCase):
    def test_pre_pr_blocks_when_ui_evidence_missing(self):
        event = {"tool_name": "Bash",
                 "tool_input": {"command": "git status && gh pr create --draft"}}
        with tempfile.TemporaryDirectory() as d, \
             mock.patch.object(gate, "REPORT", Path(d) / "missing.json"), \
             mock.patch.object(gate, "changed_ui_files", return_value=[
                 "apps/desktop_v2/studio/qml/Main.qml"]):
            result = invoke(event, "pre-pr")
        self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_pre_pr_does_not_block_other_commands_or_non_ui(self):
        event = {"tool_name": "Bash", "tool_input": {"command": "gh pr create"}}
        with mock.patch.object(gate, "changed_ui_files", return_value=[]):
            self.assertEqual(invoke(event, "pre-pr"), {})
        event["tool_input"]["command"] = "gh pr list"
        with mock.patch.object(gate, "changed_ui_files", return_value=["apps/desktop_v2/studio/qml/Main.qml"]):
            self.assertEqual(invoke(event, "pre-pr"), {})

    def test_pre_pr_passes_when_external_gate_is_green(self):
        event = {"tool_name": "Bash", "tool_input": {"command": "gh pr create --draft"}}
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "report.json"
            p.write_text("{}")
            with mock.patch.object(gate, "REPORT", p), \
                 mock.patch.object(gate, "changed_ui_files", return_value=[
                     "apps/desktop_v2/studio/qml/Main.qml"]), \
                 mock.patch.object(gate.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "OK", "")):
                self.assertEqual(invoke(event, "pre-pr"), {})

    def test_stop_only_blocks_changes_made_during_session(self):
        with tempfile.TemporaryDirectory() as d:
            with mock.patch.object(gate, "SESSIONS", Path(d) / "sessions"), \
                 mock.patch.object(gate, "REPORT", Path(d) / "not-found.json"), \
                 mock.patch.object(gate, "snapshot", side_effect=[
                     {"apps/desktop_v2/studio/qml/Main.qml": "before"},
                     {"apps/desktop_v2/studio/qml/Main.qml": "before"},
                     {"apps/desktop_v2/studio/qml/Main.qml": "after"}]):
                event = {"session_id": "session-42"}
                self.assertEqual(invoke(event, "start"), {})
                self.assertEqual(invoke(event, "stop"), {})
                blocked = invoke(event, "stop")
                self.assertEqual(blocked["decision"], "block")
                event["stop_hook_active"] = True
                self.assertEqual(invoke(event, "stop"), {})


if __name__ == "__main__":
    unittest.main()
