# Tri provisoire des 300 entrées PERF-STUDIO-2026-11-10 (2026-11-10)

**Statut : provisoire.** Fait à partir du texte des entrées Notion et de ce qui a été vu dans le code pendant la session ;
**non vérifié entrée par entrée** dans le dépôt (les deux agents de tri ont échoué : limite de crédit). Aucune entrée n'est
« Validée » : voir les règles de la page Notion (preuves avant/après, CI, revue indépendante).

| Catégorie | Nombre |
|---|---:|
| A (déjà fait, preuve dans cette session) | 8 |
| A? (couvert en partie, à vérifier dans le code) | 7 |
| B (mesure / test / doc : adapté à Ollama avec gabarits) | 72 |
| E (implémentation délicate : Claude ou humain, pas Ollama) | 127 |
| C (matériel ou environnement absent ici) | 81 |
| D (sans objet, négligeable ou code mort probable) | 5 |
| **Total** | **300** |

## A (déjà fait, preuve dans cette session)

004-005, 014-015, 021, 046, 081, 094

## A? (couvert en partie, à vérifier dans le code)

024, 032, 045, 082, 087, 109, 244

## B (mesure / test / doc : adapté à Ollama avec gabarits)

008-010, 016, 018, 022-023, 025-026, 028-030, 040, 054-055, 065-066, 080, 102, 124, 126, 134-135, 154, 201-205, 207, 217, 219-229, 231, 233-234, 236-237, 245, 247, 253-255, 258, 260, 263, 265-266, 268-270, 272, 274-276, 282-283, 285-287, 289-290, 300

## E (implémentation délicate : Claude ou humain, pas Ollama)

006, 013, 020, 031, 033-039, 041-044, 047-053, 056-064, 067-079, 083, 086, 088-093, 095-101, 103-108, 110-114, 116-120, 122-123, 127-132, 136-153, 155, 211-212, 232, 238, 240-243, 246, 248-251, 256-257, 259, 271, 273, 277-279, 288, 293, 295-296

## C (matériel ou environnement absent ici)

001-003, 007, 017, 019, 027, 115, 121, 125, 133, 156-200, 206, 208-210, 213-216, 218, 230, 239, 252, 261-262, 264, 267, 280-281, 284, 291-292, 294, 297-299

## D (sans objet, négligeable ou code mort probable)

011-012, 084-085, 235

## File de travail proposée pour Ollama (catégorie B, par ordre)

1. Bancs : PERF-026 (500 000 points / 10 000 objets), PERF-254 (pick vectoriel à 10 000 formes), PERF-286 (croissance 100/1k/10k régions), PERF-275 (microbancs par style de point).
2. Tests de parité / déterminisme : PERF-029, PERF-154, PERF-203.
3. Mesures de contention et de copies : PERF-222/223 (cache de fragments), PERF-226/227 (cache PNG de project_io), PERF-040.
4. Compteurs QML/contrôleurs headless : PERF-228, 229, 231, 247, 22 (statut d'étape).
5. Doc et rapports : PERF-217, 219, 220, 300, 009, 010.

## Limites connues du tri

- Les entrées A? et B supposent que le code actuel ne les couvre pas déjà : à confirmer par recherche avant chaque tâche.
- PERF-235 (HitIndex::inRect) : `HitIndex` servait à l'interface Widgets supprimée par #321 ; probablement du code mort, non vérifié.
- Les entrées « E » sont réalisables ici mais touchent déterminisme, ordre de couture ou concurrence : pas pour le modèle local.
- Les entrées « C » restent à mesurer sur une machine de référence (GPU, 120/144 Hz, fenêtre native, essai textile).
