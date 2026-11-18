"""Fixtures SVG (mm, 100x100) pour la voie « meme geometrie » de RND-ZIGZAG-01.

Un fichier par forme et par mode : <forme>__fill.svg et <forme>__satin.svg ; le mode est
porte par l'id du chemin (jeton ZigZag `fill` / `satin`). OpenStitch ignore l'id et recoit
le mode par operation (voir run_geometry_bench.py). Usage : python make_svg_fixtures.py <dossier>
"""
import math, sys
from pathlib import Path


def ring(cx, cy, r_out, r_in):
    f = lambda r, sweep: f"M{cx - r},{cy} a{r},{r} 0 1,{sweep} {2 * r},0 a{r},{r} 0 1,{sweep} {-2 * r},0 Z"
    return f(r_out, 1) + " " + f(r_in, 0)


def star(cx, cy, ro, ri, n=5):
    pts = []
    for i in range(2 * n):
        r = ro if i % 2 == 0 else ri
        a = -math.pi / 2 + i * math.pi / n
        pts.append(f"{cx + r * math.cos(a):.3f},{cy + r * math.sin(a):.3f}")
    return "M" + " L".join(pts) + " Z"


SHAPES = {
    "ring": ring(50, 50, 38, 24),
    "u_shape": "M18,12 H36 V62 A14,14 0 0 0 64,62 V12 H82 V62 A32,32 0 0 1 18,62 Z",
    "crescent": "M66,10 A42,42 0 1 0 66,90 A32,32 0 1 1 66,10 Z",
    "narrow_spike": "M50,6 L53,80 L88,94 H12 L47,80 Z",
    "t_shape": "M12,12 H88 V32 H60 V90 H40 V32 H12 Z",
    "big_concave": "M8,8 H92 V92 H62 V40 H38 V92 H8 Z",
    "star": star(50, 52, 44, 18),
    "p_with_hole": "M24,8 H56 A24,24 0 0 1 56,62 H44 V92 H24 Z M44,26 V44 H56 A9,9 0 0 0 56,26 Z",
}


# Variantes fines (colonnes de 5 a 6 mm) pour le mode satin : ZigZag refuse un satin large (> ~10 mm)
# et se replie sur le remplissage ; une forme large ne teste donc pas son satin.
SATIN_SHAPES = {
    "ring_thin": ring(50, 50, 40, 34),
    "u_thin": "M20,12 H26 V62 A24,24 0 0 0 74,62 V12 H80 V62 A30,30 0 0 1 20,62 Z",
    "crescent_thin": "M66,10 A42,42 0 1 0 66,90 A38,38 0 1 1 66,10 Z",
    "t_thin": "M12,12 H88 V18 H53 V90 H47 V18 H12 Z",
    "p_thin": "M24,8 H56 A22,22 0 0 1 56,52 H30 V92 H24 Z M30,14 V46 H56 A16,16 0 0 0 56,14 Z",
    "narrow_spike": SHAPES["narrow_spike"],
}

# Jetons ZigZag (id du chemin) : espacement 0,4 mm, sous-couche et compensation desactivees.
TOKENS = {"fill": "fill a=45 s=0.4 l=3 u=0 pc=0", "satin": "satin s=0.4 u=0 pc=0 ss=0"}


def svg(path_d, token):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="100mm" viewBox="0 0 100 100">'
            f'<path id="{token}" fill="#2f4858" fill-rule="evenodd" d="{path_d}"/></svg>\n')


if __name__ == "__main__":
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    n = 0
    for mode, shapes in (("fill", SHAPES), ("satin", SATIN_SHAPES)):
        for name, d in shapes.items():
            (out / f"{name}__{mode}.svg").write_text(svg(d, TOKENS[mode]), encoding="utf-8")
            n += 1
    print(n, "fichiers dans", out)
