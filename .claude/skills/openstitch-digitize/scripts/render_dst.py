#!/usr/bin/env python3
"""Rendu approximatif d'un DST (diagnostic, PAS le rendu réaliste de l'application).

    python3 -I render_dst.py fichier.dst sortie.png [--source image.png] [--palette couleurs.json]

Couleur d'un bloc (= un fil, le DST ne porte pas les couleurs) :
  --palette : liste JSON de [r,g,b], un par bloc dans l'ordre de couture ;
  sinon --source : médiane de l'image source sous les points du bloc (le DST est recadré sur la
                   boîte englobante du motif : valable si le fond n'a pas été brodé) ;
  sinon une palette fixe.
Dépend de Pillow et numpy.
"""
import argparse
import json
import numpy as np
from PIL import Image, ImageDraw

ap = argparse.ArgumentParser()
ap.add_argument("dst")
ap.add_argument("out")
ap.add_argument("--source")
ap.add_argument("--palette")
ap.add_argument("--scale", type=float, default=4.0, help="px par mm")
args = ap.parse_args()

data = open(args.dst, "rb").read()[512:]


def decode(b0, b1, b2):
    x = y = 0
    for bit, v in ((0x01, 1), (0x02, -1), (0x04, 9), (0x08, -9)):
        x += v if b0 & bit else 0
    for bit, v in ((0x80, 1), (0x40, -1), (0x20, 9), (0x10, -9)):
        y += v if b0 & bit else 0
    for bit, v in ((0x01, 3), (0x02, -3), (0x04, 27), (0x08, -27)):
        x += v if b1 & bit else 0
    for bit, v in ((0x80, 3), (0x40, -3), (0x20, 27), (0x10, -27)):
        y += v if b1 & bit else 0
    for bit, v in ((0x04, 81), (0x08, -81)):
        x += v if b2 & bit else 0
    for bit, v in ((0x20, 81), (0x10, -81)):
        y += v if b2 & bit else 0
    return x, y


x = y = jumps = 0
blocks = [[]]
for i in range(0, len(data) - 2, 3):
    b0, b1, b2 = data[i], data[i + 1], data[i + 2]
    if b2 == 0xF3:
        break
    dx, dy = decode(b0, b1, b2)
    nx, ny = x + dx, y + dy
    if b2 & 0xC3 == 0xC3:  # changement de couleur
        blocks.append([])
    elif b2 & 0x83 == 0x83:  # saut
        jumps += 1
    else:
        blocks[-1].append((x, y, nx, ny))
    x, y = nx, ny

pts = [(s[0], s[1]) for b in blocks for s in b] + [(s[2], s[3]) for b in blocks for s in b]
xs, ys = [p[0] for p in pts], [p[1] for p in pts]
minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
W = int((maxx - minx) / 10 * args.scale) + 20
H = int((maxy - miny) / 10 * args.scale) + 20
img = Image.new("RGB", (W, H), (231, 226, 214))
draw = ImageDraw.Draw(img)
palette = json.load(open(args.palette)) if args.palette else None
src = np.asarray(Image.open(args.source).convert("RGB")) if args.source else None
fallback = [(200, 30, 30), (30, 90, 200), (240, 180, 20), (20, 130, 70), (90, 40, 130),
            (20, 20, 20), (240, 120, 30), (120, 120, 120)]


def to_px(px, py):
    return ((px - minx) / 10 * args.scale + 10, (maxy - py) / 10 * args.scale + 10)


for bi, block in enumerate(blocks):
    if not block:
        continue
    if palette:
        colour = tuple(palette[min(bi, len(palette) - 1)])
    elif src is not None:
        p = np.array([(s[0], s[1]) for s in block])
        sh, sw = src.shape[:2]
        u = (p[:, 0] - minx) / max(maxx - minx, 1)
        v = (maxy - p[:, 1]) / max(maxy - miny, 1)
        px = src[np.clip((v * (sh - 1)).astype(int), 0, sh - 1),
                 np.clip((u * (sw - 1)).astype(int), 0, sw - 1)]
        colour = tuple(int(t) for t in np.median(px, axis=0))
    else:
        colour = fallback[bi % len(fallback)]
    for ax, ay, bx, by in block:
        draw.line([to_px(ax, ay), to_px(bx, by)], fill=colour, width=2)
img.save(args.out)
print(f"{len([b for b in blocks if b])} blocs, {sum(len(b) for b in blocks)} segments, "
      f"{jumps} sauts, image {img.size}")
