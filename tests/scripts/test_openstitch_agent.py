"""Tests du pont CLI agent, sans reseau ni dependances externes."""
from __future__ import annotations
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "scripts/openstitch_agent.py"
spec = importlib.util.spec_from_file_location("openstitch_agent", SCRIPT)
agent = importlib.util.module_from_spec(spec)
spec.loader.exec_module(agent)


class AgentCLITest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        cli = Path(self.tmp.name) / "fake-cli"
        cli.write_text("#!/usr/bin/env python3\n"
                       "import json,sys\n"
                       "a=sys.argv[1:]\n"
                       "if a == ['--version']: print('0.1.0')\n"
                       "elif a == ['commands','--json']: print(json.dumps({'schema':'openstitch.commands/1','subcommands':[{'name':'project'}]}))\n"
                       "elif a == ['project','operations','--json']: print(json.dumps({'schema':'openstitch.operations/1','operations':[{'name':'set_params'}]}))\n"
                       "elif a[:2] == ['project','inspect']: print(json.dumps({'schema':'openstitch.project-inspect/1','canvas':{'width_mm':300},'embroidery_objects':[{'id':8,'type':'tatami','color':'#FFFFFF','order':0,'region_area_mm2':10,'stitches':{'total':150,'jumps':4}},{'id':3,'type':'auto_satin','color':'#000000','order':1,'region_area_mm2':2,'stitches':{'total':10,'jumps':9}}]}))\n"
                       "else: print('bad args',file=sys.stderr); sys.exit(1)\n")
        cli.chmod(0o755)
        self.cli = str(cli)

    def test_doctor_contract(self):
        output, code = agent.doctor(self.cli)
        self.assertEqual(code, 0)
        self.assertTrue(output["ready"])
        self.assertEqual(output["capabilities"]["operation_names"], ["set_params"])

    def test_doctor_missing(self):
        output, code = agent.doctor(None)
        self.assertEqual(code, 2)
        self.assertFalse(output["ready"])

    def test_compact_inspect_filter_sort(self):
        data = agent.compact_inspect(self.cli, "x.osp", kind=None, color=None,
                                     min_area=0, sort_key="jumps", offset=0, limit=1)
        self.assertEqual(data["totals"]["objects"], 2)
        self.assertEqual(data["objects"][0]["id"], 3)
        self.assertEqual(data["matching"], 2)
        data = agent.compact_inspect(self.cli, "x.osp", kind="tatami", color="#ffffff",
                                     min_area=5, sort_key="order", offset=0, limit=5)
        self.assertEqual([x["id"] for x in data["objects"]], [8])

    def test_bad_cli_report_json(self):
        p = subprocess.run([sys.executable, str(SCRIPT), "--cli", self.cli,
                            "inspect", "bad.osp", "--type", "running"], capture_output=True, text=True)
        self.assertEqual(p.returncode, 0)
        self.assertEqual(json.loads(p.stdout)["matching"], 0)

    def test_invalid_json_rejected(self):
        Path(self.cli).write_text("#!/usr/bin/env python3\nprint('not json')\n")
        self.assertRaises(RuntimeError, agent.read_json, self.cli, "commands", "--json")


if __name__ == "__main__":
    unittest.main()
