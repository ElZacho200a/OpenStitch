// SPDX-License-Identifier: Apache-2.0
// Opérations de NETTOYAGE répétitif, à toutes les phases du pipeline : image (débruitage,
// segmentation), segmentation (fusion de couleurs / de régions, îlots) et formes (lissage,
// simplification, petits morceaux, petits trous). Aucun algorithme ici : chaque opération valide
// ses arguments, appelle la lib (segmentation, geometry, commands) et pousse UNE commande.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "op_registry.hpp"
#include "openstitch/commands/cleanup_ops.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/vector_ops.hpp"
#include "openstitch/image/ops.hpp"
#include "openstitch/segmentation/color_merge.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::project_ops::detail {

namespace {

using nlohmann::json;
using Rgb = std::array<std::uint8_t, 3>;

OpStatus usage(std::string m, std::string hint = {}) {
    return OpStatus::failure(code::kUsage, std::move(m), std::move(hint));
}
OpStatus out_of_range(std::string m, std::string hint = {}) {
    return OpStatus::failure(code::kOutOfRange, std::move(m), std::move(hint));
}
OpStatus refused(std::string m, std::string hint = {}) {
    return OpStatus::failure(code::kRefused, std::move(m), std::move(hint));
}

// Nombre fini dans [lo ; hi] ; `present` dit si la clé existe.
OpStatus read_number(const OpContext& ctx, const char* key, double lo, double hi, bool required,
                     double& out, bool& present) {
    present = false;
    const auto it = ctx.args.find(key);
    if (it == ctx.args.end() || it->is_null()) {
        return required ? usage(std::string("argument « ") + key + " » obligatoire (nombre)")
                        : OpStatus::success();
    }
    if (!it->is_number()) {
        return usage(std::string("argument « ") + key + " » : un nombre est attendu");
    }
    const double v = it->get<double>();
    if (!std::isfinite(v) || v < lo || v > hi) {
        return out_of_range(std::string("argument « ") + key + " » = " + std::to_string(v) +
                            " hors plage [" + std::to_string(lo) + " ; " + std::to_string(hi) +
                            "]");
    }
    out = v;
    present = true;
    return OpStatus::success();
}

OpStatus read_flag(const OpContext& ctx, const char* key, bool fallback, bool& out) {
    out = fallback;
    const auto it = ctx.args.find(key);
    if (it == ctx.args.end() || it->is_null()) {
        return OpStatus::success();
    }
    if (!it->is_boolean()) {
        return usage(std::string("argument « ") + key + " » : un booléen est attendu");
    }
    out = it->get<bool>();
    return OpStatus::success();
}

OpStatus parse_rgb(const json& j, const std::string& what, Rgb& out) {
    if (!j.is_string()) {
        return usage(what + " : texte « #RRGGBB » attendu");
    }
    std::string s = j.get<std::string>();
    if (!s.empty() && s.front() == '#') {
        s.erase(0, 1);
    }
    if (s.size() != 6 || !std::all_of(s.begin(), s.end(),
                                      [](unsigned char c) { return std::isxdigit(c) != 0; })) {
        return out_of_range(what + " : format #RRGGBB attendu");
    }
    for (std::size_t i = 0; i < 3; ++i) {
        out[i] = static_cast<std::uint8_t>(std::stoi(s.substr(i * 2, 2), nullptr, 16));
    }
    return OpStatus::success();
}

OpStatus read_rgb(const OpContext& ctx, const char* key, bool required, Rgb& out, bool& present) {
    present = false;
    const auto it = ctx.args.find(key);
    if (it == ctx.args.end() || it->is_null()) {
        return required ? usage(std::string("argument « ") + key + " » obligatoire : #RRGGBB")
                        : OpStatus::success();
    }
    if (auto st = parse_rgb(*it, std::string("argument « ") + key + " »", out); !st.ok) {
        return st;
    }
    present = true;
    return OpStatus::success();
}

std::string hex(const Rgb& c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", c[0], c[1], c[2]);
    return buf;
}

OpStatus need_segmentation(const OpContext& ctx) {
    if (!ctx.project.segmentation) {
        return refused("le projet n'a pas de segmentation",
                       "lancez d'abord `segment_image` (ou importez une image segmentée)");
    }
    return OpStatus::success();
}

OpStatus read_region(const OpContext& ctx, const char* key, RegionId& out) {
    const auto it = ctx.args.find(key);
    if (it == ctx.args.end() || !it->is_number_unsigned()) {
        return usage(std::string("argument « ") + key + " » obligatoire : identifiant de région",
                     "voir `project inspect --json` : segmentation.regions");
    }
    const RegionId id{it->get<std::uint32_t>()};
    if (ctx.project.segmentation->find(id) == nullptr) {
        return OpStatus::failure(code::kUnknownObject,
                                 "région " + std::to_string(id.value) + " introuvable",
                                 "voir `project inspect --json` : segmentation.regions");
    }
    out = id;
    return OpStatus::success();
}

json colors_json(const segmentation::ColorMergePlan& plan) {
    return json{{"recolored_regions", plan.recolors.size()},
                {"merged_regions", plan.merges.size()},
                {"colors_before", plan.colors_before},
                {"colors_after", plan.colors_after}};
}

OpStatus run_color_plan(OpContext& ctx, segmentation::ColorMergePlan plan) {
    if (plan.empty()) {
        return refused("rien à fusionner : aucune couleur ne change à ces réglages");
    }
    const json result = colors_json(plan);
    ctx.stack.execute(std::make_unique<commands::MergeColorsCommand>(std::move(plan)),
                      ctx.project);
    return OpStatus::success(result);
}

// --- Phase segmentation ---------------------------------------------------------------

OpStatus op_seg_merge_colors(OpContext& ctx) {
    if (auto st = need_segmentation(ctx); !st.ok) {
        return st;
    }
    const auto it = ctx.args.find("sources");
    if (it == ctx.args.end() || !it->is_array() || it->empty() || it->size() > 64) {
        return usage("argument « sources » obligatoire : 1 à 64 couleurs « #RRGGBB »");
    }
    std::vector<Rgb> sources;
    for (std::size_t i = 0; i < it->size(); ++i) {
        Rgb c{};
        if (auto st = parse_rgb((*it)[i], "sources[" + std::to_string(i) + "]", c); !st.ok) {
            return st;
        }
        sources.push_back(c);
    }
    Rgb target{};
    bool present = false;
    if (auto st = read_rgb(ctx, "target", true, target, present); !st.ok) {
        return st;
    }
    bool adjacent = true;
    if (auto st = read_flag(ctx, "merge_adjacent", true, adjacent); !st.ok) {
        return st;
    }
    const auto classes = segmentation::color_classes(*ctx.project.segmentation);
    for (const Rgb& s : sources) {
        const bool exists = std::any_of(classes.begin(), classes.end(),
                                        [&](const auto& c) { return c.rgb == s; });
        if (!exists) {
            return OpStatus::failure(code::kUnknownObject,
                                     "aucune région de couleur " + hex(s) + " dans la segmentation",
                                     "voir `project inspect --json` : segmentation.regions");
        }
    }
    return run_color_plan(
        ctx, segmentation::plan_merge_colors(*ctx.project.segmentation, sources, target, adjacent));
}

OpStatus op_seg_reduce_colors(OpContext& ctx) {
    if (auto st = need_segmentation(ctx); !st.ok) {
        return st;
    }
    double max_colors = 0.0;
    double max_distance = 0.0;
    bool has_colors = false;
    bool has_distance = false;
    if (auto st = read_number(ctx, "max_colors", 1, 256, false, max_colors, has_colors); !st.ok) {
        return st;
    }
    if (auto st = read_number(ctx, "max_distance", 0.1, 100, false, max_distance, has_distance);
        !st.ok) {
        return st;
    }
    if (!has_colors && !has_distance) {
        return usage("indiquez « max_colors » et/ou « max_distance » (ΔE CIEDE2000)");
    }
    bool adjacent = true;
    if (auto st = read_flag(ctx, "merge_adjacent", true, adjacent); !st.ok) {
        return st;
    }
    Rgb ignored{};
    bool has_ignored = false;
    if (auto st = read_rgb(ctx, "ignore_color", false, ignored, has_ignored); !st.ok) {
        return st;
    }
    return run_color_plan(
        ctx, segmentation::plan_reduce_colors(
                 *ctx.project.segmentation, static_cast<std::size_t>(has_colors ? max_colors : 0),
                 has_distance ? max_distance : 0.0, adjacent,
                 has_ignored ? std::optional<Rgb>{ignored} : std::nullopt));
}

OpStatus op_seg_clean_islands(OpContext& ctx) {
    if (auto st = need_segmentation(ctx); !st.ok) {
        return st;
    }
    double min_px = 0.0;
    double max_delta_e = 0.0;
    bool present = false;
    if (auto st = read_number(ctx, "min_px", 1, 1.0e7, true, min_px, present); !st.ok) {
        return st;
    }
    if (auto st = read_number(ctx, "max_delta_e", 0, 200, false, max_delta_e, present); !st.ok) {
        return st;
    }
    Rgb ignored{};
    bool has_ignored = false;
    if (auto st = read_rgb(ctx, "ignore_color", false, ignored, has_ignored); !st.ok) {
        return st;
    }
    auto command = std::make_unique<commands::CleanIslandsCommand>(
        static_cast<std::size_t>(min_px), has_ignored ? std::optional<Rgb>{ignored} : std::nullopt,
        max_delta_e);
    const auto* raw = command.get();
    const std::size_t before = ctx.project.segmentation->region_count();
    ctx.stack.execute(std::move(command), ctx.project);
    const std::size_t merged = raw->merges();
    if (merged == 0) {
        // Ne laisse aucun pas d'historique inutile : la transaction repart de l'état d'avant.
        ctx.stack.undo(ctx.project);
        return refused("aucun îlot sous " + std::to_string(static_cast<std::size_t>(min_px)) +
                       " pixels n'a pu être fusionné");
    }
    return OpStatus::success(json{{"merged_regions", merged},
                                  {"regions_before", before},
                                  {"regions_after", ctx.project.segmentation->region_count()}});
}

OpStatus op_seg_merge_regions(OpContext& ctx) {
    if (auto st = need_segmentation(ctx); !st.ok) {
        return st;
    }
    RegionId keep{};
    RegionId absorb{};
    if (auto st = read_region(ctx, "keep", keep); !st.ok) {
        return st;
    }
    if (auto st = read_region(ctx, "absorb", absorb); !st.ok) {
        return st;
    }
    if (keep == absorb) {
        return refused("« keep » et « absorb » désignent la même région");
    }
    ctx.stack.execute(std::make_unique<commands::MergeRegionsCommand>(keep, absorb), ctx.project);
    return OpStatus::success(json{{"kept", keep.value}, {"absorbed", absorb.value}});
}

OpStatus op_seg_remove_region(OpContext& ctx) {
    if (auto st = need_segmentation(ctx); !st.ok) {
        return st;
    }
    RegionId id{};
    if (auto st = read_region(ctx, "region", id); !st.ok) {
        return st;
    }
    ctx.stack.execute(std::make_unique<commands::RemoveRegionCommand>(id), ctx.project);
    return OpStatus::success(json{{"removed", id.value}});
}

OpStatus op_seg_recolor_region(OpContext& ctx) {
    if (auto st = need_segmentation(ctx); !st.ok) {
        return st;
    }
    RegionId id{};
    if (auto st = read_region(ctx, "region", id); !st.ok) {
        return st;
    }
    Rgb rgb{};
    bool present = false;
    if (auto st = read_rgb(ctx, "rgb", true, rgb, present); !st.ok) {
        return st;
    }
    ctx.stack.execute(std::make_unique<commands::RecolorRegionCommand>(id, rgb), ctx.project);
    return OpStatus::success(json{{"region", id.value}, {"rgb", hex(rgb)}});
}

// --- Phase image ----------------------------------------------------------------------

OpStatus op_image_denoise(OpContext& ctx) {
    if (!ctx.project.hasImage()) {
        return refused("le projet n'a pas d'image source");
    }
    std::string method = "median";
    if (const auto it = ctx.args.find("method"); it != ctx.args.end() && !it->is_null()) {
        if (!it->is_string()) {
            return usage("argument « method » : texte attendu (median|bilateral|flatten)");
        }
        method = it->get<std::string>();
    }
    double strength = 1.0;
    bool present = false;
    const bool median = method == "median";
    if (auto st = read_number(ctx, "strength", 1, median ? 2 : 3, false, strength, present);
        !st.ok) {
        return st;
    }
    const int level = static_cast<int>(std::lround(strength));
    image::ImageOp op;
    if (median) {
        op = image::MedianDenoiseOp{level};
    } else if (method == "bilateral") {
        op = image::BilateralDenoiseOp{level};
    } else if (method == "flatten") {
        op = image::FlattenOp{level};
    } else {
        return out_of_range("argument « method » = « " + method + " » inconnu",
                            "median | bilateral | flatten");
    }
    ctx.stack.execute(std::make_unique<commands::AppendImageOpCommand>(std::move(op)),
                      ctx.project);
    return OpStatus::success(
        json{{"method", method},
             {"strength", level},
             {"note", "la segmentation est invalidée : relancez segment_image"}});
}

OpStatus op_segment_image(OpContext& ctx) {
    if (!ctx.project.hasImage()) {
        return refused("le projet n'a pas d'image source");
    }
    double max_colors = 8.0;
    double min_region = 16.0;
    double smoothing = 0.0;
    bool present = false;
    if (auto st = read_number(ctx, "max_colors", 2, 64, false, max_colors, present); !st.ok) {
        return st;
    }
    if (auto st = read_number(ctx, "min_region_px", 1, 100000, false, min_region, present);
        !st.ok) {
        return st;
    }
    if (auto st = read_number(ctx, "smoothing_radius_px", 0, 10, false, smoothing, present);
        !st.ok) {
        return st;
    }
    // Image de travail = image d'origine + pile de prétraitements (comme le bureau).
    image::Image working = ctx.project.original;
    for (const auto& op : ctx.project.ops) {
        auto next = image::apply_op(working, op);
        if (!next) {
            return refused("prétraitement impossible : " + next.error().message);
        }
        working = std::move(*next);
    }
    auto seg = segmentation::segment(
        working, {.max_colors = static_cast<int>(std::lround(max_colors)),
                  .min_region_px = static_cast<int>(std::lround(min_region)),
                  .smoothing_radius_px = static_cast<int>(std::lround(smoothing))});
    if (!seg) {
        return refused("segmentation impossible : " + seg.error().message);
    }
    for (auto& slot : seg->region_slots) {
        if (slot) {
            if (const auto mean = segmentation::region_mean_color(*seg, working, slot->id)) {
                slot->rgb = *mean;
            }
        }
    }
    const std::size_t regions = seg->region_count();
    ctx.stack.execute(std::make_unique<commands::SetSegmentationCommand>(std::move(*seg)),
                      ctx.project);
    return OpStatus::success(json{{"regions", regions}});
}

// --- Phase formes ---------------------------------------------------------------------

// `objects` (objets de broderie) -> objets vectoriels ; absent = toutes les formes du projet.
OpStatus read_shape_targets(const OpContext& ctx, std::vector<ObjectId>& out) {
    out.clear();
    const auto it = ctx.args.find("objects");
    if (it == ctx.args.end() || it->is_null()) {
        for (const auto& v : ctx.project.vector_objects) {
            out.push_back(v.id);
        }
        return OpStatus::success();
    }
    if (!it->is_array() || it->empty() || it->size() > 1000) {
        return usage("argument « objects » : 1 à 1000 identifiants d'objets de broderie");
    }
    for (std::size_t i = 0; i < it->size(); ++i) {
        const auto& item = (*it)[i];
        const auto* emb = item.is_number_unsigned()
                              ? ctx.project.findEmbroidery(ObjectId{item.get<std::uint64_t>()})
                              : nullptr;
        if (emb == nullptr) {
            return OpStatus::failure(code::kUnknownObject,
                                     "objects[" + std::to_string(i) + "] : objet introuvable",
                                     "voir `project inspect --json` pour les identifiants");
        }
        if (std::find(out.begin(), out.end(), emb->source_vector) == out.end()) {
            out.push_back(emb->source_vector);
        }
    }
    return OpStatus::success();
}

OpStatus run_vector_cleanup(OpContext& ctx, commands::VectorOpResult made, const char* key) {
    if (!made.command) {
        return refused(made.error.empty() ? "rien à nettoyer" : made.error);
    }
    const std::size_t count = made.resulting_objects;
    ctx.stack.execute(std::move(made.command), ctx.project);
    return OpStatus::success(json{{key, count}});
}

OpStatus reshape(OpContext& ctx, commands::ReshapeKind kind) {
    std::vector<ObjectId> ids;
    if (auto st = read_shape_targets(ctx, ids); !st.ok) {
        return st;
    }
    double tol_mm = 0.1;
    bool present = false;
    if (auto st = read_number(ctx, "tolerance_mm", 0.005, 5.0, false, tol_mm, present); !st.ok) {
        return st;
    }
    return run_vector_cleanup(
        ctx,
        commands::make_reshape_paths_command(
            ctx.project, ids, kind,
            Micrometers{static_cast<std::int32_t>(std::lround(tol_mm * 1000.0))}),
        "shapes_changed");
}

OpStatus op_smooth_shapes(OpContext& ctx) {
    return reshape(ctx, commands::ReshapeKind::Smooth);
}
OpStatus op_simplify_shapes(OpContext& ctx) {
    return reshape(ctx, commands::ReshapeKind::Simplify);
}

OpStatus op_remove_small_pieces(OpContext& ctx) {
    std::vector<ObjectId> ids;
    if (auto st = read_shape_targets(ctx, ids); !st.ok) {
        return st;
    }
    double area_mm2 = 0.0;
    bool present = false;
    if (auto st = read_number(ctx, "min_area_mm2", 0.0001, 1.0e6, true, area_mm2, present);
        !st.ok) {
        return st;
    }
    return run_vector_cleanup(
        ctx, commands::make_remove_small_pieces_command(ctx.project, ids, area_mm2 * 1.0e6),
        "pieces_removed");
}

OpStatus op_fill_small_holes(OpContext& ctx) {
    std::vector<ObjectId> ids;
    if (auto st = read_shape_targets(ctx, ids); !st.ok) {
        return st;
    }
    double area_mm2 = 0.0;
    bool present = false;
    if (auto st = read_number(ctx, "max_area_mm2", 0.0001, 1.0e6, false, area_mm2, present);
        !st.ok) {
        return st;
    }
    return run_vector_cleanup(
        ctx,
        commands::make_remove_holes_command(ctx.project, ids, present ? area_mm2 * 1.0e6 : 0.0),
        "holes_removed");
}

ArgSpec objects_arg() {
    return ArgSpec{"objects", "array", "id",
                   "objets de broderie visés (défaut : toutes les formes du projet)", false,
                   "1 à 1000 objets"};
}

ArgSpec region_arg(const char* name, const char* description) {
    return ArgSpec{name, "integer", "", description, true, ""};
}

} // namespace

