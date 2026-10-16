#!/usr/bin/env python3
"""Fidélité d'une numérisation : image source contre couverture réelle des points du DST.

    python3 -I fidelity.py source.png sortie.dst palette.json mm_par_px sortie_dir [--bg #E3D4BB]

Rend la couverture des points (fil plein de 0,5 mm, ordre de couture, vraie palette) à l'échelle
EXACTE de l'image source (1 pixel = 1 pixel source), l'aligne sur la source, puis classe chaque pixel :
  manquant      la source a de la couleur (hors fond), la broderie n'a rien ;
  mauvaise      les deux ont de la couleur mais l'écart (CIELAB approché) dépasse --de ;
  en trop       la broderie couvre un pixel que la source donne au fond.
Écrit dans sortie_dir : couverture.png, erreurs.png (rouge = manquant, jaune = mauvaise,
magenta = en trop, gris = ok), details.txt (composantes d'erreur les plus grosses, avec position en
pixels source et en mm, couleur source) et imprime un résumé chiffré.
Dépend de numpy et Pillow seulement.
"""
import argparse
import json
import os

import numpy as np
from PIL import Image, ImageDraw

ap = argparse.ArgumentParser()
ap.add_argument("source")
ap.add_argument("dst")
ap.add_argument("palette")
ap.add_argument("mm_per_px", type=float)
ap.add_argument("out")
ap.add_argument("--bg", default="#E3D4BB", help="couleur du fond ignoré")
ap.add_argument("--bg-tol", type=float, default=6.0, help="écart au fond (ΔE) en dessous duquel un pixel source est du fond (en plus : connecté au bord de l'image)")
ap.add_argument("--de", type=float, default=30.0, help="écart de couleur à partir duquel un pixel est « mauvaise couleur »")
ap.add_argument("--min-px", type=int, default=12, help="aire minimale (px) d'une composante d'erreur retenue")
ap.add_argument("--near", type=float, default=18.0, help="ΔE maximal entre un pixel source et une couleur de la palette pour compter son erreur")
ap.add_argument("--thread-mm", type=float, default=0.5)
ap.add_argument("--close", type=int, default=2, help="fermeture (px) de la couverture ; 0 = aucune (révèle les petits trous que l'application affiche)")
ap.add_argument("--open", type=int, default=1, dest="open_r", help="ouverture (px) des composantes d'erreur")
args = ap.parse_args()
os.makedirs(args.out, exist_ok=True)

src = np.asarray(Image.open(args.source).convert("RGB")).astype(np.float64)
H, W = src.shape[:2]
palette = json.load(open(args.palette))


def lab(rgb):
    c = rgb / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


bg = np.array([int(args.bg[i : i + 2], 16) for i in (1, 3, 5)], dtype=np.float64)
labsrc = lab(src)
de_bg = np.linalg.norm(labsrc - lab(bg), axis=-1)
# Fond = tout pixel proche de la couleur du tissu, connecté au bord ou non : un trou de cette couleur
# entre deux zones se laisse sans fil. Le crème (ΔE ~ 11 du beige) reste du motif avec la tolérance 6.
src_fg = de_bg > args.bg_tol

# --- lecture du DST (même décodage que render_dst.py) ---
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


x = y = 0
blocks = [[]]
for i in range(0, len(data) - 2, 3):
    b0, b1, b2 = data[i], data[i + 1], data[i + 2]
    if b2 == 0xF3:
        break
    dx, dy = decode(b0, b1, b2)
    nx, ny = x + dx, y + dy
    if b2 & 0xC3 == 0xC3:
        blocks.append([])
    elif b2 & 0x83 == 0x83:
        pass
    else:
        blocks[-1].append((x, y, nx, ny))
    x, y = nx, ny

allp = [(s[0], s[1]) for b in blocks for s in b] + [(s[2], s[3]) for b in blocks for s in b]
minx, maxx = min(p[0] for p in allp), max(p[0] for p in allp)
miny, maxy = min(p[1] for p in allp), max(p[1] for p in allp)
px_per_01mm = 1.0 / (10.0 * args.mm_per_px)  # 1 unité DST = 0,1 mm

# --- alignement : centre de la boîte englobante du premier plan source = centre des points ---
ys, xs = np.nonzero(src_fg)
src_cx, src_cy = (xs.min() + xs.max()) / 2.0, (ys.min() + ys.max()) / 2.0
dst_cx, dst_cy = (minx + maxx) / 2.0, (miny + maxy) / 2.0


def to_px(dx, dy):
    return (src_cx + (dx - dst_cx) * px_per_01mm, src_cy - (dy - dst_cy) * px_per_01mm)


cover = Image.new("RGB", (W, H), (0, 255, 0))
d = ImageDraw.Draw(cover)
width = max(1, int(round(args.thread_mm / args.mm_per_px)))
for bi, block in enumerate(blocks):
    colour = tuple(palette[min(bi, len(palette) - 1)])
    for ax, ay, bx, by in block:
        d.line([to_px(ax, ay), to_px(bx, by)], fill=colour, width=width)
