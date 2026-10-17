# Analyse et validation

Public : utilisateur avancé, développeur. État : **Implémenté** (règles de base).

## But

Détecter, avant l'export, les problèmes courants de broderie sans jamais
appliquer de correction automatique silencieuse.

## Règles détectées

`analyze(sequence, options)` renvoie une liste de `Finding` triés par gravité :

| Catégorie | Condition (par défaut) | Gravité |
|---|---|---|
| `vide` | aucun point | Erreur |
| `point-court` | segment cousu < 0,5 mm, hors points d'arrêt (passe `Lock`) | Avertissement |
| `point-long` | segment cousu > 7 mm | Avertissement |
| `saut-long` | déplacement > 30 mm | Avertissement |
| `saut-sans-coupe` | déplacement > `trim_threshold` (3 mm) sans `Trim` | Avertissement |
| `hors-cadre` | point hors du cadre (si fourni) | Erreur |
| `trop-de-points` | > 100 000 points | Avertissement |
| `couches-superposees` | épaisseur de fil > `max_layer_thickness` (3,0 couches de remplissage dense) sur un bloc d'au moins 2 × 2 cases de 2,5 mm | Avertissement |

**Épaisseur de fil** (`couches-superposees`). Grille de `layer_cell` (2,5 mm) ;
dans chaque case, épaisseur = longueur de fil × `thread_width` (0,4 mm) /
surface de la case, en « couches de remplissage dense » : un remplissage dont
l'écart entre rangées égale la largeur du fil vaut 1, une sous-couche espacée de
2 mm vaut environ 0,2. Deux remplissages superposés avec leurs sous-couches
font ~2,4 (les bords ~2,6), trois ~3,6 ; le seuil de 3,0 les départage. Les
passes `Lock` et `Travel` ne comptent pas. Seule une zone contenant un bloc de
2 × 2 cases en excès est signalée : le bord d'un objet, ses connexions de
rangées et deux objets voisins ou empiétant d'1 mm (pratique courante contre
les interstices) ne déclenchent rien. Un seul résultat par zone, localisé sur
sa première case. Motivation : Liu et al. (CGF 2023) ne superposent que deux
couches pour limiter la déformation du tissu. `max_layer_thickness = 0`
désactive la règle. Les seuils sont des heuristiques d'ingénierie, pas des
valeurs de l'article.

Chaque problème porte une **gravité** (`Info`/`Warning`/`Error`), un **message**,
une **localisation** et l'**objet** concerné. Un plafond par catégorie évite
l'inondation ; le résultat est **déterministe**.

### Analyse unifiée : `analyze_project`

`analyze_project(project, sequence)` est le **point d'entrée unique** du panneau
*Analyse*, du pré-export, du dump de débogage et de `project quality` (CLI). Il
applique les seuils du projet (`options_from_project`, finitions comprises) et le
cadre brodable `canvas_hoop` (canevas moins la marge), puis enchaîne : règles de
séquence (`analyze_detailed`), règles de lettrage (`analyze_text_objects`),
réglages hors limites (`check_stitch_limits`). Avant ce changement, le panneau,
l'export et le CLI n'exécutaient pas les mêmes règles (UX-10).

`hors-cadre` est **regroupé par objet** (un constat par objet, ordre d'identifiant,
plafond par catégorie conservé) ; les points sans objet donnent un constat global.

### Corrections proposées : `Finding::fix`

Un constat peut porter un `Fix` : donnée pure (type, valeur, objet), jamais
appliquée par le moteur. Le bureau la traduit en **une** commande annulable
(`ICommand`) derrière le bouton *Corriger* du panneau.

| Constat | `FixKind` | Effet |
|---|---|---|
| `hors-cadre` | `MoveIntoHoop` | translation minimale (µm entiers) qui fait rentrer la boîte englobante de l'objet dans le cadre ; aucune correction si la boîte dépasse le cadre sur un axe |
| `texte-trop-petit` | `EnlargeText` | hauteur de capitale portée à 5 mm |
| `parametre-hors-limites` | `ClampStitchLength` | longueur ramenée dans la plage du projet |
| `point-long` | `SplitLongStitches` | `finishing.split_long_stitches = true`, proposé seulement si cela résout le constat |
| `saut-sans-coupe` | `EnableFinishing` | `finishing.enabled = true`, proposé seulement si les finitions sont désactivées **et** si leur simulation supprime le constat |

Il n'existe pas d'interrupteur « coupes seules » : activer les finitions ajoute
aussi les points d'arrêt, les coupes avant changement de fil et le filtre des
points courts ; le libellé du bouton le dit. `trait-trop-fin`, `saut-long`,
`couches-superposees`, `point-court`, `trop-de-points` et `vide` n'ont pas de
correction automatique (pistes textuelles seulement). `fix` n'est jamais
sérialisé dans le JSON du CLI.

## Mesures de qualité (Lot G, audit marine plein cadre)

