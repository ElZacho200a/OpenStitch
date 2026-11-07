# Studio — matrice de migration (source de vérité)

Base : PR #282 (shell Qt Quick + canevas GPU). Les contrôleurs QML sont des adaptateurs ;
`DocumentSession`, `SelectionModel` et `UndoStack` restent l'unique chemin d'édition (ADR 0004/0005).
Statut : ✅ migré et testé · 🟡 partiel · ⬜ à faire. Inventaire fonctionnel détaillé :
[02](02-inventaire-et-parite.md), [13](13-retards-v2-vs-v1.md), [14](14-dependances-parcours-et-visuel.md).

## Contrat contrôleur ↔ QML (lot 1)

| Membre `StudioController` | Service réutilisé | Rôle QML |
|---|---|---|
| `openProject(url)` | `project_io::load_project` (tâche) | Ctrl+O, bouton Ouvrir |
| `newProject()` | `replaceProject(Project{}, clean)` | Ctrl+N |
| `saveProject()` / `saveProjectAs(url)` | `project_io::save_project` + `markSaved` | Ctrl+S, Ctrl+Maj+S, bouton |
| `undo()` / `redo()`, `canUndo/canRedo`, `undoLabel/redoLabel` | `DocumentSession` | Ctrl+Z / Ctrl+Y, boutons |
| `moveSelection(dx, dy)` | `batchTranslate` → `applyBatch` (une étape « Déplacer ») | flèches 1 mm, Maj 5 mm |
| `dirty`, `hasSelection` | `UndoStack::isClean`, `SelectionModel` | titre « • », pastille Modifié |

## Lots

| Lot | Contenu | Statut | Validation |
|---|---|---|---|
| 1 | Nouveau, ouvrir, enregistrer (sous), sélection, déplacement clavier, undo/redo, état modifié | 🟡 | `openMoveUndoSaveReopenJourney` (test_desktop_v2_studio_shell) |
| 1 | Déplacement à la souris (glisser) sur le canevas | ⬜ | — |
| 1 | Inspecteur de base éditable (nom, couleur), liste des révisions | ⬜ | — |
| 1 | Confirmation avant perte de modifications (nouveau/ouvrir) | ⬜ | — |
| 2–7 | Images/segmentation, vectoriel, numérisation, analyse/production, compagnon, bascule | ⬜ | Voir plan de migration du prompt ; matrice par fonctionnalité à détailler au démarrage de chaque lot |

## Écarts connus
- « Enregistrer » après « Ouvrir » écrit dans le fichier d'origine sans confirmation ; aucune
  sauvegarde automatique ni récupération dans Studio (le shell Widgets les garde).
- Fichiers non `.osp` (DST, SVG…) : pas encore ouvrables dans Studio.

## Matrice par fonction (générée)
`python scripts/parity/build_studio_matrix.py` produit `parite-studio/matrice-studio.csv` : les 444
fonctions de l'inventaire V1 automatisé (#279) × l'état V2 Widgets × un statut Studio normalisé
(`ABSENT … VALIDATED`). Les statuts Studio se saisissent dans `parite-studio/statuts/<famille>.csv`
(id, statut, preuve, contrôleur, composant) ; seul `VALIDATED` compte pour la DoD
(`--dod` échoue tant qu'une ligne ne l'est pas). Les 11 fonctions « exclues » en V2 sont `BLOCKED` :
toute exclusion définitive attend l'accord du propriétaire. La matrice ne couvre que l'inventaire V1 :
les capacités V2-only et les paramètres du moteur sont à ajouter (voir ci-dessous).
