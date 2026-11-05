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
| A | Extraction de `ShellWindow` | — | — | 🟡 | — | ✅ (suites inchangées) | `NodeEditController` (nœuds, rails, segments, axe du fondu) et `AnalysisOverlays` (densité, plissement du tissu) extraits sans changer le comportement : `shell_window.cpp` 3 679 → 3 229 lignes, 92 QTest et 179 cas Catch2 inchangés. Restent à extraire : cycle de vie du document, aperçu/simulation, compagnon, optimisation, import |
| B | Déplacement de nœuds au canevas (aperçu pendant le glisser, 1 pas d'annulation) | ✅ `TransformNodesCommand` | ⬜ | ✅ | ✅ | ✅ | `node_edit` + outil « Nœuds du contour » (B) ; Catch2 `[nodes]`, QTest `nodeToolDrags…` |
| B | Insertion de nœud (double-clic sur un segment, subdivision exacte) | ✅ `InsertNodeCommand` | ⬜ | ✅ | ✅ | ✅ | idem |
| B | Remodelage direct d'un segment de courbe au canevas (saisie sur le tracé, aperçu, refus si croisement, nœuds immobiles, 1 pas d'annulation) | ✅ `ReshapeSegmentCommand` | ⬜ | ✅ outil Nœuds | ✅ | ✅ | Catch2 `[curves]` (4 cas), QTest `curvesCanBeCreatedAndBent…` |
| B | Conversion des contours en courbes de Bézier et lissage (commandes `vector.curves`, `vector.smooth`), refus clair quand il n'y a rien à arrondir (ex. rectangle à coins francs) | ✅ | ⬜ | ✅ | — | ✅ | |
| B | Outil Plume (clic = angle, clic-glisser = nœud lisse à poignée symétrique, Entrée / double-clic / clic sur le 1er nœud = fermer, Échap = abandonner, Retour arrière = retirer) : forme créée en UNE commande, refus clairs (croisement, trop petit, nœuds confondus, hors zone) | ✅ | ⬜ | ✅ outil `draw.bezier` (P) | ✅ | ✅ | `core/pen_shape` (Catch2 `[pen]`, 4 cas), QTest `penToolDrawsACurvedShape…` |
| B | Suppression de nœuds (touche Suppr dans l'outil Nœuds : les nœuds choisis, pas l'objet ; un pas d'annulation ; refus clair sous 3 nœuds ou si le contour se croiserait ; pas sur les rails de satin) | ✅ `RemoveNodeCommand` | ⬜ | ✅ | ✅ | ✅ | Catch2 `[delete]` (2 cas), QTest `deleteKeyRemovesTheChosenNodes…` |
| B | Crayon (main levée) : trait refermé puis ajusté en courbes de Bézier (tolérance 2 % de la diagonale, au moins 4 nœuds), refus clairs (trait court, forme minuscule, boucle qui se croise), 1 pas d'annulation | ✅ `fit_curves` | ⬜ | ✅ outil `draw.freeform` (N) | ✅ | ✅ | Catch2 `[freehand]` (2 cas), QTest `pencilTurnsAHandDrawnLoop…` (moins de 40 nœuds pour ~90 points tracés) |
| B | Poignées de tangentes visibles et déplaçables après création | 🟡 (tangentes dans `PathNode`) | ⬜ | ⬜ | ⬜ | ⬜ | **non fait** : la courbure d'une forme existante s'obtient en tirant un segment |
| B | Magnétisme des bords, centres et axe du motif pendant le déplacement d'objets (seuil 6 px de vue, Alt le suspend), repères d'alignement tracés, décalage annoncé en mm, valeur validée = valeur affichée | ✅ (calcul pur) | — | ✅ | ✅ | ✅ | `core/snap` (Catch2 `[snap]`, 7 cas), QTest `draggingAnObjectSnaps…` (vrai glisser à la souris : l'écart au voisin tombe à < 0,05 mm, contre > 0,2 mm sans magnétisme) |
| B | Cotes entre objets (distances), angle de rotation affiché, magnétisme des nœuds et du redimensionnement | ⬜ | — | ⬜ | ⬜ | ⬜ | à faire : seul le déplacement d'objets est aimanté |
| B | Navigation trackpad / zoom / pan | ✅ | — | ✅ | ✅ | ✅ | #257 |
| C | Satin : création par colonne à deux rails | ✅ | ✅ | ✅ (outil `satin.column`) | 🟡 tracé, pas d'édition des rails | ✅ | `satin_column` |
| C | Satin : rails éditables au canevas (déplacement de nœuds d'un rail en 1 pas d'annulation, insertion de nœud, barreaux dessinés, largeur aux barreaux avec avertissement « trop large ») | ✅ | 🟡 services `satin_column` | ✅ | ✅ | ✅ | outil Nœuds (B) ; Catch2 `[satin]`, QTest `nodeToolEditsSatinRails…` |
| C | Satin : tangentes Bézier des rails, édition des barreaux, suppression de nœud, diagnostic des jonctions/croisements | ✅ (`SatinParams`) | 🟡 | ⬜ | ⬜ | ⬜ | les barreaux restent ceux de la génération : la largeur affichée est celle aux barreaux, pas recalculée après déplacement des rails |
| C | Auto-satin (squelette / balayage / axe médian) | ✅ | ✅ | 🟡 exécuté dans la numérisation | ⬜ | 🟡 | pas de pilotage manuel des stratégies |
| C | Réglages d'import avec segmentation en aperçu (couleurs, plus petite région, lissage, fond, style, hauteur) côte à côte avec l'image d'origine, recalcul en tâche annulable, aperçu périmé jamais affiché | ✅ | ✅ `DigitizeSettings` | ✅ fenêtre d'import (Fichier › Ouvrir une image) | — | ✅ | `core/segmentation_preview` (Catch2 `[segment]`), QTest `importDialogShowsTheSegmentation…` (les réglages choisis sont ceux de la numérisation : ≤ 3 fils pour 3 couleurs) |
| C | Correction de segmentation APRÈS numérisation : fusionner / supprimer / recolorer une région, nettoyer les îlots, fusionner des couleurs, corriger trous et frontières, puis régénérer | ✅ (`MergeRegionsCommand`, `RemoveRegionCommand`, `CleanIslandsCommand`, `MergeColorsCommand`) | 🟡 | ⬜ | ⬜ | ⬜ | **non fait** : les commandes existent, l'interface de sélection de régions et la régénération des objets restent à construire ; seul le pinceau « zones de détail » existe |
| C | Segmentation IA / SAM | ✅ (V1) | 🟡 | ⬜ | ⬜ | ⬜ | hors priorité produit ; état de disponibilité à afficher |
| C | Inspecteur piloté par métadonnées | ✅ | ✅ | 🟡 (`param_spec`) | — | ✅ | niveaux essentiel/avancé/expert, recherche, valeurs mixtes à finir |
| C | Retouche de points | ✅ | ⬜ | ✅ (clic-clic) | 🟡 | ✅ | pas de glisser direct, pas de sélection multiple |
| D | Fondu entre fils : création, fils, rampes | ✅ | ✅ | ✅ action `fade.create` | — | ✅ | |
| D | Fondu : axe manipulable au canevas (début/fin de transition, déplacement, rotation, aperçu pendant le glisser, Maj = symétrique, 1 pas d'annulation, résumé « de x % à y % ») | ✅ `drag_gradient_handle` | ✅ | ✅ outil « Axe du fondu » (L) | ✅ | ✅ | `core/fade_axis` (Catch2 `[fade]`), QTest `fadeAxisToolMovesAndRotates…` |
| D | Fondu : courbe de densité éditable, aperçu des deux fils côte à côte, comparaison avant/après | ✅ (`DensityCurve`) | 🟡 | ⬜ | ⬜ | ⬜ | à faire |
| D | Risque de plissement et déformation prévue du tissu (carte de risque en surimpression, flèches de déplacement amplifiées ×20, verdict chiffré, recalcul après modification) | ✅ `solve_sheet_deformation` | ✅ `predict_fabric_deformation` | ✅ commande `analysis.fabric` | — | ✅ | `core/fabric_risk` (Catch2 `[fabric]`), QTest `fabricRiskOverlay…`. **Estimation non calibrée** : le verdict le répète tant que `calibrated` est faux |
| D | Compensation de tirage manuelle / automatique / prédictive : réglages, comparaison nominal ↔ prédit, étalonnage sur pièce-test | ✅ | ✅ | 🟡 inspecteur (mode et valeurs) | ⬜ | 🟡 | comparaison nominal/prédit et assistant d'étalonnage à faire ; modèle prédictif non validé physiquement |
| D | Optimisation de l'ordre avec mesures RÉELLES avant/après (changements de fil, sauts, coupes, déplacements à vide, points), compromis signalé, proposition périmée refusée, 1 pas d'annulation | ✅ `optimize_order` | 🟡 pas d'opération batch dédiée | ✅ dock « Optimisation » + commande `optimize.order` | — | ✅ | `core/path_optimizer` (Catch2 `[path]`, 7 cas : la séquence réelle après acceptation donne bien les mesures annoncées), QTest `orderOptimisationShows…` |
| D | Optimisation : surimpression des sauts avant/après sur le canevas, départ/arrivée du motif, tri par calque | ✅ (`jump_segments`, `finishing`) | ⬜ | ⬜ | ⬜ | ⬜ | à faire |
| D | Carte de densité | ✅ | ⬜ | ✅ | — | ✅ | #265 |
| D | Simulation de couture | ✅ | — | ✅ | — | ✅ | vitesse réglable, sons ⬜ |
| E | Pont live : un agent MCP modifie le document OUVERT (approbation obligatoire, une étape d'annulation, refus si le document a changé, session close au remplacement) | ✅ `LiveBridge` | ✅ `openstitch-mcp --live` | ✅ dock « Compagnon IA » + commande `companion.toggle` | — | ✅ | `core/live_companion` (Catch2 `[companion]`, 5 cas) + `ui/live_server` ; QTest bout en bout sur une vraie socket locale. Désactivé par défaut |
| E | Boîte d'approbation avec rendu avant/après, opérations, mesures et réserves | ✅ (`ApprovalRequest`) | ✅ | ✅ `askAgentApproval` | — | 🟡 réponse imposée en test, rendu des images non vérifié | |
| E | Chat intégré à V2 (envoyer un message à Claude depuis l'application) | 🟡 V1 : `claude_backend`, `claude_code_process_backend` | — | ⬜ | — | ⬜ | **non fait** : l'agent se branche aujourd'hui via un client MCP externe ; aucun faux dialogue n'a été ajouté |
| F | Identité visuelle « Atelier » (jetons clair/sombre, feuille de style, focus accessible, bascule de thème en direct) | — | — | ✅ intégrée depuis #262 | — | ✅ | QTest `studioThemeUses…` ; le « blocage » observé sur #262 seule ne se reproduit pas une fois fusionnée avec la branche actuelle (93 QTest verts). **Rendu visuel jamais vérifié à l'écran** |
| F | Micro-interactions (confirmation d'enregistrement, sélection tactile, mouvement réduit) | — | — | ✅ #263 | — | ✅ | |
| G | Benchmarks 1 / 100 / 500 / 1 000 objets | — | — | ✅ | — | ✅ | `test_perf_budget.cpp` (`[.perf]`) + `10-performances.md` : Debug ET Release : retour local p95 ≤ 0,03 ms ; en Release tous les gestes ≤ 9 ms à 1 000 objets, aperçu réaliste 98 ms en tâche de fond ; le lot de 1 000 objets (276 ms en Debug) vaut 8,9 ms en Release |

## Ce qui reste avant la PR finale
Voir la colonne « Direct/Testé » : les lignes ⬜ sont du travail restant, pas des promesses.
La PR ne sera livrée « prête pour revue » que lorsque les lignes P0 (B, C, E) auront une preuve.
