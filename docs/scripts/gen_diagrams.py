#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Génère les diagrammes de la documentation en SVG (déterministe, hors ligne).

Aucun rendu Mermaid/Graphviz n'est disponible dans l'environnement (ni node
mermaid-cli, ni dot). Ce script produit directement des SVG vectoriels, propres
et embarquables par xhtml2pdf. Les sources Mermaid/DOT équivalentes sont
conservées dans docs/assets/diagrams/ pour une édition ultérieure.
"""
from __future__ import annotations
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "generated")
os.makedirs(OUT, exist_ok=True)

FONT = "Helvetica, Arial, sans-serif"


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Svg:
    def __init__(self, w: int, h: int, title: str = ""):
        self.w, self.h = w, h
        self.parts: list[str] = []
        self.title = title

    def box(self, x, y, w, h, text, sub="", fill="#eaf0fb", stroke="#3a5a8c", fs=12):
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="1.4"/>'
        )
        cy = y + h / 2 + (fs / 3 if not sub else -2)
        self.parts.append(
            f'<text x="{x + w/2}" y="{cy}" text-anchor="middle" '
            f'font-family="{FONT}" font-size="{fs}" fill="#12233d">{esc(text)}</text>'
        )
        if sub:
            self.parts.append(
                f'<text x="{x + w/2}" y="{y + h/2 + fs - 1}" text-anchor="middle" '
                f'font-family="{FONT}" font-size="{fs-3}" fill="#5b6b82">{esc(sub)}</text>'
            )

    def arrow(self, x1, y1, x2, y2, dashed=False, label=""):
        dash = ' stroke-dasharray="5 4"' if dashed else ""
        self.parts.append(
            f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#33475b" '
            f'stroke-width="1.4" marker-end="url(#a)"{dash}/>'
        )
        if label:
            self.parts.append(
                f'<text x="{(x1+x2)/2+4}" y="{(y1+y2)/2-3}" font-family="{FONT}" '
                f'font-size="10" fill="#5b6b82">{esc(label)}</text>'
            )

    def text(self, x, y, s, fs=11, anchor="start", color="#33475b", bold=False):
        w = ' font-weight="bold"' if bold else ""
        self.parts.append(
            f'<text x="{x}" y="{y}" text-anchor="{anchor}" font-family="{FONT}" '
            f'font-size="{fs}" fill="{color}"{w}>{esc(s)}</text>'
        )

    def save(self, name):
        head = (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" '
            f'height="{self.h}" viewBox="0 0 {self.w} {self.h}">'
            '<defs><marker id="a" markerWidth="9" markerHeight="9" refX="7" refY="3" '
            'orient="auto"><path d="M0,0 L7,3 L0,6 z" fill="#33475b"/></marker></defs>'
            f'<rect width="{self.w}" height="{self.h}" fill="white"/>'
        )
        body = "".join(self.parts)
        with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
            f.write(head + body + "</svg>")


# 1. Pipeline image -> broderie
s = Svg(760, 470)
steps = [
    ("Image matricielle", "PNG/JPEG/BMP/TIFF"),
    ("Prétraitement", "image::ops"),
    ("Quantification + segmentation", "segmentation (CIELAB)"),
    ("Vectorisation", "vectorization"),
    ("Objets de broderie", "document"),
    ("Génération de points", "stitch_generation"),
    ("Analyse + simulation", "stitch_analysis"),
    ("Export DST", "formats::dst"),
]
x = 40
y = 30
for i, (t, sub) in enumerate(steps):
    s.box(x, y, 300, 40, t, sub=sub)
    if i < len(steps) - 1:
        s.arrow(x + 150, y + 40, x + 150, y + 54)
    y += 54
s.text(360, 40, "Modèle de document éditable", bold=True)
s.text(360, 58, "(les objets survivent ; les points", fs=10)
s.text(360, 72, "sont recalculés à la demande —", fs=10)
s.text(360, 86, "séparation intention / points)", fs=10)
s.save("pipeline.svg")

# 2. Architecture / modules (couches)
s = Svg(760, 430)
s.text(30, 24, "apps (dépendent des libs ; seul desktop voit Qt)", bold=True)
s.box(40, 34, 200, 40, "apps/desktop", "Qt 6 Widgets", fill="#fdeede", stroke="#b5791f")
s.box(260, 34, 200, 40, "apps/cli", "openstitch-cli", fill="#fdeede", stroke="#b5791f")
s.text(30, 104, "libs cœur (jamais de Qt)", bold=True)
row1 = ["document", "commands", "project_io", "stitch_analysis", "optimization"]
row2 = ["stitch_generation", "stitch", "vectorization", "segmentation"]
row3 = ["geometry", "image", "formats", "thread (palette: prévu)"]
for r, ys in ((row1, 118), (row2, 172), (row3, 226)):
    xx = 40
    for m in r:
        s.box(xx, ys, 138, 40, m, fill="#eaf3ec", stroke="#3f7a4e", fs=11)
        xx += 144
s.box(40, 300, 680, 44, "core — unités fortes (µm/mm/px), ids, Result<T>, logging",
      fill="#e9eef7", stroke="#3a5a8c")
s.text(40, 372, "Dépendances orientées vers le bas. geometry encapsule Clipper2 ;", fs=11)
s.text(40, 388, "image/segmentation encapsulent OpenCV ; formats contient le codec DST ;", fs=11)
s.text(40, 404, "project_io encapsule nlohmann/json et minizip-ng.", fs=11)
s.save("architecture.svg")

# 3. Modèle de données (relations principales)
s = Svg(760, 430)
s.box(300, 20, 160, 40, "Project", "document::Project", fill="#e9eef7")
s.box(60, 110, 170, 40, "Image original", "+ ops (ImageOp[])")
s.box(300, 110, 160, 40, "Segmentation", "region_slots[]")
s.box(300, 190, 160, 40, "VectorObject[]", "PathSet (outer+holes)")
s.box(300, 270, 160, 40, "EmbroideryObject[]", "params: variant")
s.box(560, 270, 170, 40, "StitchSequence", "cache recalculable")
s.box(60, 270, 170, 40, "SewingOrder", "ObjectId ordonnés")
s.box(560, 190, 170, 56, "StitchParams",
      "Running | Tatami | Satin", fill="#eef6ea", stroke="#3f7a4e", fs=11)
s.arrow(340, 60, 200, 110)
s.arrow(380, 60, 380, 110)
s.arrow(380, 150, 380, 190)
s.arrow(380, 230, 380, 270)
s.arrow(420, 290, 560, 290)
s.arrow(340, 290, 230, 290)
s.arrow(560, 250, 480, 260, dashed=True)
s.text(470, 250, "type", fs=9)
s.text(60, 360, "La géométrie éditable (VectorObject) et les points générés", fs=11)
s.text(60, 376, "(StitchSequence) sont séparés : régénérer ne détruit pas la source.", fs=11)
s.save("data-model.svg")

# 4. Enregistrement DST (3 octets)
s = Svg(760, 300)
s.text(30, 26, "Enregistrement DST : 3 octets = déplacement (dx, dy) en unités de 0,1 mm",
       bold=True, fs=13)
labels = ["octet 1", "octet 2", "octet 3"]
x = 60
for i, lb in enumerate(labels):
    s.box(x, 50, 200, 40, lb, fill="#fbf3e6", stroke="#b5791f")
    x += 220
s.text(60, 120, "Bits de valeur : ±1, ±3, ±9, ±27, ±81 (ternaire équilibré)", fs=11)
s.text(60, 140, "Octet 3, bits hauts : type de commande", fs=11)
s.box(60, 160, 300, 34, "0b..11 (bas) → point normal (Stitch)", fill="#eef6ea", stroke="#3f7a4e", fs=11)
s.box(60, 200, 300, 34, "bit 7 → saut (Jump)", fill="#eef6ea", stroke="#3f7a4e", fs=11)
s.box(380, 160, 320, 34, "bits 7+6 → changement de couleur (arrêt)", fill="#eef6ea", stroke="#3f7a4e", fs=11)
s.box(380, 200, 320, 34, "0x00 0x00 0xF3 → fin (End)", fill="#fde7e7", stroke="#b23", fs=11)
s.text(60, 268, "En-tête ASCII de 512 octets (LA, ST, CO, ±X, ±Y, AX, AY) calculé après le corps.",
       fs=11)
s.save("dst-record.svg")


def sequence(name, title, actors, msgs):
    n = len(actors)
    colw = 180
    w = 60 + colw * n
    h = 90 + len(msgs) * 34 + 30
    s = Svg(w, h)
    s.text(20, 22, title, bold=True, fs=13)
    xs = {}
    for i, a in enumerate(actors):
        cx = 60 + colw * i + colw / 2
        xs[a] = cx
        s.box(cx - 80, 34, 160, 36, a, fill="#e9eef7")
        s.parts.append(
            f'<line x1="{cx}" y1="70" x2="{cx}" y2="{h-20}" stroke="#c3ccd8" stroke-dasharray="3 3"/>'
        )
    y = 100
    for src, dst, label, dashed in msgs:
        x1, x2 = xs[src], xs[dst]
        s.arrow(x1, y, x2, y, dashed=dashed)
        mx = (x1 + x2) / 2
        s.text(mx, y - 6, label, fs=10, anchor="middle")
        y += 34
    s.save(name)


sequence(
    "seq-import.svg", "Séquence — import d'une image",
    ["MainWindow", "image::load", "ImportDialog", "document"],
    [("MainWindow", "image::load", "load_image(path)", False),
     ("image::load", "MainWindow", "Image (RGBA)", True),
     ("MainWindow", "ImportDialog", "taille physique (mm)", False),
     ("ImportDialog", "MainWindow", "ImagePlacement", True),
     ("MainWindow", "document", "Project{original, mm_per_px}", False)],
)

sequence(
    "seq-stitchgen.svg", "Séquence — génération des points",
    ["MainWindow", "generate_sequence", "run_stitch / fill_*", "StitchSequence"],
    [("MainWindow", "generate_sequence", "generate_sequence(project)", False),
     ("generate_sequence", "run_stitch / fill_*", "par objet visible", False),
     ("run_stitch / fill_*", "generate_sequence", "points + travels", True),
     ("generate_sequence", "StitchSequence", "Stitch/Jump/ColorChange/End", False),
     ("StitchSequence", "MainWindow", "Result<StitchSequence>", True)],
)

sequence(
    "seq-dst.svg", "Séquence — export DST",
    ["MainWindow", "encode_dst", "en-tête + corps", "fichier .dst"],
    [("MainWindow", "encode_dst", "write_dst_file(seq)", False),
     ("encode_dst", "en-tête + corps", "quantif. 0,1 mm + subdivision", False),
     ("en-tête + corps", "encode_dst", "octets déterministes", True),
     ("encode_dst", "fichier .dst", "512 o + records + 0x00 00 F3", False)],
)

print("Diagrammes générés dans", os.path.normpath(OUT))