void register_cleanup_ops(std::vector<OpSpec>& out) {
    out.push_back(OpSpec{
        "image_denoise",
        "Phase image : ajoute un débruitage (médian, bilatéral ou aplatissement) à la pile de "
        "prétraitements ; la segmentation est invalidée.",
        {{"method", "string", "", "median (défaut) | bilateral | flatten", false,
          "median|bilateral|flatten"},
         {"strength", "integer", "", "force : 1..2 (médian) ou 1..3 (autres), défaut 1", false,
          "[1 ; 3]"}},
        R"({"op":"image_denoise","method":"flatten","strength":2})",
        op_image_denoise});

    out.push_back(OpSpec{
        "segment_image",
        "Phase segmentation : (re)calcule les régions de l'image prétraitée ; remplace les "
        "fusions et couleurs modifiées à la main.",
        {{"max_colors", "integer", "", "nombre maximal de couleurs (défaut 8)", false, "[2 ; 64]"},
         {"min_region_px", "integer", "px", "régions plus petites absorbées (défaut 16)", false,
          "[1 ; 100000]"},
         {"smoothing_radius_px", "integer", "px", "lissage des frontières (défaut 0)", false,
          "[0 ; 10]"}},
        R"({"op":"segment_image","max_colors":6,"min_region_px":30})",
        op_segment_image});

    out.push_back(OpSpec{
        "seg_merge_colors",
        "Segmentation : fait prendre à toutes les régions des couleurs `sources` la couleur "
        "`target`, puis fusionne les voisines devenues identiques.",
        {{"sources", "array", "", "couleurs « #RRGGBB » à absorber", true, "1 à 64"},
         {"target", "string", "", "couleur « #RRGGBB » résultante", true, ""},
         {"merge_adjacent", "boolean", "", "fusionner les régions voisines (défaut vrai)", false,
          ""}},
        R"({"op":"seg_merge_colors","sources":["#FE0000","#FD0101"],"target":"#FF0000"})",
        op_seg_merge_colors});

    out.push_back(OpSpec{
        "seg_reduce_colors",
        "Segmentation : réduit la palette (nombre maximal et/ou écart ΔE CIEDE2000 entre "
        "teintes proches), le plus gros aplat gardant sa teinte.",
        {{"max_colors", "integer", "", "nombre maximal de teintes", false, "[1 ; 256]"},
         {"max_distance", "number", "ΔE", "fusionne les teintes plus proches que ceci", false,
          "[0.1 ; 100]"},
         {"merge_adjacent", "boolean", "", "fusionner les régions voisines (défaut vrai)", false,
          ""},
         {"ignore_color", "string", "", "couleur de fond « #RRGGBB » laissée intacte", false, ""}},
        R"({"op":"seg_reduce_colors","max_colors":6,"max_distance":8})",
        op_seg_reduce_colors});

    out.push_back(OpSpec{
        "seg_clean_islands",
        "Segmentation : supprime les petits îlots parasites en les fusionnant dans la voisine "
        "de couleur la plus proche.",
        {{"min_px", "integer", "px", "toute région de moins de ce nombre de pixels est absorbée",
          true, "[1 ; 10000000]"},
         {"max_delta_e", "number", "ΔE",
          "refuse une voisine plus éloignée (défaut : aucune limite)", false, "[0 ; 200]"},
         {"ignore_color", "string", "", "couleur de fond « #RRGGBB » laissée intacte", false, ""}},
        R"({"op":"seg_clean_islands","min_px":40,"max_delta_e":25})",
        op_seg_clean_islands});

    out.push_back(OpSpec{
        "seg_merge_regions",
        "Segmentation : fusionne la région `absorb` dans la région `keep` (couleur de `keep` "
        "conservée).",
        {region_arg("keep", "région conservée"), region_arg("absorb", "région absorbée")},
        R"({"op":"seg_merge_regions","keep":3,"absorb":7})",
        op_seg_merge_regions});

    out.push_back(OpSpec{"seg_remove_region",
                         "Segmentation : supprime une région (absorbée par sa voisine majoritaire "
                         "ou par le fond).",
                         {region_arg("region", "région à supprimer")},
                         R"({"op":"seg_remove_region","region":7})",
                         op_seg_remove_region});

    out.push_back(OpSpec{"seg_recolor_region",
                         "Segmentation : change la couleur représentative d'une région.",
                         {region_arg("region", "région à recolorer"),
                          {"rgb", "string", "", "nouvelle couleur « #RRGGBB »", true, ""}},
                         R"({"op":"seg_recolor_region","region":3,"rgb":"#C81E1E"})",
                         op_seg_recolor_region});

    out.push_back(OpSpec{
        "smooth_shapes",
        "Formes : lisse les contours (moins de nœuds, parties arrondies en courbes) pour "
        "effacer les dentelures de la vectorisation.",
        {objects_arg(),
         {"tolerance_mm", "number", "mm", "écart maximal au tracé d'origine (défaut 0.1)", false,
          "[0.005 ; 5]"}},
        R"({"op":"smooth_shapes","tolerance_mm":0.15})",
        op_smooth_shapes});

    out.push_back(OpSpec{
        "simplify_shapes",
        "Formes : simplifie les contours en polygones à moins de sommets (Douglas-Peucker).",
        {objects_arg(),
         {"tolerance_mm", "number", "mm", "écart maximal au tracé d'origine (défaut 0.1)", false,
          "[0.005 ; 5]"}},
        R"({"op":"simplify_shapes","objects":[1,2],"tolerance_mm":0.2})",
        op_simplify_shapes});

    out.push_back(OpSpec{
        "remove_small_pieces",
        "Formes : supprime les petits morceaux disjoints (miettes, artefacts) d'aire inférieure "
        "au seuil ; une forme n'est jamais vidée.",
        {objects_arg(),
         {"min_area_mm2", "number", "mm2", "les morceaux plus petits sont supprimés", true,
          "[0.0001 ; 1000000]"}},
        R"({"op":"remove_small_pieces","min_area_mm2":0.5})",
        op_remove_small_pieces});

    out.push_back(OpSpec{
        "fill_small_holes",
        "Formes : comble les trous (tous, ou ceux dont l'aire ne dépasse pas un seuil) de "
        "plusieurs objets d'un coup.",
        {objects_arg(),
         {"max_area_mm2", "number", "mm2", "seuil d'aire des trous comblés (défaut : tous)", false,
          "[0.0001 ; 1000000]"}},
        R"({"op":"fill_small_holes","max_area_mm2":1.0})",
        op_fill_small_holes});
}

} // namespace openstitch::project_ops::detail
