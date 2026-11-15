#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Matrice des fiches de points Crosstela classées pour la broderie machine (STCH-CROS-01, L0).

Lit l'index public https://crosstela.com/fr/learn/stitches/ (et sa version anglaise) pour n'en
garder que des MÉTADONNÉES factuelles : identifiant d'URL, nom FR/EN, famille, type de technique,
usages, structure, niveau. Aucun texte, aucune image ni illustration n'est recopié. Le classement
A-E, la correspondance OpenStitch et la justification sont une analyse originale d'OpenStitch.

Classes (mutuellement exclusives) :
  A  déjà couverte par un type de point existant, tel quel ;
  B  préréglage / recette réalisable avec les primitives existantes ;
  C  nouvelle primitive ou extension de générateur nécessaire ;
  D  simulation visuelle seulement (boucle, nœud, fil glissé sous un point, fil métallique) :
     jamais présentée comme équivalente mécaniquement ;
  E  inadaptée à la broderie machine à point noué (perles, ruban, fils tirés, découpe, smocks).

Usage :
  python scripts/crosstela_matrix.py                    # télécharge les deux index
  python scripts/crosstela_matrix.py --fr a.html --en b.html
Sortie : docs/research/crosstela-stitch-matrix.csv (ordre stable : identifiant d'URL).
"""
from __future__ import annotations

import argparse
import csv
import html
import pathlib
import re
import sys
import urllib.request

INDEX_FR = "https://crosstela.com/fr/learn/stitches/"
INDEX_EN = "https://crosstela.com/learn/stitches/"
OUT = pathlib.Path(__file__).resolve().parent.parent / "docs" / "research" / "crosstela-stitch-matrix.csv"

# --- Règles par défaut -------------------------------------------------------------------------
# Familles dont l'effet repose sur un matériau ou une manipulation du tissu absents d'un DST.
FAMILY_E = {
    "bead": "perle cousue : matériau non géré par la machine à point noué",
    "beads": "perle cousue : matériau non géré par la machine à point noué",
    "button": "bouton cousu : opération de couture, pas de broderie",
    "ribbon": "ruban passé dans le tissu : impossible avec le fil de l'aiguille",
    "smocking": "fronces du tissu : manipulation textile hors du DST",
    "drawn": "fils retirés du tissu : opération manuelle préalable",
    "pulled": "jours tirés : l'effet vient de la tension qui écarte les fils du tissu",
    "hardanger": "broderie ajourée : découpe et barrettes tissées hors aiguille",
}
FAMILY_D = {
    "chain": "boucle du point de chaînette : le point noué ne forme pas de boucle ; simulation par motif",
    "feather": "boucle tenue par une attache : silhouette en V reproductible, boucle non",
    "buttonhole": "boucle du feston : la machine coud la silhouette, pas le nœud de boucle",
    "knot": "nœud enroulé : simulation par amas de micro-points",
    "knots": "nœud enroulé : simulation par amas de micro-points",
    "laced": "fil glissé sous les points sans traverser le tissu : simulation par recouvrement",
    "wheels": "fil tissé autour de rayons sans traverser le tissu : simulation radiale",
    "goldwork": "fil métallique couché : la machine ne pose pas de fil de chaîne séparé",
}
STRUCTURE_RULES = [  # (structure, classe, justification) appliquées dans cet ordre
    ("knotted", "D", "nœud enroulé : simulation par amas de micro-points"),
    ("looped", "D", "boucle non reproductible au point noué : simulation par motif"),
    ("woven", "D", "fil tissé/enroulé sans traverser le tissu : simulation par recouvrement"),
    ("couched", "C", "couchage : rangée longue puis points d'attache, primitive d'attache à créer"),
    ("radiating", "C", "points rayonnants vers un centre : primitive d'œillet/étoile à créer"),
    ("crossed", "C", "croisements réguliers : variante de grille à créer"),
    ("composite", "C", "point composé de plusieurs passes : recette multi-passes à créer"),
    ("straight", "C", "motif de points droits sur grille : tuile de motif à créer"),
]

# --- Exceptions fiche par fiche (classe, correspondance OpenStitch, justification) --------------
R = "RunningStitchParams"
OVERRIDES: dict[str, tuple[str, str, str]] = {
    # Contours couverts tels quels.
    "running-stitch": ("A", f"{R} style=Run", "point droit de contour"),
    "straight-stitch": ("A", f"{R} style=Run", "point lancé = segment de point droit"),
    "backstitch": ("A", f"{R} style=Backstitch", "style point arrière existant"),
    "stem-stitch": ("A", f"{R} style=Stem", "style point tige existant"),
    "outline-stitch": ("A", f"{R} style=Stem", "point de contour = point tige décalé de l'autre côté"),
    "holbein-stitch": ("A", f"{R} style=Run repeats=2", "double point droit aller-retour"),
    "satin-stitch": ("A", "AutoSatinParams (opération auto_satin)", "colonne satin ; pas une recette : conversion planifiée"),
    "slanted-satin-stitch": ("A", "SatinParams (barreaux inclinés)", "satin avec guides inclinés"),
    "cross-stitch": ("A", "CrossStitchParams full_cross=true", "croix complète sur grille"),
    "half-cross-stitch": ("A", "CrossStitchParams full_cross=false", "demi-croix existante"),
    "blanket-stitch": ("A", "BlanketStitchParams style=Blanket", "point de feston machine = silhouette en dents"),
    "buttonhole-stitch": ("A", "BlanketStitchParams style=Blanket spacing serré", "silhouette du feston, sans nœud de boucle"),
    "long-and-short-stitch": ("A", "DirectionalFillParams + DensityTexture::LongShort / GradientMethod::LongAndShort", "passé empiétant existant ; amélioration L2 (fondus multicouleurs)"),
    # Recettes de paramètres existants (B).
    "upright-cross": ("B", "CrossStitchParams grid_angle=45°", "croix droite = croix tournée de 45°"),
    "tent-stitch": ("B", "CrossStitchParams full_cross=false cell=2 mm", "petit point = demi-croix fine"),
    "continental-tent-stitch": ("B", "CrossStitchParams full_cross=false", "même aspect que la demi-croix"),
    "basketweave-tent-stitch": ("B", "CrossStitchParams full_cross=false", "même aspect ; ordre de couture différent sans effet visible"),
    "reversed-tent": ("B", "CrossStitchParams full_cross=false grid_angle=90°", "demi-croix inversée"),
    "split-stitch": ("B", f"{R} style=Backstitch stitch_length=1,5 mm", "approximation : points courts qui reviennent sur le précédent, sans fendre le fil"),
    "darning-stitch": ("B", "TatamiParams stitch_length long, row_spacing large", "reprisage = rangées droites espacées"),
    "japanese-darning-stitch": ("B", "TatamiParams rangées espacées, phase décalée", "rangées de reprisage décalées"),
    "double-darning-stitch": ("B", "TatamiParams rangées espacées", "reprisage double"),
    "closed-diagonal-darning": ("B", "TatamiParams angle=45°", "reprisage diagonal fermé"),
    "laid-work": ("B", "TatamiParams / SatinParams", "broderie posée = aplat de points longs parallèles"),
    "encroaching-satin-stitch": ("B", "DirectionalFillParams stagger élevé", "satin empiétant = rangées imbriquées"),
    "block-shading": ("B", "MulticolorGradientParams method=Directional", "bandes de couleurs franches"),
    "padded-satin-stitch": ("B", "SatinParams avec sous-couche contour + zigzag", "rembourrage = sous-couche"),
    "counted-satin-stitch": ("B", "SatinParams", "satin compté = satin à longueur régulière"),
    "turned-satin-stitch": ("B", "SatinParams avec guides tournants", "satin dont l'angle suit la forme"),
    "kloster-block": ("B", "SatinParams (bloc de 5 points)", "blocs de satin ; la découpe Hardanger est hors périmètre"),
    "satin-diamond": ("B", "SatinParams (losange)", "satin en losange"),
    "felt-padding": ("B", "SatinParams sous-couche épaisse", "rembourrage simulé par sous-couches"),
    "soft-string-padding": ("B", "SatinParams sous-couche centrale", "rembourrage simulé par sous-couche"),
    "tapered-trailing-stitch": ("B", "SatinParams colonne effilée + sous-couche centrale", "satin étroit sur âme"),
    "trailing-stitch": ("B", "SatinParams colonne étroite + sous-couche centrale", "satin étroit sur âme"),
    "zigzag-row": ("B", "MotifRunParams motif=Zigzag", "zigzag le long d'un tracé"),
    "zigzag-columns": ("B", "MotifFillParams motif=Zigzag", "zigzag en remplissage"),
    "diamond-lattice": ("B", "CrosshatchFillParams layers=2 angle=45°", "treillis de losanges"),
    "blackwork-diamond-trellis": ("B", "CrosshatchFillParams layers=2 angle=45°", "treillis de losanges"),
    "blackwork-small-diamond": ("B", "MotifFillParams motif=Diamond", "petit losange répété"),
    # Nouvelles primitives prioritaires (C).
    "trellis-filling-stitch": ("C", "CrosshatchFillParams + attaches aux intersections", "P1-D : treillis attaché"),
    "trellis-couching": ("C", "CrosshatchFillParams + attaches aux intersections", "treillis couché"),
    "jacobean-trellis-stitch": ("C", "CrosshatchFillParams angle=45° + attaches en croix", "treillis jacobéen (attache en croix au lieu du nœud)"),
    "bayeux-stitch": ("C", "TatamiParams couching_spacing / couching_tie_spacing", "P1-B : couche posée + fils de couchage attachés (primitive livrée)"),
    "brick-shading-stitch": ("C", "Tatami/directionnel à phase en briques + fondu", "P1-C : alternance de longueurs en quinconce"),
    "brick-stitch": ("B", "TatamiParams stagger=2", "décalage d'une demi-longueur par rangée = quinconce du tatami à stagger 2"),
    "seeding-stitch": ("C", "nouveau semis déterministe", "P2-A : semis à exclusion spatiale"),
    "dot-stitch": ("C", "semis déterministe (paires)", "semis de points doublés"),
    "granito-stitch": ("C", "semis déterministe (amas dans les mêmes trous)", "amas de points superposés"),
    "chevron-stitch": ("C", "MotifRunParams nouveau motif", "chevron = motif périodique"),
    "herringbone-stitch": ("C", "MotifRunParams nouveau motif croisé", "chevrons croisés le long d'un tracé"),
    "couching": ("C", "Run long + points d'attache", "couchage : le fil posé est cousu par le même fil"),
    "fishbone-stitch": ("C", "SatinParams + croisement central", "satin dont les points se croisent sur l'axe : variante de satin à créer"),
    "open-fishbone-stitch": ("C", "SatinParams + croisement central espacé", "variante ajourée du point d'épine"),
    "flat-stitch": ("C", "SatinParams + croisement central", "même structure que le point d'épine"),
    "leaf-stitch-surface": ("C", "SatinParams + croisement central", "feuille à nervure croisée"),
    # Simulations à documenter (D) / inadaptées (E) au-delà des règles.
    "pekinese-stitch": ("D", "", "fil lacé dans un point arrière sans traverser le tissu"),
    "whipped-backstitch": ("D", "", "fil enroulé autour d'un point arrière sans traverser le tissu"),
    "whipped-satin-stitch": ("D", "", "fil enroulé par-dessus le satin sans traverser le tissu"),
    "rope-stitch": ("D", "", "boucle torsadée sous chaque point"),
    "raised-fishbone-stitch": ("D", "", "relief obtenu par la tension, simulation par sous-couche"),
    "or-nue": ("D", "simulation de couchage + dégradé de densité", "P2-C : pas d'or nué physique (fil métallique couché)"),
    "sashiko-stitch": ("D", f"{R} style=Bean (simulation)", "les intervalles visibles du sashiko exigent des sauts"),
    "kantha-stitch": ("D", "TatamiParams rangées espacées (simulation)", "les intervalles visibles exigent des sauts"),
    "sashiko-jujizashi": ("D", "CrosshatchFillParams (simulation)", "croix espacées : intervalles visibles impossibles sans sauts"),
    "whip-stitch": ("E", "", "point de surjet sur la lisière"),
    "overcast-stitch": ("D", "SatinParams étroit (simulation)", "le surjet enroule le fil autour d'un fil posé"),
    "slip-stitch": ("E", "", "point invisible de couture"),
    "swedish-weaving": ("E", "", "fil glissé sous les flottés d'un tissu huckaback"),
    "underside-couching": ("E", "", "le fil métallique est tiré à l'envers"),
    "kid-leather-application": ("E", "", "application de cuir"),
    "spangles-with-purl": ("E", "", "paillettes et cannetille"),
    "cutwork-purls-over-string": ("E", "", "cannetille coupée"),
    "flat-cutwork": ("E", "", "cannetille coupée"),
    "pearl-purl": ("E", "", "cannetille perlée"),
    "chipping-goldwork": ("E", "", "brins de cannetille coupés"),
    "shisha-stitch-foundation": ("E", "", "miroir cousu"),
    "chain-stitch-drawn-thread": ("E", "", "jours tirés"),
    "velvet-stitch": ("E", "", "boucles coupées en velours"),
    "turkey-rug-knot": ("E", "", "nœud de tapis coupé"),
}

# Recettes pilotes retenues pour L1/L2 (rang de priorité ; ordre de livraison).
PILOTS = {
    "running-stitch": "L1", "backstitch": "L1", "stem-stitch": "L1", "holbein-stitch": "L1",
    "zigzag-row": "L1", "laid-work": "L1", "tent-stitch": "L1", "cross-stitch": "L1", "half-cross-stitch": "L1", "upright-cross": "L1",
    "blanket-stitch": "L1", "darning-stitch": "L1", "split-stitch": "L1",
    "long-and-short-stitch": "L1", "diamond-lattice": "L1",
    "trellis-filling-stitch": "L2", "trellis-couching": "L2", "brick-stitch": "L2", "bayeux-stitch": "L2", "brick-shading-stitch": "L2",
    "seeding-stitch": "L3", "or-nue": "L3",
}
PRIORITY = {"trellis-filling-stitch": "P1", "bayeux-stitch": "P1", "brick-shading-stitch": "P1",
            "long-and-short-stitch": "P1", "seeding-stitch": "P2", "holbein-stitch": "P2", "or-nue": "P2"}


def status(slug: str) -> str:
    lot = PILOTS.get(slug)
    if lot == "L1" or slug in ("trellis-filling-stitch", "trellis-couching", "bayeux-stitch",
                               "brick-stitch"):
        return "recette intégrée (openstitch-cli recipes)"
    return "planifié" if lot else "non planifié"


def fetch(url: str) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": "OpenStitch-research/1.0"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read().decode("utf-8")


TILE = re.compile(
    r'<li class="st-tile"([^>]*)><a class="st-card[^"]*" href="([^"]+)/"><canvas[^>]*'
    r'data-stitch="([^"]+)".*?data-n>([^<]*)<', re.S)


def tiles(page: str) -> dict[str, dict]:
    out = {}
    for m in TILE.finditer(page):
        attrs = dict(re.findall(r'(data-[a-z]+)="([^"]*)"', m.group(1)))
        out[m.group(2)] = {"attrs": attrs, "stitch": m.group(3), "name": html.unescape(m.group(4)).strip()}
    return out


def classify(slug: str, family: str, structure: list[str]) -> tuple[str, str, str]:
    if slug in OVERRIDES:
        return OVERRIDES[slug]
    if family in FAMILY_E:
        return "E", "", FAMILY_E[family]
    if family in FAMILY_D:
        return "D", "", FAMILY_D[family]
    for key, cls, why in STRUCTURE_RULES:
        if key in structure:
            return cls, "", why
    return "C", "", "à analyser"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fr", help="index FR déjà téléchargé")
    ap.add_argument("--en", help="index EN déjà téléchargé")
    ap.add_argument("--out", default=str(OUT))
    a = ap.parse_args()
    fr = tiles(pathlib.Path(a.fr).read_text(encoding="utf-8") if a.fr else fetch(INDEX_FR))
    en = tiles(pathlib.Path(a.en).read_text(encoding="utf-8") if a.en else fetch(INDEX_EN))
    if not fr:
        print("index FR vide : la structure de la page a changé", file=sys.stderr)
        return 1
    unknown = sorted(set(OVERRIDES) - set(fr))
    if unknown:
        print("exceptions sans fiche :", ", ".join(unknown), file=sys.stderr)
        return 1
    cols = ["slug", "name_fr", "name_en", "url_fr", "url_en", "crosstela_family", "technique",
            "uses", "structure", "level", "class", "openstitch_mapping", "justification",
            "priority", "pilot_lot", "status"]
    out = pathlib.Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(cols)
        for slug in sorted(fr):
            t = fr[slug]
            at = t["attrs"]
            family = t["stitch"].split(".")[0]
            structure = at.get("data-s", "").split()
            cls, mapping, why = classify(slug, family, structure)
            w.writerow([slug, t["name"], en.get(slug, {}).get("name", ""), INDEX_FR + slug + "/",
                        INDEX_EN + slug + "/", family, at.get("data-t", ""), at.get("data-u", ""),
                        " ".join(structure), at.get("data-l", ""), cls, mapping, why,
                        PRIORITY.get(slug, "P3" if cls in "DE" else "P2" if cls == "C" else "P2"),
                        PILOTS.get(slug, ""), status(slug)])
    print(f"{len(fr)} fiches -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
