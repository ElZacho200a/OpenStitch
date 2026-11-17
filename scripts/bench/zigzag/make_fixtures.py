"""Fixtures PNG deterministes pour le banc RND-ZIGZAG-01 (96 dpi : 1 px = 0,2646 mm).

Usage : python make_fixtures.py <dossier_sortie>
Chaque motif est un aplat sur fond blanc, sans anticrenelage aleatoire.
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

W = H = 400  # ~106 mm


def canvas():
    im = Image.new("RGB", (W, H), "white")
    return im, ImageDraw.Draw(im)


def font(size):
    for name in ("arialbd.ttf", "DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    raise SystemExit("police introuvable : installer arialbd.ttf ou DejaVuSans-Bold.ttf")


def text_peto():
    im, d = canvas()
    f = font(150)
    for i, ch in enumerate("PeTO"):
        d.text((20 + (i % 2) * 190, 20 + (i // 2) * 190), ch, font=f, fill=(20, 40, 120))
    return im


def u_ring_crescent():
    im, d = canvas()
    d.rectangle([40, 40, 120, 200], fill=(180, 30, 30))
    d.pieslice([40, 120, 120, 280], 0, 180, fill=(180, 30, 30))
    d.rectangle([60, 40, 100, 200], fill="white")
    d.ellipse([220, 60, 360, 200], outline=(30, 120, 60), width=28)
    d.ellipse([60, 250, 200, 390], fill=(220, 160, 20))
    d.ellipse([90, 240, 230, 380], fill="white")
    return im


def narrow_spike():
    im, d = canvas()
    d.polygon([(200, 20), (215, 360), (185, 360)], fill=(40, 40, 40))
    d.polygon([(40, 380), (360, 380), (200, 330)], fill=(40, 40, 40))
    return im


def big_concave():
    im, d = canvas()
    d.polygon([(20, 20), (380, 20), (380, 380), (240, 380), (240, 140), (150, 140),
               (150, 380), (20, 380)], fill=(90, 30, 140))
    return im


def logo_4c():
    im, d = canvas()
    d.ellipse([40, 40, 360, 360], fill=(47, 72, 88))
    d.ellipse([90, 90, 310, 310], fill=(232, 163, 60))
    d.polygon([(200, 120), (270, 270), (130, 270)], fill=(201, 69, 46))
    d.ellipse([175, 190, 225, 240], fill=(47, 72, 88))
    return im


def many_small():
    im, d = canvas()
    n = 0
    for gy in range(20, 380, 14):
        for gx in range(20, 380, 14):
            if (n * 7) % 5 != 0:
                d.ellipse([gx, gy, gx + 9, gy + 9], fill=(30, 30, 30))
            n += 1
    return im


FIXTURES = {
    "peto_text": text_peto, "u_ring_crescent": u_ring_crescent,
    "narrow_spike": narrow_spike, "big_concave": big_concave,
    "logo_4c": logo_4c, "many_small": many_small,
}

if __name__ == "__main__":
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for name, fn in FIXTURES.items():
        fn().save(out / f"{name}.png", dpi=(96, 96))
        print(out / f"{name}.png")
