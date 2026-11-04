# 05 — Parcours utilisateurs

Pour chaque parcours : intention, interaction **cible** (idéal, sans les limites historiques), **état réel** de V2
(✅ faisable aujourd'hui dans V2 · 🟡 partiel · ⬜ pas encore) et nombre d'actions mesuré quand il l'est.

| # | Parcours | Interaction cible | État V2 | Mesure |
|---|---|---|---|---|
| 1 | Image → broderie | Ctrl+O (ou glisser-déposer) → hauteur visée dans la barre → numérisation automatique en tâche annulable → aperçu réaliste → correction dans l'arbre → Ctrl+E | ✅ | 3 actions utilisateur (ouvrir, [régler la hauteur], exporter), **0 dialogue bloquant** hors sélecteurs de fichiers (ancien desktop : dialogues d'import et de numérisation décrits par l'audit UX du 18/10 ; **non re-mesuré ici**) |
| 2 | Créer à partir de zéro | Ctrl+N → outil de dessin → objet vectoriel → type de point par défaut → points immédiats | 🟡 | **Nouveau projet, rectangle / ellipse / polygone au glisser ou aux clics, points immédiats (tatami), sélection, inspecteur, annulation ✅** (tests `drawingToolsCreateObjectsFromMouseGestures`, `polygonToolClicks…`) ; Bézier, main levée, édition de nœuds ⬜ |
| 3 | Importer un vectoriel et numériser | glisser un SVG → objets → choix du type par lot → aperçu | ⬜ | opération `import_svg` existe côté lot |
| 4 | Modifier une broderie existante | ouvrir `.osp` → sélectionner au canevas ou dans l'arbre → inspecteur | 🟡 | ouverture ✅, arbre ✅, **sélection au canevas ✅**, **inspecteur (4 réglages) ✅**, **déplacer / mettre à l'échelle à la souris ✅** ; paramètres de points ⬜ |
| 5 | Optimisation automatique | un bouton « Optimiser » avec aperçu avant/après (`previewBatch`) puis application = 1 annulation | ⬜ | op `optimize_order` existe ; `previewBatch` prêt |
| 6 | Modifier plusieurs objets | sélection multiple (arbre ✅) → inspecteur « valeurs communes / mixtes » → un lot | 🟡 | sélection multiple ✅ ; **édition multiple de type / couleur / visibilité / verrou en une annulation ✅** ; autres paramètres ⬜ |
| 7 | Satin complexe | rails et guides en manipulation directe, aperçu du satin en direct | ⬜ | |
| 8 | Fondu entre deux fils | outil de transition visuel sur le canevas, paramètres en inspecteur | ⬜ | voir `docs/audit-fondu-ux-2026-11-03.md` |
| 9 | Déformation / tirage | carte de compensation en surimpression, curseurs, aperçu non destructif | ⬜ | |
| 10 | Diagnostic de brodabilité | liste de défauts cliquables → sélection + zoom sur l'objet → correction proposée | 🟡 | **liste de constats cliquables (sélection + centrage), synthèse chiffrée, péremption après modification ✅** ; correction proposée en un clic ⬜ |
| 11 | Préparer la production | espace Production : simulation, rendu, cadre, fils, export multi-format | 🟡 | rendu réaliste ✅ (aperçu), export DST ✅ ; reste ⬜ |
| 12 | Claude modifie en direct | session live, aperçu avant/après, approbation, annulation en un pas | 🟡 | primitive `previewBatch`/`applyBatch` ✅ ; panneau et session ⬜ |

## Parcours 1 — preuve

Image `tests/fixtures/tentabrode.png`, hauteur visée 300 mm (voir `06` pour les chiffres et `captures/` pour l'écran).
Le même scénario passe en test automatique (`imageToDstEndToEnd`, `pipeline digitizes tentabrode at 300 mm…`).

## Ce que la recette des parcours doit trancher plus tard

- Les 4 espaces sont-ils nécessaires, ou 2 suffisent-ils (« Concevoir/Numériser » et « Vérifier/Produire ») ?
  Indicateur : nombre de changements d'espace par parcours dans la recette.
- « Valeurs communes / mixtes » : affichage d'un seul champ ou de la liste ? À tester sur le parcours 6.
