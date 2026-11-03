// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_analysis/sheet_registration.hpp"

// Prédiction du décalage entre couleurs d'un projet et contre-compensation : le solveur de la
// feuille tissu rejoué dans l'ordre de couture (`stitch_analysis::measure_registration`) sur la
// séquence EFFECTIVE, cadre = canevas. La séquence compensée n'est JAMAIS stockée dans le document
// : elle ne sert qu'à écrire un fichier machine. Estimation NON calibrée : voir «
// deformation-tissu-solveur ».
namespace openstitch::project_ops {

inline constexpr const char* kColorRegistrationSchema = "openstitch.color-registration/1";

struct ColorRegistrationOptions {
    // Vide : repris de `project.fabric.material` / `project.fabric.stabilizer`.
    std::string material;
    std::string stabilizer;
    double cell_mm{1.0};
    int max_cells_per_side{128};
    bool hoop_is_canvas{true};
    double margin_mm{10.0};
    // Surcharges facultatives (<= 0 : valeur du préréglage / par défaut).
    double thread_tension_n{0.0};
    double visible_gap_mm{0.0};
    double own_block_fraction{-1.0}; // < 0 : défaut (0,5)
    // Contre-compensation : calculée si `compensate`, à la force `strength` (0..1).
    bool compensate{false};
    double strength{1.0};
    int iterations{2};
    bool heatmap{false};
    double heatmap_px_per_cell{4.0};
};

struct ColorRegistrationReport {
    std::string json; // openstitch.color-registration/1
    std::string text;
    std::vector<std::uint8_t> heatmap_png;
    stitch_analysis::SheetRegistration registration;
    // Séquence décalée à écrire dans le fichier machine ; vide sans `compensate`.
    stitch::StitchSequence compensated;
    // Octets du DST de la séquence compensée (encodage par défaut) ; vide sans `compensate`.
    std::vector<std::uint8_t> compensated_dst;
};

[[nodiscard]] Result<ColorRegistrationReport>
predict_color_registration(const document::Project& project,
                           const ColorRegistrationOptions& options = {});

} // namespace openstitch::project_ops
