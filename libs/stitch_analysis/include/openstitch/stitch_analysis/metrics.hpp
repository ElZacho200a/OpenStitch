// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>

#include "openstitch/core/units.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_analysis {

// Mesures de qualité d'une séquence (Lot G, audit marine plein cadre
// 2026-09-22), calculables sur un DST relu comme sur la séquence effective.
struct SequenceMetricsOptions {
    Micrometers trim_threshold{3'000}; // déplacement « long » (au-delà : coupe attendue)
    Micrometers short_stitch{500};     // point « court »
    // DST relu : les passes sont perdues. Reconnaît alors un point d'arrêt par
    // sa forme (retour exact sur le point d'il y a deux piqûres : aller-retour).
    bool infer_locks{false};
    Micrometers direction_min_length{1'000}; // points pris en compte dans l'histogramme
};

struct SequenceMetrics {
    std::size_t stitches{0};
    std::size_t moves{0}; // arrivées de saut (une suite de Jump = un déplacement)
    std::size_t long_moves_without_trim{0}; // déplacements > trim_threshold sans Trim
    std::size_t trims{0};
    std::size_t color_changes{0};
    // Points plus courts que `short_stitch` (distance depuis la position
    // précédente de l'aiguille, saut compris : une piqûre de longueur nulle à
    // l'arrivée d'un saut compte), hors points d'arrêt...
    std::size_t short_stitches{0};
    // ...et points d'arrêt courts, comptés à part.
    std::size_t short_lock_stitches{0};
    // Directions des points d'au moins `direction_min_length`, modulo 180°,
    // par tranches de 5° (indice = angle / 5).
    std::array<std::size_t, 36> direction_histogram{};
};

[[nodiscard]] SequenceMetrics sequence_metrics(const stitch::StitchSequence& sequence,
                                               const SequenceMetricsOptions& options = {});

} // namespace openstitch::stitch_analysis
