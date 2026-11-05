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
| C | Satin : rails éditables au canevas (déplacement de nœuds d'un rail en 1 pas d'annulation, insertion de nœud, barreaux dessinés, largeur aux barreaux avec avertissement « trop large ») | ✅ | 🟡 services `satin_column` | ✅ | ✅ | ✅ | outil Nœuds (B) ; Catch2 `[satin]`, QTest `nodeToolEditsSatinRails…` |
| C | Satin : tangentes Bézier des rails, édition des barreaux, suppression de nœud, diagnostic des jonctions/croisements | ✅ (`SatinParams`) | 🟡 | ⬜ | ⬜ | ⬜ | les barreaux restent ceux de la génération : la largeur affichée est celle aux barreaux, pas recalculée après déplacement des rails |
| C | Auto-satin (squelette / balayage / axe médian) | ✅ | ✅ | 🟡 exécuté dans la numérisation | ⬜ | 🟡 | pas de pilotage manuel des stratégies |
| C | Correction de segmentation (fusion, îlots, trous, comparaison) | ✅ | 🟡 | ⬜ | ⬜ | ⬜ | seul le pinceau « zones de détail » existe |
| C | Segmentation IA / SAM | ✅ (V1) | 🟡 | ⬜ | ⬜ | ⬜ | hors priorité produit ; état de disponibilité à afficher |
| C | Inspecteur piloté par métadonnées | ✅ | ✅ | 🟡 (`param_spec`) | — | ✅ | niveaux essentiel/avancé/expert, recherche, valeurs mixtes à finir |
| C | Retouche de points | ✅ | ⬜ | ✅ (clic-clic) | 🟡 | ✅ | pas de glisser direct, pas de sélection multiple |
| D | Fondu entre fils : création, fils, rampes | ✅ | ✅ | ✅ action `fade.create` | — | ✅ | |
| D | Fondu : axe manipulable au canevas (début/fin de transition, déplacement, rotation, aperçu pendant le glisser, Maj = symétrique, 1 pas d'annulation, résumé « de x % à y % ») | ✅ `drag_gradient_handle` | ✅ | ✅ outil « Axe du fondu » (L) | ✅ | ✅ | `core/fade_axis` (Catch2 `[fade]`), QTest `fadeAxisToolMovesAndRotates…` |
| D | Fondu : courbe de densité éditable, aperçu des deux fils côte à côte, comparaison avant/après | ✅ (`DensityCurve`) | 🟡 | ⬜ | ⬜ | ⬜ | à faire |
| D | Compensation de tirage (manuelle, auto, prédictive) | ✅ | ✅ | ⬜ | ⬜ | ✅ (moteur) | visualisation à faire ; modèle prédictif non validé physiquement |
| D | Optimisation de l'ordre avec mesures RÉELLES avant/après (changements de fil, sauts, coupes, déplacements à vide, points), compromis signalé, proposition périmée refusée, 1 pas d'annulation | ✅ `optimize_order` | 🟡 pas d'opération batch dédiée | ✅ dock « Optimisation » + commande `optimize.order` | — | ✅ | `core/path_optimizer` (Catch2 `[path]`, 7 cas : la séquence réelle après acceptation donne bien les mesures annoncées), QTest `orderOptimisationShows…` |
| D | Optimisation : surimpression des sauts avant/après sur le canevas, départ/arrivée du motif, tri par calque | ✅ (`jump_segments`, `finishing`) | ⬜ | ⬜ | ⬜ | ⬜ | à faire |
| D | Carte de densité | ✅ | ⬜ | ✅ | — | ✅ | #265 |
| D | Simulation de couture | ✅ | — | ✅ | — | ✅ | vitesse réglable, sons ⬜ |
| E | Pont live : un agent MCP modifie le document OUVERT (approbation obligatoire, une étape d'annulation, refus si le document a changé, session close au remplacement) | ✅ `LiveBridge` | ✅ `openstitch-mcp --live` | ✅ dock « Compagnon IA » + commande `companion.toggle` | — | ✅ | `core/live_companion` (Catch2 `[companion]`, 5 cas) + `ui/live_server` ; QTest bout en bout sur une vraie socket locale. Désactivé par défaut |
| E | Boîte d'approbation avec rendu avant/après, opérations, mesures et réserves | ✅ (`ApprovalRequest`) | ✅ | ✅ `askAgentApproval` | — | 🟡 réponse imposée en test, rendu des images non vérifié | |
| E | Chat intégré à V2 (envoyer un message à Claude depuis l'application) | 🟡 V1 : `claude_backend`, `claude_code_process_backend` | — | ⬜ | — | ⬜ | **non fait** : l'agent se branche aujourd'hui via un client MCP externe ; aucun faux dialogue n'a été ajouté |
| F | Identité visuelle « Atelier » | — | — | 🟡 | — | 🟡 | #262 ouverte : bloque les tests (cf. journal) ; #263 micro-interactions |
| G | Benchmarks 1 / 100 / 500 / 1 000 objets | — | — | ⬜ | — | ⬜ | aucune mesure à ce jour : à produire avant toute affirmation de performance |

## Ce qui reste avant la PR finale
Voir la colonne « Direct/Testé » : les lignes ⬜ sont du travail restant, pas des promesses.
La PR ne sera livrée « prête pour revue » que lorsque les lignes P0 (B, C, E) auront une preuve.
