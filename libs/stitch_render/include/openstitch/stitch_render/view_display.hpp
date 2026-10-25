// SPDX-License-Identifier: Apache-2.0
#pragma once

// Réglages d'AFFICHAGE du canevas (HP-VIEW-003 passes, HP-VIEW-010 grille et guides) : de purs
// calculs sans Qt, déterministes, qui ne modifient jamais le document. L'IHM (apps/desktop)
// ne fait que les mémoriser et les dessiner.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_render {

// --- Passes de couture (HP-VIEW-003) ---

struct PassDisplay {
    bool underlay{true};
    bool top{true};
    bool travel{true};
    bool lock{true};
    bool manual{true};
    // Une teinte distincte par passe au lieu de la couleur du fil.
    bool distinct_colors{false};
    // Symboles de fonctions (entrée = cercle, coupe = triangle), HP-VIEW-003 / HP-SEQ-006.
    bool function_markers{false};

    bool operator==(const PassDisplay&) const = default;
    [[nodiscard]] bool all_passes_visible() const {
        return underlay && top && travel && lock && manual;
    }
};

[[nodiscard]] bool pass_visible(const PassDisplay& display, stitch::StitchPass pass);

// Teinte fixe d'une passe (affichage « couleurs distinctes »).
[[nodiscard]] std::array<std::uint8_t, 3> pass_tint(stitch::StitchPass pass);

// Nombre de commandes Stitch par passe, indexé par `static_cast<size_t>(StitchPass)`.
struct PassCounts {
    std::array<std::size_t, 5> stitches{};
    std::size_t jumps{0};
    std::size_t trims{0};
};
[[nodiscard]] PassCounts count_passes(const stitch::StitchSequence& sequence);

enum class FunctionKind : std::uint8_t {
    Entry, // première pénétration d'un enchaînement (cercle)
    Cut,   // coupe de fil (triangle)
};

// Position en mm, repère « écran » (Y vers le bas), comme `ThreadSegment`.
struct FunctionMarker {
    float x{0};
    float y{0};
    FunctionKind kind{FunctionKind::Entry};
    bool operator==(const FunctionMarker&) const = default;
};

// Symboles de fonctions de `sequence`, dans l'ordre de couture. Une entrée est la première
// pénétration d'un objet après le début, une coupe ou un changement de couleur ; une coupe est
// une commande Trim (à la position de la dernière pénétration si elle ne porte pas la sienne).
[[nodiscard]] std::vector<FunctionMarker>
collect_function_markers(const stitch::StitchSequence& sequence);

// --- Grille (HP-VIEW-010) ---

struct GridSettings {
    bool visible{true};
    // Pas fin en mm ; 0 = automatique (adapté au zoom).
    double step_mm{0.0};
    // Une ligne majeure tous les N pas fins (>= 1) ; ignoré en mode automatique.
    int major_every{5};
    // Couleur RGBA ; alpha 0 = couleur du thème.
    std::array<std::uint8_t, 4> rgba{0, 0, 0, 0};

    bool operator==(const GridSettings&) const = default;
};

inline constexpr double kGridStepMinMm = 0.1;
inline constexpr double kGridStepMaxMm = 100.0;

// Valeurs bornées (pas fin dans [0,1 ; 100] ou 0, major_every dans [1 ; 50]).
[[nodiscard]] GridSettings sanitized(GridSettings settings);

// Pas effectivement dessinés pour l'échelle `px_per_mm`. 0 = rien à dessiner pour ce niveau.
struct GridPlan {
    double minor_mm{0.0};
    double major_mm{0.0};
};
[[nodiscard]] GridPlan plan_grid(const GridSettings& settings, double px_per_mm);

// Multiples de `step_mm` dans [lo, hi], croissants. Vide si le pas est invalide ou si le nombre
// de lignes dépasserait `max_lines` (garde contre un pas minuscule à fort dézoom).
[[nodiscard]] std::vector<double> grid_positions(double step_mm, double lo, double hi,
                                                 std::size_t max_lines = 4000);

// --- Guides (HP-VIEW-010) ---

enum class GuideAxis : std::uint8_t {
    Vertical,   // x = position
    Horizontal, // y = position (repère physique, Y vers le haut)
};

struct Guide {
    GuideAxis axis{GuideAxis::Vertical};
    Micrometers position{};

    auto operator<=>(const Guide&) const = default;
};

// Tri (verticaux d'abord, par position croissante) et suppression des doublons.
[[nodiscard]] std::vector<Guide> normalized(std::vector<Guide> guides);

// Texte stable « v:12000;h:-5000 » (µm) ; lecture tolérante : entrées invalides ignorées,
// au plus `kMaxGuides` guides.
inline constexpr std::size_t kMaxGuides = 64;
[[nodiscard]] std::string serialize_guides(const std::vector<Guide>& guides);
[[nodiscard]] std::vector<Guide> parse_guides(std::string_view text);

} // namespace openstitch::stitch_render
