// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/predicted_pull.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

#include "openstitch/stitch_generation/pull_map.hpp"

namespace openstitch::stitch_generation {

namespace {

// Normalisation ASCII volontaire : seules les catégories et les libellés connus
// activent des profils. Les descriptions libres restent neutres.
std::string normalized(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '_' || c == ' ') {
            c = '-';
        }
    }
    return value;
}

bool is_predicted(const document::StitchParams& params) {
    return std::visit(
        [](const auto& p) {
            if constexpr (requires { p.pull_mode; }) {
                return p.pull_mode == document::PullMode::Predicted;
            } else {
                return false;
            }
        },
        params);
}

// Modulation indicative : la densité locale découle déjà du nombre de segments.
// On réduit le poids des sous-couches et celui des courts segments, sans prétendre
// convertir longueur de fil et nombre de pénétrations en une force physique.
double segment_weight(stitch::StitchPass pass, double length_um) {
    const double length_factor = std::clamp(length_um / 3000.0, 0.8, 1.2);
    return (pass == stitch::StitchPass::Underlay ? 0.55 : 1.0) * length_factor;
}

// Les colonnes satin tirent généralement différemment des remplissages.
// Ce facteur, borné et empirique, ne modifie pas les règles Adaptive.
double stitch_type_weight(const document::StitchParams& params) {
    if (std::holds_alternative<document::SatinParams>(params) ||
        std::holds_alternative<document::AutoSatinParams>(params)) {
        return 1.15;
    }
    return 1.0;
}

std::vector<PullSource> predicted_sources(const stitch::StitchSequence& sequence,
                                          const document::Project& project) {
    std::unordered_map<std::uint64_t, double> by_id;
    for (const auto& object : project.embroidery_objects) {
        by_id[object.id.value] = stitch_type_weight(object.params);
    }
    std::vector<PullSource> sources;
    const auto& cmds = sequence.commands;
    sources.reserve(cmds.size());
    for (std::size_t i = 1; i < cmds.size(); ++i) {
        const auto& a = cmds[i - 1];
        const auto& b = cmds[i];
        if (a.type != stitch::CommandType::Stitch || b.type != stitch::CommandType::Stitch ||
            a.source != b.source || b.pass == stitch::StitchPass::Travel ||
            b.pass == stitch::StitchPass::Lock || a.pos == b.pos) {
            continue;
        }
        const double length = std::hypot(static_cast<double>(b.pos.x.value - a.pos.x.value),
                                         static_cast<double>(b.pos.y.value - a.pos.y.value));
        const auto it = by_id.find(b.source.value);
        const double type_factor = it == by_id.end() ? 1.0 : it->second;
        sources.push_back({a.pos, b.pos, segment_weight(b.pass, length) * type_factor});
    }
    return sources;
}

} // namespace

FabricResponse predicted_fabric_response(const document::Project& project,
                                         const FabricResponse& base) {
    FabricResponse result = base;
    if (result.calibrated) {
        return result; // la mesure réelle prime sur toute catégorie indicative
    }
    const std::string material = normalized(project.fabric.material);
    double material_factor = 1.0;
    if (material == "jersey" || material == "knit" || material == "t-shirt") {
        material_factor = 1.20;
    } else if (material == "terry" || material == "fleece" || material == "sweat") {
        material_factor = 1.25;
    } else if (material == "felt") {
        material_factor = 0.75;
    } else if (material == "denim") {
        material_factor = 0.85;
    }
    // woven, chaîne inconnue et matière absente conservent le gain original.
    const std::string stabilizer = normalized(project.fabric.stabilizer);
    double stabilizer_factor = 1.0;
    if (stabilizer == "cut-away" || stabilizer == "cutaway" ||
        stabilizer.starts_with("cut-away-") || stabilizer.starts_with("cutaway-")) {
        stabilizer_factor = 0.80;
    } else if (stabilizer == "tear-away" || stabilizer == "tearaway" ||
               stabilizer.starts_with("tear-away-") || stabilizer.starts_with("tearaway-")) {
        stabilizer_factor = 0.92;
    } else if (stabilizer == "none" || stabilizer == "sans" || stabilizer == "no-stabilizer") {
        stabilizer_factor = 1.10;
    }
    result.gain *= material_factor * stabilizer_factor;
    return result;
}

void apply_predicted_pull(stitch::StitchSequence& sequence, const document::Project& project,
                          const FabricResponse& fabric) {
    std::vector<ObjectId> predicted;
    for (const document::EmbroideryObject& object : project.embroidery_objects) {
        if (is_predicted(object.params)) {
            predicted.push_back(object.id);
        }
    }
    if (predicted.empty()) {
        return; // aucun objet concerné : séquence strictement inchangée
    }
    std::sort(predicted.begin(), predicted.end());

    const auto& cmds = sequence.commands;
    std::vector<PullSource> sources = predicted_sources(sequence, project);
    if (sources.empty()) {
        return;
    }
    const PullField field(std::move(sources), predicted_fabric_response(project, fabric));

    // Positions d'origine échantillonnées d'abord : le champ ne voit jamais un point déjà déplacé.
    std::vector<Vec2um> shifted(cmds.size());
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        const stitch::StitchCommand& c = cmds[i];
        shifted[i] = c.pos;
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::TopStitch &&
            std::binary_search(predicted.begin(), predicted.end(), c.source)) {
            shifted[i] = c.pos + field.compensation_at(c.pos);
        }
    }
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        sequence.commands[i].pos = shifted[i];
    }
}

} // namespace openstitch::stitch_generation
