# Ligne de commande (openstitch-cli)

Public : utilisateur avancé, développeur, intégrateur. `openstitch-cli` est le
même moteur que l'application, sans interface graphique et **sans Qt** : il sert
à numériser une image en lot, à relire un DST, à comparer le résultat à une
référence externe et à diagnostiquer le moteur de points. Il est construit avec
le reste du dépôt (voir *Installation* ; exécutable
`build\msvc\apps\cli\Release\openstitch-cli.exe`) et existe aussi sous Linux.

```
openstitch-cli [--json] [--version] <sous-commande> [options]
openstitch-cli <sous-commande> --help
```

## Sous-commandes

| Groupe | Sous-commande | Rôle |
|---|---|---|
| Commandes | `info` | Métadonnées d'une image (dimensions, canaux, dpi, taille estimée) |
| Commandes | `stats` | Statistiques et mesures de qualité d'un fichier DST |
| Commandes | `digitize` | Pipeline complet image → DST (segmentation, numérisation automatique, points) |
| Commandes | `production` | Fiche de production d'un `.osp` ou d'un DST (texte, JSON, HTML) |
| Commandes | `dst2svg` | Aperçu SVG d'un DST |
| Commandes | `osp2dst` | Exporte un projet `.osp` en DST par le même chemin que le bureau |
| Diagnostic | `osp2svg` | `[diagnostic]` Séquence effective d'un `.osp` en SVG (contours des vecteurs, un seul objet) |
| Diagnostic | `stitchdebug` | `[diagnostic]` Moteur de points sur une forme de référence |
| Diagnostic | `satin-auto-debug` | `[diagnostic]` Auto-satin par squelette sur une forme de référence ou un vecteur d'un `.osp` |

Les sous-commandes **`[diagnostic]`** inspectent le moteur : leur sortie texte
n'est **pas un contrat stable** (elle peut changer d'une version à l'autre).
`info`, `stats`, `digitize` (avec `--json`), `dst2svg` et `osp2dst` sont les
interfaces destinées à être scriptées.

## Options communes

