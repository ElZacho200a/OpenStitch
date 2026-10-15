// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::project_ops {

inline constexpr const char* kQualitySchema = "openstitch.quality/1";

struct QualityOptions {
    // Détail par objet (couverture région d'origine vs points, points courts/longs…).
    bool per_object{true};
    // Limite l'analyse détaillée à un objet de broderie (0 = tous).
    ObjectId only_object{};
};

struct QualityReport {
    std::string json;          // schéma `openstitch.quality/1`
    std::size_t errors{0};     // constats de gravité Error (hors cadre, séquence vide…)
    std::size_t warnings{0};   // constats de gravité Warning
    [[nodiscard]] bool passed() const { return errors == 0; }
};

// Rapport de contrôle qualité d'un projet, SANS mesure inventée : tout vient de
// `stitch_analysis` (`analyze_detailed` avec `options_from_project` + cadre du canevas,
// `sequence_metrics`, `project_metrics`, `check_stitch_limits`) sur la séquence EFFECTIVE
// (`effective_sequence`). S'y ajoute la couverture par objet (`stitch_analysis::
// object_coverage`, nouvelle métrique du moteur) : part de la région d'origine réellement
// balayée par les points, et part des points qui sortent de la région. Déterministe.
[[nodiscard]] Result<QualityReport> build_quality_report(const document::Project& project,
                                                         const QualityOptions& options = {});

struct QualitySvgOptions {
    bool outlines{true};          // contour des régions d'origine (vecteurs sources), en gris
    bool directions{false};       // flèches de direction de broderie (angle des rangées)
    bool findings{true};          // marqueurs des constats (points courts, longs, sauts…)
    ObjectId only_object{};       // 0 = tous les objets
};

// SVG de diagnostic (mm, Y vers le bas) : séquence effective par objet (couleur du fil),
// contours d'origine, directions de broderie, marqueurs des constats `analyze_detailed`.
// Réutilise `formats::write_svg`-équivalent en mémoire ; ne modifie pas le projet.
[[nodiscard]] Result<std::string> render_quality_svg(const document::Project& project,
                                                     const QualitySvgOptions& options = {});

} // namespace openstitch::project_ops
