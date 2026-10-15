#!/usr/bin/env bash
# Workflow complet image -> .osp -> DST -> contrôle -> rendu en vraie palette.
#
#   workflow.sh image.png sortie_dir LARGEUR_MM HAUTEUR_MM DPI [options de digitize...]
#
# Exemple (navire, 300 mm) :
#   workflow.sh navire.png $SCRATCH/v1 300 210 122.6 --max-colors 12 --dark-lines --island-cleanup 20
#
# Variables : CLI = chemin de openstitch-cli (défaut : openstitch-cli du PATH).
# Tout est écrit dans sortie_dir (à mettre dans le scratchpad, jamais dans le dépôt).
set -euo pipefail
img=$1; out=$2; w=$3; h=$4; dpi=$5; shift 5
CLI=${CLI:-openstitch-cli}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"

# 1. Numérisation en projet natif (+ DST secondaire). `project digitize` n'imprime ni points ni coupes.
"$CLI" project digitize "$img" -o "$out/p.osp" --dst "$out/out.dst" --dpi "$dpi" "$@" \
    > "$out/digitize.log" 2>&1 || { cat "$out/digitize.log"; exit 1; }
grep -E "Région candidate|Ignorer|Objets brodés" "$out/digitize.log" || true

# 2. Cadre : `project digitize` laisse 100 x 100 mm -> sans ceci `project check` donne des « hors-cadre ».
"$CLI" project set "$out/p.osp" --width-mm "$w" --height-mm "$h" > /dev/null

# 3. Mesures du DST (c'est la source des chiffres à comparer d'une variante à l'autre).
"$CLI" stats "$out/out.dst" | grep -E "Points  |Sauts|Coupes|Changements|Dimensions|Fil cousu"

# 4. Contrôle qualité : 1ère ligne = verdict ; le détail est plafonné à 100 avertissements.
"$CLI" project check "$out/p.osp" 2>&1 | head -1 || true

# 5. Vraie palette (ordre de couture) + rendu. Ne JAMAIS juger les couleurs d'un rendu fait avec --source.
"$CLI" project inspect "$out/p.osp" --json 2>/dev/null \
    | python3 -I "$here/osp_palette.py" "$out/pal.json" --report
python3 -I "$here/render_dst.py" "$out/out.dst" "$out/rendu.png" --palette "$out/pal.json" | tail -1
echo "Rendu approximatif (pas le rendu réaliste de l'application) : $out/rendu.png"
