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

Chaque problème porte une **gravité** (`Info`/`Warning`/`Error`), un **message**,
une **localisation** et l'**objet** concerné. Un plafond par catégorie évite
l'inondation ; le résultat est **déterministe**.

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

Limitation : l'analyse **spatiale de densité** (carte de chaleur, superposition
de couches, satin trop large/étroit dédié, alignement excessif des pénétrations)
et les **corrections automatiques proposées** sont **prévues**, non implémentées.

## Implémentation associée

- `libs/stitch_analysis/include/openstitch/stitch_analysis/analyze.hpp` —
  `Finding`, `Severity`, `AnalysisOptions`.
- `libs/stitch_analysis/src/analyze.cpp` — `analyze`.
- `apps/desktop/main_window.cpp` — `buildAnalysisPanel`, `runAnalysis`.
- Tests : `tests/unit/stitch_analysis/test_analyze.cpp`.
