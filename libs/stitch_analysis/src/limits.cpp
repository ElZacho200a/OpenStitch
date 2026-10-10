// SPDX-License-Identifier: Apache-2.0
#include <string>
#include <variant>

#include "openstitch/stitch_analysis/analyze.hpp"

namespace openstitch::stitch_analysis {

AnalysisOptions options_from_project(const document::Project& project) {
    AnalysisOptions options;
    const document::SequenceFinishing& f = project.finishing;
    if (!f.enabled) {
        return options; // projet antérieur aux finitions : seuils historiques
    }
    options.min_stitch = f.min_stitch_length;
    options.max_stitch = f.max_stitch_length;
    options.trim_threshold = f.trim_threshold;
    return options;
}

std::vector<Finding> check_stitch_limits(const document::Project& project) {
    const AnalysisOptions limits = options_from_project(project);
    std::vector<Finding> findings;

    const auto position = [&](const document::EmbroideryObject& object) {
        if (const auto* vec = project.findObject(object.source_vector);
            vec != nullptr && !vec->paths.empty() && !vec->paths.front().outer.nodes.empty()) {
            return vec->paths.front().outer.nodes.front().pos;
        }
        return Vec2um{};
    };
    const auto check = [&](const document::EmbroideryObject& object, const char* what,
                           Micrometers value) {
        const bool tooLong = value.value > limits.max_stitch.value;
        const bool tooShort = value.value > 0 && value.value < limits.min_stitch.value;
        if (!tooLong && !tooShort) {
            return;
        }
        findings.push_back(
            {Severity::Warning, "parametre-hors-limites",
             "« " + object.name + " » : " + what + " de " + format_mm_fr(value.value) +
                 " mm, hors de la plage du projet (" + format_mm_fr(limits.min_stitch.value) +
                 " à " + format_mm_fr(limits.max_stitch.value) + " mm).",
             position(object), object.id,
             tooLong ? "Réduisez cette longueur ou activez le découpage des points trop longs "
                       "dans les options de génération."
                     : "Augmentez cette longueur : les points trop courts sont fusionnés ou "
                       "risquent de casser le fil."});
    };

    for (const document::EmbroideryObject& object : project.embroidery_objects) {
        if (!object.visible) {
            continue;
        }
        std::visit(
            [&](const auto& p) {
                using T = std::decay_t<decltype(p)>;
                if constexpr (std::is_same_v<T, document::RunningStitchParams>) {
                    check(object, "longueur de point", p.stitch_length);
                } else if constexpr (std::is_same_v<T, document::TatamiParams> ||
                                     std::is_same_v<T, document::DirectionalFillParams>) {
                    check(object, "longueur de point", p.stitch_length);
                } else if constexpr (std::is_same_v<T, document::SatinParams>) {
                    check(object, "longueur maximale de point", p.max_stitch_length);
                } else if constexpr (std::is_same_v<T, document::AutoSatinParams>) {
                    check(object, "seuil de fractionnement", p.split_threshold);
                }
            },
            object.params);
    }
    return findings;
}

} // namespace openstitch::stitch_analysis
