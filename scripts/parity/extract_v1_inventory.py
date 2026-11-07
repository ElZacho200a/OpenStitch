#!/usr/bin/env python3
"""Inventaire exhaustif des fonctions de la V1 (apps/desktop), généré depuis ses sources.

Sortie : docs/desktop-v2/parite-v1/inventaire-v1.csv (une ligne par fonction de la V1 que
l'utilisateur peut atteindre : entrée de menu, action de barre, entrée de menu contextuel, outil
du canevas, panneau, boîte de dialogue, raccourci). Déterministe (ordre fixe), sans réseau.

Usage : python scripts/parity/extract_v1_inventory.py [--check]
  --check : échoue (code 1) si le fichier généré diffère de celui du dépôt.
"""
import csv
import hashlib
import io
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
V1 = ROOT / "apps" / "desktop"
OUT = ROOT / "docs" / "desktop-v2" / "parite-v1" / "inventaire-v1.csv"

STR = r'(?:QObject::|QCoreApplication::)?tr\("((?:[^"\\]|\\.)*)"\)'

PATTERNS = [
    ("action", re.compile(r"addAction\(\s*" + STR)),
    ("action", re.compile(r"new\s+QAction\(\s*" + STR)),
    ("menu", re.compile(r"addMenu\(\s*" + STR)),
    ("bouton", re.compile(r"new\s+QPushButton\(\s*" + STR)),
    ("case", re.compile(r"new\s+QCheckBox\(\s*" + STR)),
    ("dock", re.compile(r"new\s+QDockWidget\(\s*" + STR)),
    ("dialogue", re.compile(r"setWindowTitle\(\s*" + STR)),
]


def clean(label: str) -> str:
    label = label.replace("\\n", " ").replace('\\"', '"')
    label = re.sub(r"&(?=\w)", "", label)  # accélérateur Qt
    return re.sub(r"\s+", " ", label).strip().rstrip("…").rstrip(".").strip()


def tools():
    """Outils du canevas : enum Tool de apps/desktop/tools.hpp."""
    text = (V1 / "tools.hpp").read_text(encoding="utf-8")
    body = text[text.index("enum class Tool") :]
    body = body[: body.index("};")]
    for line in body.splitlines():
        m = re.match(r"\s*([A-Z][A-Za-z0-9]+)\s*[,=]", line)
        if m:
            yield m.group(1)


def main() -> int:
    rows = {}
    for path in sorted(V1.glob("*.cpp")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for kind, pattern in PATTERNS:
            for m in pattern.finditer(text):
                label = clean(m.group(1))
                if len(label) < 2 or label.startswith("%") or label in {"OK", "Annuler"}:
                    continue
                key = (kind, label.lower())
                rows.setdefault(key, (kind, label, path.name))
    for name in tools():
        rows.setdefault(("outil", name.lower()), ("outil", name, "tools.hpp"))
    # raccourcis déclarés
    for path in sorted(V1.glob("*.cpp")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for m in re.finditer(r'setShortcut\(\s*QKeySequence\(\s*(?:QStringLiteral|tr)\("([^"]+)"\)', text):
            rows.setdefault(("raccourci", m.group(1).lower() + path.name),
                            ("raccourci", m.group(1), path.name))
    ordered = sorted(rows.values(), key=lambda r: (r[0], r[1].lower(), r[2]))
    buf = io.StringIO(newline="")
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(["id", "type", "libelle_v1", "source_v1"])
    for kind, label, source in ordered:
        ident = hashlib.sha1(f"{kind}|{label.lower()}".encode()).hexdigest()[:8]
        w.writerow([ident, kind, label, source])
    new = buf.getvalue()
    if "--check" in sys.argv:
        old = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if old != new:
            print("inventaire-v1.csv périmé : relancer scripts/parity/extract_v1_inventory.py")
            return 1
        return 0
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(new, encoding="utf-8", newline="")
    print(f"{len(ordered)} fonctions V1 inventoriées -> {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
