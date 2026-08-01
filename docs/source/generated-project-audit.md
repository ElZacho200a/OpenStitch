# Annexe — Audit du dépôt

Cet audit est la **base factuelle** de toute la documentation : chaque affirmation
technique du présent document est vérifiable dans le code réel du dépôt. Il a été
établi par inspection directe de l'arborescence, des fichiers CMake, des en-têtes,
des sources, des tests et des configurations.

## Identité du projet

| Élément | Valeur | Source |
|---|---|---|
| Nom | OpenStitch Studio (temporaire, remplaçable) | `libs/core/include/openstitch/core/app_info.hpp` (`kAppName`) |
| Version | 0.1.0 | même fichier (`kAppVersion`), `CMakeLists.txt` (`project(... VERSION 0.1.0)`) |
| Licence | Apache-2.0 | `LICENSE`, `docs/phase0/03-bibliotheques-licences.md` (ADR-002) |
| Langage principal | C++23 | `CMakeLists.txt` (`CMAKE_CXX_STANDARD 23`) |
| Plateforme | Windows 10/11 (cœur portable Linux) | `CMakePresets.json` (presets `msvc`, `linux-core`) |
| Interface | Qt 6 Widgets (LGPL, liaison dynamique) | `apps/desktop/CMakeLists.txt` |
| Gestion des dépendances | vcpkg (manifeste) | `vcpkg.json`, `CMakePresets.json` |

## Exécutables produits

| Cible | Type | Fichier | Dépend de Qt |
|---|---|---|---|
| `openstitch` | Application de bureau | `apps/desktop/` | Oui |
| `openstitch-cli` | Outil en ligne de commande | `apps/cli/main.cpp` | Non |

Sous-commandes CLI réellement définies (`app.add_subcommand`, `apps/cli/main.cpp`) :
`info`, `stats`, `dst2svg`, `stitchdebug`.

## Bibliothèques internes (14)

Chaque dossier de `libs/` est une cible CMake `openstitch::<nom>` :

| Lib | Rôle | Dépendances internes clés | Encapsule |
|---|---|---|---|
| `core` | unités fortes, ids, `Result<T>`, logging | — | fmt, spdlog |
| `geometry` | chemins, simplification, booléens/offsets, longueur d'arc | core | Clipper2 |
| `image` | chargement + prétraitement non destructif | core | OpenCV |
| `segmentation` | quantification CIELAB, régions connexes | core, image | OpenCV |
| `vectorization` | régions → contours vectoriels | core, geometry, segmentation | OpenCV |
| `document` | modèle métier (projet, objets) | core, geometry, image, segmentation | — |
| `stitch` | commandes machine, statistiques | core | — |
| `stitch_generation` | running / tatami / satin | core, geometry, stitch, document | — |
| `stitch_analysis` | moteur de règles de validation | core, stitch | — |
| `optimization` | ordre de couture (coût, stratégies) | core | — |
| `autodigitize` | image → objets éditables | vectorization, stitch_generation | — |
| `auto_satin` | squelette → satinabilité (rails/rungs à venir) | core, geometry | OpenCV |
| `commands` | undo/redo (Command pattern) | document | — |
| `formats` | codec DST maison, SVG de diagnostic | core, stitch | fmt |
| `project_io` | format projet `.osp` | core, document, image | nlohmann/json, minizip-ng |

## Dépendances externes

Depuis `vcpkg.json` et `apps/desktop/CMakeLists.txt` :
`fmt`, `spdlog`, `cli11`, `clipper2`, `nlohmann-json`, `minizip-ng`,
`opencv4` (features `core`, `png`, `jpeg`, `tiff`), `catch2` (tests) ; **Qt 6.8**
hors vcpkg (binaires officiels, variable `QT_ROOT`). Voir le chapitre *Licences*.

## Principaux espaces de noms et types

- Espace racine `openstitch` ; sous-espaces `geometry`, `image`, `segmentation`,
  `vectorization`, `document`, `stitch`, `stitch_generation`, `stitch_analysis`,
  `optimization`, `autodigitize`, `commands`, `formats`, `project_io`, `desktop`.
- Unités fortes : `Micrometers`, `Millimeters`, `Pixels`, `Angle`, `Vec2um`
  (`libs/core/include/openstitch/core/units.hpp`).
- Identifiants : `ObjectId`, `RegionId`, `ColorId`, `ThreadId`, `IdGenerator<T>`
  (`libs/core/include/openstitch/core/ids.hpp`).
- Erreurs : `Result<T>` = `std::expected<T, Error>` (`.../core/error.hpp`).
- Document : `Project`, `VectorObject`, `EmbroideryObject`, `StitchParams`
  (variant `RunningStitchParams | TatamiParams | SatinParams`), `Canvas`
  (`libs/document/include/openstitch/document/`).