cover.save(os.path.join(args.out, "couverture.png"))
cov = np.asarray(cover).astype(np.float64)
cov_fg = ~((cov[..., 0] == 0) & (cov[..., 1] == 255) & (cov[..., 2] == 0))

# fermeture 1 px de la couverture (les fils isolés d'un satin n'ont pas de trou réel)
def dilate(m, r):
    out = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            out |= np.roll(np.roll(m, dy, 0), dx, 1)
    return out


def erode(m, r):
    return ~dilate(~m, r)


if args.close > 0:
    cov_fg = erode(dilate(cov_fg, args.close), args.close)
dcol = np.linalg.norm(labsrc - lab(cov), axis=-1)
missing = src_fg & ~cov_fg
# Un détail perdu : le pixel source est proche (ΔE < --near) d'une couleur de la palette mais la
# broderie le couvre avec une AUTRE couleur. Les pixels de transition (anti-crénelage, proches d'aucun
# fil) ne sont pas des détails : aucun fil ne peut les représenter.
pal_lab = lab(np.array(palette, dtype=np.float64))
dpal = np.linalg.norm(labsrc[..., None, :] - pal_lab[None, None, :, :], axis=-1)
nearest = dpal.argmin(-1)
near_pal = dpal.min(-1) < args.near
cov_idx = np.zeros((H, W), dtype=np.int32) - 1
for k, c in enumerate(palette):
    cov_idx[(cov == np.array(c, dtype=np.float64)).all(-1)] = k
wrong = src_fg & cov_fg & near_pal & (nearest != cov_idx) & (dcol > args.de)
extra = ~src_fg & cov_fg


def opening(m, r=None):
    r = args.open_r if r is None else r
    return m if r <= 0 else dilate(erode(m, r), r)


def components(mask):
    """Composantes 4-connexes (parcours itératif), triées par aire décroissante."""
    lab_ = np.zeros(mask.shape, dtype=np.int32)
    n = 0
    out = []
    ys_, xs_ = np.nonzero(mask)
    for y0, x0 in zip(ys_, xs_):
        if lab_[y0, x0]:
            continue
        n += 1
        stack = [(y0, x0)]
        lab_[y0, x0] = n
        pts = []
        while stack:
            cy, cx = stack.pop()
            pts.append((cy, cx))
            for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                if 0 <= ny < mask.shape[0] and 0 <= nx < mask.shape[1] and mask[ny, nx] and not lab_[ny, nx]:
                    lab_[ny, nx] = n
                    stack.append((ny, nx))
        out.append(pts)
    out.sort(key=len, reverse=True)
    return out


# Trous INTÉRIEURS : manquant à plus de 8 px du fond extérieur (le tissu qui apparaît au milieu du motif,
# bien plus grave qu'un liseré au bord).
edge_zone = np.zeros_like(src_fg)
grow = ~src_fg
for _ in range(8):
    grow = grow | np.roll(grow, 1, 0) | np.roll(grow, -1, 0) | np.roll(grow, 1, 1) | np.roll(grow, -1, 1)
edge_zone = grow
interior = missing & ~edge_zone
visible = {"manquant": opening(missing), "trou interieur": opening(interior), "mauvaise": opening(wrong), "en trop": opening(extra)}
total_fg = int(src_fg.sum())
lines = []
summary = []
for name, m in visible.items():
    comps = [c for c in components(m) if len(c) >= args.min_px]
    area = sum(len(c) for c in comps)
    summary.append(f"{name:9s}: {area:7d} px ({100 * area / max(total_fg, 1):5.2f} % du motif), {len(comps)} composantes")
    lines.append(f"## {name} ({len(comps)} composantes >= {args.min_px} px)")
    for c in comps[:40]:
        cy = np.array([p[0] for p in c])
        cx = np.array([p[1] for p in c])
        med = np.median(src[cy, cx], axis=0).astype(int)
        mm2 = len(c) * args.mm_per_px ** 2
        lines.append(
            f"  {len(c):6d} px = {mm2:7.1f} mm2  centre ({int(cx.mean())},{int(cy.mean())}) px  "
            f"boite x {cx.min()}-{cx.max()} y {cy.min()}-{cy.max()}  source #{med[0]:02X}{med[1]:02X}{med[2]:02X}"
        )
open(os.path.join(args.out, "details.txt"), "w", encoding="utf8").write("\n".join(lines) + "\n")

vis = np.full((H, W, 3), 235, dtype=np.uint8)
vis[src_fg | cov_fg] = (200, 200, 200)
vis[visible["en trop"]] = (255, 0, 255)
vis[visible["mauvaise"]] = (255, 210, 0)
vis[visible["manquant"]] = (255, 0, 0)
vis[visible["trou interieur"]] = (0, 160, 0)
Image.fromarray(vis).save(os.path.join(args.out, "erreurs.png"))
print(f"alignement: boite source ({xs.min()},{ys.min()})-({xs.max()},{ys.max()}), stitches {maxx - minx}x{maxy - miny} (0,1 mm)")
print("\n".join(summary))
