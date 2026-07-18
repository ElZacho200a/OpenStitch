# Phase 0 — Bibliothèques open source, licences et licence du projet

## 1. Analyse des bibliothèques candidates

Critères (§2 du cahier des charges) : maintenance, Windows/MSVC, licence, intégration CMake/vcpkg, valeur vs. implémentation interne. Décisions : **OUI** (MVP), **PLUS TARD** (phase identifiée), **NON** (écarté avec motif).

### Images et vision

| Bibliothèque | Licence | Windows/CMake | Décision | Justification |
|---|---|---|---|---|
| **OpenCV** (≥4.x, modules core/imgproc/imgcodecs uniquement) | Apache-2.0 | Excellent, vcpkg | **OUI** | Couvre chargement PNG/JPEG/BMP/TIFF, filtres, morphologie, contours (Suzuki-Abe), k-means, conversions Lab. Réimplémenter tout cela serait des mois de travail. Risque : taille des binaires → limiter aux 3 modules, pas de `highgui`/`dnn`. |
| stb_image | MIT/domaine public | Trivial | **NON** | Redondant avec OpenCV imgcodecs. Le garder en tête si on veut un jour un cœur sans OpenCV. |
| libpng / libjpeg-turbo / libtiff | zlib/BSD-like | Bon | **NON (direct)** | Tirés transitivement par OpenCV via vcpkg ; pas d'API directe chez nous. |

### Géométrie

| Bibliothèque | Licence | Windows/CMake | Décision | Justification |
|---|---|---|---|---|
| **Clipper2** | BSL-1.0 | Très bon, vcpkg | **OUI** | Offsets, booléens, robustesse par coordonnées entières — exactement notre besoin (compensation, sous-couches, nettoyage de polygones). Activement maintenu. Encapsulé dans `libs/geometry` (ADR-005). |
| Boost.Geometry | BSL-1.0 | Bon | **NON** | Recouvre Clipper2 avec plus de complexité ; éviter la dépendance Boost entière. |
| CGAL | GPLv3/LGPLv3 selon modules | Lourd | **NON** | Puissant mais complexité (nombres exacts, temps de compilation) non justifiée pour le MVP. Réévaluable si besoin de squelettes droits (axe médian pour satin auto, Phase 13). |
| Eigen | MPL-2.0 | Header-only | **PLUS TARD** | Pas de besoin d'algèbre linéaire dense au MVP. Éventuellement pour les champs de direction (remplissages courbes). |
| Courbes de Bézier | — | — | **Interne** | Évaluation/subdivision/aplatissement de Béziers cubiques : ~300 lignes bien testées, mieux maîtrisées en interne. |
| Triangulation (earcut, CDT) | ISC / MPL-2.0 | Header-only | **PLUS TARD** | Pas nécessaire pour le tatami scanline. Utile pour remplissages avancés. |

### Interface graphique

| Bibliothèque | Licence | Décision | Justification |
|---|---|---|---|
| **Qt 6 Widgets** | **LGPLv3** (édition open source) | **OUI** | Standard de fait pour les apps de bureau C++ ; QGraphicsView adapté au canevas ; docking natif. Obligations LGPL : **liaison dynamique** aux DLL Qt officielles, mention dans THIRD_PARTY_LICENSES, possibilité pour l'utilisateur de remplacer les DLL. Aucune obligation commerciale. Compatible avec un projet GPLv3. Modules non-LGPL de Qt (Charts, etc.) : interdits. |
| wxWidgets | wxWindows (LGPL+exception) | NON | Viable mais canevas et modernité inférieurs à Qt pour ce type d'application. |
| Dear ImGui | MIT | **PLUS TARD (outils internes)** | Utile pour des prototypes/outils de debug (visualiseur de sections satin), pas pour l'app principale. |
| SDL + UI maison | zlib | NON | Coût de développement d'une UI complète injustifiable. |

### Sérialisation, archives, infra

