# Interface programmatique : projets `.osp` en ligne de commande

Public : intégrateur, script, agent IA (pour un agent, préférer le serveur MCP : `mcp.md`). Ce chapitre décrit le groupe de
commandes `openstitch-cli project …` : numériser une image **directement en
projet `.osp` natif et éditable**, l'inspecter, le modifier objet par objet ou
par lots, contrôler sa qualité et l'exporter, sans interface graphique. Le DST
n'est qu'un export secondaire : **le `.osp` est la source de vérité**
(ADR-014 : les points ne sont jamais stockés, ils sont régénérés à la demande).

## Architecture

```
apps/cli (project_cmd.cpp)        analyse des arguments, formats de sortie, codes de sortie
        │
libs/project_ops  (Qt-free)       fabrique de projets · inspection JSON · opérations et lots
        │                          · contrôle qualité · service auto-satin · rapports
        ▼
commands, autodigitize, segmentation, auto_satin, stitch_generation,
stitch_analysis, optimization, project_io, formats   (moteur existant, inchangé)
```

Règles tenues par la CLI :

- **Aucun algorithme réimplémenté.** La numérisation appelle la même chaîne
  que `digitize` et le bureau (`segmentation` → `autodigitize`) ; l'auto-satin
  passe par la façade `auto_satin::generate_satin` (squelette, balayage parallèle ou axe médian vectoriel, au choix) avec
  ses refus nommés ; le contrôle qualité réutilise `stitch_analysis`.
- **Toute mutation du document passe par une `commands::ICommand`.** Les
  opérations sont des assemblages des commandes du bureau (`SetStitchParamsCommand`,
  `ConvertFillGroupCommand`, `make_transform_command`, `make_boolean_command`…).
- **Séquence de points** : toujours `effective_sequence` (retouches manuelles
  incluses), jamais `generate_sequence`.
- **Coordonnées en µm entiers** dans le modèle ; mm et degrés seulement aux
  frontières JSON/CLI (conversion exacte, `1 µm = 0,001 mm`).
- **Déterminisme** : mêmes entrées → mêmes octets (rapports JSON compris, aucun
  horodatage).
- **Sans Qt** : `libs/project_ops` et `apps/cli` se construisent avec le preset
  `linux-core`.
- **Pas d'état caché, pas d'interaction** : aucune commande ne pose de question ;
  tout est dans les arguments et les fichiers.

## Codes de sortie

| Code | Sens |
|---|---|
| 0 | succès |
| 1 | entrée, fichier ou erreur interne (fichier illisible, dossier de sortie absent, option incohérente, rien à produire) |
| 2 | contrôle qualité ou validation non satisfait (`project check`, `project validate`) |
| 3 | lot ou opération **refusé** : aucune modification écrite (`project apply`, `op`, `set`) |
| autres | erreur d'usage de la ligne de commande (CLI11 : option inconnue, valeur hors plage…) |

`stdout` / `stderr` : avec `--json`, `stdout` ne reçoit **qu'un seul document
JSON** (erreurs comprises, schéma `openstitch.error/1`) ; les messages lisibles
vont sur `stderr`. Sans `--json`, le texte lisible est sur `stdout` (états de
progression sur `stderr` pour les commandes qui produisent un JSON).

### Codes d'erreur stables

