# Implementation Plan: S1 — Catalogue de fils (`libs/thread_palette`)

Couvre HP-THR-001, HP-THR-002, HP-THR-003. Implémente exactement l'architecture
approuvée (branche `integration`,
`specs/arch-plan/vision/20260929-110240-arm-1-ar-0.md` : AD-S1-1..4, AI-01,
S1-POLICY-1..3). Pas de re-décision de conception ci-dessous — seulement le
détail d'exécution.

## 1. Scope recap

S1 crée `openstitch::thread_palette`, une lib cœur sans Qt ni OpenCV, ne liant
que `openstitch::core`. Elle livre : (a) les types valeur `ThreadKey`/`Thread`/
`ThreadChart` et un registre en mémoire déterministe (`all_charts`,
`find_chart`, `find_by_code`, `search_by_name`) construit à partir de données
compilées dans le binaire — HP-THR-001 ; (b) deux nuanciers réels de
fabricants, Madeira Polyneon puis Isacord 40, chacun sourcé depuis la page
officielle du fabricant avec un `source_note` traçable — HP-THR-002 ; (c)
conversion sRGB→CIELAB et distance CIEDE2000 avec une recherche top-N
(`nearest_threads(rgb, chart, top_n)`) validée contre les paires de référence
Sharma/Wu/Dalal 2005 — HP-THR-003. Hors scope explicite : fil assigné par
objet (`ThreadRef` sur `EmbroideryObject`, HP-THR-004/S4), filtrage de stock
possédé et nuanciers personnels (HP-THR-007, P1), poids/type de fil
(HP-THR-009, P2), toute UI (film couleur, pipette, etc.).

## 2. Nouveaux fichiers

```
libs/thread_palette/CMakeLists.txt
libs/thread_palette/include/openstitch/thread_palette/thread.hpp
libs/thread_palette/include/openstitch/thread_palette/catalog.hpp
libs/thread_palette/include/openstitch/thread_palette/color_distance.hpp
libs/thread_palette/src/catalog.cpp
libs/thread_palette/src/chart_data.hpp          # privé, déclare les factories par nuancier
libs/thread_palette/src/color_distance.cpp
libs/thread_palette/data/madeira_polyneon.cpp   # output 0
libs/thread_palette/data/isacord_40.cpp         # output 1

tests/unit/thread_palette/CMakeLists.txt
tests/unit/thread_palette/test_catalog.cpp
tests/unit/thread_palette/test_color_distance.cpp
```

Points de contact (additifs, une ligne chacun, gabarit = `libs/geometry` /
`tests/unit/geometry`) :
- `CMakeLists.txt` racine : `add_subdirectory(libs/thread_palette)` juste après
  `add_subdirectory(libs/geometry)` (ligne 32), avant `libs/image`.
- `tests/unit/CMakeLists.txt` : `add_subdirectory(thread_palette)` juste après
  `add_subdirectory(geometry)` (ligne 2).

`libs/thread_palette/CMakeLists.txt` (gabarit verbatim de
`libs/geometry/CMakeLists.txt`, aucun `find_package` tiers) :
```cmake
add_library(openstitch_thread_palette
    src/catalog.cpp
    src/color_distance.cpp
    data/madeira_polyneon.cpp
    data/isacord_40.cpp
)
add_library(openstitch::thread_palette ALIAS openstitch_thread_palette)
target_include_directories(openstitch_thread_palette PUBLIC include)
target_link_libraries(openstitch_thread_palette
    PUBLIC openstitch::core
    PRIVATE openstitch::warnings
)
```
(C-S1-01 : aucune autre lib `openstitch::*`, aucune dépendance tierce — c'est
tout le fichier.)

`tests/unit/thread_palette/CMakeLists.txt` (gabarit =
`tests/unit/geometry/CMakeLists.txt`) :
```cmake
add_executable(test_thread_palette
    test_catalog.cpp
    test_color_distance.cpp
)
target_link_libraries(test_thread_palette PRIVATE openstitch::thread_palette Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_thread_palette)
```

## 3. Types et API

