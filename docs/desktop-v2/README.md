# OpenStitch Desktop V2

Espace documentaire de la refonte du desktop. **Source de vérité de l'avancement : [`07-journal.md`](07-journal.md)**
(état réel, pas les intentions). À chaque reprise de session : lire le journal, `git log`, les PR ouvertes
(`gh pr list`), puis la matrice de parité.

## Ce qui existe aujourd'hui (mesuré, pas promis)

`apps/desktop_v2/` : un nouveau desktop **coexistant** avec `apps/desktop` (qui reste le desktop principal
jusqu'à la parité démontrée). Il contient :

| Couche | Fichiers | Sans Qt | Testé par |
|---|---|---|---|
| `core/` session documentaire, sélection unique, gestionnaire de tâches, registre d'outils/espaces, chaîne image → DST, aperçu | `apps/desktop_v2/core/` | oui (compile en CI `linux-core`) | `tests/unit/desktop_v2/` (Catch2) |
| `ui/` fenêtre « Application Shell », canevas d'aperçu, arbre documentaire, rail d'outils, barre d'état | `apps/desktop_v2/ui/` | non (Qt Widgets) | `test_desktop_v2_shell` (QTest headless) |

Chaîne verticale **réelle** (vrais moteurs, vrai document) : ouvrir une image → numériser (tâche annulable) →
aperçu réaliste → arbre d'objets synchronisé avec la sélection → renommer / masquer (lot `project_ops`, une étape
d'annulation) → exporter un DST. Capture : [`captures/shell-tentabrode-300mm.png`](captures/shell-tentabrode-300mm.png).

**Ce qui n'existe PAS encore** : tout le reste de la matrice de parité (voir [`02`](02-inventaire-et-parite.md)) —
dessin vectoriel, édition de satin, fondus, tirage prédictif, simulation, nuancier, compagnon Claude, etc.
Desktop V2 n'est **pas** utilisable comme desktop principal : le critère de réussite n° 12 de la mission n'est pas atteint.

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
| ADR | [0003 techno](../adr/0003-desktop-v2-technologie.md), [0004 architecture](../adr/0004-desktop-v2-architecture.md) | décisions et conditions de révision |

## Construire et lancer

```powershell
cmake --preset msvc
cmake --build --preset msvc-debug --target openstitch_v2 test_desktop_v2_core test_desktop_v2_shell
.\build\msvc\apps\desktop_v2\Debug\openstitch_v2.exe tests\fixtures\tentabrode.png --height-mm 300
# recette visuelle reproductible (capture puis sortie) :
.\build\msvc\apps\desktop_v2\Debug\openstitch_v2.exe tests\fixtures\tentabrode.png --height-mm 300 --screenshot out.png
```

Le mode `QT_QPA_PLATFORM=offscreen` n'a pas de polices sous Windows : les captures se font sur la plateforme native.
