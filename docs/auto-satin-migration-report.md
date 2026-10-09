# Rapport de migration — auto-satin par squelette (2026-10)

Branche : `claude/openstitch-patent-research-252787` (non poussée, non fusionnée sur `main`).

## Remplacé
| Ancien | Nouveau |
|---|---|
| `auto_satin::build_satin_columns` (rails/barreaux, appariement, SGSD) | `auto_satin::generate_skeleton_satin` (axe, traversées orientées, cellules) |
| `SatinParams` généré automatiquement | `document::AutoSatinParams` (`.osp` type `autoSatin`, schéma 5) |
| `satin_planning`, `satin_coverage` | diagnostics du moteur (`measure_coverage`, `SkeletonSatinDiagnostics`) |
| outil « ligne de coupe satin » | guides d'orientation (tracé, flèches déplaçables) |
| CLI `auto-satin-debug`, `sgsd-debug` | CLI `satin-auto-debug` |

## Supprimé
`libs/satin_planning`, `libs/satin_coverage`, `libs/auto_satin/src/{satin_column,medial_field,corridor,debug_export}`,
leurs en-têtes publics et tests (`test_columns`, `test_corridor`, `test_medial_field`, `test_turning_satin`,
`test_satin_column_view`, `test_coverage_regression`), `tests/golden/auto-satin`, tool `DrawSatinCutLine`.

## Conservé (justification)
- `SatinParams`, `fill_satin_columns`, `satin_guides` : satin manuel à deux rails et anciens `.osp`.
- Primitives squelette/raster/graph_cleanup/satinability/shapes : utilisées par `autodigitize` et le nouveau moteur.
- `geometry::cut_path_set` : générique, plus utilisé par le bureau (candidat à suppression ultérieure).

## Compatibilité `.osp`
Lecture de v1–v4 inchangée (satin à rails conservé tel quel, jamais converti automatiquement) ; écriture en v5.

## Tests
761 tests verts en Release (966 avant retrait des tests de l'ancien moteur). `test_main_window` réactivé.
Docs : PDF 225 pages, 0 problème.

## Non fait / à vérifier
Essais machine ; réintégration dans l'auto-numérisation (RD-PAT-002/003/004) ; densité selon la largeur
(RD-PAT-013) ; `deep_channel` ≈ 0,65 ; build `linux-core` non exécuté (poste Windows) mais aucune dépendance Qt ajoutée.