Chaque refus porte un `code` machine-lisible (enveloppe d'erreur ou rapport de lot).

| Code | Cas |
|---|---|
| `E_USAGE` | argument manquant, mal typé ou inconnu |
| `E_UNKNOWN_OP` | opération inconnue (voir `project operations`) |
| `E_UNKNOWN_OBJECT` | identifiant d'objet introuvable |
| `E_UNKNOWN_ALIAS` | alias `$x` utilisé avant sa définition |
| `E_OUT_OF_RANGE` | valeur hors plage métier (la plage est dans le message) |
| `E_WRONG_STITCH_TYPE` | champ qui n'existe pas pour le type de points de l'objet (la liste des champs valides est donnée) |
| `E_REFUSED` | refus métier du moteur (ex. auto-satin sans colonne constructible) |
| `E_SCHEMA` | JSON de lot illisible ou schéma inconnu |
| `E_IO` | fichier illisible ou non écrit, `--no-clobber` |
| `E_INVALID_PROJECT` | `.osp` illisible ou invalide |
| `E_NOTHING_TO_DO` | rien produit (aucun objet, aucun point) |
| `E_INTERNAL` | défaut interne |

## Commandes

Toutes acceptent `--json`. Les options de sortie `-o/--output` créent le fichier
**de façon atomique** (fichier temporaire puis renommage) ; `--no-clobber` refuse
d'écraser. Le dossier de sortie doit exister.

### `project new <sortie.osp>`

Projet vide : `--width-mm` / `--height-mm` (100), `--margin-mm` (0),
`--trim-threshold` (3 mm), `--lock none|backforth|triangle|zigzag`.

### `project digitize <image> -o <projet.osp>`

Numérise l'image (mêmes réglages et défauts que `digitize` : `--mode
shapes|contours|stipple`, `--max-colors`, `--min-region-px`, `--smoothing-px`,
`--skip-background auto|yes|no`, `--min-feature-mm`, `--min-region-mm2`,
`--curve-tolerance`, `--satin-bands`, `--detail`, `--technique`, `--single-path`,
`--coherent-lines`, `--pre` (traitements d'image avant segmentation : `median[:1|2]`,
`bilateral[:1..3]`, `flatten[:1..3]`, `clahe[:C]`, `sharpen[:A]`, `quantize[:N]`, `contrast:B:C`, `gray`, `crop:x:y:w:h`, `flip:h|v`, `rotate:1..3`,
enchaînés par `+` ; stockés dans `project.ops`, `original` reste la source ; les outils d'analyse
et `at --px` travaillent sur l'image de travail ; axe de balayage `pre=none,median:1,…`),
`--island-cleanup`, `--island-max-delta-e`, `--dark-lines`
(`--dark-lines-width`, `--dark-lines-lightness`, `--dark-lines-bars`, `--dark-lines-min-length`,
`--dark-lines-objects-only`), `--rings-satin` (avec `--dark-lines` : les anneaux sombres — hublots, ventouses, bulles, yeux — sont reconnus sur le masque des pixels sombres, cousus en auto-satin à contour exact, et retirés des traits et du remplissage marine), `--flow-fill` (`--flow-min-area`, `--flow-min-elongation` : les grandes formes allongées passent en remplissage directionnel guidé par leur axe médian ; la couleur la plus sombre reste en tatami), `--fill-inset`, `--fill-overlap`, `--solid-block` (`--solid-block-overlap` : bloc uni, voir *Auto-numérisation*), `--no-underpath`, `--stipple-*`,
`--trim-threshold`, `--lock`, `--hidden-travel*`, `--dpi`). En plus : `--debug-dir DOSSIER`
(masques et décisions, voir *Outils d'analyse*), `--dst f.dst` et `--output-svg f.svg` (exports secondaires),
`--report rapport.json` (rapport de numérisation, schéma
`openstitch.digitize-report/1`), `--no-clobber`.

`--settings profil.json` (HP-FILE-014, aussi pour `digitize`, `digitize-batch` et les
commandes de balayage) : profil de réglages, objet JSON dont les clés sont les noms longs des
options de numérisation sans `--`, par exemple
`{"max-colors": 6, "dark-lines": true, "no-underpath": true, "hidden-travel-levels": ["between","within"]}`
(drapeau = booléen, `false` = absent, liste de chaînes jointe par des virgules). Le profil est
traduit en options équivalentes avant l'analyse de la ligne : mêmes bornes, mêmes octets que
les options passées à la main ; une option donnée explicitement l'emporte sur le profil. Une
clé inconnue est refusée (code 1, `E_USAGE` avec `--json`) avec la liste des clés admises et la
plus proche suggérée. Le rapport de numérisation cite le profil (`settings.profile`), le
rapport de lot aussi (`settings_profile`).

Le `.osp` contient l'image source, la segmentation, les objets vectoriels
(contours éditables) et les objets de broderie dans l'ordre de couture. Rien
n'est écrit si aucun objet ou aucun point n'est produit (`E_NOTHING_TO_DO`).

### `project digitize-batch <images…> --out-dir <dossier>` *(initiative)*

Un `.osp` par image (`<nom>.osp`), mêmes options de numérisation. Rapport agrégé
`openstitch.digitize-batch/1` ; code 1 si au moins une image échoue (les autres
sont quand même produites).

### `project inspect <projet.osp> [--object ID] [--no-stitches] [--geometry]`

Sans `--json` : résumé lisible. Avec `--json` : document `openstitch.project-inspect/1`
(structure ci-dessous). `--object ID` : `openstitch.object-inspect/1` (un objet,
avec `--geometry` ses contours en mm). `--no-stitches` évite la génération des points.

### `project apply <projet.osp> <lot.json|-> [-o sortie.osp] [--dry-run] [--report r.json]`

Applique un lot (voir *Lots*). Sans `-o`, le fichier est modifié **en place**
(écriture atomique ; un fichier d'un schéma plus ancien est d'abord sauvegardé en
`<nom>.v<N>.osp.bak`, comme dans l'application). Un lot refusé ne laisse **aucune
trace** : code 3, fichier inchangé.

### `project op <projet.osp> <opération> --arg clé=valeur…`

Une seule opération, par **le même chemin** que `apply`. La valeur est lue comme
littéral JSON (nombre, booléen, tableau, objet) sinon comme chaîne :
`project op p.osp set_params --arg object=12 --arg 'params={"row_spacing_mm":0.45}'`.

### `project set <projet.osp> [--width-mm --height-mm --margin-mm --trim-threshold --lock]`

Cadre et finitions générales (sucre pour `set_canvas` / `set_finishing`).

Retours d'essais d'agents (branche `agent-trials`) :

- **`set_source_image`** : définit l'image source (`path` : PNG/JPEG/BMP/TIFF) et `mm_per_px`
  (optionnel ; défaut : l'image remplit le cadre `canvas` sans déformation) d'un projet bâti sans
  image (`import_svg`, `semantic_import`). Les objets et leurs identifiants ne changent pas ;
  l'opération est annulable. Elle rend `project fidelity` utilisable sur ces projets.
  `semantic_import` accepte aussi `source_path` (mêmes dimensions que le manifeste, échelle du
  manifeste) : même effet en une seule opération.
- **`project render`** : le JSON contient `fabric` (fond utilisé) ; sans `--fabric`, stderr rappelle
  que le défaut est `#00FF00`.
- **`set_params`** : une clé inconnue ou non applicable au type refuse avec la liste des clés
  valides ; si elle existe pour un autre type, le message nomme ce type et `set_stitch_type`.
- **`auto_satin` refusé** : le message se termine par ` [reason_code=<code>] Suggestion : <piste>`
  et `hint` porte la suggestion. Codes : `no_source_contour`, `self_intersecting_contour`,
  `contour_too_complex`, `branched_shape`, `compact_shape`, `has_holes`, `too_narrow`,
  `width_out_of_range`, `invalid_geometry`, `engine_no_column`. Les pistes ne citent que des
  opérations du registre (`split`, `break_apart`, `set_geometry`, `remove_holes`,
  `set_stitch_type`).

### `project operations [--json]`

Catalogue des opérations (`openstitch.operations/1`) : nom, résumé, arguments
(type, unité, plage, obligatoire) et exemple valide. C'est la **même source** que
le dispatcher.

### `project check <projet.osp> [--svg f.svg] [--directions] [--only ID] [-o rapport.json]`

Contrôle qualité (`openstitch.quality/1`), code 2 si au moins une erreur.
Voir *Contrôle qualité*.

### Outils d'analyse de la qualité

Cinq commandes mesurent un résultat **sur la séquence effective**, dans le repère exact de l'image
source (`x = (px + 0,5 − L/2) × mm/px`, Y vers le haut : aucun calage heuristique). Elles sont
déterministes et Qt-free (`libs/project_ops/src/analysis.cpp`, `sweep.cpp`). Chaque commande accepte
`--json` (schéma stable) ; sans lui, un résumé lisible. Elles regardent ce que le fil recouvre
réellement (fil de 0,55 mm), pas ce que les contours promettent : un trou vu à l'écran y est visible.

#### `project fidelity <projet.osp> [--map carte.png] [--thread-mm 0.55] [--hole-zone-mm 1.6] [-o rapport.json]`

Compare l'image source à la couverture des fils (`openstitch.fidelity/1`). Le fond ignoré est la plus
grande région dont la couleur n'est celle d'aucun fil. Mesures :

- **tissu visible à l'intérieur du motif** (`interior_holes`) : pixel à broder sans fil, à plus de
  `--hole-zone-mm` du fond extérieur ; liste des plus gros trous avec position en mm et en pixels source ;
- **sans fil** (`missing`) : idem, bords compris ;
- **détails perdus** (`lost_detail`) : un pixel proche (ΔE < 18) d'une couleur de fil mais cousu d'une
  autre (écart > 30), après ouverture 3x3 qui écarte le liseré anti-crénelé d'un pixel ;
- `--map` : carte d'erreurs PNG (rouge = trou intérieur, rose = manque de bord, jaune = détail perdu).

#### `project density <projet.osp> [--heatmap carte.png] [--cell-mm 1] [--hotspot-layers 4]`

Épaisseurs de fil par cellule (`openstitch.density/1`) : 1 épaisseur = la longueur de fil par mm² d'un
remplissage à 0,4 mm. Donne moyenne, p99, max, la surface à ≥ 2, 3 et 4 épaisseurs et les **zones
chaudes** (composantes de cellules au-dessus du seuil) avec leur position et les objets qui y
contribuent le plus. `--heatmap` : carte PNG (bleu = peu, vert = 1, jaune = 2, orange = 3, rouge ≥ 4).

#### `project at <projet.osp> (--mm X,Y | --px X,Y) [--radius-mm 0.4]`

Quel objet a cousu ce point ? (`openstitch.locate/1`) Objets dont un brin passe près du point, **du
dernier cousu (visible dessus) au premier**, avec type, couleur, passes, angle et espacement pour un
tatami. `--px` prend un pixel de l'image source (ce qu'on lit sur une capture), `--mm` le repère modèle.

