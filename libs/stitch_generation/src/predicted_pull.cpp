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

bool has_pull_mode(const document::StitchParams& params) {
    return std::visit([](const auto& p) { return requires { p.pull_mode; }; }, params);
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

// Les remplissages sans réglage de tirage propre (contour, motifs, radial, spirale, hachures,
// point de croix) tirent le même tissu que les autres : dans un projet qui utilise la prédiction
// ils sont compensés eux aussi, sinon ils déformeraient le champ sans jamais être corrigés. Les
// lignes de point droit (point droit, bourdon, motif en ligne) restent exclues.
bool follows_project_prediction(const document::StitchParams& params) {
    return !has_pull_mode(params) &&
           !std::holds_alternative<document::RunningStitchParams>(params) &&
           !std::holds_alternative<document::BlanketStitchParams>(params) &&
           !std::holds_alternative<document::MotifRunParams>(params);
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
        sources.push_back({a.pos, b.pos, segment_weight(b.pass, length) * type_factor,
                           static_cast<std::uint32_t>(i)});
    }
    return sources;
}

} // namespace

FabricResponse predicted_fabric_response(const document::Project& project,
                                         const FabricResponse& base) {
    FabricResponse result = base;
    // Le sens du tissu est un réglage du projet, valable aussi pour un tissu calibré.
    result.grain_angle_rad = project.fabric.grain_angle.radians;
    result.grain_anisotropy *= project.fabric.weft_softness;
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
    bool any_predicted = false;
    for (const document::EmbroideryObject& object : project.embroidery_objects) {
        any_predicted = any_predicted || is_predicted(object.params);
    }
    for (const document::EmbroideryObject& object : project.embroidery_objects) {
        if (is_predicted(object.params) ||
            (any_predicted && follows_project_prediction(object.params))) {
            predicted.push_back(object.id);
        }
    }
    if (predicted.empty()) {
        return; // aucun objet concerné : séquence strictement inchangée
    }
    std::sort(predicted.begin(), predicted.end());

    // Compensation à point fixe : la géométrie cousue compensée tire elle aussi le tissu, et
    // chaque zone est décalée du tirage qu'elle subira au moment de sa couture (rang de
    // couture). La passe k évalue le champ de la géométrie compensée à la passe k-1 ; la
    // première passe part de la géométrie nominale. Les positions d'origine restent la cible.
    const FabricResponse response = predicted_fabric_response(project, fabric);
    const int passes = std::clamp(response.compensation_passes, 1, 6);
    const std::size_t count = sequence.commands.size();
    std::vector<Vec2um> nominal(count);
    std::vector<bool> movable(count, false);
    for (std::size_t i = 0; i < count; ++i) {
        const stitch::StitchCommand& c = sequence.commands[i];
        nominal[i] = c.pos;
        movable[i] = c.type == stitch::CommandType::Stitch &&
                     c.pass == stitch::StitchPass::TopStitch &&
                     std::binary_search(predicted.begin(), predicted.end(), c.source);
    }
    stitch::StitchSequence work = sequence;
    for (int pass = 0; pass < passes; ++pass) {
        std::vector<PullSource> sources = predicted_sources(work, project);
        if (sources.empty()) {
            return;
        }
        const PullField field(std::move(sources), response);
        // Jacobi : toutes les évaluations lisent la géométrie de la passe précédente.
        std::vector<Vec2um> next(count);
        for (std::size_t i = 0; i < count; ++i) {
            next[i] = movable[i] ? nominal[i] + field.compensation_at(work.commands[i].pos,
                                                                      static_cast<std::uint32_t>(i))
                                 : nominal[i];
        }
        for (std::size_t i = 0; i < count; ++i) {
            work.commands[i].pos = next[i];
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        sequence.commands[i].pos = work.commands[i].pos;
    }
}

} // namespace openstitch::stitch_generation
