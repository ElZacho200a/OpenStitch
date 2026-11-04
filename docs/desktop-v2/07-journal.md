# 07 — Journal d'avancement et problèmes ouverts

Reflète l'état **réel**. Chaque entrée : ce qui a été fait, la preuve, ce qui ne l'est pas. Ne jamais y écrire une
intention comme un résultat.

## 2026-11-04 — Session 1 : audit, architecture, fondation exécutable (branche `feat/desktop-v2-foundation`, PR V2-0)

### Fait, avec preuve

| Élément | Preuve |
|---|---|
| Audit de dette mesuré | `01-audit-dette.md` (chiffres tirés de grep/wc/CLI le jour même) |
| Inventaire et matrice de parité (44 familles) | `02` — ✅ 2 · 🟡 13 · ⬜ 29 |
| ADR 0003 (technologie) et 0004 (architecture) | `docs/adr/` |
| `core/` Qt-free : `SelectionModel`, `TaskManager`, `DocumentSession`, `ToolRegistry`, chaîne image → DST, aperçu | `apps/desktop_v2/core/` ; **32 cas Catch2** (`test_desktop_v2_core`) verts |
| `ui/` : shell, 4 espaces, rail d'outils, arbre documentaire, canevas d'aperçu, barre d'état avec progression/annulation | `apps/desktop_v2/ui/` ; **11 tests QTest headless** (`test_desktop_v2_shell`) verts |
| Parcours image → aperçu → renommage/masquage annulable → DST | tests `imageToDstEndToEnd`, `renamingFromTheTreeIsOneUndoStep`, `hidingFromTheTreeGoesThroughTheBatchPathAndUndoes` |
| Aller-retour `.osp` | test `projectSaveThenReopenRoundTrips` |
| Déterminisme | test : deux numérisations → DST identiques octet pour octet |
| Parité des 20 outils de canevas (déclarés) | test `every legacy canvas tool has a V2 equivalent` |
| Capture réelle (Windows, plateforme native, thème sombre) | `captures/shell-tentabrode-300mm.png` |
| Garde-fous du dépôt | `check_*` (CTest) verts ; clang-format 18.1.8 : `--dry-run --Werror` propre sur les fichiers ajoutés |
| Build Windows MSVC Debug | `cmake --build --preset msvc-debug` : 0 avertissement sur les cibles V2 |

Un bug a été trouvé **par les tests** pendant la session (et corrigé) : `TaskManager::submit` testait l'`optional`
au lieu du `Result`, donc une tâche en échec était comptée comme réussie ; les tests `task failure…` et `cancelled task…`
l'ont détecté. Une fuite de verrou Windows dans un test (flux ouvert pendant `remove`) a aussi été corrigée.

### Mesures

Tentabrode (`tests/fixtures/tentabrode.png`), hauteur d'import visée 300 mm, `max_colors = 6` : **139 objets, 43 562 points,
401 sauts, 372 coupes, 5 changements de couleur, emprise cousue 230,0 × 276,9 mm**, ≈ 1,5 s (Debug). Avec les réglages
par défaut du shell (`max_colors = 8`) : 138 objets, 43 670 points.

### Non fait (explicitement)

- Presque tout le périmètre fonctionnel : **29 familles sur 44 sont ⬜**, 13 partielles (`02`). Desktop V2 n'est pas
  utilisable comme desktop principal ; la bascule n'est pas à l'ordre du jour de cette PR.
- Aucun prototype QML, aucune mesure de fluidité Widgets/QML (ADR 0003 : décision conditionnelle, déclencheurs écrits).
- Pas de maquettes de parcours, pas de design system matérialisé (tokens, icônes), pas de mode tactile, pas d'anglais
  (chaînes V2 non extraites dans `openstitch_en.ts`), pas de test sous 1000 px.
- Pas de comparaison chiffrée avec l'ancien desktop (indicateurs `06` à remplir), pas de test utilisateur.
- Pas de validation sous Linux (CI `linux-core` à lire sur la PR) ni de recette sur une autre machine.
- Compagnon Claude : seule la primitive (`previewBatch`/`applyBatch`) existe, testée côté session ; aucun panneau.
- Sous-agents non utilisés : la fondation (modèles partagés + contrats) demandait un architecte unique ; la
  parallélisation devient rentable à partir de V2-3/V2-4 (périmètres disjoints).

### Problèmes ouverts

1. **« 300 mm »** — l'import à 300 mm donne un dessin cousu de 276,9 mm de haut (marge blanche non cousue). Si la cible
   est la hauteur du *motif*, il faut un mode « hauteur du motif » (recadrage sur l'emprise des objets) : décision à
   prendre avec l'utilisateur avant de s'en servir comme critère de recette.
2. `saveProject` et la reconstruction de la liste d'objets sont synchrones sur le fil d'interface (OK à 138 objets).
3. Un aperçu est relancé à **chaque** changement de révision, sans debounce ; le rendu n'étant pas interruptible en cours
   de calcul, une rafale d'éditions peut empiler des rendus inutiles (livraison périmée écartée, mais CPU consommé).
4. Écart catalogue : 55 opérations de lot vs ≈ 91 commandes. Chaque lot de migration doit fermer l'écart de sa famille.
5. `ShellWindow` détruit ses abonnements dans le destructeur mais `DocumentSession` est un membre : un futur composant
   qui survit à la fenêtre devra se désabonner (à généraliser par un petit RAII d'abonnement).
6. Le test de parité des outils inclut `apps/desktop/tools.hpp` (à retirer à la bascule).

### Prochaine action recommandée

Lot **V2-1 + V2-2** (infrastructure d'interface puis canevas), en commençant par le banc de mesure du calque
d'interaction (déclencheurs ADR 0003) et la sélection au canevas, qui débloquent tous les lots d'édition. Les lots
V2-3 et V2-4 sont ensuite parallélisables (sous-agents en worktrees, périmètres de fichiers disjoints).
