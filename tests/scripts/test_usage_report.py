# SPDX-License-Identifier: Apache-2.0
"""Tests du rapport d'usage (scripts/usage_report.py) : lecture, KPI, filtres, robustesse."""

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import usage_report  # noqa: E402


def event(day, sid, kind, ident, ver="0.1", app="studio", hour="10:00:00.000"):
    return {"v": 1, "t": f"{day}T{hour}", "sid": sid, "app": app, "ver": ver, "os": "windows",
            "k": kind, "id": ident}


class UsageReportTest(unittest.TestCase):
    def write(self, folder, name, rows, raw_lines=()):
        path = Path(folder) / name
        with path.open("w", encoding="utf-8") as handle:
            for row in rows:
                handle.write(json.dumps(row) + "\n")
            for raw in raw_lines:
                handle.write(raw + "\n")

    def test_reads_every_monthly_file_and_skips_bad_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.write(tmp, "usage-2026-10.jsonl", [event("2026-10-30", "a", "click", "b1")])
            self.write(tmp, "usage-2026-11.jsonl", [event("2026-11-02", "b", "tool", "knife")],
                       raw_lines=["{pas du json", '{"k":"click"}', ""])
            events, skipped = usage_report.read_events([Path(tmp)])
        self.assertEqual(len(events), 2)
        self.assertEqual(skipped, 2)

    def test_summary_counts_sessions_days_and_rankings(self):
        events = [
            event("2026-11-01", "a", "tool", "knife"),
            event("2026-11-01", "a", "tool", "knife"),
            event("2026-11-01", "a", "click", "gesture_satin"),
            event("2026-11-03", "b", "tool", "bucket", ver="0.2"),
            event("2026-11-03", "b", "action", "fill.guides", ver="0.2"),
        ]
        s = usage_report.summarize(events, top=1)
        self.assertEqual((s["events"], s["sessions"], s["active_days"]), (5, 2, 2))
        self.assertEqual((s["first_day"], s["last_day"]), ("2026-11-01", "2026-11-03"))
        self.assertEqual(s["top"]["tool"], [{"id": "knife", "count": 2}])
        self.assertEqual(s["versions"]["studio 0.1"], {"events": 3, "sessions": 1})
        self.assertEqual(s["versions"]["studio 0.2"], {"events": 2, "sessions": 1})
        self.assertEqual(s["per_day"], {"2026-11-01": 3, "2026-11-03": 2})

    def test_since_filter_and_empty_report(self):
        events = [event("2026-10-01", "a", "click", "x"), event("2026-11-05", "b", "click", "y")]
        self.assertEqual(usage_report.summarize(events, since="2026-11-01")["events"], 1)
        self.assertEqual(usage_report.render_text(usage_report.summarize([])),
                         "Aucun événement dans le journal.")

    def test_default_dir_follows_the_platform_and_the_override(self):
        win = usage_report.default_dir({"LOCALAPPDATA": "C:/L"}, "win32")
        self.assertEqual(win, Path("C:/L") / "OpenStitch" / "usage")
        self.assertEqual(usage_report.default_dir({"XDG_DATA_HOME": "/x"}, "linux"),
                         Path("/x") / "OpenStitch" / "usage")
        self.assertEqual(usage_report.default_dir({"OPENSTITCH_USAGE_DIR": "/shared"}, "win32"),
                         Path("/shared"))

    def test_command_line_json_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.write(tmp, "usage-2026-11.jsonl", [event("2026-11-02", "a", "tool", "knife")])
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = usage_report.main(["--dir", tmp, "--json", "--top", "3"])
        self.assertEqual(code, 0)
        data = json.loads(out.getvalue())
        self.assertEqual(data["events"], 1)
        self.assertEqual(data["skipped_lines"], 0)


if __name__ == "__main__":
    unittest.main()
