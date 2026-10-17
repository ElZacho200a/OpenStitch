#!/usr/bin/env python3
"""Applique set_params à TOUS les objets d'un type d'un .osp (lot transactionnel).

    python3 -I set_params_all.py CLI p.osp tatami '{"inset_mm":0.0}' [-o out.osp]

Sert à tester un réglage global (inset, compensation de tirage, espacement…) sans toucher
au code : un seul lot `project apply`, annulable en repartant du .osp d'origine.
"""
import json
import subprocess
import sys

cli, osp, typ, params = sys.argv[1], sys.argv[2], sys.argv[3], json.loads(sys.argv[4])
out = sys.argv[sys.argv.index("-o") + 1] if "-o" in sys.argv else osp

d = json.loads(subprocess.run([cli, "project", "inspect", osp, "--json"], capture_output=True, text=True, check=True).stdout)
ops = [{"op": "set_params", "object": e["id"], "params": params} for e in d["embroidery_objects"] if e["type"] == typ]
batch = {"schema": "openstitch.batch/1", "operations": ops}
r = subprocess.run([cli, "project", "apply", osp, "-", "-o", out], input=json.dumps(batch), capture_output=True, text=True)
print(f"{len(ops)} objets {typ} -> {out} (code {r.returncode})")
if r.returncode:
    sys.exit(r.stderr[-800:] or r.stdout[-800:])
