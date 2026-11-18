"""Couverture / debordement d'un DST par rapport a la forme SVG source (RND-ZIGZAG-01).

Decode le DST (0,1 mm, sauts exclus), trace chaque point cousu (fil de 0,4 mm par defaut) et
compare au masque de la forme. Mesure identique pour les deux moteurs. Les stitches sont centres
sur la boite englobante de la forme (le DST n'a pas d'origine absolue). Hypotheses a verifier :
orientation Y (Y DST vers le haut) et fil de 0,4 mm ; une couverture de 100 % n'est pas un
point correct (densite, tirage, sous-couche non evalues).
"""
import math, re
from PIL import Image, ImageChops, ImageDraw

PX = 10  # px par mm


def decode_dst(path):
    """-> liste de segments cousus [(x0,y0,x1,y1)] en mm (Y vers le haut), nb de stitches normaux."""
    d = open(path, "rb").read()[512:]
    x = y = 0
    segs, n, jumpcount = [], 0, 0
    for i in range(0, len(d) - 2, 3):
        b0, b1, b2 = d[i], d[i + 1], d[i + 2]
        dx = ((b0 & 1) - ((b0 >> 1) & 1) + 9 * ((b0 >> 2) & 1) - 9 * ((b0 >> 3) & 1)
              + 3 * (b1 & 1) - 3 * ((b1 >> 1) & 1) + 27 * ((b1 >> 2) & 1) - 27 * ((b1 >> 3) & 1)
              + 81 * ((b2 >> 2) & 1) - 81 * ((b2 >> 3) & 1))
        dy = (((b0 >> 7) & 1) - ((b0 >> 6) & 1) + 9 * ((b0 >> 5) & 1) - 9 * ((b0 >> 4) & 1)
              + 3 * ((b1 >> 7) & 1) - 3 * ((b1 >> 6) & 1) + 27 * ((b1 >> 5) & 1) - 27 * ((b1 >> 4) & 1)
              + 81 * ((b2 >> 5) & 1) - 81 * ((b2 >> 4) & 1))
        if b2 & 0xF3 == 0xF3:
            break
        nx, ny = x + dx, y + dy
        if b2 & 0x83 == 0x03:          # point normal
            segs.append((x / 10, y / 10, nx / 10, ny / 10)); n += 1
        elif b2 & 0x83 == 0x83:        # saut / changement de couleur
            jumpcount += 1
        x, y = nx, ny
    return segs, n


def flatten(d):
    """Aplatit un chemin SVG M,L,H,V,A,Z (absolu, notre jeu de fixtures) -> liste de sous-chemins."""
    toks = re.findall(r"[MLHVAZmla]|-?\d*\.?\d+(?:e-?\d+)?", d)
    subs, cur, i, x, y = [], [], 0, 0.0, 0.0
    num = lambda: float(toks[i])
    cmd = None
    while i < len(toks):
        if re.match(r"[A-Za-z]", toks[i]):
            cmd = toks[i]; i += 1
            if cmd == "Z":
                if cur: subs.append(cur); cur = []
                continue
        if cmd == "M":
            if cur: subs.append(cur)
            x, y = num(), float(toks[i + 1]); i += 2; cur = [(x, y)]; cmd = "L"
        elif cmd == "L":
            x, y = num(), float(toks[i + 1]); i += 2; cur.append((x, y))
        elif cmd == "H":
            x = num(); i += 1; cur.append((x, y))
        elif cmd == "V":
            y = num(); i += 1; cur.append((x, y))
        elif cmd in ("A", "a"):
            rx, ry, rot, fa, fs, ex, ey = [float(t) for t in toks[i:i + 7]]; i += 7
            if cmd == "a": ex, ey = x + ex, y + ey
            cur.extend(_arc(x, y, rx, fa, fs, ex, ey)); x, y = ex, ey
        else:
            raise ValueError(f"commande non geree : {cmd}")
    if cur: subs.append(cur)
    return subs


def _arc(x1, y1, r, fa, fs, x2, y2):
    """Arc circulaire (rx = ry) -> points (conversion endpoint -> centre, spec SVG F.6.5)."""
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    lam = (dx * dx + dy * dy) / (r * r)
    if lam > 1: r *= math.sqrt(lam)
    sq = math.sqrt(max(0.0, (r * r * r * r - r * r * (dx * dx + dy * dy)) / (r * r * (dx * dx + dy * dy)))) if (dx or dy) else 0
    sq = -sq if fa == fs else sq
    cx_, cy_ = sq * dy, -sq * dx
    cx, cy = cx_ + (x1 + x2) / 2, cy_ + (y1 + y2) / 2
    a1 = math.atan2(y1 - cy, x1 - cx); a2 = math.atan2(y2 - cy, x2 - cx)
    da = a2 - a1
    if fs and da < 0: da += 2 * math.pi
    if not fs and da > 0: da -= 2 * math.pi
    n = max(8, int(abs(da) * r))
    return [(cx + r * math.cos(a1 + da * k / n), cy + r * math.sin(a1 + da * k / n)) for k in range(1, n + 1)]


def shape_mask(subs, size, off):
    m = Image.new("1", size, 0)
    for s in subs:
        layer = Image.new("1", size, 0)
        ImageDraw.Draw(layer).polygon([((px - off[0]) * PX, (py - off[1]) * PX) for px, py in s], fill=1)
        m = ImageChops.logical_xor(m, layer)   # regle pair-impair : trous
    return m


def coverage(svg_path, dst_path, thread_mm=0.4, flip_y=True):
    d = re.search(r'\sd="([^"]+)"', open(svg_path, encoding="utf-8").read()).group(1)
    subs = flatten(d)
    xs = [p[0] for s in subs for p in s]; ys = [p[1] for s in subs for p in s]
    sb = ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2)
    segs, n = decode_dst(dst_path)
    pad = 5
    off = (min(xs) - pad, min(ys) - pad)
    size = (int((max(xs) - min(xs) + 2 * pad) * PX) + 1, int((max(ys) - min(ys) + 2 * pad) * PX) + 1)
    sx = [p for s in segs for p in (s[0], s[2])]; sy = [p for s in segs for p in (s[1], s[3])]
    if not segs:
        return None
    cb = ((min(sx) + max(sx)) / 2, (min(sy) + max(sy)) / 2)
    img = Image.new("1", size, 0); dr = ImageDraw.Draw(img)
    sgn = -1 if flip_y else 1
    for x0, y0, x1, y1 in segs:
        p = lambda x, y: ((x - cb[0] + sb[0] - off[0]) * PX, (sgn * (y - cb[1]) + sb[1] - off[1]) * PX)
        dr.line([p(x0, y0), p(x1, y1)], fill=1, width=max(1, round(thread_mm * PX)))
    mask = shape_mask(subs, size, off)
    inter = ImageChops.logical_and(img, mask)
    cnt = lambda im: sum(im.histogram()[255:]) or sum(1 for v in im.getdata() if v)
    a_shape, a_sew, a_int = cnt(mask), cnt(img), cnt(inter)
    return {"coverage": a_int / a_shape, "spill": 1 - a_int / a_sew if a_sew else 0.0, "stitches": n}
