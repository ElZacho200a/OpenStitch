#!/usr/bin/env python3
"""Mesure de parité moteur -> bureau V2 (lecture seule, sans compilation).

Compte les opérations de `libs/project_ops` et les commandes annulables de `libs/commands`,
puis cherche chacune dans `apps/desktop_v2` (et dans `apps/desktop` pour les commandes).
LIMITE : c'est une recherche de NOMS. Une opération peut être exposée sous un autre chemin
(commande directe au lieu d'un lot JSON, par exemple) : « non référencée » veut dire « à
qualifier à la main », jamais « absente » ; à l'inverse un nom présent dans le code ne prouve
pas qu'il soit atteignable depuis l'interface. Usage : python3 scripts/engine_parity.py
"""
import glob
import re
import sys


def read_all(patterns):
    return "".join(open(f, encoding="utf-8").read() for p in patterns for f in glob.glob(p, recursive=True))


def main():
    ops = {}
    for f in sorted(glob.glob("libs/project_ops/src/op_*.cpp")):
        s = open(f, encoding="utf-8").read()
        found = set(re.findall(r'OpSpec\s*\{\s*"([a-z0-9_]+)"', s)) | set(
            re.findall(r"\bs\.name\s*=\s*\"([a-z0-9_]+)\"", s))
        for n in found:
            ops[n] = f.split("/")[-1]
    v2 = read_all(["apps/desktop_v2/**/*.cpp", "apps/desktop_v2/**/*.hpp"])
    v1 = read_all(["apps/desktop/*.cpp"])
    unref = [n for n in sorted(ops) if ('"' + n + '"') not in v2]
    print(f"Opérations du moteur : {len(ops)} ; non référencées par la V2 : {len(unref)}")
    for n in unref:
        print(f"  - {n} ({ops[n]})")

    cmds = {}
    for f in sorted(glob.glob("libs/commands/include/openstitch/commands/*.hpp")):
        s = open(f, encoding="utf-8").read()
        for m in re.finditer(r"^class\s+(\w+)\s+final\s*:\s*public\s+(?:commands::)?ICommand", s, re.M):
            cmds[m.group(1)] = f.split("/")[-1]
    in_v2 = {c for c in cmds if re.search(r"\b" + c + r"\b", v2)}
    in_v1 = {c for c in cmds if re.search(r"\b" + c + r"\b", v1)}
    only_v1 = sorted(in_v1 - in_v2)
    print(f"\nCommandes annulables du moteur : {len(cmds)} ; utilisées par la V2 : {len(in_v2)} ; "
          f"par la V1 : {len(in_v1)} ; V1 seulement : {len(only_v1)}")
    for c in only_v1:
        print(f"  - {c}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
