# Référence des modules

Public : développeur. Ce chapitre aide à trouver **où** modifier une
fonctionnalité. Chaque module est une cible `openstitch::<nom>` sous `libs/`.

## Tableau de synthèse

| Module | Responsabilité | Dépendances | Tests |
|---|---|---|---|
| `core` | unités fortes, ids, `Result`, logging | — | `tests/unit/core` |
| `geometry` | chemins, simplification, booléens/offsets, longueur d'arc | core (+Clipper2) | `tests/unit/geometry` |
| `image` | chargement, prétraitement non destructif | core (+OpenCV) | `tests/unit/image` |
| `segmentation` | quantification CIELAB, régions connexes | core, image (+OpenCV) | `tests/unit/segmentation` |
| `vectorization` | régions → contours vectoriels | core, geometry, segmentation | `tests/unit/vectorization` |
| `document` | modèle métier (projet, objets) | core, geometry, image, segmentation | via commands/project_io |
| `stitch` | commandes machine, statistiques | core | `tests/unit/stitch` |
| `stitch_generation` | running / tatami / satin | core, geometry, stitch, document | `tests/unit/stitch` |
| `stitch_analysis` | règles de validation | core, stitch | `tests/unit/stitch_analysis` |
| `optimization` | ordre de couture | core | `tests/unit/optimization` |
| `autodigitize` | image → objets éditables | vectorization, stitch_generation | (intégration) |
| `commands` | undo/redo | document | `tests/unit/commands` |
| `formats` | codec DST, SVG diagnostic | core, stitch | `tests/unit/formats` |
| `project_io` | format `.osp` | core, document, image | `tests/unit/project_io` |

## Points d'extension

- **Ajouter un type de point** : nouvelle alternative de `StitchParams`
  (`document`), un générateur dans `stitch_generation`, une branche `std::visit`
  dans `generate.cpp`, un dialogue et une action dans `apps/desktop`.
- **Ajouter un format d'export** : une fonction dans `formats` (encapsulée
  derrière une interface interne), une sous-commande CLI et une action Fichier.
- **Ajouter une règle d'analyse** : une catégorie dans `analyze.cpp`.
- **Ajouter une opération d'image** : une alternative de `ImageOp` (`image`), un
  cas dans `apply_op`, une action menu.
- **Ajouter une commande annulable** : une classe `ICommand` dans `commands`.

## Où trouver quoi (raccourci)

| Je veux modifier… | Fichier |
|---|---|
| l'échantillonnage des points | `libs/stitch_generation/src/running_stitch.cpp`, `polyline.cpp` |
| le remplissage/routage tatami | `libs/stitch_generation/src/tatami.cpp` |
| la colonne satin | `libs/stitch_generation/src/satin.cpp` |
| l'encodage DST | `libs/formats/src/dst.cpp` |
| le format projet | `libs/project_io/src/` |
| les menus | `apps/desktop/main_window.cpp` |
| les unités | `libs/core/include/openstitch/core/units.hpp` |

## Implémentation associée

Voir les chapitres thématiques pour le détail de chaque module.
