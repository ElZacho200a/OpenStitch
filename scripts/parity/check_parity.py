#!/usr/bin/env python3
"""Garde-fou de parité V1 -> V2 (lancé par CTest : `check_v1_parity`).

Vérifie, sans rien deviner :
  1. l'inventaire des fonctions de la V1 est à jour (régénéré depuis apps/desktop) ;
  2. chaque fonction de l'inventaire a UNE ligne de correspondance, avec un statut parmi
     fait | partiel | absent | exclu ;
  3. chaque ligne `fait` ou `partiel` cite une preuve (texte, identifiant de commande ou de
     widget) qui existe réellement dans apps/desktop_v2 ;
  4. chaque ligne `exclu` porte une raison écrite ;
  5. le nombre de lignes `absent` ne dépasse pas le plafond du fichier `plafond-absent.txt`
     (le plafond ne fait que descendre : une régression de parité fait échouer le test).
Sortie : un résumé chiffré. Code 0 si tout est cohérent.
"""
import csv
import subprocess
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DIR = ROOT / "docs" / "desktop-v2" / "parite-v1"
STATUTS = {"fait", "partiel", "absent", "exclu"}


def norm(text: str) -> str:
    text = text.replace("&", "")
    return "".join(c for c in unicodedata.normalize("NFD", text.lower()) if unicodedata.category(c) != "Mn")


def v2_sources() -> str:
    out = []
    for pattern in ("*.cpp", "*.hpp", "*.qml", "*.js"):  # le Studio vit en QML : ses preuves comptent
        for p in sorted((ROOT / "apps" / "desktop_v2").rglob(pattern)):
            out.append(p.read_text(encoding="utf-8", errors="ignore"))
    return norm("\n".join(out))


def main() -> int:
    errors = []
    gen = subprocess.run([sys.executable, str(ROOT / "scripts/parity/extract_v1_inventory.py"), "--check"],
                         capture_output=True, text=True)
    if gen.returncode != 0:
        errors.append("inventaire-v1.csv périmé (relancer scripts/parity/extract_v1_inventory.py)")
    inventory = list(csv.DictReader((DIR / "inventaire-v1.csv").open(encoding="utf-8")))
    mapping = {}
    for row in csv.DictReader((DIR / "correspondances.csv").open(encoding="utf-8")):
        if row["id"] in mapping:
            errors.append(f"correspondance en double : {row['id']}")
        mapping[row["id"]] = row
    sources = v2_sources()
    counts = {s: 0 for s in STATUTS}
    for item in inventory:
        row = mapping.get(item["id"])
        label = f"{item['type']} « {item['libelle_v1']} » ({item['source_v1']})"
        if row is None:
            errors.append(f"sans correspondance : {label}")
            continue
        status = row["statut"]
        if status not in STATUTS:
            errors.append(f"statut inconnu « {status} » : {label}")
            continue
        counts[status] += 1
        if status in {"fait", "partiel"}:
            proof = row["preuve_v2"].strip()
            if not proof:
                errors.append(f"{status} sans preuve : {label}")
            elif norm(proof) not in sources:
                errors.append(f"preuve introuvable dans apps/desktop_v2 (« {proof} ») : {label}")
        if status == "exclu" and not row["note"].strip():
            errors.append(f"exclu sans raison écrite : {label}")
    extra = set(mapping) - {i["id"] for i in inventory}
    for ident in sorted(extra):
        errors.append(f"correspondance orpheline (la fonction V1 n'existe plus) : {ident}")
    ceiling = int((DIR / "plafond-absent.txt").read_text().strip())
    if counts["absent"] > ceiling:
        errors.append(f"{counts['absent']} fonctions absentes > plafond {ceiling} : régression de parité")
    total = sum(counts.values())
    print(f"parité V1 -> V2 : {total} fonctions ; fait {counts['fait']}, partiel {counts['partiel']}, "
          f"absent {counts['absent']} (plafond {ceiling}), exclu {counts['exclu']}")
    if counts["absent"] < ceiling:
        print(f"  -> le plafond peut descendre à {counts['absent']} (plafond-absent.txt)")
    for e in errors:
        print("ERREUR :", e)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