En plus des règles, deux fonctions **mesurent** une séquence
(`openstitch-cli stats` et `digitize` les impriment) :

- `sequence_metrics(sequence, options)` (`metrics.hpp`), calculable sur un DST
  relu : nombre de points, de déplacements (une suite de sauts = un
  déplacement) et de coupes ; **déplacements plus longs que `trim_threshold`
  sans coupe** ; **points de moins de 0,5 mm hors points d'arrêt**, les points
  d'arrêt courts étant comptés à part. Sur un DST relu, les passes sont
  perdues : avec `infer_locks`, un point qui revient exactement sur la
  position d'il y a deux piqûres (aller-retour) est reconnu comme point
  d'arrêt. S'y ajoute l'**histogramme des directions** des points d'au moins
  1 mm, modulo 180°, par tranches de 5°. Une piqûre de longueur nulle à
  l'arrivée d'un saut compte comme point court : c'est un enregistrement de
  0 mm dans le fichier machine. `length_tolerance` évite de compter comme
  « long » un déplacement que la quantification DST (pas de 0,1 mm, erreur
  jusqu'à ~0,07 mm) a fait passer d'un cheveu au-dessus du seuil ;
  `openstitch-cli stats` relit un DST avec une tolérance d'une unité DST
  (0,1 mm).
- `project_metrics(project, sequence, options)` (`project_metrics.hpp`), qui
  a besoin du projet : **objets brodés de moins de 3 mm²** (objet vectoriel
  suivi, compté une fois), **angles de remplissage** des tatami, **part de
  l'image non couverte** par les points et une ventilation des déplacements et
  des points courts par type d'objet et passe.

  La couverture se calcule sur les pixels de la segmentation, hors fond
  transparent et hors couleur du fond ignoré (`excluded_rgb`). Un pixel est
  couvert si son centre est à moins de `coverage_width / 2` (0,25 mm) d'un
  segment cousu. Le repère est le repère vectoriel centré sur l'image, comme à
  la vectorisation. Cette mesure n'est pas calculable depuis un DST seul
  (origine décalée, pas d'image).

## `point-long` fantôme juste après un saut (2026-08-12)

**Défaut trouvé en usage réel** (export debug utilisateur, objet satin) : la
règle `point-long` comparait chaque point cousu à `prevStitch`, la position du
DERNIER point cousu — sans jamais tenir compte d'un `Jump` intercalé entre les
deux. Un `Jump` lève l'aiguille : le fil n'est plus continu, donc la distance
entre le point cousu juste avant le saut et celui juste après n'a AUCUN sens en
tant que « longueur de point cousu ». Or `emit_polyline` (§ *Génération de
points*) fait systématiquement suivre un `Jump` d'un `Stitch` à la position
d'atterrissage (point de « pinning », distance nulle) : la comparaison portait
donc en réalité sur la longueur du SAUT lui-même, rejouée comme un second
avertissement `point-long` trompeur en plus du `saut-long` déjà émis pour le
même saut — quasi systématique dès qu'un saut dépassait 7 mm (le seuil
`point-long`), qui est bien plus bas que le seuil `saut-long` (30 mm).

Corrigé en réinitialisant `hasPrevStitch` sur tout `Jump` : la continuité du
fil (et donc la comparaison `point-court`/`point-long`) ne traverse plus un
saut. Vérifié : `tests/unit/stitch_analysis/test_analyze.cpp` (absence de
`point-long`/`point-court` fantôme après un saut, y compris à distance
d'atterrissage nulle).

## Dans l'interface

**Analyse → Analyser le motif** (F5) remplit le dock *Analyse* ; un double-clic
sur un problème centre la vue sur sa localisation. Le cadre utilisé est le cadre
courant (100 × 100 mm par défaut).

Le bouton **Corriger** (sous la piste de correction) applique la correction du
problème sélectionné quand il en porte une ; il est désactivé tant que le
résultat est périmé. Annulable d'un Ctrl+Z (une seule entrée d'annulation).

Limitation : l'analyse **spatiale de densité** (carte de chaleur, satin trop
large/étroit dédié, alignement excessif des pénétrations) est **prévue**, non
implémentée ; les corrections automatiques ne couvrent que les constats du tableau
ci-dessus.

## Implémentation associée

- `libs/stitch_analysis/include/openstitch/stitch_analysis/analyze.hpp` —
  `Finding`, `Fix`, `FixKind`, `Severity`, `AnalysisOptions`, `canvas_hoop`.
- `libs/stitch_analysis/src/analyze.cpp` — `analyze`.
- `libs/stitch_analysis/src/project_analysis.cpp` — `analyze_project`, `canvas_hoop`.
- `apps/desktop/main_window_analysis.cpp` — panneau, bouton *Corriger*.
- `apps/desktop/main_window.cpp` — pré-export et dump de débogage.
- Tests : `tests/unit/stitch_analysis/test_analyze.cpp`,
  `test_analyze_project.cpp`, `tests/unit/desktop/test_analysis_fix.cpp`.