| Option | Effet |
|---|---|
| `--json` | Pour `info`, `stats`, `digitize` : un **unique document JSON sur la sortie standard**. Les messages d'état et le détail lisible passent alors sur la sortie d'erreur. Acceptée avant ou après la sous-commande. |
| `--no-clobber` | `digitize`, `dst2svg`, `osp2dst`, `osp2svg` : refuse d'écraser un fichier de sortie existant. |
| `--force` | Écrase explicitement (**comportement par défaut**) ; incompatible avec `--no-clobber`. |
| `--version`, `--help` | Version ; aide (la racine affiche aussi le pipeline d'exemple et les codes de sortie). |

Avant tout calcul coûteux, la commande vérifie que le **dossier de sortie
existe** (elle ne le crée pas) et, avec `--no-clobber`, que le fichier n'existe pas.

## info

```
openstitch-cli info <image> [--dpi N] [--json]
```

Affiche format, dimensions, canaux, canal alpha et la **taille estimée en mm**.
La résolution est lue dans le fichier quand il la déclare (PNG `pHYs`, JPEG
JFIF) et la sortie dit d'où elle vient : « lue dans le fichier », « option
--dpi » ou « défaut (aucune résolution dans le fichier) » (96 dpi). `--dpi`
l'emporte toujours.

JSON : `file`, `format`, `width_px`, `height_px`, `channels`, `has_alpha`, `dpi`,
`dpi_source`, `width_mm`, `height_mm`.

## stats

```
openstitch-cli stats <fichier.dst> [--json]
```

Points, sauts, coupes, changements de fil, dimensions, fil estimé, puis les
mesures de qualité de la séquence (déplacements longs sans coupe, points
courts hors points d'arrêt reconnus par leur forme, directions dominantes).
JSON : `stitches`, `jumps`, `trims`, `color_changes`, `width_mm`, `height_mm`,
`thread_m`, `moves`, `long_moves_without_trim`, `short_stitches`,
`short_lock_stitches` (le détail lisible reste sur la sortie d'erreur).

## production

```
openstitch-cli production <fichier.osp|fichier.dst> [--json] [-o fiche.html]
    [--name NOM] [--date AAAA-MM-JJ] [--notes TEXTE] [--speed PTS_PAR_MIN] [--no-clobber]
```

Fiche de production ([détail](production-sheet.md)) : dimensions, points, sauts, coupes,
changements de fil, fil et temps estimés, blocs de couleur dans l'ordre de couture,
avertissements d'analyse. Un `.osp` passe par la séquence effective (et son cadre) ; un DST
n'a pas de cadre. Sans option : texte lisible. `--json` : JSON stable sur stdout (`schema`,
`project`, `date`, `notes`, `size_mm`, `frame_mm`, `fits_frame`, `totals`, `estimate`,
`blocks[]`, `findings[]`, `suppressed`). `-o` écrit une page HTML autonome (aperçu SVG
intégré) ; le PDF, lui, s'exporte depuis le bureau (le CLI reste sans Qt). `--speed` règle
l'hypothèse de vitesse (défaut 700).

## digitize

```
openstitch-cli digitize <image> <sortie.dst> [options]
```

Mêmes valeurs par défaut que le dialogue **Numérisation automatique** du bureau.

| Option | Défaut | Effet |
|---|---|---|
| `--dpi N` | 96 | Résolution supposée de l'image (échelle mm/px). Ne lit **pas** le dpi du fichier : le préciser pour une taille réelle exacte. |
| `--max-colors N` | 8 | Couleurs maximales (2 à 64) |
| `--min-region-px N` | 16 | Taille minimale d'une région |
| `--smoothing-px N` | 3 | Lissage des formes |
| `--skip-background` | `auto` | `auto`, `yes` ou `no` : ignorer la plus grande région (le fond). `auto` : seulement si c'est un fond quasi blanc qui encadre le motif. Les anciennes valeurs `-1`, `0`, `1` restent acceptées. |
| `--trim-threshold MM` | 3 | Coupe au-delà de ce déplacement |
| `--lock` | `backforth` | Point d'arrêt : `none`, `backforth`, `triangle` ou `zigzag` |
| `--mode` | `shapes` | `shapes` : formes pleines ; `contours` : dessin au trait (lignes médianes en point droit) |
| `--detail 0..1` | 0.5 | Mode contours : niveau de détail |
| `--technique` | `auto` | Mode contours : `auto`, `running` ou `legacy-satin`. `legacy-satin` : ancien satin dégradé en point droit (**pas** l'auto-satin) ; l'ancien nom `satin` est accepté avec un avertissement. |
| `--output-svg FICHIER` | — | SVG de diagnostic en plus du DST |
| `--json`, `--no-clobber`, `--force` | — | voir *Options communes* |

La commande **échoue sans écrire de DST** (code 1, avec une piste de correction)
quand elle ne produit rien : aucune région exploitable, aucun objet de broderie
ou aucun point généré. Sinon elle affiche régions, objets (satin / tatami /
running), points, sauts, coupes, dimensions et mesures de qualité (surface non
couverte, angles de remplissage, déplacements et points courts par source).

JSON : `image`, `dst`, `svg`, `image_width_px`, `image_height_px`, `regions`,
`background_skipped`, `objects` (`total`, `satin`, `tatami`, `running`),
`stitches`, `jumps`, `trims`, `color_changes`, `width_mm`, `height_mm`,
`thread_m`, `moves`, `long_moves_without_trim`, `short_stitches`,
`small_objects`, `uncovered_ratio` (nul si non mesuré).

Note : `digitize` n'invoque **pas** l'auto-satin par squelette ; les zones
remplissables deviennent des tatamis. L'auto-satin se demande région par région
dans l'application (voir *Guide utilisateur détaillé*).

## dst2svg et osp2dst

```
openstitch-cli dst2svg <entree.dst> <sortie.svg>
openstitch-cli osp2dst <projet.osp> <sortie.dst>
```

`osp2dst` passe par la fonction d'export du bureau : on y voit exactement les
coupes et la fin de fichier que la machine recevra (voir *Format DST*). Pour
`dst2svg`, la sortie peut aussi s'écrire `--output-svg`/`--output`.

## Sous-commandes de diagnostic

### osp2svg

```
openstitch-cli osp2svg <projet.osp> <sortie.svg> [--outlines] [--only ID]
```

Séquence effective du projet (celle de l'aperçu, de l'export et de l'analyse).
`--outlines` superpose en gris le contour des vecteurs sources (les points qui
débordent se voient d'un coup d'œil) ; `--only ID` limite à un objet brodé (un id
inconnu liste les ids disponibles). Anciennes formes `--osp FICHIER --output
FICHIER` (ou `--output-svg`) toujours acceptées.

### stitchdebug

```
openstitch-cli stitchdebug [--shape line|corner|circle|bezier|star|ring]
                           [--length MM] [--repeats 1|2|3] [--output-svg F]
                           [--underlay MASQUE] [--underpath]
```

Point droit sur une forme de référence ; `--repeats` : 1 simple, 2 aller-retour,
3 triple. `--shape ring` génère un anneau tatami (avec `--underlay` : 1 contour,
2 parallèle ; `--underpath` : liaisons cousues cachées) et **compte les coutures
qui traversent le trou** : il sort avec le code **2** si ce nombre n'est pas 0.

### satin-auto-debug

```
openstitch-cli satin-auto-debug [--shape NOM | --osp F.osp --vector ID]
                                [--spacing MM] [--guide x,y,angle[,1]]
                                [--pristine] [--output-svg F] [--list-shapes]
```

Auto-satin par squelette et traversées orientées : colonnes, longueurs de
traversées, diagnostics (garde de rayon, guides orphelins, couverture estimée)
et SVG optionnel. `--list-shapes` affiche les noms de formes valides ; une forme
inconnue produit la même liste dans le message d'erreur. `--vector` et
`--pristine` exigent `--osp` ; `--osp` sans `--vector` liste les ids de vecteurs
du projet. Le premier bloc de la sortie rappelle la source (« forme X » ou
« vecteur N de fichier.osp »).

## Codes de sortie et erreurs

| Code | Sens |
|---|---|
| 0 | Succès |
| 1 | Entrée ou fichier invalide / illisible, dossier de sortie absent, sortie existante avec `--no-clobber`, rien à produire, option incohérente (forme, id de vecteur…) |
| 2 | Contrôle qualité non satisfait (`stitchdebug --shape ring` : des coutures traversent le trou) |
| 105 à 109 | Erreur d'usage détectée par l'analyseur d'arguments : option inconnue, valeur hors plage (`--repeats 5`), option requise absente, `--vector` sans `--osp`… (codes de CLI11, sans préfixe `openstitch-cli`) |

Les erreurs de code 1 ont toutes la forme
`openstitch-cli <sous-commande> : message`, suivie d'une ligne `Piste :` qui
indique quoi vérifier (formats d'image acceptés, extension attendue, ids
disponibles…). Elles vont sur la **sortie d'erreur**, jamais dans le JSON.

## Deux pipelines types

**1. Image vers DST, avec contrôle** :

```powershell
openstitch-cli info logo.png                    # taille estimée, dpi du fichier
openstitch-cli digitize logo.png logo.dst --dpi 300 --output-svg logo.svg
openstitch-cli stats logo.dst                   # relire le DST produit
openstitch-cli dst2svg logo.dst apercu.svg      # vérifier visuellement
```

**2. Traitement en lot avec JSON** (sortie standard = JSON seul) :

```powershell
Get-ChildItem *.png | ForEach-Object {
    $r = openstitch-cli digitize $_.FullName "out\$($_.BaseName).dst" --json | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { "ECHEC $($_.Name)"; return }
    "{0} : {1} points, {2:N1} x {3:N1} mm" -f $_.Name, $r.stitches, $r.width_mm, $r.height_mm
}
```

Les messages d'état de `digitize` vont sur la sortie d'erreur ; en cas d'échec le
code de sortie est non nul et aucun JSON n'est écrit.

## Implémentation associée

- `apps/cli/main.cpp` — sous-commandes, options, codes de sortie, JSON.
- `tests/cli/check_cli.cmake`, `tests/cli/CMakeLists.txt` — tests de contrat
  (`ctest -R cli_`), image de test `tests/fixtures/cli/`.
- `scripts/acceptance-sample.ps1` — recette sur l'image d'exemple
  (`digitize` + `stats`).
