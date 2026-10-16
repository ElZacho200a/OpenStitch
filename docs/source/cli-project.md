# Interface programmatique : projets `.osp` en ligne de commande

Public : intégrateur, script, agent IA. Ce chapitre décrit le groupe de
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
`--coherent-lines`, `--island-cleanup`, `--island-max-delta-e`, `--solid-block`
(`--solid-block-overlap`), `--dark-lines`
(`--dark-lines-width`, `--dark-lines-lightness`), `--stipple-*`, `--trim-threshold`, `--lock`, `--hidden-travel*`,
`--dpi`). En plus : `--dst f.dst` et `--output-svg f.svg` (exports secondaires),
`--report rapport.json` (rapport de numérisation, schéma
`openstitch.digitize-report/1`), `--no-clobber`.

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

### `project operations [--json]`

Catalogue des opérations (`openstitch.operations/1`) : nom, résumé, arguments
(type, unité, plage, obligatoire) et exemple valide. C'est la **même source** que
le dispatcher.

### `project check <projet.osp> [--svg f.svg] [--directions] [--only ID] [-o rapport.json]`

Contrôle qualité (`openstitch.quality/1`), code 2 si au moins une erreur.
Voir *Contrôle qualité*.

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
| Type et paramètres | `set_stitch_type` (running, tatami, directional), `set_params` (mise à jour partielle : angle, densité, espacement, longueur de point, retrait, sous-couches, compensation du tirage… par type), `discard_overrides` |
| Ordre | `set_order` (position, before, after, to_front, to_back), `optimize_order` (document, by_color, by_proximity, color_then_proximity, layered ; objets verrouillés immobiles) |
| Création / suppression | `add_shape` (rect, ellipse, polygone), `duplicate_object`, `remove_object` |
| Géométrie | `translate`, `rotate`, `scale`, `mirror`, `set_geometry`, `merge` (union, subtract, intersect, exclude), `split`, `break_apart`, `remove_holes` |
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
  interpolate_density}` ou `null`) ; satin `rail_a`, `rail_b` (polyligne), `rungs`
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