- Points : `StitchCommand`, `CommandType`, `StitchSequence`, `StitchStats`
  (`libs/stitch/include/openstitch/stitch/sequence.hpp`).

## Formats pris en charge

| Format | Import | Export | Module |
|---|---|---|---|
| PNG, JPEG, BMP, TIFF | Oui | — (export projet uniquement) | `image` (OpenCV) |
| DST (Tajima) | Oui | Oui | `formats/src/dst.cpp` |
| SVG (diagnostic) | — | Oui | `formats/src/svg.cpp` |
| `.osp` (projet) | Oui | Oui | `project_io` |

## Interface graphique réellement implémentée

Menus construits dans `apps/desktop/main_window.cpp` (`buildMenus`,
`buildAnalysisPanel`, `buildSimulationToolbar`, `buildOrderPanel`) :
Fichier, Édition, Image, Segmentation, Broderie, Affichage, Analyse, plus un
panneau d'ordre de couture (dock), un panneau d'analyse (dock) et une barre de
simulation. Le détail est donné dans le *Guide utilisateur détaillé*.

## Types d'objets de broderie

`RunningStitchParams` (point droit/double/triple), `TatamiParams` (remplissage),
`SatinParams` (colonne satin), portés par le variant `StitchParams`
(`libs/document/include/openstitch/document/embroidery_object.hpp`).

## Algorithmes présents (vérifiés dans le code)

Conversion RGB↔CIELAB et k-means (`segmentation`), composantes connexes 4-conn
(`segmentation`), extraction de contours Suzuki-Abe (`vectorization`),
simplification Douglas-Peucker (`geometry/simplify.cpp`), union/offset via
Clipper2 (`geometry/clean.cpp`, `geometry/offset.cpp`), aplatissement Bézier
adaptatif + longueur d'arc (`geometry/polyline.cpp`), running stitch par
longueur d'arc avec découpe aux coins (`stitch_generation/running_stitch.cpp`),
tatami scanline + routage par graphe (`stitch_generation/tatami.cpp`), satin à
deux rails (`stitch_generation/satin.cpp`), règles d'analyse
(`stitch_analysis/analyze.cpp`), ordre de couture (`optimization/order.cpp`),
codec DST (`formats/dst.cpp`).

## Tests présents

25 fichiers `test_*.cpp` sous `tests/unit/<lib>/` et un test d'intégration
`tests/integration/test_pipeline.cpp`. Total : **161 tests** au dernier passage
(`ctest`). Framework : Catch2 v3. Voir le chapitre *Tests*.

## Exemples et ressources

- SVG de diagnostic de référence : `tests/golden/stitch-generation/` (formes de
  running stitch et remplissage anneau).
- Aucun jeu d'images d'exemple versionné à la racine `examples/` à ce jour
  (*Information non déterminée dans le dépôt* : dossier `examples/` non peuplé).

## Fonctionnalités expérimentales / partielles / prévues

- **Prévu (architecture mentionnée, non implémenté)** : palette de fils réelle
  (`thread_palette`), profils de machine/cadres avancés, remplissages courbes /
  radiaux / spirale / motifs, sous-couches tatami, édition manuelle des points.
- **Partiel** : classification auto des régions (heuristique de largeur) — essaie
  désormais le vrai moteur `auto_satin` par squelette puis retombe sur le
  **tatami** ; le satin **automatique** naïf reste désactivé par défaut
  (`use_naive_satin`). Rails, barreaux et décomposition multi-sections sont
  présents. Compensation
  (satin uniquement) ; routage tatami (graphe + validation géométrique, underpath
  caché non implémenté — les liaisons hors-région sont des sauts).
- **Expérimental** : `openstitch-cli stitchdebug` / `auto-satin-debug` (outils de
  diagnostic du moteur).

## Marqueurs, incohérences

- Aucun `FIXME` critique repéré ; le `README.md` mentionne un compte de tests
  d'un jalon antérieur alors que le compte réel est **161** — incohérence mineure,
  à resynchroniser dans le README.
- Documents de conception : `docs/phase0/` (étude de cadrage, 14 ADR),
  `docs/stitch-engine-audit.md` et `docs/stitch-engine-research.md` (refonte du
  moteur de points).

## Éléments non déterminés dans le dépôt

- URL de dépôt public : aucune (dépôt local uniquement).
- Fichier de logo : aucun logo bitmap/vectoriel dédié trouvé ; la couverture du
  PDF n'en utilise donc pas.
- Couverture de code chiffrée : non configurée.
