# Phase 0 — Stratégie de tests et qualité

## 1. Pyramide de tests

### 1.1 Tests unitaires (Catch2 v3, un exécutable par lib, CTest)

Cibles prioritaires par lib (créés en même temps que le code, jamais après coup) :

- `core` : arithmétique des unités, conversions µm↔mm exactes, ids.
- `geometry` : aire/orientation de polygones, point-dans-polygone, aplatissement de Bézier (tolérance), simplification Douglas-Peucker (cas dégénérés : 2 points, colinéaires, boucle), offsets via Clipper2 (carré, concave, avec trou), nettoyage d'auto-intersections.
- `segmentation` : median cut / k-means sur images synthétiques 4 couleurs (résultat exact attendu), régions connexes (labels stables), suppression de petites régions.
- `vectorization` : contours d'un rectangle/anneau synthétique → hiérarchie extérieur/trou correcte, sens des contours.
- `stitch_generation` : point droit (subdivision longueur max, fusion segments courts, phases), triple (motif A-B-A-B), tatami (nombre de rangées attendu pour rectangle, alternance de sens, décalage des pénétrations, forme avec trou).
- `formats/dst` : voir 05-format-dst.md §4.
- `stitch` : statistiques, bornes.
- `project_io` : sérialisation → désérialisation = identité, versions inconnues refusées proprement, fichier corrompu → erreur.
- `stitch_analysis` : chaque règle déclenchée par un cas minimal construit.
- `thread_palette` : distance perceptuelle (paires de référence), fil le plus proche.

### 1.2 Tests d'intégration (`tests/integration`)

Chaînes complètes sur images synthétiques générées par le test (pas de binaires en dépôt quand évitable) :

1. PNG 2 couleurs → quantification → régions (nombre exact attendu) ;
2. région → vecteurs → objet tatami → points → statistiques dans une fourchette ;
3. points → DST → relecture → mêmes stats ;
4. document complet → save → load → égalité structurelle ;
5. undo/redo : séquence d'opérations, invariant « undo total = document initial ».

### 1.3 Tests golden (`tests/golden`)

Fichiers de référence versionnés : rectangle, cercle, anneau (trou), triangle, forme concave, chemin en point triple, tatami rectangulaire, deux couleurs avec saut. Pour chacun : projet d'entrée + DST attendu + SVG de diagnostic attendu. Comparaison : octets DST (déterminisme), nombre de points, bornes, séquence de types de commandes. Un script `tools/regen-golden` régénère après changement volontaire (diff revu en PR).

L'**export SVG de diagnostic** (points, sauts colorés, numéros) sert à la fois d'outil de debug humain et de format de comparaison lisible dans les diffs Git.

### 1.4 Fuzzing (post-MVP, Phase 11+)

libFuzzer/AFL sur : décodeur DST, lecteur de projet, simplification de polygones. Build clang dédié.

## 2. Qualité du code

- **Warnings** : MSVC `/W4 /permissive-`, clang/GCC `-Wall -Wextra -Wconversion` ; warnings = erreurs en CI sur notre code (pas sur third_party).
- **clang-format** (fichier à la racine, style LLVM modifié 4 espaces/100 colonnes) vérifié en CI ; **clang-tidy** progressif (modernize, bugprone, readability) sur les nouvelles libs.
- **Sanitizers** : ASan/UBSan sur le build Linux CI du cœur (MSVC ASan en local si stable).
- C++20, RAII, `std::optional`/`std::variant`/`std::span`/`std::filesystem` ; `std::expected` si dispo (MSVC ✔), sinon alias interne `Result<T, Error>`.
- Erreurs : taxonomie de `Error { Category, message utilisateur, détail technique }` — catégories du §26 du cahier des charges. Les exceptions ne traversent pas les frontières de lib ; les API publiques des libs retournent `Result`.

## 3. Intégration continue (GitHub Actions)

| Job | Contenu |
|---|---|
| `windows-msvc` | vcpkg cache, CMake presets, build Debug + Release, CTest, artefact ZIP de l'app en Release |
| `linux-core` | build GCC ou Clang **des libs cœur + CLI uniquement** (garde-fou anti-dépendances Windows/Qt involontaires) + tests + ASan/UBSan |
| `lint` | clang-format --dry-run, clang-tidy sur diff |

Pas de release automatique avant stabilisation (§29). Badge CI dans le README.
