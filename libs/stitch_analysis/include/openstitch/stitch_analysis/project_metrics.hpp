// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_analysis {

// Mesures qui demandent le projet, pas seulement la séquence (Lot G).
struct ProjectMetricsOptions {
    double small_object_mm2{3.0};  // objet brodé « trop petit »
    Micrometers short_stitch{500}; // point « court » (ventilation diagnostique)
    // Largeur de la trace d'un point pour la couverture : un pixel est
    // couvert si son centre est à moins de la moitié de cette largeur d'un
    // segment cousu. 0,5 mm couvre l'intervalle d'un tatami ou d'un satin de
    // densité 0,4 mm, pas un vide réel.
    Micrometers coverage_width{500};
    // Couleur du fond ignoré (auto-numérisation) : ses pixels ne comptent pas.
    std::optional<std::array<std::uint8_t, 3>> excluded_rgb;
};

struct ProjectMetrics {
    std::size_t embroidered_objects{0};
    // Objets vectoriels suivis (visibles) d'aire nette sous `small_object_mm2`,
    // chacun compté une fois même s'il porte plusieurs sections.
    std::size_t small_objects{0};
    // Angles de remplissage des tatami visibles (degrés entiers modulo 180)
    // -> nombre d'objets.
    std::map<int, std::size_t> fill_angles_deg;
    // Part des pixels de l'image segmentée (hors fond transparent et hors
    // `excluded_rgb`) qu'aucun point ne couvre. Absent sans segmentation.
    std::optional<double> uncovered_ratio;
    // Diagnostic : déplacements (arrivées de saut) et points courts (hors
    // verrous) ventilés par « type d'objet source/passe », ex.
    // "satin/TopStitch", "tatami/Underlay" -- pour savoir QUOI corriger.
    std::map<std::string, std::size_t> moves_by_kind;
    std::map<std::string, std::size_t> short_stitches_by_kind;
};

[[nodiscard]] ProjectMetrics project_metrics(const document::Project& project,
                                             const stitch::StitchSequence& sequence,
                                             const ProjectMetricsOptions& options = {});

} // namespace openstitch::stitch_analysis
