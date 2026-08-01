// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/satinability.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin {

// Barreau (rung) : segment transversal reliant les deux rails. `a` est du côté
// du rail A (gauche du sens de parcours), `b` du côté du rail B.
struct SatinRung {
    Vec2um a{};
    Vec2um b{};
};

// Géométrie d'une colonne satin ÉDITABLE : deux rails ouverts et les barreaux
// qui les découpent en intervalles correspondants. Les rails viennent des
// sections transversales de l'axe (squelette), PAS d'une découpe du contour.
struct SatinColumnGeometry {
    geometry::Path rail_a;
    geometry::Path rail_b;
    std::vector<SatinRung> rungs;
    std::uint32_t section_index{0};
    std::uint32_t section_count{1};
    std::optional<std::uint32_t> start_junction;
    std::optional<std::uint32_t> end_junction;
    double length_um{0.0};
    double mean_width_um{0.0};
    double min_width_um{0.0};
    double max_width_um{0.0};
};

struct SatinColumnsParameters {
    AutoSatinParameters analysis{};
    Micrometers station_spacing{500};       // pas d'échantillonnage de l'axe (0,5 mm)
    Micrometers rung_max_spacing{2'500};     // barreau au moins tous les 2,5 mm
    double rung_angle_threshold_deg{18.0};   // barreau si l'axe tourne au-delà
    double rung_width_ratio{0.30};           // barreau si la largeur varie au-delà
    int axis_smoothing_iterations{2};        // lissage Chaikin de l'axe
    int max_junctions{2};                    // au-delà : refus (trop complexe)
};

struct SatinColumnsResult {
    SatinabilityStatus status{SatinabilityStatus::Unsuitable};
    std::vector<SatinColumnGeometry> columns;
    std::vector<std::string> warnings;
    std::string refusal;         // non vide = refus explicite (aucune colonne)
    SatinabilityReport report;   // rapport de satinabilité (diagnostic UI)
    AutoSatinDebug debug;        // étapes intermédiaires (SVG)
};

// Construit une ou plusieurs colonnes satin depuis une région vectorielle.
// - Suitable          -> une colonne (l'axe principal) ;
// - RequiresDecomposition (Y/T) -> une colonne par branche menant à une extrémité ;
// - Ambiguous / Unsuitable      -> refus explicite (aucune colonne).
// Déterministe. Ne modifie pas la région source.
[[nodiscard]] SatinColumnsResult build_satin_columns(const geometry::PathSet& region,
                                                     const SatinColumnsParameters& params);

}  // namespace openstitch::auto_satin
