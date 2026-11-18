# RND-ZIGZAG-01 — L0 : inventaire ZigZag / OpenStitch

Statut : **L0 partiel** (références gelées, matrice issue de la fiche Notion et de greps ciblés).
Une fonctionnalité n'est marquée « présente » que si un fichier a été vu ; sinon « non vérifié ».

## Références

| Dépôt | SHA | Version | Licence | État |
| --- | --- | --- | --- | --- |
| OpenStitch | `3fade8c7cff1006969d4f042b1d303e0fb301a5a` | 0.1.0 | Apache-2.0 | build msvc Release OK, PR ouverte #375 (UI Studio, sans lien moteur) |
| ZigZag | `429a028054145fc3e541525bb05b57596fa49d22` | 0.1.5 | Apache-2.0 (+ `vendor/vectorcraft-*` MIT/Apache-2.0, `NOTICE`, `THIRD-PARTY-NOTICES.md` à relire) | `cargo build --release --locked` OK ; CLI `convert` / `info` utilisables |

## Matrice (chemins vus)

| Sujet | ZigZag | OpenStitch | Statut |
| --- | --- | --- | --- |
| Satin par découpe en colonnes | `src/stitch/columns.rs` (44 Ko) | stratégies `skeleton`, `parallel_sweep`, `medial_axis`, `guided` (`libs/auto_satin/src/satin_strategy.cpp`) ; comparaison `openstitch-cli satin-compare` | présent des deux côtés, équivalence non évaluée |
| Routage caché | `src/route.rs` (A*, `--over-sewn`) | `libs/stitch_generation/src/hidden_travel.cpp` (désactivé par défaut), `routing.cpp` | présent ; premier banc L3 non discriminant (voir benchmarks) |
| Cache de planification | `PlanCache` dans `src/design.rs` | `chunk_cache.cpp/.hpp` | présent, granularité non comparée |
| Worker de planification | `src/studio/planner.rs` | workers Studio (non relus) | non vérifié |
| Rendu GPU | `src/studio/gpu.rs` | Qt Quick RHI (non relu) | non vérifié |
| Tracé de logos | `src/trace.rs` + `vendor/vectorcraft-*` | segmentation + vectorisation | présent, A/B non fait |
| Formats | `src/formats/{dst,pes,jef_data,pen,read,more}.rs` | DST/PES/JEF/EXP/VP3 (`libs/formats`) | croisement des codecs non fait |
| Protocole machine Skitch | `src/machine/*` | absent côté OpenStitch (vu par grep ciblé) | hors périmètre |
| Import SVG → points | `convert` accepte SVG et PNG | opération `import_svg` (tracé direct, `libs/project_ops`) vue dans `project operations` et utilisée par le banc L1 « même géométrie » | présent des deux côtés |

## Limites

Les statuts « non vérifié » exigent une lecture de code dédiée (L4). Aucune validation textile.
Rien ici n'autorise de réécriture du satin, du routage ou du GPU (voir fiche Notion).
