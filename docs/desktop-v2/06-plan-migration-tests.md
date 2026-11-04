# 06 — Plan de migration, indicateurs et recette

## Chaîne de PR (périmètres relisables, dépendances explicites)

| PR | Périmètre | Dépend de | État |
|---|---|---|---|
| **V2-0** | architecture et contrats : docs, ADR 0003/0004, `core/` (session, sélection, tâches, registre d'outils, chaîne image → DST, aperçu), squelette `ui/` (shell, arbre, canevas d'aperçu), tests | `main` | **cette branche** (`feat/desktop-v2-foundation`) |
| V2-1 | infrastructure d'interface : design system (tokens, thème, icônes), docks et espaces persistants, palette de commandes, i18n FR/EN, centre de notifications | V2-0 | ⬜ |
| V2-2 | canevas et interactions : calque `QGraphicsItem`, sélection au canevas, poignées, outils de dessin, banc de mesure (déclencheurs de l'ADR 0003) | V2-1 | 🟡 **tranche livrée dans `feat/desktop-v2-canvas-inspector`** : sélection au clic et au cadre élastique, inspecteur 4 réglages, debounce d'aperçu, palette de commandes ; poignées, dessin, banc de mesure ⬜ |
| V2-3 | workflows de conception : prétraitement, segmentation, régions, vectorisation | V2-2 | ⬜ |
| V2-4 | workflows de numérisation : inspecteur contextuel, types de points, satin/auto-satin, remplissages spéciaux, fondus | V2-2 | ⬜ |
| V2-5 | analyse et production : ordre/coupes, densité, tirage prédictif, simulation, rendu, cadres, nuancier, exports | V2-4 | ⬜ |
| V2-6 | compagnon IA : panneau, session live, aperçu avant/après, approbation | V2-1 (V2-4 pour la couverture) | ⬜ |
| V2-7 | parité et bascule : matrice entièrement ✅/exception justifiée, recette complète, `openstitch` pointe vers V2 | tout | ⬜ |

Règle de livraison : une PR V2 compile, passe ses tests, est formatée (clang-format 18), **met à jour la matrice `02`**
et le journal `07`, et ne casse aucun test de l'ancien desktop. L'ancien desktop reste buildable jusqu'à V2-7.

## Indicateurs UX (base de référence)

| Indicateur | Mesure V2 au 2026-11-04 | Comment | Comparaison ancien desktop |
|---|---|---|---|
| Actions pour image → DST | 3 (ouvrir, hauteur facultative, exporter) | parcours 1, test `imageToDstEndToEnd` | **à mesurer** sur la même image |
| Dialogues bloquants (hors sélecteur de fichier) | 0 | revue de `ui/` : aucun `exec()`/`QMessageBox` | à mesurer |
| Surface du canevas | ≈ 65 % de la fenêtre, ≈ 75 % de la largeur | capture 1920×1200 | à mesurer à 1920×1200 et 1280×720 |
| Temps de numérisation Tentabrode 300 mm | ≈ 1,5 s (Debug, machine de développement) | test Catch2 | à mesurer (Release) |
| Interface bloquée pendant les calculs | non (tâche + pump 33 ms) | test `pipeline runs through the task manager…` | — |
| Cohérence de sélection | arbre ↔ modèle testés dans les deux sens | QTest | canevas ⬜ |
| Taux d'erreurs utilisateur, temps de correction | **non mesurés** (aucun test utilisateur) | — | — |

Aucun indicateur n'est annoncé « meilleur que l'ancien » tant que la ligne de comparaison est vide.

## Scénarios obligatoires de la mission

| # | Scénario | État | Preuve |
|---|---|---|---|
| 1 | Numérisation complète de Tentabrode à 300 mm | 🟡 | 139 objets, 43 562 points, 401 sauts, 372 coupes, 5 changements de couleur (`max_colors=6`) ; **emprise cousue 230,0 × 276,9 mm** (le fond blanc ignoré n'est pas cousu ; le cadre d'import fait 300 mm de haut) ; DST relu (`read_dst_file`) ; deux exécutions → DST identiques octet pour octet ; capture `captures/shell-tentabrode-300mm.png`. **Question ouverte** : si « 300 mm » désigne la hauteur du *dessin cousu*, il faut un mode « hauteur du motif » (recadrage sur l'emprise) — voir journal |
| 2 | Création et édition d'un satin complexe | ⬜ | |
| 3 | Modification simultanée de plusieurs objets | 🟡 | sélection multiple (arbre + clic) ✅ ; édition simultanée type/couleur/visibilité/verrou en **une** annulation ✅ (test `inspectorEditsAllSelectedObjectsInOneUndoStep`) ; autres paramètres ⬜ |
| 4 | Fondu de deux fils | ⬜ | |
| 5 | Compensation prédictive | ⬜ | |
| 6 | Optimisation de l'ordre et des coupes | ⬜ | (appliquée à la numérisation, pas pilotable) |
| 7 | Modification d'un `.osp` existant | 🟡 | aller-retour enregistrement → ouverture ✅ ; renommer / masquer + annuler ✅ ; modification de paramètres ⬜ |
| 8 | Import SVG puis conversion | ⬜ | |
| 9 | Session compagnon Claude / MCP | ⬜ | primitive `previewBatch`/`applyBatch` testée |
| 10 | Simulation, diagnostic, export final | 🟡 | export DST ✅ ; simulation, diagnostic ⬜ |

## Plan de tests

| Niveau | Outil | Existant (V2-0) | À faire |
|---|---|---|---|
| Cœur Qt-free | Catch2 `tests/unit/desktop_v2/` | sélection (8), tâches (6), registre/parité (5), session + chaîne (10), aperçu (3) | outils, historique, compagnon |
| Interface | QTest headless (`QT_QPA_PLATFORM=offscreen`) | `test_desktop_v2_shell` | un fichier par panneau, par outil |
| Contrats desktop ↔ moteur | Catch2 | mêmes appels que la CLI ; déterminisme DST | lot « catalogue » : chaque opération migrée a un test d'équivalence geste ↔ lot |
| Anciens projets | Catch2/QTest | un aller-retour de projet généré | corpus de `.osp` historiques (schémas v1…courant) |
| FR/EN, 2 thèmes, résolutions | QTest + captures | capture sombre 1920×1200 | grille de captures (clair/sombre × FR/EN × 1280×720 / 1920×1200) |
| Clavier | QTest | conflits de raccourcis, F du canevas | parcours 100 % clavier |
| MCP | Catch2 + QTest | — | session live bout en bout |
| Recette visuelle | `openstitch_v2 --screenshot` | 1 capture | par parcours |

CI : la PR V2-0 ajoute `apps/desktop_v2/core` au build `linux-core` ; **non vérifié sur Linux depuis cet
environnement (Windows)** — le résultat de la CI de la PR fait foi.
