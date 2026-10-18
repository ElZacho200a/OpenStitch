#!/usr/bin/env bash
# Workflow complet image -> .osp -> DST -> contrôle -> diagnostic -> fidélité -> densité -> rendu réaliste.
#
#   workflow.sh image.png sortie_dir LARGEUR_MM HAUTEUR_MM DPI [options de digitize...]
#
# Exemple (navire, 300 mm) :
#   workflow.sh navire.png $SCRATCH/v1 300 210 122.6 --max-colors 12 --dark-lines \
#       --dark-lines-bars 4 --dark-lines-min-length 6 --min-region-mm2 1 --fill-inset 0 \
#       --satin-bands --curve-tolerance 0.4 --no-underpath --hidden-travel
#
# Variables : CLI = chemin de openstitch-cli (défaut : openstitch-cli du PATH).
# Tout est écrit dans sortie_dir (à mettre dans le scratchpad, jamais dans le dépôt) :
#   p.osp, out.dst, debug/ (masques et décisions), realiste.png, fidelite.png, densite.png.
set -euo pipefail

img=$1; out=$2; w=$3; h=$4; dpi=$5; shift 5
CLI=${CLI:-openstitch-cli}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"

# 1. Numérisation en projet natif (+ DST secondaire + masques de décision).
"$CLI" project digitize "$img" -o "$out/p.osp" --dst "$out/out.dst" --dpi "$dpi" \
    --debug-dir "$out/debug" "$@" > "$out/digitize.log" 2>&1 || { cat "$out/digitize.log"; exit 1; }
grep -E "Région candidate|Ignorer|Objets brodés|Traits sombres" "$out/digitize.log" || true

# 2. Cadre : `project digitize` laisse 100 x 100 mm -> sans ceci `project check` donne des « hors-cadre ».
"$CLI" project set "$out/p.osp" --width-mm "$w" --height-mm "$h" > /dev/null

# 3. Mesures du DST : la source des chiffres à comparer d'une variante à l'autre.
"$CLI" stats "$out/out.dst" | grep -E "Points  |Sauts|Coupes|Changements|Dimensions|Fil cousu"

# 4. Contrôle qualité du moteur (1ère ligne = verdict) puis détecteurs de défauts connus.
"$CLI" project check "$out/p.osp" 2>&1 | head -1 || true
"$CLI" project diagnose "$out/p.osp" || true

# 5. Fidélité à la source (tissu visible, détails perdus) et épaisseurs de fil.
"$CLI" project fidelity "$out/p.osp" --map "$out/fidelite.png" | head -6
"$CLI" project density "$out/p.osp" --heatmap "$out/densite.png" | head -4

# 6. Palette réelle dans l'ordre de couture.
"$CLI" project inspect "$out/p.osp" --json 2>/dev/null \
    | python3 -I "$here/osp_palette.py" "$out/pal.json" --report

# 7. Rendu RÉALISTE (même moteur que l'application), fond vert : tout le tissu visible se voit.
"$CLI" project render "$out/p.osp" -o "$out/realiste.png" --px-per-mm 5 | tail -1
echo "Zoom : $CLI project render $out/p.osp -o zoom.png --px-per-mm 14 --crop 0.1,0.25,0.5,0.75"
echo "Masques : $out/debug/traits.png (rouge = trait, bleu = barre, vert = séparation, orange = court)"
