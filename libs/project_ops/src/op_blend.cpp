// SPDX-License-Identifier: Apache-2.0
// Fondus à parts lues dans l'image (2026-11-17) : opération `image_blend`. Le calcul vit dans
// `project_ops::make_image_blend_command` (partagé avec Studio) ; ce fichier n'en fait que
// l'adaptateur JSON du langage d'opérations.
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "op_registry.hpp"
#include "openstitch/document/multi_blend.hpp"
#include "openstitch/project_ops/gradient_fill.hpp"
#include "openstitch/project_ops/inspect.hpp"

namespace openstitch::project_ops::detail {

namespace {

using json = nlohmann::json;

OpSpec op_image_blend() {
    OpSpec s;
    s.name = "image_blend";
    s.summary = "Fondu dont la part de chaque fil est lue dans l'image source (ciel, eau, brume) : "
                "le remplissage devient un fondu multicolore complémentaire (ou le fondu existant "
                "est recalculé), couleurs proposées ou imposées.";
    s.args = {
        {"object", "object_id", "", "remplissage (tatami, directionnel) ou fondu existant", true,
         ""},
        {"threads", "integer", "", "nombre de fils proposés si « colors » est absent (défaut 4)",
         false, "[2 ; 8]"},
        {"colors", "array", "", "couleurs imposées « #RRGGBB », dans l'ordre de couture", false,
         "2 à 8"},
        {"cell_mm", "number", "mm", "pas de la grille de parts (défaut 1)", false, "[0.5 ; 5]"},
        {"locality", "number", "",
         "pénalité de localité du mélange (défaut 0,05 ; plus fort = fils voisins plus proches)",
         false, "[0 ; 2]"},
        {"angle", "number", "deg", "direction des points (défaut : celle du fondu)", false, ""},
        {"continuous", "boolean", "",
         "rangées continues par fil, écart selon sa part (peu de coupes) au lieu de la grille "
         "partagée (couverture exacte)",
         false, ""},
        {"min_share", "number", "", "part minimale d'un fil en un point (défaut 0)", false,
         "[0 ; 0.5]"},
        {"max_mix", "integer", "", "fils mélangés au plus en un point (défaut 4)", false,
         "[2 ; 4]"}};
    s.example = R"({"op":"image_blend","object":2,"threads":4})";
    s.apply = [](OpContext& ctx) -> OpStatus {
        const auto& args = ctx.args;
        const ObjectId id{args.at("object").get<std::uint64_t>()};
        if (ctx.project.findEmbroidery(id) == nullptr) {
            return OpStatus::failure(code::kUnknownObject, "objet de broderie introuvable");
        }
        ImageBlendRequest request;
        if (const auto it = args.find("threads"); it != args.end() && !it->is_null()) {
            const auto n = it->get<std::int64_t>();
            if (n < 2 || n > 8) {
                return OpStatus::failure(code::kOutOfRange, "argument « threads » hors plage",
                                         "[2 ; 8]");
            }
            request.threads = static_cast<std::size_t>(n);
        }
        if (const auto it = args.find("colors"); it != args.end() && !it->is_null()) {
            if (!it->is_array() || it->size() < 2 || it->size() > 8) {
                return OpStatus::failure(code::kOutOfRange, "« colors » : 2 à 8 couleurs",
                                         R"(["#1A2B3C","#D0A060"])");
            }
            for (const auto& c : *it) {
                const auto rgb = c.is_string() ? rgb_from_hex(c.get<std::string>()) : std::nullopt;
                if (!rgb) {
                    return OpStatus::failure(code::kUsage, "couleur invalide dans « colors »",
                                             "format #RRGGBB");
                }
                request.colors.push_back(*rgb);
            }
        }
        if (const auto it = args.find("cell_mm"); it != args.end() && !it->is_null()) {
            const double mm = it->get<double>();
            if (!std::isfinite(mm) || mm < 0.5 || mm > 5.0) {
                return OpStatus::failure(code::kOutOfRange, "argument « cell_mm » hors plage",
                                         "[0.5 ; 5]");
            }
            request.cell = Micrometers{static_cast<std::int32_t>(std::lround(mm * 1000.0))};
        }
        if (const auto it = args.find("locality"); it != args.end() && !it->is_null()) {
            const double v = it->get<double>();
            if (!std::isfinite(v) || v < 0.0 || v > 2.0) {
                return OpStatus::failure(code::kOutOfRange, "argument « locality » hors plage",
                                         "[0 ; 2]");
            }
            request.locality = v;
        }
        if (const auto it = args.find("angle"); it != args.end() && !it->is_null()) {
            const double deg = it->get<double>();
            if (!std::isfinite(deg)) {
                return OpStatus::failure(code::kOutOfRange, "argument « angle » invalide");
            }
            request.stitch_angle = Angle{deg * 3.14159265358979323846 / 180.0};
        }
        if (const auto it = args.find("min_share"); it != args.end() && !it->is_null()) {
            const double v = it->get<double>();
            if (!std::isfinite(v) || v < 0.0 || v > 0.5) {
                return OpStatus::failure(code::kOutOfRange, "argument « min_share » hors plage",
                                         "[0 ; 0.5]");
            }
            request.min_share = v;
        }
        if (const auto it = args.find("max_mix"); it != args.end() && !it->is_null()) {
            const auto v = it->get<std::int64_t>();
            if (v < 2 || v > 4) {
                return OpStatus::failure(code::kOutOfRange, "argument « max_mix » hors plage",
                                         "[2 ; 4]");
            }
            request.max_threads_per_point = static_cast<std::size_t>(v);
        }
        if (const auto it = args.find("continuous"); it != args.end() && !it->is_null()) {
            request.continuous_rows = it->get<bool>();
        }
        auto command = make_image_blend_command(ctx.project, id, request);
        if (!command) {
            return OpStatus::failure(code::kRefused, command.error().message);
        }
        ctx.stack.execute(std::move(*command), ctx.project);
        const auto group = document::find_gradient_group(ctx.project, id);
        if (!group) {
            return OpStatus::failure(code::kInternal, "fondu introuvable après création");
        }
        json colors = json::array();
        json layers = json::array();
        for (const auto layer : group->layers) {
            layers.push_back(layer.value);
            if (const auto* e = ctx.project.findEmbroidery(layer)) {
                colors.push_back(rgb_to_hex(e->rgb));
            }
        }
        const auto& params = std::get<document::MulticolorGradientParams>(
            ctx.project.findEmbroidery(group->anchor)->params);
        json grid = nullptr;
        if (params.image_weights) {
            grid = {{"columns", params.image_weights->columns},
                    {"rows", params.image_weights->rows},
                    {"cell_mm", params.image_weights->cell.value / 1000.0}};
        }
        return OpStatus::success(
            {{"id", group->anchor.value}, {"layers", layers}, {"colors", colors}, {"grid", grid}});
    };
    return s;
}

} // namespace

void register_blend_ops(std::vector<OpSpec>& out) {
    out.push_back(op_image_blend());
}

} // namespace openstitch::project_ops::detail