#### `project diagnose <projet.osp>`

Détecteurs de défauts connus (`openstitch.diagnose/1`, code 2 si au moins une erreur), chacun avec
les objets en cause, la position et l'option conseillée :

| Code | Niveau | Détecte |
|---|---|---|
| `motif-hors-image` | erreur | points à plus de 3 mm de l'image source (géométrie aberrante, ex. poignées de Bézier non bornées) |
| `motif-hors-cadre` | avertissement | la boîte des points dépasse le cadre `project.canvas` (marge incluse) ; le message donne la boîte réelle et le cadre minimal à demander (`set_canvas`) |
| `objet-etale` | avertissement | objet de plus de 300 mm² à moins de 8 % de remplissage de sa boîte et d'au moins 45 % de la diagonale du motif (réseau de barres relié en un objet) |
| `fils-quasi-identiques` | info | deux fils à ΔE < 12 : un changement de fil pour rien |
| `tissu-visible` | info ≥ 0,15 %, avertissement > 0,8 % | tissu visible à l'intérieur du motif |
| `details-perdus` | avertissement > 1 % | couleurs absorbées par une voisine |
| `surepaisseur` | avertissement ≥ 20 mm² | zones à ≥ 4 épaisseurs de fil |
| `liaisons-visibles` | avertissement > 0,5 % | brins de remplissage s'écartant de plus de 25° de l'angle des rangées (liaisons posées par-dessus) |

