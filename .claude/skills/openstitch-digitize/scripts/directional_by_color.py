#!/usr/bin/env python3
"""Lot `directional_guides` sur tous les tatami (aire > 3 mm2) des couleurs données.

    python3 -I directional_by_color.py CLI p.osp out.osp "#1C7F8F,#295C6C"
"""
import json,subprocess,sys
C,osp,out,cols=sys.argv[1],sys.argv[2],sys.argv[3],sys.argv[4].split(',')
d=json.loads(subprocess.run([C,'project','inspect',osp,'--json'],capture_output=True,text=True,check=True).stdout)
vec={v['id']:v for v in d['vector_objects']}
ops=[{"op":"directional_guides","object":e['id']} for e in d['embroidery_objects'] if e['color'].upper() in cols and e['type']=='tatami' and vec[e['source_vector']]['area_mm2']>3]
r=subprocess.run([C,'project','apply',osp,'-','-o',out],input=json.dumps({"schema":"openstitch.batch/1","operations":ops}),capture_output=True,text=True)
print(len(ops),r.returncode,(r.stderr or r.stdout)[-600:])
