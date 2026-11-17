# R02 — Cartographie du calcul

État au commit `beb57380` (`main`, 2026-11-17). Les références renvoient aux fichiers du
dépôt ; les durées sont celles de la [baseline](baseline.md) (tentabrode, médianes chaudes,
réglage de fils natif sauf mention).

## Chaîne image → DST

| Étape | Point d'entrée | Algorithme | Parallélisme | Coût mesuré |
|---|---|---|---|---:|
| Décodage | `image::load_image` (`libs/image/src/load.cpp`) | décodeur PNG/JPEG, RGBA → BGRA | aucun | 18,6 ms |
| Préfiltre médian | `apply_median_denoise` (`libs/image/src/ops.cpp`) | `cv::medianBlur` par bandes de 64 lignes | `parallel_for` par bande | 10,7 ms (59 ms à 1 fil) |
| Segmentation | `segmentation::segment` (`libs/segmentation/src/segmentation.cpp`) | k-means Lab sur ≤ 20 000 échantillons, affectation O(N·k), lissage par `cv::boxFilter` par classe, `cv::connectedComponents` par classe, fusion des petites régions par file de priorité | par ligne, par classe, par lot de classes | 53 ms |
| Vectorisation | `vectorize_regions` (`libs/vectorization/src/vectorize.cpp`) | `cv::findContours` RETR_CCOMP sur masque borné à la région | par région | 19 ms (somme des tâches) |
| Auto-numérisation | `auto_digitize` (`libs/autodigitize/src/autodigitize.cpp`) | choix satin/tatami par région ; squelette `thin_zhang_suen` (`libs/auto_satin/src/skeleton.cpp`, bord actif + table 256) ; distance `cv::distanceTransform` ; rognage `clip_to_cell` (`skeleton_satin.cpp`) | par région et par vecteur | 140 ms au total (squelette 112, distance 50 en somme des tâches) |
| Génération | `generate_sequence` (`libs/stitch_generation/src/generate.cpp`) | tatami par balayage, routage `HiddenRouter` (A* de visibilité) | par unité, cache de fragments par objet | 78 ms (tatami, somme des tâches) |
| Finitions | `finish_sequence_report` (`libs/stitch_generation/src/finish.cpp`) | bornes de longueur, ordre des régions et des sous-couches, points d'arrêt, coupes, **ponts cachés** (`HiddenTravelPlanner`, `hidden_travel.cpp`) | **aucun** | **68 ms** |
| Tirage prédit | `apply_predicted_pull` (`predicted_pull.cpp`) | point fixe de Jacobi, 1 à 6 passes de `PullField` | **aucun** | 0 (inactif sur le corpus) |
| Simulation du tirage | `simulate_fabric_pull` (`pull_map.cpp`) | somme gaussienne anisotrope par point via `PullField` (`pull_field.cpp`) | **aucun** | **95 ms** (marine, lin) |
| Aperçu CPU | `render_threads` (`libs/stitch_render/src/raster.cpp`) | rastérisation réaliste par bandes | 8 bandes égales, `std::thread` créés à chaque rendu, indépendant de `OPENSTITCH_THREADS` | 35 ms |
| Export | `encode_dst` (`libs/formats/src/dst.cpp`) | encodage séquentiel | aucun | ~5 ms |

L'aperçu de Studio n'utilise pas `render_threads` : points, cadre et rendu réaliste y sont des
géométries du scene graph Qt Quick sur RHI/D3D11 ([audit §12](../../performance-audit.md)). Le
rastériseur CPU sert au CLI, aux miniatures, au rendu MCP et aux feuilles de production.

## Parallélisme et synchronisation existants

- `openstitch::parallel_for` (`libs/core/include/openstitch/core/parallel.hpp`) crée ses fils
  à chaque appel (jusqu'à 16) et distribue les indices par compteur atomique. Environ dix
  appels par parcours complet ; coût fixe ~1 ms par appel d'après l'audit historique.
- Le rastériseur a son propre ensemble de fils, lui aussi recréé à chaque rendu.
- OpenCV est compilé sans backend parallèle : ses filtres sont séquentiels, la répartition
  est faite par le projet (par classe, par ligne, par bande).
- Aucun calcul n'est asynchrone dans le moteur ; Studio lance les calculs longs hors du fil
  d'interface (`BusyIndicator::run`) sur un instantané.

## Caches existants

| Cache | Clé | Borne | Invalidation |
|---|---|---|---|
| Fragments de génération (`chunk_cache.hpp`) | objet, paramètres et géométrie complets, comparés octet à octet | taille bornée | contenu |
| Squelettes auto-satin (`skeleton_satin.cpp`) | clé exacte hachée | 64 Mio, LRU | contenu |
| PNG du projet (`project_io.cpp`) | une entrée | 1 | dimensions et contenu |
| Routeurs de liaisons tatami (`tatami.cpp`) | suit le cache de fragments | — | — |

## Constats de conception relevés par l'audit

1. **Travail séquentiel sur des données indépendantes.** `simulate_fabric_pull`,
   `compute_pull_map` et les passes de `apply_predicted_pull` évaluent chaque point
   indépendamment, sur un seul fil. *(corrigé R02)*
2. **Index et réévaluations dans `PullField`.** Index `std::map` de vecteurs, grandeurs par
   segment (longueur, direction, raideurs) recalculées à chaque échantillon, deux `exp` par
   candidat même quand le poids est d'emblée sous le seuil. *(corrigé R02)*
3. **Rastérisation de la couverture en O(L²) par point.** `HiddenTravelPlanner::stamp`
   testait toutes les cellules de la boîte englobante de chaque point (un point oblique de
   5 mm : ~2 500 cellules pour une bande de 2 à 4 cellules), avec une division et un `hypot`
   par cellule. Cette construction faisait plus de 80 % des finitions. *(corrigé R02)*
4. **Mesure trompeuse.** La portée `hidden_travel` du profil incluait la construction
   paresseuse de la grille au premier appel ; le coût semblait être celui des recherches de
   chemin (60 ms) alors qu'elles n'en coûtaient que 5. *(R02 : la construction est hors de la
   portée ; la portée `finish` reste la référence)*
5. **Fils recréés à chaque appel** (`parallel_for`, rastériseur) : coût fixe, et l'aperçu
   ignore `OPENSTITCH_THREADS`. *(non traité, voir [roadmap](roadmap.md))*
6. **Segmentation en O(k·N)** : un masque plein et une passe de composantes connexes par
   classe, `k` cartes `CV_32F` pour le lissage. *(non traité)*
7. **Régénération complète** de la séquence à chaque mutation hors cache de fragments ; les
   finitions et le tirage s'appliquent à toute la séquence. *(partiellement traité par le
   cache par objet existant ; voir [roadmap](roadmap.md))*
