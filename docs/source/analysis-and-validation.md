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