| Bibliothèque | Licence | Décision | Justification |
|---|---|---|---|
| **nlohmann/json** | MIT | **OUI** | Format projet + palettes. Ergonomie > RapidJSON ; les performances ne sont pas critiques ici. |
| RapidJSON / cereal / protobuf | MIT/BSD | NON | Redondants ou surdimensionnés. |
| **minizip-ng** | zlib | **OUI (Phase 10)** | Conteneur ZIP du format projet. Maintenu, vcpkg. miniz possible en repli si l'intégration pose problème. |
| **Catch2 v3** | BSL-1.0 | **OUI** | Tests expressifs, sections, générateurs ; intégration CTest simple. |
| GoogleTest / doctest | BSD/MIT | NON | Catch2 suffit ; un seul framework. |
| **spdlog** | MIT | **OUI** | Journalisation. Tire fmt. |
| **fmt** | MIT | **OUI** | Formatage ; base de spdlog. |
| **CLI11** | BSD-3 | **OUI** | Parsing d'arguments de la CLI. |

### Formats de broderie

| Bibliothèque | Licence | Décision | Justification |
|---|---|---|---|
| libembroidery (Embroidermodder) | zlib | **NON comme dépendance — OUI comme documentation** | Le cahier des charges impose de comprendre le DST nous-mêmes (§17). Le codec DST est petit (~500 lignes) et critique : implémentation interne, testée en aller-retour. libembroidery sert de référence croisée pour valider notre lecture du format (licence zlib : consultation et tests croisés autorisés ; pas de copie de code sans attribution — nous n'en copierons pas). |

## 2. Gestion des dépendances : vcpkg en mode manifeste

**Décision : vcpkg + manifeste `vcpkg.json` + baseline verrouillée** (ADR-004).

- Intégration MSVC/CMake native (`CMAKE_TOOLCHAIN_FILE`), la plus simple pour un contributeur Windows : `git clone`, `cmake --preset msvc-release`, tout est buildé.
- Versions reproductibles par `builtin-baseline` + `overrides` si besoin.
- Fonctionne aussi sous Linux pour le build CI du cœur.
- Conan écarté (courbe d'apprentissage plus raide côté contributeurs Windows), FetchContent réservé aux très petites dépendances non packagées, sous-modules Git évités.

## 3. Licence du projet

### 3.1 Analyse des candidates

| Licence | Conséquences pour ce projet |
|---|---|
| **GPLv3** | Copyleft fort : tout binaire distribué intégrant notre code doit être publié sous GPL. Garantit que le logiciel et ses dérivés restent libres. Compatible avec toutes nos dépendances (Apache-2.0 ✔ compatible GPLv3, LGPL ✔, MIT/BSD/BSL/zlib ✔). Empêche une réutilisation propriétaire du cœur. C'est le choix d'Ink/Stitch et d'Embroidermodder (versions récentes). |
| LGPLv3 | Copyleft faible : permettrait à un logiciel propriétaire de lier notre cœur. Intéressant si l'objectif était de diffuser une *bibliothèque* ; ce n'est pas l'objectif premier. |
| MPL-2.0 | Copyleft par fichier ; hybride raisonnable mais protection plus faible du projet dans son ensemble. |
| Apache-2.0 | Permissif + clause brevets ; n'empêche pas l'absorption propriétaire. |
| MIT | Permissif maximal ; même remarque. |

### 3.2 Décision (validée)

La proposition initiale était GPLv3 ; **le mainteneur a choisi une licence permissive**. Décision retenue (ADR-002) : **Apache-2.0 pour l'ensemble du dépôt**.

- Apache-2.0 plutôt que MIT pour sa **clause explicite de brevets** (protection utile sur du code algorithmique) ; à part cela, les deux sont pratiquement équivalentes.
- Compatible avec toutes les dépendances retenues (Qt reste utilisé en LGPL **liaison dynamique** ; OpenCV est elle-même Apache-2.0 ; le reste est permissif).
- Conséquence assumée : le code peut être réutilisé dans des produits propriétaires. En contrepartie, l'adoption et la contribution sont facilitées.
- Attention pour l'avenir : le code sous GPL (ex. Ink/Stitch) **ne peut pas** être porté dans ce dépôt — seules les idées d'algorithmes publiés et les formats documentés sont utilisables.
- Contributions : « inbound = outbound » (les contributions arrivent sous Apache-2.0), pas de CLA.

### 3.3 Fichiers à créer (Phase 1)

- `LICENSE` : texte Apache-2.0 intégral.
- `THIRD_PARTY_LICENSES.md` : tableau dépendance / version / licence / lien, mis à jour à chaque ajout.
- Pas d'en-têtes de licence par fichier (bruit) : un `SPDX-License-Identifier: Apache-2.0` en première ligne suffit.
