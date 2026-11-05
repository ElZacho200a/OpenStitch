# Matrice de finalisation Desktop V2 (instrument de suivi de la PR unique)

Dernière vérification : 2026-11-05, sur `main` à `e15e35b` + branche `feat/desktop-v2-finalisation`.
Légende : ✅ vérifié par un test ou par le code · 🟡 partiel (précisé) · ⬜ absent · — sans objet.
Colonnes : **Moteur** = capacité dans les libs · **Ops** = exposée par `project_ops` / CLI / MCP ·
**V2** = accessible dans l'interface V2 · **Direct** = manipulable au canevas · **Testé** = test
automatisé qui l'exerce. Une ligne ne passe à ✅ que si le test existe et passe.

## Chantiers de la mission

| Chantier | Capacité | Moteur | Ops | V2 | Direct | Testé | Preuve / remarque |
|---|---|---|---|---|---|---|---|
| A | Protection des documents (nouveau, ouvrir, quitter, récupération) | ✅ | — | ✅ | — | ✅ | #253 : confirmation Enregistrer/Ignorer/Annuler, chargements périmés ignorés |
| A | Extraction de `ShellWindow` (≈ 3 000 lignes) | — | — | 🟡 | — | 🟡 | `core/` reste Qt-free ; l'UI reste concentrée dans `shell_window.cpp` : à découper |
| B | Déplacement de nœuds au canevas (aperçu pendant le glisser, 1 pas d'annulation) | ✅ `TransformNodesCommand` | ⬜ | ✅ | ✅ | ✅ | `node_edit` + outil « Nœuds du contour » (B) ; Catch2 `[nodes]`, QTest `nodeToolDrags…` |
| B | Insertion de nœud (double-clic sur un segment, subdivision exacte) | ✅ `InsertNodeCommand` | ⬜ | ✅ | ✅ | ✅ | idem |
| B | Remodelage d'un segment (`ReshapeSegmentCommand`) | ✅ | ⬜ | 🟡 service `reshapeSegment` | ⬜ | ⬜ | service prêt, geste canevas à câbler |
| B | Suppression de nœud, tangentes Bézier, plume, main levée | 🟡 (tangentes dans `PathNode`) | ⬜ | ⬜ | ⬜ | ⬜ | outils `draw.bezier` / `draw.freeform` déclarés mais non migrés |
| B | Repères intelligents, magnétisme, cotes pendant le glisser | ⬜ | — | ⬜ | ⬜ | ⬜ | à faire |
| B | Navigation trackpad / zoom / pan | ✅ | — | ✅ | ✅ | ✅ | #257 |
| C | Satin : création par colonne à deux rails | ✅ | ✅ | ✅ (outil `satin.column`) | 🟡 tracé, pas d'édition des rails | ✅ | `satin_column` |
| C | Satin : rails/tangentes/barreaux éditables au canevas | ✅ (`SatinParams`) | 🟡 | ⬜ | ⬜ | ⬜ | gros lot à faire |
| C | Auto-satin (squelette / balayage / axe médian) | ✅ | ✅ | 🟡 exécuté dans la numérisation | ⬜ | 🟡 | pas de pilotage manuel des stratégies |
| C | Correction de segmentation (fusion, îlots, trous, comparaison) | ✅ | 🟡 | ⬜ | ⬜ | ⬜ | seul le pinceau « zones de détail » existe |
| C | Segmentation IA / SAM | ✅ (V1) | 🟡 | ⬜ | ⬜ | ⬜ | hors priorité produit ; état de disponibilité à afficher |
| C | Inspecteur piloté par métadonnées | ✅ | ✅ | 🟡 (`param_spec`) | — | ✅ | niveaux essentiel/avancé/expert, recherche, valeurs mixtes à finir |
| C | Retouche de points | ✅ | ⬜ | ✅ (clic-clic) | 🟡 | ✅ | pas de glisser direct, pas de sélection multiple |
| D | Fondu entre fils | ✅ | ✅ | 🟡 action `fade.create` | ⬜ | ✅ | poignées, courbe de densité, avant/après à faire |
| D | Compensation de tirage (manuelle, auto, prédictive) | ✅ | ✅ | ⬜ | ⬜ | ✅ (moteur) | visualisation à faire ; modèle prédictif non validé physiquement |
| D | Optimisation des parcours avec mesures avant/après | ✅ | ✅ | 🟡 appliquée à la numérisation | ⬜ | 🟡 | pilotage et prévisualisation à faire |
| D | Carte de densité | ✅ | ⬜ | ✅ | — | ✅ | #265 |
| D | Simulation de couture | ✅ | — | ✅ | — | ✅ | vitesse réglable, sons ⬜ |
| E | Compagnon Claude / MCP | ✅ serveur `openstitch-mcp --live` (tube nommé) | ✅ | ⬜ | — | ⬜ | V1 : `companion_dock`, `live_server`. V2 : aucun lien live ni panneau ; à construire sur `DocumentSession::previewBatch/applyBatch` |
| F | Identité visuelle « Atelier » | — | — | 🟡 | — | 🟡 | #262 ouverte : bloque les tests (cf. journal) ; #263 micro-interactions |
| G | Benchmarks 1 / 100 / 500 / 1 000 objets | — | — | ⬜ | — | ⬜ | aucune mesure à ce jour : à produire avant toute affirmation de performance |

## Ce qui reste avant la PR finale
Voir la colonne « Direct/Testé » : les lignes ⬜ sont du travail restant, pas des promesses.
La PR ne sera livrée « prête pour revue » que lorsque les lignes P0 (B, C, E) auront une preuve.