`thread.hpp` (AD-S1-1, pas de `Result<T>` ici — ce sont des types valeur,
pas des opérations faillibles) :
```cpp
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace openstitch::thread_palette {

struct ThreadKey {
    std::string chart_id;   // ex. "madeira_polyneon"
    std::string code;       // référence fabricant, ex. "1919"
    constexpr auto operator<=>(const ThreadKey&) const = default;
    bool operator==(const ThreadKey&) const = default;
};

struct Thread {
    ThreadKey key;
    std::string brand;
    std::string range;
    std::string name;
    std::array<std::uint8_t, 3> rgb{};
};

struct ThreadChart {
    std::string chart_id;
    std::string display_name;
    std::string source_note;   // S1-POLICY-2 : fabricant, gamme, URL, date
    std::vector<Thread> threads;
};

} // namespace openstitch::thread_palette
```
Note : `weight` apparaît dans le libellé libre de HP-THR-001 mais **n'est
pas** dans AD-S1-1 — délibérément reporté à HP-THR-009 (P2). Ne pas l'ajouter
ici (voir §8).

`catalog.hpp` (AD-S1-2/AD-S1-3 ; absence = "vide/optionnel", pas de nouvelle
catégorie d'`Error`, pas de `Result<T>` pour une simple recherche) :
```cpp
#pragma once
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::thread_palette {

[[nodiscard]] std::span<const ThreadChart> all_charts();
[[nodiscard]] const ThreadChart* find_chart(std::string_view chart_id) noexcept;
[[nodiscard]] std::optional<Thread> find_by_code(std::string_view chart_id, std::string_view code) noexcept;
[[nodiscard]] std::vector<Thread> search_by_name(std::string_view needle) noexcept; // sous-chaîne insensible à la casse, ordre du registre

} // namespace openstitch::thread_palette
```
`catalog.cpp` construit un `static const std::vector<ThreadChart>` local à la
fonction, une seule fois, en appelant **dans un ordre textuel fixe** une
factory par nuancier déclarée dans `src/chart_data.hpp` (privé) :
```cpp
// src/chart_data.hpp (privé, inclus seulement par catalog.cpp et data/*.cpp)
namespace openstitch::thread_palette::detail {
ThreadChart make_madeira_polyneon_chart();
ThreadChart make_isacord_40_chart();
}
```
Évite les problèmes d'ordre d'initialisation statique entre unités de
traduction et tire le déterminisme d'AD-S1-2 directement de l'ordre textuel
des appels dans `catalog.cpp`, pas d'une map auto-enregistrée.

`color_distance.hpp` (AD-S1-4, module séparé, aucune dépendance depuis
`catalog.hpp`) :
```cpp
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::thread_palette {

struct CieLab { double l; double a; double b; };

[[nodiscard]] CieLab to_cielab(std::array<std::uint8_t, 3> srgb) noexcept; // D65, observateur 2°

[[nodiscard]] double ciede2000(const CieLab& a, const CieLab& b) noexcept;

struct ThreadMatch { ThreadKey key; double distance{}; };

// Retourne exactement top_n résultats quand chart.threads.size() >= top_n,
// triés par distance ciede2000 croissante, égalités départagées par l'ordre
// de déclaration du nuancier.
[[nodiscard]] std::vector<ThreadMatch> nearest_threads(
    std::array<std::uint8_t, 3> rgb, const ThreadChart& chart, std::size_t top_n);

} // namespace openstitch::thread_palette
```
`to_cielab` est écrit depuis les formules standard sRGB→linéaire→XYZ(D65)→
CIELAB, indépendamment du `cv::cvtColor` de `libs/segmentation` (non spécifié
bit à bit, et `thread_palette` ne peut pas lier OpenCV, C-S1-01).

## 4. Sourcing des données de nuanciers

Par S1-POLICY-1/2/3 : output 0 = Madeira Polyneon, output 1 = Isacord 40,
tous deux transcrits uniquement depuis la page/le PDF de nuancier officiel du
fabricant (jamais un agrégateur tiers). Intégration : chaque nuancier est un
fichier source C++ sous `libs/thread_palette/data/` retournant un
`ThreadChart` construit depuis un tableau `static constexpr`/`static const`
de `Thread` — pas de lecture JSON/CSV à l'exécution (C-S1-02 ; dérogation
volontaire au libellé plus large de la roadmap, voir §8). `source_note` sur
chaque `ThreadChart` porte, en dur dans le binaire : fabricant, gamme, URL
source, date de consultation. `THIRD_PARTY_LICENSES.md` reçoit une nouvelle
sous-section `## Nuanciers de fils`, une ligne par nuancier, colonnes :
Fabricant | Gamme | Source (URL) | Date de consultation — à côté du tableau
de dépendances existant, pas fusionnée dedans (ce sont des faits de données,
pas des licences logicielles).

