#!/usr/bin/env python3
"""Matrice de migration Studio : inventaire V1 (automatisé) x correspondances V2 Widgets x statuts Studio.

Sorties : docs/desktop-v2/parite-studio/matrice-studio.csv (générée, ne pas éditer à la main).
Entrées éditables : parite-studio/statuts-studio.csv (id, statut, preuve, controleur, composant).
Statuts : ABSENT SERVICE_ONLY UI_PARTIAL FUNCTIONAL_UNTESTED TESTED VALIDATED BLOCKED.
Défaut : SERVICE_ONLY si V2 Widgets est « fait/partiel » (le service existe, pas l'UI Studio), sinon ABSENT ; BLOCKED si exclu en V2 (décision du propriétaire requise).
`--dod` : code 1 tant qu'une ligne n'est pas VALIDATED (ou exclue avec justification en V2).
"""
import csv, collections, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
P = ROOT / "docs/desktop-v2/parite-v1"
S = ROOT / "docs/desktop-v2/parite-studio"
STATUTS = ["ABSENT", "SERVICE_ONLY", "UI_PARTIAL", "FUNCTIONAL_UNTESTED", "TESTED", "VALIDATED", "BLOCKED"]


def rows(path):
    with open(path, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def main():
    inv = rows(P / "inventaire-v1.csv")
    v2 = {r["id"]: r for r in rows(P / "correspondances.csv")}
    ov = {r["id"]: r for r in rows(S / "statuts-studio.csv")}
    unknown = [i for i in ov if i not in {r["id"] for r in inv}]
    bad = [i for i, r in ov.items() if r["statut"] not in STATUTS]
    if unknown or bad:
        sys.exit(f"statuts-studio.csv invalide : ids inconnus {unknown}, statuts invalides {bad}")
    out = []
    for r in inv:
        c = v2.get(r["id"], {})
        o = ov.get(r["id"])
        v2s = c.get("statut", "absent")
        statut = o["statut"] if o else ("SERVICE_ONLY" if v2s in ("fait", "partiel") else
                                         "BLOCKED" if v2s == "exclu" else "ABSENT")
        out.append({"id": r["id"], "type": r["type"], "libelle_v1": r["libelle_v1"],
                    "source_v1": r["source_v1"], "statut_v2_widgets": v2s,
                    "preuve_v2": c.get("preuve_v2", ""), "statut_studio": statut,
                    "preuve_studio": o["preuve"] if o else "", "controleur": o["controleur"] if o else "",
                    "composant": o["composant"] if o else ""})
    with open(S / "matrice-studio.csv", "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(out[0]))
        w.writeheader()
        w.writerows(out)
    n = collections.Counter(x["statut_studio"] for x in out)
    print(f"Studio : {len(out)} fonctions V1 ; " + " ; ".join(f"{k} {n.get(k, 0)}" for k in STATUTS))
    if "--dod" in sys.argv and n.get("VALIDATED", 0) != len(out):
        print(f"DoD non satisfaite : {len(out) - n.get('VALIDATED', 0)} fonction(s) non VALIDATED")
        sys.exit(1)


main()
