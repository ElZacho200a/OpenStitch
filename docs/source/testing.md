# Tests

Public : développeur, mainteneur.

## Structure

- **Framework** : Catch2 v3 (via vcpkg), intégré à CTest.
- **Tests unitaires** : `tests/unit/<lib>/test_*.cpp` (un exécutable par lib).
- **Test d'intégration** : `tests/integration/test_pipeline.cpp` (chaîne complète).
- **Golden (SVG de diagnostic)** : `tests/golden/stitch-generation/`.
- Total au dernier passage : **139 tests**, 100 % réussis (Debug et Release).

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
  de déplacements.
- **Running** : espacement par longueur d'arc (cercle), coins préservés, courbes
  Bézier suivies, résultats déterministes.
- **Undo/redo** : « undo total = état initial », restauration exacte des labels.
- **Projet** : aller-retour complet (image, ops, segmentation, tangentes, params).

## Ajouter un test

Ajoutez un `TEST_CASE` (nom **ASCII**) dans le `test_*.cpp` du module, ou un
nouveau fichier référencé dans le `CMakeLists.txt` du dossier de tests. Les golden
SVG ne sont **jamais** réécrits par les tests : ils se régénèrent explicitement
via `openstitch-cli stitchdebug`.

## Implémentation associée

- `tests/` (arborescence complète), `tests/unit/*/CMakeLists.txt`.
- `CMakeLists.txt` (racine) — `enable_testing`, `Catch`.