## 5. Tests (Catch2 v3, noms `TEST_CASE` ASCII)

`test_catalog.cpp` :
- `all_charts returns at least one chart`
- `all_charts order is stable across two calls`
- `every ThreadChart has a non-empty source_note`
- `find_chart resolves a known chart_id`
- `find_chart returns nullptr for an unknown chart_id`
- `find_by_code resolves a known thread in Madeira Polyneon`
- `find_by_code returns empty optional for an unknown code`
- `search_by_name is case insensitive and matches substrings`
- `search_by_name returns results in registry order`
- (output 1) `all_charts contains exactly Madeira Polyneon and Isacord in declaration order`
- (output 1) `find_by_code resolves a known thread in Isacord 40`

`test_color_distance.cpp` :
- `to_cielab matches known sRGB to CIELAB reference values` (blanc/noir/primaires, D65)
- `ciede2000 of a color against itself is zero`
- `ciede2000 matches Sharma Wu Dalal 2005 reference pairs within tolerance` (table-driven, 34 paires)
- `nearest_threads returns exactly top_n results when chart has at least top_n threads`
- `nearest_threads sorts by increasing ciede2000 distance`
- `nearest_threads breaks ties by chart declaration order`
- `nearest_threads is deterministic across repeated calls`

## 6. Docs à mettre à jour

`docs/source/palettes-and-threads.md` : retirer la phrase "La bibliothèque
`thread_palette` ... n'est pas présente dans `libs/`." du paragraphe "Ce qui
n'existe pas encore". Ajouter une section "Catalogue" (propriété exclusive de
S1) : `ThreadKey`/`Thread`/`ThreadChart`, les deux nuanciers chargés, la
procédure de sourcing (S1-POLICY-1/2/3, comment ajouter un nuancier), données
compilées (pas d'I/O à l'exécution). Mettre à jour "Implémentation associée"
avec les nouveaux fichiers `libs/thread_palette/`.

`docs/roadmap-parite-hatch.md` : passer HP-THR-001, HP-THR-002, HP-THR-003 à
`☑ Fait` au format §0.1/§0.3 (date + commit + une ligne "Livré : …" chacune),
sans toucher au texte d'une autre entrée.

## 7. Commandes de validation

```powershell
cmake --build --preset msvc-debug --target openstitch_thread_palette test_thread_palette
ctest --preset msvc-debug -R thread_palette
cmake --build --preset linux-core --target openstitch_thread_palette   # garde-fou Qt-free, GR-T0
git ls-files '*.cpp' '*.hpp' | xargs clang-format --dry-run --Werror
```

## 8. Questions ouvertes / risques

1. **Conflit roadmap/architecture — format des données** : le libellé de
   HP-THR-001 parle de "fichiers de données (JSON/CSV) embarqués" ;
   l'architecture approuvée (C-S1-02) impose des constantes C++ compilées
   pour éviter l'I/O à l'exécution. Ce plan suit l'architecture. À confirmer
   avant de passer HP-THR-001 à fait, et à clarifier dans l'entrée roadmap
   ("données embarquées = constantes C++, pas de JSON/CSV à l'exécution").
2. **Conflit roadmap/architecture — champ `weight`** : présent dans le
   libellé de HP-THR-001, absent d'AD-S1-1 (reporté à HP-THR-009, P2).
   Signalé pour qu'on ne l'ajoute pas par erreur plus tard.
3. **Forme du retour "non trouvé"** : l'architecture laisse la signature
   exacte au code-planner. Ce plan choisit `const ThreadChart*` (nullptr)
   pour `find_chart` et `std::optional<Thread>` pour `find_by_code`. À
   vérifier contre le style d'autres libs avant de verrouiller.
4. **Transcription des nuanciers non faite par ce plan** : suppose que
   Madeira et Isacord publient toujours des approximations RGB par couleur —
   à vérifier à l'implémentation ; si un fabricant ne publie plus, l'output
   correspondant passe BLOCKED et substitue un autre nuancier de la liste de
   HP-THR-002 (le registre est agnostique au nuancier choisi).
5. **Licence des données de référence CIEDE2000** : suppose que la table de
   34 paires Sharma/Wu/Dalal 2005 est republiable sous une licence compatible
   Apache-2.0 — à confirmer avant de l'embarquer dans les tests, et choisir/
   documenter une tolérance numérique.
6. **Point d'insertion CMake** (§2) est un choix de style (aucune règle
   explicite au-delà d'un ordre de dépendance approximatif) — risque faible.
