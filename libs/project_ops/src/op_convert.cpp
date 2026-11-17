// SPDX-License-Identifier: Apache-2.0
// HP-AUTO-012 : opération `convert_vectors` -- numérise automatiquement les formes vectorielles
// (SVG importé) qui n'ont pas encore d'objet de broderie, sans passer par l'image. Le choix du
// type (tatami / contour) vient de `autodigitize::convert_vectors_to_embroidery`.
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "op_registry.hpp"
#include "openstitch/autodigitize/vector_convert.hpp"
#include "openstitch/commands/project_commands.hpp"

namespace openstitch::project_ops::detail {

namespace {

using json = nlohmann::json;

OpSpec op_convert_vectors() {
    OpSpec s;
    s.name = "convert_vectors";
    s.summary = "Numérise automatiquement les formes vectorielles sans broderie (SVG importé) : "
                "tatami pour les zones, contour pour les petites formes.";
    s.summary += " Les tatamis issus de l'image débordent ensuite de overlap_mm sous leurs voisins "
                 "(opération overlap_fills) : aucun tissu nu entre deux zones.";
    s.args = {{"overlap_mm", "number", "mm",
               "débord des tatamis sous leurs voisins brodés (défaut 0,5 ; 0 = aucun)", false,
               "[0 ; 2]"}};
    s.example = R"({"op":"convert_vectors"})";
    s.apply = [](OpContext& ctx) -> OpStatus {
        autodigitize::AutoOptions options;
        options.mm_per_px = ctx.project.imageSource().mm_per_px;
        auto next_ids = ctx.project.object_ids;
        auto built = autodigitize::convert_vectors_to_embroidery(ctx.project, next_ids, options);
        if (!built) {
            return OpStatus::failure(code::kRefused, built.error().message);
        }
        if (built->embroideries.empty()) {
            return OpStatus::failure(code::kRefused, "aucun objet de broderie produit");
        }
        json created = json::array();
        for (const auto& e : built->embroideries) {
            created.push_back(e.id.value);
        }
        const std::uint64_t first = built->embroideries.front().id.value;
        json warnings = json::array();
        for (const auto& w : built->warnings) {
            warnings.push_back(w);
        }
        json result = {{"id", first},
                       {"created", created},
                       {"tatami", built->tatami},
                       {"contour", built->contour},
                       {"other", built->other},
                       {"skipped_existing", built->already_embroidered},
                       {"warnings", warnings}};
        ctx.project.object_ids = next_ids;
        ctx.stack.execute(std::make_unique<commands::AddObjectBatchCommand>(
                              std::vector<document::VectorObject>{}, std::move(built->embroideries),
                              "Numériser les formes vectorielles"),
                          ctx.project);
        // Sans débord, contours tracés par les centres des pixels + retrait du tatami laissent un
        // liseré de tissu nu à chaque frontière : le débord fait partie de la conversion.
        double overlap_mm = 0.5;
        if (const auto it = ctx.args.find("overlap_mm"); it != ctx.args.end() && !it->is_null()) {
            if (!it->is_number() || it->get<double>() < 0.0 || it->get<double>() > 2.0) {
                return OpStatus::failure(code::kOutOfRange, "argument « overlap_mm » hors plage",
                                         "[0 ; 2]");
            }
            overlap_mm = it->get<double>();
        }
        if (overlap_mm > 0.0) {
            const auto widened = run_nested_op(ctx, "overlap_fills", {{"overlap_mm", overlap_mm}});
            if (!widened.ok) {
                return widened;
            }
            result["overlap"] = widened.result;
        }
        return OpStatus::success(result);
    };
    return s;
}

} // namespace

void register_convert_ops(std::vector<OpSpec>& out) {
    out.push_back(op_convert_vectors());
}

} // namespace openstitch::project_ops::detail