Le rapport JSON contient aussi un bloc `metrics` (tissu visible %, détails perdus %, surépaisseur
mm², épaisseurs max, liaisons %, dimensions, points, sauts, coupes, fils) que `sweep` réutilise.

#### `project sweep <image> [options de digitize] --vary option=v1,v2 … [--sheet planche.png]`

Balayage de réglages de numérisation. Les options de `digitize` donnent la base ; chaque `--vary`
est un axe (nom de l'option sans `--`, ex. `island-cleanup=0,20`) ; le produit cartésien (64
variantes au plus) est numérisé et mesuré. Sortie : tableau comparatif (points, coupes, fils, tissu
visible, détails perdus, surépaisseur, liaisons, défauts) et colonne **non dominée** (aucune autre
variante n'est au moins aussi bonne sur coupes, tissu visible, détails perdus et surépaisseur et
meilleure sur l'une). `--sheet` : planche de vignettes réalistes (une par variante, ordre du
tableau), `--sheet-crop x0,y0,x1,y1` pour zoomer. `--markdown`, `--csv`, `--keep-dir` (chaque variante
en `.osp`), `--journal FICHIER --title TITRE` (**ajoute** le tableau à un journal). Une option ou une
valeur inconnue est refusée avant tout calcul, avec la liste des options valides. Exemple :

