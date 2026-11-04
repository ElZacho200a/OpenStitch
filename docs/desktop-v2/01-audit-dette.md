# 01 — Audit de dette du desktop actuel

Méthode : mesures sur `origin/main` au 2026-11-04 (grep/wc sur `apps/desktop`, `libs/commands`, `libs/project_ops`,
catalogue du CLI). Les documents d'audit antérieurs (`docs/ux/*`, `docs/ui-audit-2026-10.md`, `docs/ui-redesign-*`) ont été
lus comme **hypothèses** ; seuls les constats re-vérifiés dans le code sont marqués « vérifié ».

## Mesures

| Mesure | Valeur | Lecture |
|---|---|---|
| Fichiers `apps/desktop` (`.cpp`/`.hpp`) | 213, 60 825 lignes | la couche UI est plus grosse que la plupart des libs métier |
| `main_window*.cpp` | 62 fichiers, 29 814 lignes | « extraction » par responsabilité déjà faite (PR #236 en cours) mais **la classe reste unique** |
| `main_window.hpp` | 2 013 lignes, 109,6 Ko | une seule classe porte tout |
| Membres `QAction*` dans `MainWindow` | 174 | chaque fonctionnalité = une action membre |
| Champs d'état (suffixe `_`) dans `MainWindow` | ≈ 449 | état dupliqué entre fenêtre, panneaux et canevas |
| Déclarations de méthodes dans `main_window.hpp` | ≈ 744 | surface de couplage |
| `properties_panel.cpp` | 2 951 lignes | un inspecteur monolithique |
| Dialogues (`: public QDialog`) | 22 | dont plusieurs modaux de création (audit UX du 18/10) |
| Docks / barres d'outils | 5 / 4 (grep de création) | |
| Outils de canevas (`enum Tool`) | 20 | |
| Classes dérivées d'`ICommand` | ≈ 91 | mutation déjà unifiée par `UndoStack` : **bon invariant à garder** |
| Opérations du lot `project_ops` (catalogue CLI/MCP) | 55 | les capacités que Claude peut déclencher |
| Tests desktop | 79 fichiers, 38 957 lignes (QTest headless) | filet de sécurité réel, à ne pas perdre |

## Constats de la mission : vérifiés ou non

| Constat de la mission | Verdict | Preuve |
|---|---|---|
| `MainWindow` concentre encore beaucoup de responsabilités | **Vérifié** | 174 actions + ≈ 449 champs d'état + 62 fichiers `main_window_*.cpp` ; la scission en fichiers n'a pas réduit le couplage (un seul `this`) |
| Fonctionnalités dispersées entre menus, outils, panneaux, dialogues | **Vérifié** | ≈ 40 appels `addMenu` (menus, sous-menus, menus contextuels), 22 dialogues, 20 outils, 5 docks |
| Plusieurs systèmes d'interaction indépendants | **Vérifié** | `interaction_map` (gestes), `tools.hpp` (outils), `command_palette`, raccourcis `QShortcut` hors `QAction`, menus contextuels par widget (`main_window_context_menu.cpp`) |
| Sélection multiple sans source unique | **Partiellement infirmé** | `SelectionModel` (règles pures) et un garde CTest (`check_selection_single_mutator`) existent ; mais l'écrivain unique reste `MainWindow::setSelection` : canevas, arbre et inspecteur lisent des copies de `MainWindow`, pas un modèle observable |
| Calculs longs hors du fil d'interface | **Partiellement vérifié** | `BusyIndicator::run` existe ; chaque appelant décide, aucun gestionnaire central, pas de liste de tâches ni d'annulation uniforme |
| Mutation du document par commandes | **Infirmé (c'est un point fort)** | ≈ 91 `ICommand`, `UndoStack`, garde `check_no_raw_sequence_bypass` |
| Écart manuel / agent | **Nouveau constat** | 55 opérations de lot contre ≈ 91 commandes : une partie des gestes manuels n'a pas d'équivalent pour Claude. À réduire par **un seul** catalogue (voir ADR 0004) |

## Conclusion de l'audit

La dette est **dans la couche desktop, pas dans le moteur**. Le moteur offre déjà les bons points d'appui pour une
nouvelle application : `commands::ICommand` + `UndoStack` (mutations), `project_ops::apply_batch`
(lots transactionnels, simulation `dry_run`, même chemin que la CLI et MCP), `effective_sequence`
(séquence unique), `TaskContext` (annulation/progression sans Qt), `render_realistic_png` (aperçu).
Une réécriture du moteur n'est justifiée par rien dans les mesures ; une nouvelle couche desktop qui s'appuie sur ces
points d'appui l'est (ADR 0004).

La stratégie « corrections incrémentales » des audits antérieurs reste valable **pour l'ancien desktop pendant la
migration** (PR #236 continue la scission de `MainWindow`) ; elle ne résout pas l'absence de modèles partagés
(sélection, tâches, outils) qui est la cause commune des constats ci-dessus.
