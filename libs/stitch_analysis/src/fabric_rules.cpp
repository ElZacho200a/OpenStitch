// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/fabric_rules.hpp"

#include <string>

#include "openstitch/thread_palette/color_distance.hpp"

namespace openstitch::stitch_analysis {

double thread_fabric_distance(std::array<std::uint8_t, 3> thread,
                              std::array<std::uint8_t, 3> fabric) {
    return thread_palette::ciede2000(thread_palette::to_cielab(thread),
                                     thread_palette::to_cielab(fabric));
}

std::vector<Finding> check_thread_vs_fabric(const document::Project& project) {
    std::vector<Finding> findings;
    if (project.fabric == document::FabricSettings{}) {
        return findings; // tissu non défini (défaut) : aucun contrôle, documents existants
                         // inchangés
    }
    for (const auto& object : project.embroidery_objects) {
        if (!object.visible || !project.calqueVisible(object)) {
            continue;
        }
        const double distance = thread_fabric_distance(object.rgb, project.fabric.color);
        if (distance >= kMinThreadFabricContrast) {
            continue;
        }
        Finding finding;
        finding.severity = Severity::Warning;
        finding.category = "fil-couleur-tissu";
        finding.message = "« " + object.name +
                          " » : fil trop proche de la couleur du tissu (écart " +
                          std::to_string(static_cast<int>(distance)) + ").";
        finding.object = object.id;
        finding.hint = "Choisissez un fil plus contrasté ou changez la couleur du tissu.";
        findings.push_back(std::move(finding));
    }
    return findings;
}

} // namespace openstitch::stitch_analysis
