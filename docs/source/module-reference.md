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
| `stitch_generation` | running / tatami / satin / directionnel | core, geometry, stitch, document | `tests/unit/stitch` |
| `stitch_analysis` | règles de validation | core, stitch | `tests/unit/stitch_analysis` |
| `stitch_render` | rendu réaliste des points (brins de fil, ombrage, tissu, image RGBA ; sans Qt) | core, stitch | `tests/unit/stitch_render` |
| `optimization` | ordre de couture | core | `tests/unit/optimization` |
| `autodigitize` | image → objets éditables | vectorization, stitch_generation | `tests/unit/autodigitize` |
| `lettering` | texte → contours de glyphes (FreeType) → lettres satin/tatami/contour éditables ; voir `lettering.md` | core, geometry, document, auto_satin (+FreeType) | `tests/unit/lettering` |
| `auto_satin` | squelette → satinabilité → auto-satin par traversées orientées (axe, chord, guides) | core, geometry (+OpenCV) | `tests/unit/auto_satin` |
| `commands` | undo/redo | document | `tests/unit/commands` |
| `formats` | codec DST, import DXF/SVG, export DXF/SVG diagnostic | core, geometry, stitch (+pugixml) | `tests/unit/formats` |
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
| le remplissage directionnel (champ, lignes de courant, secteurs, fait main) | `libs/stitch_generation/src/directional_fill.cpp` |
| l'outil de guides de direction (canevas) | `apps/desktop/main_window_directional.cpp` |
| la colonne satin | `libs/stitch_generation/src/satin.cpp` |
| le satin par squelette et ses sections | `libs/auto_satin/src/` |
| la mesure de couverture du satin automatique | `libs/auto_satin/src/skeleton_satin.cpp` (`measure_coverage`) |
| le choix de type auto (tatami/satin), avec ou sans segmentation | `libs/autodigitize/src/autodigitize.cpp` (`auto_digitize`/`auto_digitize_vectors`) |
| l'édition (type, orientation, filtres, calques) | `apps/desktop/main_window.cpp` |
| l'encodage DST | `libs/formats/src/dst.cpp` |
| l'import SVG (contourne image/segmentation) | `libs/formats/src/svg_import.cpp` |
| le format projet | `libs/project_io/src/` |
| les menus | `apps/desktop/main_window.cpp` |
| les unités | `libs/core/include/openstitch/core/units.hpp` |
| les fichiers récents (HP-FILE-003) | `apps/desktop/recent_files.hpp/.cpp` |
| la sauvegarde automatique et la récupération après plantage (HP-FILE-004) | `apps/desktop/autosave.hpp/.cpp` |

## Implémentation associée

Voir les chapitres thématiques pour le détail de chaque module.

## Préférences et données applicatives persistantes (`apps/desktop`)

Deux emplacements distincts, à ne pas confondre :

- `QSettings` (`ai_preferences.hpp`, `recent_files.hpp`, géométrie/état des
  panneaux dans `main_window.cpp`) : petites préférences clé-valeur, namespace
  par préfixe (`ui/*`, `ai/*`, `recent/*`).
- `QStandardPaths::AppDataLocation` : dossier de données applicatives
  persistantes, distinct de `QSettings`. Premier usage dans
  `apps/desktop/autosave.hpp/.cpp` (HP-FILE-004, sous-dossier `autosave/`) —
  le seul précédent dans `apps/desktop` avant cette entrée,
  `ai_segmentation_dialog.cpp`, n'utilise que `QStandardPaths::TempLocation`
  (fichiers de travail éphémères, pas de persistance voulue). Un périmètre
  futur qui a besoin d'un dossier de données applicatives persistant (pas
  seulement une préférence clé-valeur) suit ce même étendard plutôt que d'en
  introduire un troisième.