```bash
openstitch-cli project sweep navire.png --dpi 122.6 --max-colors 12 --dark-lines \
    --vary island-cleanup=0,20 --vary curve-tolerance=0,0.4 --vary no-underpath=0,1 \
    --sheet planche.png --sheet-crop 0.12,0.3,0.46,0.72 --journal docs/journal.md --title "Navire"
```

#### `digitize … --debug-dir DOSSIER`

Écrit dans le dossier : `regions.png` (segmentation), `traits.png` (décisions de la couche de traits
sur la source atténuée : **rouge** = trait cousu, **bleu** = barre, **vert** = séparation non cousue,
**orange** = fragment trop court), `couverture.png` (fils réellement posés, vert = tissu),
`fidelite.png` (carte d'erreurs) et `decisions.json` (seuil de clarté effectif, couleur de fil,
compteurs de pixels). Sans effet sur la sortie.

### `project select <projet.osp> --where clé=valeur…` *(initiative)*

Sélection d'objets de broderie (critères combinés en ET) : `id`, `color=#RRGGBB`,
`type=running|tatami|satin|directional|auto_satin`, `visible`, `locked`, `name`
(sous-chaîne), `area_gt`, `area_lt` (mm²), `within=x0,y0,x1,y1` (mm). Sortie : un
identifiant par ligne, ou `openstitch.selection/1`. Un critère inconnu est une
erreur, jamais ignoré.

### `project diff <a.osp> <b.osp>` *(initiative)*

Différences structurées (`openstitch.project-diff/1`) : cadre, finitions, séquence,
objets ajoutés / supprimés / modifiés champ par champ. Code 0 dans tous les cas
(le champ `identical` donne le verdict).

### `project validate <projet.osp>` *(initiative)*

