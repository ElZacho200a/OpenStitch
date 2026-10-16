#!/usr/bin/env python3
"""Vert intérieur d'un rendu réaliste sur fond vert : le tissu visible À L'INTÉRIEUR du motif.

    openstitch-cli project render p.osp -o rendu.png --px-per-mm 6
    python3 -I green_holes.py rendu.png [--mm-per-px 0.1667]

Le vert connecté au bord de l'image est le fond extérieur ; le reste (trous, interstices entre zones,
dentelure des rangées de tatami) est le défaut mesuré. Imprime la surface en mm2 et sa part du motif.
Dépend de numpy et Pillow seulement.
"""
import argparse

import numpy as np
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument("png")
ap.add_argument("--px-per-mm", type=float, default=6.0)
ap.add_argument("--out", help="PNG des trous intérieurs (rouge sur gris)")
a = ap.parse_args()

im = np.asarray(Image.open(a.png).convert("RGB")).astype(int)
green = (im[..., 1] > 180) & (im[..., 0] < 90) & (im[..., 2] < 90)
H, W = green.shape
# Fond extérieur : vert connecté au bord (propagation par dilatation bornée, sur un maillage /2).
f = 2
gh, gw = H // f * f, W // f * f
low = green[:gh, :gw].reshape(gh // f, f, gw // f, f).any(axis=(1, 3))
reach = np.zeros_like(low)
reach[0, :] = low[0, :]
reach[-1, :] = low[-1, :]
reach[:, 0] = low[:, 0]
reach[:, -1] = low[:, -1]
while True:
    g = reach.copy()
    for _ in range(16):
        g |= (np.roll(g, 1, 0) | np.roll(g, -1, 0) | np.roll(g, 1, 1) | np.roll(g, -1, 1)) & low
    if (g == reach).all():
        break
    reach = g
outside = np.zeros((H, W), dtype=bool)
outside[:gh, :gw] = np.repeat(np.repeat(reach, f, 0), f, 1)
inner = green & ~outside
motif = ~(green & outside)
mm2 = 1.0 / (a.px_per_mm ** 2)
print(f"vert intérieur : {inner.sum() * mm2:8.1f} mm2  ({100 * inner.sum() / max(motif.sum(), 1):.3f} % du motif)  motif {motif.sum() * mm2:.0f} mm2")
if a.out:
    v = np.full((H, W, 3), 235, np.uint8)
    v[motif] = (190, 190, 190)
    v[inner] = (255, 0, 0)
    Image.fromarray(v).save(a.out)
