# Tests

Public : développeur, mainteneur.

## Structure

- **Framework** : Catch2 v3 (via vcpkg), intégré à CTest.
- **Tests unitaires** : `tests/unit/<lib>/test_*.cpp` (un exécutable par lib).
- **Test d'intégration** : `tests/integration/test_pipeline.cpp` (chaîne complète).
- **Golden (SVG de diagnostic)** : `tests/golden/stitch-generation/`.
- Total au dernier passage vérifié : **161 tests CTest**, 100 % réussis.

## Encadré de traçabilité (dernier passage vérifié)

Un simple « 161/161 » devient vite périmé ; voici le contexte exact du dernier
passage vérifié manuellement. Régénérez ces valeurs avant toute publication.

| Élément | Valeur |
|---|---|
| Commit (état du code testé) | `449c0a1` |
| Compilateur | MSVC toolset 14.50 (Visual Studio 2026) |
| CMake | 4.4.0-rc3 |
| Configurations | Debug **et** Release |
| Résultat CTest | 161 / 161 réussis |
| Tests désactivés | 0 |
| Fichiers de tests d'intégration | 1 (`tests/integration/test_pipeline.cpp`) |
| Tests sur machine réelle | 0 |
| Couverture de code | non mesurée |

Note : chaque `TEST_CASE` Catch2 est enregistré comme un test CTest (via
`catch_discover_tests`) ; le nombre d'**assertions** Catch2 est supérieur. Le
chiffre 161 compte les cas de test, pas les assertions.

## Exécution

```powershell
ctest --preset msvc-debug                 # tous les tests
ctest --preset msvc-debug -R tatami       # filtre par nom
build\msvc\tests\unit\stitch\Debug\test_stitch.exe "[nom]"   # un exécutable
```

## Correspondance modules ↔ tests

| Module | Tests |
|---|---|
| core | `tests/unit/core/test_units.cpp`, `test_ids.cpp` |
| geometry | `test_simplify.cpp`, `test_clean.cpp`, `test_offset.cpp`, `test_polyline.cpp` |
| image | `test_image_load.cpp`, `test_ops.cpp` |
| segmentation | `test_segmentation.cpp` |
| vectorization | `test_vectorize.cpp` |
| stitch / stitch_generation | `test_stats.cpp`, `test_running_stitch.cpp`, `test_generate.cpp`, `test_tatami.cpp`, `test_satin.cpp` |
| autodigitize | `test_autodigitize.cpp` |
| auto_satin | `tests/unit/auto_satin/test_pipeline.cpp` |
| stitch_analysis | `test_analyze.cpp` |
| optimization | `test_order.cpp` |
| commands | `test_undo_stack.cpp` |
| formats | `test_dst.cpp`, `test_svg.cpp` |
| project_io | `test_roundtrip.cpp` |
| intégration | `tests/integration/test_pipeline.cpp` |

## Types de vérifications notables

- **DST aller-retour** sur tous les deltas de −121 à +121, déterminisme octet à
  octet.
- **Tatami** : invariant « aucune couture ne traverse le trou » d'un anneau, peu
  de déplacements ; et « aucune couture hors région » sur une forme concave en
  **L** échantillonnée à cinq angles (filet anti-débordement).
- **Auto-numérisation** : une bande fine devient un **tatami** par défaut (satin
  naïf désactivé), et un satin uniquement si `use_naive_satin` est activé.
- **Running** : espacement par longueur d'arc (cercle), coins préservés, courbes
  Bézier suivies, résultats déterministes.
- **Undo/redo** : « undo total = état initial », restauration exacte des labels ;
  aller-retour exact des commandes d'objet (`SetFillAngle`, `SetStitchType`,
  `ConvertFillsToTatami`).
- **Projet** : aller-retour complet (image, ops, segmentation, tangentes, params).

## Ajouter un test

Ajoutez un `TEST_CASE` (nom **ASCII**) dans le `test_*.cpp` du module, ou un
nouveau fichier référencé dans le `CMakeLists.txt` du dossier de tests. Les golden
SVG ne sont **jamais** réécrits par les tests : ils se régénèrent explicitement
via `openstitch-cli stitchdebug`.

## Implémentation associée

- `tests/` (arborescence complète), `tests/unit/*/CMakeLists.txt`.
- `CMakeLists.txt` (racine) — `enable_testing`, `Catch`.