Intégrité des références (identifiants uniques, sources vectorielles, régions,
partenaires de fondu, compteur d'identifiants) puis contrôle qualité. Code 2 si invalide.

### `commands [--json]` *(initiative)*

Liste machine-lisible de toutes les commandes et options de la CLI
(`openstitch.commands/1`, générée par introspection de CLI11 : toujours à jour).

### `project render <projet.osp> -o <rendu.png> [--px-per-mm N] [--fabric #RRGGBB] [--thread-mm MM] [--crop x0,y0,x1,y1]`

Rendu **réaliste** (fils texturés, ombres, torsion : le moteur `stitch_render` de l'aperçu de
l'application, sans Qt) en PNG, sur la séquence EFFECTIVE (objets masqués exclus). Le tissu est un
**aplat vert vif** par défaut : tout ce qui n'est pas brodé (trous, interstices entre zones, dentelure
des bouts de rangées) se voit, comme dans l'application sur fond vert. `--crop` donne une zone en
fractions de la boîte du motif (origine en haut à gauche), p. ex. `--crop 0.1,0.25,0.5,0.75
--px-per-mm 14` pour zoomer sur un détail. Déterministe (mêmes octets). Résolution abaissée si l'image
dépasserait 40 millions de pixels. Code : `project_ops::render_realistic_png`.
Mesure associée : `scripts/green_holes.py` du skill `openstitch-digitize` (part du vert intérieur).

### `project export <projet.osp> -o <sortie>`

Exporte en `dst`, `pes`, `jef`, `exp` ou `svg` (format déduit de l'extension, ou
`--format`). Options machine identiques à `osp2dst` : `--no-trims`,
`--no-color-changes`, `--stops native|as-color-change|drop`, `--start first|center|X,Y`, `--reduce-trims`
(`--reduce-trims-levels`, `--reduce-trims-same-colour`, `--reduce-trims-max`).
SVG : `--outlines` (contours des régions) et `--only ID`. Rapport
`openstitch.export/1` avec `--json`. L'export machine passe par le **même appel**
(`project_io::export_machine_file`) que `osp2dst` : les octets sont identiques (testé
par empreinte SHA-256). `osp2dst` et `osp2svg` restent disponibles, inchangés.
Les points sont régénérés depuis le `.osp` (`effective_sequence`) à chaque export :
modifier un objet puis exporter suffit, il n'y a rien à « régénérer » dans le fichier.

## Inspection JSON (`openstitch.project-inspect/1`)

Clés de premier niveau (ordre fixe) : `schema`, `osp_schema_version`, `canvas`,
`resolution`, `image`, `finishing`, `imported_design`, `segmentation` (`regions`),
`vector_objects`, `sequence`, `embroidery_objects`, `warnings`, `counts`.

Un objet de broderie (`embroidery_objects[]`) :

| Champ | Contenu |
|---|---|
| `id` | identifiant **stable** (persistant dans le `.osp`, jamais réutilisé) |
| `order` | rang 0-based dans l'ordre de couture |
| `source_vector` | id de l'objet vectoriel (contour éditable) |
| `type` | `running`, `tatami`, `satin`, `directional`, `auto_satin` |
| `color`, `thread` | `#RRGGBB` ; fil de nuancier `{chart, code}` ou `null` |
| `visible`, `locked`, `intent`, `join`, `connector` | états |
| `params` | paramètres de couture (mm, degrés, énumérations en chaîne) |
| `manual_overrides` | nombre de retouches manuelles de points |
| `region_area_mm2`, `geometry_bounds` | aire nette et boîte de la région source |
| `stitches` | `total`, `top`, `underlay`, `jumps`, `thread_mm`, `first_command_index`, `bounds` |

Les **noms de `params`** sont ceux que `set_params` accepte (un test vérifie que
tout ce qu'`inspect` publie est ré-acceptable) : `row_spacing_mm`, `angle_deg`,
`stitch_length_mm`, `inset_mm`, `underlay_edge`, `pull_compensation_mm`,
`underlay_mode` (`manual|auto`)… Les géométries (guides, rails) sont résumées par un compte.

## Lots (`openstitch.batch/1`)

```json
{
  "schema": "openstitch.batch/1",
  "operations": [
    {"op": "add_shape", "as": "bande", "shape": "rect", "width_mm": 30, "height_mm": 3},
    {"op": "auto_satin", "object": "$bande", "spacing_mm": 0.4},
    {"op": "set_color", "object": "$bande", "rgb": "#C81E1E"}
  ]
}
```

Garanties :

1. **Validation complète avant toute application** : JSON, schéma, nom
   d'opération, types d'arguments, arguments inconnus (avec la liste des valides),
   alias définis avant usage.
2. **Transaction** : les opérations sont jouées sur une **copie** du projet. À la
   première opération refusée, la copie est abandonnée, le projet et le fichier
   restent strictement inchangés, le rapport désigne l'opération fautive, les
   suivantes ne sont pas examinées.
3. **Écriture atomique** du `.osp` seulement après succès complet.
4. `--dry-run` : même validation et même simulation, rien n'est écrit.
5. **Alias** : `"as":"nom"` sur une opération qui crée un objet (`result.id`) ;
   les suivantes écrivent `"$nom"` à la place d'un identifiant.

Rapport (`openstitch.batch-report/1`) : `ok`, `applied`, `dry_run`,
`operation_count`, `error` (global : JSON/schéma) et `outcomes[]` (`index`, `op`,
`ok`, `result`, `error{code,message,hint}`).

### Catalogue des opérations

Un objet est désigné par `object` = **identifiant d'objet de broderie**.

| Famille | Opérations |
|---|---|
| Style | `set_color` (`rgb` ou `chart`+`code`), `rename`, `set_visible`, `set_locked`, `set_join`, `set_connector` |
| Type et paramètres | `set_stitch_type` (running, tatami, directional), `directional_guides` (convertit en directionnel et pose des guides suivant l'axe médian de la forme, comme « Générer un guide depuis la forme » du bureau ; sans lui un objet converti n'a qu'un guide droit), `set_params` (mise à jour partielle : angle, densité, espacement, longueur de point, retrait, sous-couches, compensation du tirage… par type), `discard_overrides` |
| Ordre | `set_order` (position, before, after, to_front, to_back), `optimize_order` (document, by_color, by_proximity, color_then_proximity, layered ; objets verrouillés immobiles) |
| Création / suppression | `add_shape` (rect, ellipse, polygone), `duplicate_object`, `remove_object` |
| Géométrie | `translate`, `rotate`, `scale`, `mirror`, `set_geometry`, `merge` (union, subtract, intersect, exclude), `split`, `break_apart`, `remove_holes`, `fill_block_holes` (comble les vides sans fil d'un bloc, détectés depuis les points cousus) |
| Import et texte | `import_svg` (path, rect, circle, ellipse, line, polyline, polygon), `add_text`, `set_text`, `remove_text` |
| Auto-satin | `auto_satin` (espacement, `compact_axis`, `strategy` = `skeleton` \| `parallel_sweep` \| `medial_axis`, `sweep_angle_deg`, guides) : vrai pipeline, refus nommé si aucune colonne ; `strategy` et `sweep_angle_deg` se règlent ensuite par `set_params` |
| Projet | `set_canvas`, `set_finishing` |

Détail exact des arguments : `openstitch-cli project operations` (texte) ou
`--json`. Particularités :

- `set_stitch_type` refuse `satin` et `auto_satin` : le satin passe par
  l'opération `auto_satin`, qui applique le même contour source, les mêmes
  critères et les mêmes diagnostics que l'application.
- `set_params` ne laisse **aucun champ silencieusement ignoré** : champ absent du
  type = `E_WRONG_STITCH_TYPE` ; hors plage = `E_OUT_OF_RANGE`. Un seul pas
  d'annulation par opération. Champs géométriques (coordonnées en mm, repère du
  modèle) : remplissage directionnel `guides` et `break_lines` (liste de polylignes
  `[[x,y],...]`), `density_gradient` (`{from_mm,to_mm,spacing_from_mm,spacing_to_mm,
  interpolate_density?,curve?}` avec `curve` = `linear` (défaut) / `smooth` / `steep`,
  ou `null`) ; satin `rail_a`, `rail_b` (polyligne), `rungs`
  (`[{a_mm,b_mm}]`) ; auto-satin `guides` (`[{anchor_mm,angle_deg,absolute?}]`).
  `inspect` publie ces champs sous les **mêmes noms et formes** : relire puis réécrire
  ne change rien, et un chemin dont les sommets sont inchangés garde ses tangentes de
  Bézier ; un chemin modifié devient une polyligne. Non modifiable par cette voie : la
  topologie de réseau satin et la bordure.
- `import_svg` : `file`, `stitch_type` (`auto` = classification tatami/contour de
  l'auto-numérisation, `tatami`, `running`), `rgb`, `name`. Mêmes formes et mêmes
  diagnostics que l'ouverture d'un SVG dans l'application : `<text>`, dégradés,
  filtres… sont **signalés dans `result.warnings`**, jamais ignorés en silence ; les
  couleurs du SVG ne sont pas reprises (une couleur de fil unique).
- `add_text` / `set_text` / `remove_text` : lettrage de l'application (un objet texte +
  une lettre = un objet vectoriel et un objet de broderie). `font_file` (TTF/OTF) est
  **obligatoire** : les polices intégrées de l'application sont des ressources Qt,
  inaccessibles à la CLI sans Qt. `result.id` = identifiant de l'objet **texte**
  (`text_objects[]` de l'inspection) ; `set_text` désigne `text_object` et repart de la
  position visible des lettres si `origin_mm` est absent. Avertissements du lettrage
  (texte trop petit, glyphe absent…) dans `result.warnings`.
- `remove_object` conserve la forme vectorielle source (comme le bureau) sauf
  `with_source=true` et si plus aucun objet ne l'utilise.
- `set_geometry` remplace **toute** la géométrie du vecteur ; `dropped_pieces` /
  `dropped_holes` dans le résultat disent ce qui disparaît.
- Les identifiants créés par `merge`, `split`, `duplicate_object`… sont tirés du
  compteur du document : jamais réutilisés.

## Contrôle qualité (`openstitch.quality/1`)

Séquence **effective**. Aucune mesure inventée : chaque chiffre vient de
`stitch_analysis` (`analyze_detailed` avec les limites du projet et le cadre,
`sequence_metrics`, `project_metrics`, `check_stitch_limits`).

Contenu : `summary` (`errors`, `warnings`, `passed`), `sequence` (points, sauts,
coupes, déplacements longs sans coupe, points courts, histogramme des directions),
`frame` (cadre brodable, `fits`), `findings` (gravité, catégorie, position en mm,
objet, piste), `limits` (les seuils utilisés et leur source), `project_metrics`
(surface non couverte globale, objets trop petits, angles de remplissage),
`objects[]` (voir ci-dessous) et `fill_params_checks`.

**Comparer régions d'origine et points** : `stitch_analysis::object_coverage`
(nouvelle métrique du moteur) donne, par objet, `covered_ratio` (part de la région
vectorielle source balayée par les points visibles de l'objet : passes `TopStitch`
et `Manual`) et `overflow_ratio` (part de la surface cousue hors région, demi-trace
de 0,25 mm comprise). Un objet en contour (`running`) n'a pas de surface à couvrir :
`measured=false` avec la raison. Le moteur n'a pas de seuil documenté de « couverture
insuffisante » : la valeur est exposée **sans verdict**. `--svg` produit les points,
les contours d'origine (gris), les directions de rangées (`--directions`) et des
marqueurs de constats.

## Exemple complet : numérisation automatisée

```powershell
# 1. Image -> projet natif + rapport (le DST est un export secondaire)
openstitch-cli project digitize logo.png -o logo.osp --report logo-report.json --dst logo.dst

# 2. Inspection : identifiants, types, paramètres, ordre de couture
openstitch-cli project inspect logo.osp --json > logo-inspect.json

# 3. Sélection puis lot de corrections, validé à blanc d'abord
openstitch-cli project select logo.osp --where type=tatami --where area_gt=50
openstitch-cli project apply logo.osp corrections.json --dry-run --json
openstitch-cli project apply logo.osp corrections.json -o logo-v2.osp --json

# 4. Contrôle qualité (code 2 s'il y a des erreurs) et aperçu
openstitch-cli project check logo-v2.osp --svg logo-v2.svg --directions --json

# 5. Export machine ; le .osp reste ouvrable et éditable dans l'application
openstitch-cli osp2dst logo-v2.osp logo-v2.dst
```

## Orchestration par un agent IA (pour un futur Skill)

Boucle recommandée, chaque étape étant non interactive et reproductible :

1. `project digitize … --report` puis lire `objects.ids_by_type`, `quality`, `warnings`.
2. `project inspect --json` : base de décision (types, paramètres, aire, nombre de points).
3. `project check --json` : `findings` et `objects[].covered_ratio` désignent *quoi*
   corriger ; `project check --svg --directions` donne le support visuel.
4. Construire un lot ciblé (jamais d'identifiant deviné : toujours lus dans
   l'inspection ou créés par alias), `--dry-run`, lire les `outcomes[].error.code`,
   corriger, puis appliquer. Un code 3 n'a rien modifié : on peut recommencer.
5. `project diff` entre versions pour vérifier l'effet d'un lot ; `project validate`
   avant livraison ; `osp2dst` en dernier.
6. Découvrir l'interface par `commands --json` et `project operations --json` plutôt
   que de la coder en dur.

Limite à garder en tête : des tests verts et un contrôle qualité sans erreur ne
prouvent pas la qualité d'une broderie sur machine réelle (tension, tissu, fil) ;
un essai physique reste nécessaire.

## Conventions de stabilité

- Schémas `openstitch.<nom>/<majeure>` : on **ajoute** des champs sans changer la
  majeure ; les lecteurs ignorent les champs inconnus ; un renommage ou une
  suppression exige une nouvelle majeure.
- Codes d'erreur (`E_…`) et codes de sortie : jamais renommés ni réutilisés.
- Noms d'opérations et d'arguments du catalogue : stables ; nouveautés ajoutées.
- Identifiants d'objets : stables d'un fichier à l'autre, jamais réutilisés.
- Les anciennes commandes (`digitize`, `osp2dst`, `osp2svg`, `stats`…) sont inchangées ;
  `digitize` partage désormais la même fabrique que `project digitize`.

## Limites connues

- Guides, ruptures et rails s'éditent en polylignes : pas de poignées de Bézier en
  ligne de commande (les courbes existantes sont conservées tant que leurs sommets ne
  changent pas).
- Lettrage : police fournie par fichier ; pas de texte sur courbe ni de justification
  (mêmes limites que l'application, voir lettering.md).
- Import SVG : couleurs non reprises, formes de `<g>` aplaties, pas de détection de
  trous entre éléments distincts (limites de `formats::decode_svg`).
- La couverture par objet est mesurée sur une grille de 0,25 mm : indicative
  au-dessous de ce pas.
- `merge`/`split`/`duplicate_object` ne rembobinent pas le compteur d'identifiants
  à l'annulation (comportement existant des commandes).
