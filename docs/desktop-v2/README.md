# OpenStitch Desktop V2

> **2026-11-10 — l'interface V2 Widgets est supprimée** (`apps/desktop_v2/ui/`, cible
> `openstitch_v2`, bibliothèque `openstitch_desktop_v2_ui`, test `test_desktop_v2_shell`, étape de
> captures V2 de la CI). Décision du propriétaire du dépôt. Le Studio (`openstitch_v2_studio`,
> exécutable `openstitch.exe`) est le seul desktop V2 ; le cœur Qt-free `apps/desktop_v2/core/` est
> conservé, ainsi que `live_server` (déplacé dans `apps/desktop_v2/live/`). **Ce qui n'existait que
> dans V2 Widgets n'est plus disponible** : récupérable dans l'historique git avant cette
> suppression ; la parité Studio reste un travail ouvert (Notion, roadmap de parité). Les chapitres
> ci-dessous décrivent l'état avant suppression quand ils parlent de `openstitch_v2` ou de
> « Widgets ». Le banc `openstitch-studio-bench` ne mesure plus que le Studio (Quick).

Espace documentaire de la refonte du desktop. **Source de vérité de l'avancement : [`07-journal.md`](07-journal.md)**
(état réel, pas les intentions). À chaque reprise de session : lire le journal, `git log`, les PR ouvertes
(`gh pr list`), puis la matrice de parité.

**Nouvelle cible expérimentale : `openstitch_v2_studio`** (2026-11-06). Le shell Qt Quick et le
canevas GPU chargent un `.osp` réel, avec pan/zoom ancré et sélection partagée. `--software`
permet le repli CPU du canevas ; `openstitch_v2` conserve l'ensemble des outils existants.
[Architecture, lancement et migration](17-studio-gpu.md) · [Benchmarks](18-studio-benchmarks.md) ·
[ADR 0005](../adr/0005-studio-qtquick-gpu.md). La parité et 60 FPS restent des critères à vérifier.

## Ce qui existe aujourd'hui (mesuré, pas promis)

`apps/desktop_v2/` : un nouveau desktop **coexistant** avec `apps/desktop` (qui reste le desktop principal
jusqu'à la parité démontrée). Il contient :

| Couche | Fichiers | Sans Qt | Testé par |
|---|---|---|---|
| `core/` session documentaire, sélection unique, gestionnaire de tâches, registre d'outils/espaces, chaîne image → DST, aperçu | `apps/desktop_v2/core/` | oui (compile en CI `linux-core`) | `tests/unit/desktop_v2/` (Catch2) |
| `ui/` fenêtre « Application Shell », canevas d'aperçu, arbre documentaire, rail d'outils, barre d'état | `apps/desktop_v2/ui/` | non (Qt Widgets) | `test_desktop_v2_shell` (QTest headless) |
| `studio/` shell QML, pont de session, canevas Scene Graph / repli CPU | `apps/desktop_v2/studio/` | non (Qt Quick optionnel) ; scène/caméra dans `core/` sans Qt | tests de scène/caméra et intégration Qt ; banc natif séparé |

Chaîne verticale **réelle** (vrais moteurs, vrai document) : ouvrir une image → numériser (tâche annulable) →
aperçu réaliste → arbre d'objets synchronisé avec la sélection → renommer / masquer (lot `project_ops`, une étape
d'annulation) → exporter un DST. Capture : [`captures/shell-tentabrode-300mm.png`](captures/shell-tentabrode-300mm.png).

Cette chaîne décrit le premier socle V2 ; les ajouts d'édition, satin, fils, compagnon et autres
outils ultérieurs sont détaillés dans le [journal](07-journal.md) et la [matrice](02-inventaire-et-parite.md).
Le shell Studio expérimental ne porte pas encore ces outils. Sa disponibilité ne vaut pas une
validation de la parité ni une bascule du desktop principal.

## Documents

| # | Document | Contenu |
|---|---|---|
| 01 | [Audit de dette](01-audit-dette.md) | mesures du desktop actuel, constats vérifiés ou infirmés |
| 02 | [Inventaire et matrice de parité](02-inventaire-et-parite.md) | toutes les capacités, leur place dans V2, leur statut |
| 03 | [Architecture cible](03-architecture.md) | couches, flux, règles de dépendance, ce qui est implémenté |
| 04 | [UX et design system](04-ux-design-system.md) | principes, espaces de travail, interactions, tokens |
| 05 | [Parcours utilisateurs](05-parcours.md) | les 12 parcours, interaction cible, état |
| 06 | [Plan de migration et de tests](06-plan-migration-tests.md) | chaîne de PR, indicateurs UX, scénarios de recette |
| 07 | [Journal d'avancement](07-journal.md) | état réel, preuves, problèmes ouverts |
| 17 | [Studio GPU](17-studio-gpu.md) | audit, contrats C++/QML, rendu, repli et migration |
| 18 | [Banc Studio](18-studio-benchmarks.md) | scènes identiques, métriques, données brutes et limites |
| ADR | [0003 techno historique](../adr/0003-desktop-v2-technologie.md), [0004 modèles](../adr/0004-desktop-v2-architecture.md), [0005 Studio](../adr/0005-studio-qtquick-gpu.md) | décisions et conditions de révision |

## Construire et lancer

```powershell
cmake --preset msvc
cmake --build --preset msvc-debug --target openstitch_v2 test_desktop_v2_core test_desktop_v2_shell
.\build\msvc\apps\desktop_v2\Debug\openstitch_v2.exe tests\fixtures\tentabrode.png --height-mm 300
# recette visuelle reproductible (capture puis sortie) :
.\build\msvc\apps\desktop_v2\Debug\openstitch_v2.exe tests\fixtures\tentabrode.png --height-mm 300 --screenshot out.png
```

Le mode `QT_QPA_PLATFORM=offscreen` n'a pas de polices sous Windows : les captures se font sur la plateforme native.
