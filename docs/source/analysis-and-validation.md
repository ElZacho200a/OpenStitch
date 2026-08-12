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
| `point-court` | segment cousu < 0,5 mm | Avertissement |
| `point-long` | segment cousu > 7 mm | Avertissement |
| `saut-long` | déplacement > 30 mm | Avertissement |
| `hors-cadre` | point hors du cadre (si fourni) | Erreur |
| `trop-de-points` | > 100 000 points | Avertissement |

Chaque problème porte une **gravité** (`Info`/`Warning`/`Error`), un **message**,
une **localisation** et l'**objet** concerné. Un plafond par catégorie évite
l'inondation ; le résultat est **déterministe**.

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
