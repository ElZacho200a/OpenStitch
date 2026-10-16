// SPDX-License-Identifier: Apache-2.0
// Opérations de forme, de transformation, de fusion/découpe et d'auto-satin du moteur
// piloté sans IHM. Chaque opération construit UNE `commands::ICommand` (annulable en un pas)
// avec les fabriques existantes ; aucun algorithme géométrique n'est réimplémenté ici.
// Repère des arguments : millimètres, centre du canevas, Y vers le haut ; conversion en
// micromètres entiers à la frontière uniquement.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "op_registry.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/transform_ops.hpp"
#include "openstitch/commands/vector_ops.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/project_ops/auto_satin_service.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"

namespace openstitch::project_ops::detail {

namespace {

using nlohmann::json;

// Coordonnée maximale acceptée en argument (mm) : très au-delà de tout cadre de broderie, très
// en deçà du dépassement du `int32` de µm.
constexpr double kMaxCoordMm = 1000.0;
// Borne des coordonnées résultantes d'une transformation (µm), marge sous 2^31.
constexpr double kMaxResultUm = 2.0e9;
constexpr std::size_t kMaxPolygonPoints = 2000;
constexpr double kMinPolygonAreaMm2 = 0.01;

OpStatus usage(std::string message, std::string hint = {}) {
    return OpStatus::failure(code::kUsage, std::move(message), std::move(hint));
}

OpStatus out_of_range(std::string message, std::string hint = {}) {
    return OpStatus::failure(code::kOutOfRange, std::move(message), std::move(hint));
}

OpStatus refused(std::string message, std::string hint = {}) {
    return OpStatus::failure(code::kRefused, std::move(message), std::move(hint));
}

// --- Lecture d'arguments --------------------------------------------------------------

// Nombre fini dans [lo ; hi] ; `present` indique si la clé existe.
OpStatus read_number(const OpContext& ctx, const char* key, double lo, double hi, bool required,
                     double& out, bool& present) {
    present = false;
    const auto it = ctx.args.find(key);
    if (it == ctx.args.end() || it->is_null()) {
        if (required) {
            return usage(std::string("argument « ") + key + " » obligatoire (nombre)");
        }
        return OpStatus::success();
    }
    if (!it->is_number()) {
        return usage(std::string("argument « ") + key + " » : un nombre est attendu");
    }
    const double v = it->get<double>();
    if (!std::isfinite(v) || v < lo || v > hi) {
        return out_of_range(std::string("argument « ") + key + " » = " + std::to_string(v) +
                                " hors plage [" + std::to_string(lo) + " ; " + std::to_string(hi) +
                                "]",
                            "");
    }
    out = v;
    present = true;
    return OpStatus::success();
}

OpStatus point_from(const json& j, const std::string& what, Vec2um& out) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) {
        return usage(what + " : un point [x, y] en millimètres est attendu");
    }
    const double x = j[0].get<double>();
    const double y = j[1].get<double>();
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > kMaxCoordMm ||
        std::abs(y) > kMaxCoordMm) {
        return out_of_range(what + " : coordonnées hors plage [-" +
                            std::to_string(static_cast<int>(kMaxCoordMm)) + " ; " +
                            std::to_string(static_cast<int>(kMaxCoordMm)) + "] mm");
    }
    out = Vec2um{to_micrometers(Millimeters{x}), to_micrometers(Millimeters{y})};
    return OpStatus::success();
}

OpStatus read_point(const OpContext& ctx, const char* key, bool required, Vec2um& out,
                    bool& present) {
    present = false;
    const auto it = ctx.args.find(key);
    if (it == ctx.args.end() || it->is_null()) {
        if (required) {
            return usage(std::string("argument « ") + key + " » obligatoire : [x, y] en mm");
        }
        return OpStatus::success();
    }
    auto st = point_from(*it, std::string("argument « ") + key + " »", out);
    present = st.ok;
    return st;
}

OpStatus read_points(const OpContext& ctx, const char* key, std::size_t min_count,
                     std::size_t max_count, std::vector<Vec2um>& out) {
    const auto it = ctx.args.find(key);
    if (it == ctx.args.end() || !it->is_array()) {
        return usage(std::string("argument « ") + key +
                     " » obligatoire : tableau de points [x, y]");
    }
    if (it->size() < min_count) {
        return usage(std::string("argument « ") + key + " » : au moins " +
                         std::to_string(min_count) + " points sont requis (" +
                         std::to_string(it->size()) + " donné(s))",
                     "");
    }
    if (it->size() > max_count) {
        return out_of_range(std::string("argument « ") + key + " » : au plus " +
                            std::to_string(max_count) + " points");
    }
    out.clear();
    out.reserve(it->size());
    for (std::size_t i = 0; i < it->size(); ++i) {
        Vec2um p;
        const auto st = point_from((*it)[i], std::string(key) + "[" + std::to_string(i) + "]", p);
        if (!st.ok) {
            return st;
        }
        out.push_back(p);
    }
    return OpStatus::success();
}

OpStatus read_enum(const OpContext& ctx, const char* key,
                   std::initializer_list<const char*> allowed, bool required, std::string& out,
                   bool& present) {
    present = false;
    const auto it = ctx.args.find(key);
    std::string list;
    for (const char* a : allowed) {
        list += (list.empty() ? "" : "|");
        list += a;
    }
    if (it == ctx.args.end() || it->is_null()) {
        if (required) {
            return usage(std::string("argument « ") + key + " » obligatoire (" + list + ")");
        }
        return OpStatus::success();
    }
    if (!it->is_string()) {
        return usage(std::string("argument « ") + key + " » : texte attendu (" + list + ")");
    }
    const std::string v = it->get<std::string>();
    for (const char* a : allowed) {
        if (v == a) {
            out = v;
            present = true;
            return OpStatus::success();
        }
    }
    return out_of_range(std::string("argument « ") + key + " » = « " + v + " » inconnu",
                        "valeurs : " + list);
}

// « #RRGGBB » ou « RRGGBB » strict.
OpStatus read_rgb(const OpContext& ctx, std::array<std::uint8_t, 3>& out, bool& present) {
    present = false;
    const auto it = ctx.args.find("rgb");
    if (it == ctx.args.end() || it->is_null()) {
        return OpStatus::success();
    }
    if (!it->is_string()) {
        return usage("argument « rgb » : texte « #RRGGBB » attendu");
    }
    std::string s = it->get<std::string>();
    if (!s.empty() && s.front() == '#') {
        s.erase(s.begin());
    }
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    if (s.size() != 6) {
        return usage("argument « rgb » = « " + it->get<std::string>() +
                     " » : format #RRGGBB attendu");
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const int hi = hex(s[2 * i]);
        const int lo = hex(s[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return usage("argument « rgb » = « " + it->get<std::string>() +
                         " » : format #RRGGBB attendu");
        }
        out[i] = static_cast<std::uint8_t>(hi * 16 + lo);
    }
    present = true;
    return OpStatus::success();
}

OpStatus read_name(const OpContext& ctx, std::string& out, bool& present) {
    present = false;
    const auto it = ctx.args.find("name");
    if (it == ctx.args.end() || it->is_null()) {
        return OpStatus::success();
    }
    if (!it->is_string() || it->get<std::string>().empty() || it->get<std::string>().size() > 128) {
        return usage("argument « name » : texte de 1 à 128 caractères attendu");
    }
    out = it->get<std::string>();
    present = true;
    return OpStatus::success();
}

// --- Cible d'une opération ------------------------------------------------------------

struct Target {
    ObjectId embroidery{};
    ObjectId vector{};
    commands::BoundsUm bounds;
};

// Objet de broderie `key` -> son objet vectoriel source (et sa boîte englobante).
OpStatus resolve_target(const OpContext& ctx, const char* key, Target& out) {
    const document::EmbroideryObject* emb = nullptr;
    auto st = require_embroidery(ctx, key, emb);
    if (!st.ok) {
        return st;
    }
    const auto* vec = ctx.project.findObject(emb->source_vector);
    if (vec == nullptr) {
        return OpStatus::failure(code::kUnknownObject,
                                 "l'objet de broderie " + std::to_string(emb->id.value) +
                                     " n'a pas d'objet vectoriel source",
                                 "projet incohérent : voir `project check`");
    }
    const auto bounds = commands::objects_bounds(ctx.project, {vec->id});
    if (!bounds) {
        return refused("l'objet " + std::to_string(emb->id.value) + " n'a aucune géométrie");
    }
    out.embroidery = emb->id;
    out.vector = vec->id;
    out.bounds = *bounds;
    return OpStatus::success();
}

json point_json(Vec2um p) {
    return json::array({to_millimeters(p.x).value, to_millimeters(p.y).value});
}

json bounds_json(const commands::BoundsUm& b) {
    return json{{"min_mm", point_json(b.min)}, {"max_mm", point_json(b.max)}};
}

std::vector<std::uint64_t> embroidery_ids(const document::Project& project) {
    std::vector<std::uint64_t> ids;
    ids.reserve(project.embroidery_objects.size());
    for (const auto& e : project.embroidery_objects) {
        ids.push_back(e.id.value);
    }
    return ids;
}

std::vector<std::uint64_t> vector_ids(const document::Project& project) {
    std::vector<std::uint64_t> ids;
    ids.reserve(project.vector_objects.size());
    for (const auto& v : project.vector_objects) {
        ids.push_back(v.id.value);
    }
    return ids;
}

// Éléments de `after` absents de `before`, dans l'ordre de `after`.
std::vector<std::uint64_t> new_ids(const std::vector<std::uint64_t>& before,
                                   const std::vector<std::uint64_t>& after) {
    std::vector<std::uint64_t> out;
    for (const auto id : after) {
        if (std::find(before.begin(), before.end(), id) == before.end()) {
            out.push_back(id);
        }
    }
    return out;
}

// --- Transformations ------------------------------------------------------------------

// Borne le résultat d'une transformation pour ne jamais déborder le `int32` de µm.
bool transform_in_range(const commands::AffineTransform& t, const commands::BoundsUm& b) {
    const std::array<Vec2um, 4> corners{b.min, b.max, Vec2um{b.min.x, b.max.y},
                                        Vec2um{b.max.x, b.min.y}};
    for (const auto& c : corners) {
        const double dx = static_cast<double>(c.x.value) - t.pivot.x.value;
        const double dy = static_cast<double>(c.y.value) - t.pivot.y.value;
        const double x = t.pivot.x.value + t.m00 * dx + t.m01 * dy + t.translation.x.value;
        const double y = t.pivot.y.value + t.m10 * dx + t.m11 * dy + t.translation.y.value;
        if (std::abs(x) > kMaxResultUm || std::abs(y) > kMaxResultUm) {
            return false;
        }
    }
    return true;
}

OpStatus run_transform(OpContext& ctx, const Target& target, const commands::AffineTransform& tr,
                       const std::string& label) {
    if (!transform_in_range(tr, target.bounds)) {
        return out_of_range(
            "la transformation sortirait l'objet de la plage des coordonnées du modèle");
    }
    auto command = commands::make_transform_command(ctx.project, {target.vector}, tr, label);
    if (!command) {
        return refused("transformation impossible : objet vectoriel introuvable");
    }
    ctx.stack.execute(std::move(command), ctx.project);
    const auto bounds = commands::objects_bounds(ctx.project, {target.vector});
    json result{{"id", target.embroidery.value}, {"vector_id", target.vector.value}};
    if (bounds) {
        result["bounds_mm"] = bounds_json(*bounds);
    }
    return OpStatus::success(std::move(result));
}

// Pivot optionnel `center_mm`, sinon centre de la boîte englobante de l'objet.
OpStatus read_pivot(const OpContext& ctx, const Target& target, Vec2um& pivot) {
    bool present = false;
    Vec2um p;
    auto st = read_point(ctx, "center_mm", false, p, present);
    if (!st.ok) {
        return st;
    }
    pivot = present ? p : target.bounds.center();
    return OpStatus::success();
}

OpStatus op_translate(OpContext& ctx) {
    Target target;
    if (auto st = resolve_target(ctx, "object", target); !st.ok) {
        return st;
    }
    double dx = 0.0;
    double dy = 0.0;
    bool present = false;
    if (auto st =
            read_number(ctx, "dx_mm", -2.0 * kMaxCoordMm, 2.0 * kMaxCoordMm, true, dx, present);
        !st.ok) {
        return st;
    }
    if (auto st =
            read_number(ctx, "dy_mm", -2.0 * kMaxCoordMm, 2.0 * kMaxCoordMm, true, dy, present);
        !st.ok) {
        return st;
    }
    const Vec2um delta{to_micrometers(Millimeters{dx}), to_micrometers(Millimeters{dy})};
    return run_transform(ctx, target, commands::AffineTransform::translate(delta), "Déplacer");
}

OpStatus op_rotate(OpContext& ctx) {
    Target target;
    if (auto st = resolve_target(ctx, "object", target); !st.ok) {
        return st;
    }
    double angle_deg = 0.0;
    bool present = false;
    if (auto st = read_number(ctx, "angle_deg", -360.0, 360.0, true, angle_deg, present); !st.ok) {
        return st;
    }
    Vec2um pivot;
    if (auto st = read_pivot(ctx, target, pivot); !st.ok) {
        return st;
    }
    const Angle angle{angle_deg * std::numbers::pi / 180.0};
    return run_transform(ctx, target, commands::AffineTransform::rotation(pivot, angle), "Pivoter");
}

OpStatus op_scale(OpContext& ctx) {
    Target target;
    if (auto st = resolve_target(ctx, "object", target); !st.ok) {
        return st;
    }
    double factor = 1.0;
    bool present = false;
    if (auto st = read_number(ctx, "factor", 0.05, 20.0, true, factor, present); !st.ok) {
        return st;
    }
    Vec2um pivot;
    if (auto st = read_pivot(ctx, target, pivot); !st.ok) {
        return st;
    }
    return run_transform(ctx, target, commands::AffineTransform::scale(pivot, factor),
                         "Mettre à l'échelle");
}

OpStatus op_mirror(OpContext& ctx) {
    Target target;
    if (auto st = resolve_target(ctx, "object", target); !st.ok) {
        return st;
    }
    std::string axis;
    bool present = false;
    if (auto st = read_enum(ctx, "axis", {"horizontal", "vertical"}, true, axis, present); !st.ok) {
        return st;
    }
    Vec2um pivot;
    if (auto st = read_pivot(ctx, target, pivot); !st.ok) {
        return st;
    }
    // « horizontal » = retournement gauche/droite (axe vertical passant par le pivot).
    return run_transform(ctx, target,
                         commands::AffineTransform::mirror(pivot, axis == "horizontal"), "Miroir");
}

// --- Création de formes ---------------------------------------------------------------

// Polygone simple exploitable : sommets consécutifs distincts, aire non dégénérée, aucune
// auto-intersection (arêtes non adjacentes qui se croisent).
OpStatus validate_polygon(const std::vector<Vec2um>& pts) {
    const std::size_t n = pts.size();
    if (n < 3) {
        return usage("un polygone exige au moins 3 sommets");
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (pts[i] == pts[(i + 1) % n]) {
            return refused("polygone invalide : deux sommets consécutifs identiques (sommet " +
                           std::to_string(i) + ")");
        }
    }
    geometry::Path path = geometry::polygon_path(pts);
    const double area_mm2 = std::abs(geometry::signed_area_um2(path)) / 1e6;
    if (area_mm2 < kMinPolygonAreaMm2) {
        return refused("polygone dégénéré : aire " + std::to_string(area_mm2) + " mm² (minimum " +
                       std::to_string(kMinPolygonAreaMm2) + " mm²)");
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            const bool adjacent = (j == i + 1) || (i == 0 && j == n - 1);
            if (adjacent) {
                continue;
            }
            const std::vector<Vec2um> a{pts[i], pts[(i + 1) % n]};
            const std::vector<Vec2um> b{pts[j], pts[(j + 1) % n]};
            if (geometry::polylines_cross(a, b)) {
                return refused("polygone auto-intersectant (les arêtes " + std::to_string(i) +
                                   " et " + std::to_string(j) + " se croisent)",
                               "ordonner les sommets le long du contour");
            }
        }
    }
    return OpStatus::success();
}

OpStatus op_add_shape(OpContext& ctx) {
    std::string shape;
    bool has_shape = false;
    if (auto st = read_enum(ctx, "shape", {"polygon", "rect", "ellipse"}, false, shape, has_shape);
        !st.ok) {
        return st;
    }
    const bool has_points = ctx.args.contains("points_mm") && !ctx.args.at("points_mm").is_null();
    if (!has_shape) {
        if (!has_points) {
            return usage("préciser « shape » (rect|ellipse|polygon) ou « points_mm »");
        }
        shape = "polygon";
    }

    geometry::Path path;
    std::string default_name;
    if (shape == "polygon") {
        for (const char* forbidden : {"width_mm", "height_mm", "center_mm"}) {
            if (ctx.args.contains(forbidden)) {
                return usage(std::string("argument « ") + forbidden +
                             " » incompatible avec shape=polygon (utiliser points_mm)");
            }
        }
        std::vector<Vec2um> pts;
        if (auto st = read_points(ctx, "points_mm", 3, kMaxPolygonPoints, pts); !st.ok) {
            return st;
        }
        if (auto st = validate_polygon(pts); !st.ok) {
            return st;
        }
        path = geometry::polygon_path(pts);
        default_name = "Forme";
    } else {
        if (has_points) {
            return usage("argument « points_mm » incompatible avec shape=" + shape +
                         " (utiliser width_mm et height_mm)");
        }
        double w = 0.0;
        double h = 0.0;
        bool present = false;
        if (auto st = read_number(ctx, "width_mm", 0.1, 2.0 * kMaxCoordMm, true, w, present);
            !st.ok) {
            return st;
        }
        if (auto st = read_number(ctx, "height_mm", 0.1, 2.0 * kMaxCoordMm, true, h, present);
            !st.ok) {
            return st;
        }
        Vec2um center{};
        if (auto st = read_point(ctx, "center_mm", false, center, present); !st.ok) {
            return st;
        }
        const double cx = to_millimeters(center.x).value;
        const double cy = to_millimeters(center.y).value;
        const Vec2um c1{to_micrometers(Millimeters{cx - w / 2.0}),
                        to_micrometers(Millimeters{cy - h / 2.0})};
        const Vec2um c2{to_micrometers(Millimeters{cx + w / 2.0}),
                        to_micrometers(Millimeters{cy + h / 2.0})};
        if (c1.x == c2.x || c1.y == c2.y) {
            return refused("forme dégénérée après arrondi au micromètre");
        }
        path = shape == "rect" ? geometry::rectangle_path(c1, c2) : geometry::ellipse_path(c1, c2);
        default_name = shape == "rect" ? "Rectangle" : "Ellipse";
    }

    std::string stitch_type = "tatami";
    bool present = false;
    if (auto st = read_enum(ctx, "stitch_type", {"tatami", "running", "directional"}, false,
                            stitch_type, present);
        !st.ok) {
        return st;
    }
    std::array<std::uint8_t, 3> rgb{80, 120, 200}; // même teinte par défaut que le bureau
    if (auto st = read_rgb(ctx, rgb, present); !st.ok) {
        return st;
    }
    std::string name = default_name;
    if (auto st = read_name(ctx, name, present); !st.ok) {
        return st;
    }

    document::VectorObject vec;
    vec.id = ctx.project.object_ids.next();
    vec.name = name;
    vec.rgb = rgb;
    vec.paths.push_back(geometry::PathSet{std::move(path), {}});

    document::EmbroideryObject emb;
    emb.id = ctx.project.object_ids.next();
    emb.name = name;
    emb.source_vector = vec.id;
    emb.rgb = rgb;
    emb.intent = document::EmbroideryIntent::ForcedUserChoice; // forme voulue par l'utilisateur
    if (stitch_type == "running") {
        emb.params = document::RunningStitchParams{};
    } else if (stitch_type == "directional") {
        emb.params = stitch_generation::directional_from_tatami(
            document::TatamiParams{}, vec.paths, static_cast<std::uint32_t>(emb.id.value));
    } else {
        emb.params = document::TatamiParams{};
    }

    const ObjectId vec_id = vec.id;
    const ObjectId emb_id = emb.id;
    ctx.stack.execute(std::make_unique<commands::AddObjectBatchCommand>(
                          std::vector<document::VectorObject>{std::move(vec)},
                          std::vector<document::EmbroideryObject>{std::move(emb)},
                          "Ajouter une forme"),
                      ctx.project);
    json result{{"id", emb_id.value}, {"vector_id", vec_id.value}, {"stitch_type", stitch_type}};
    if (const auto b = commands::objects_bounds(ctx.project, {vec_id})) {
        result["bounds_mm"] = bounds_json(*b);
    }
    return OpStatus::success(std::move(result));
}

OpStatus op_set_geometry(OpContext& ctx) {
    Target target;
    if (auto st = resolve_target(ctx, "object", target); !st.ok) {
        return st;
    }
    std::vector<Vec2um> pts;
    if (auto st = read_points(ctx, "points_mm", 3, kMaxPolygonPoints, pts); !st.ok) {
        return st;
    }
    if (auto st = validate_polygon(pts); !st.ok) {
        return st;
    }
    const auto* source = ctx.project.findObject(target.vector);
    std::size_t dropped_pieces = source->paths.empty() ? 0 : source->paths.size() - 1;
    std::size_t dropped_holes = 0;
    for (const auto& set : source->paths) {
        dropped_holes += set.holes.size();
    }
    std::vector<geometry::PathSet> paths;
    paths.push_back(geometry::PathSet{geometry::polygon_path(pts), {}});
    ctx.stack.execute(std::make_unique<commands::SetVectorPathsCommand>(
                          target.vector, std::move(paths), "Remplacer le contour"),
                      ctx.project);
    json result{{"id", target.embroidery.value},
                {"vector_id", target.vector.value},
                {"dropped_pieces", dropped_pieces},
                {"dropped_holes", dropped_holes}};
    if (const auto b = commands::objects_bounds(ctx.project, {target.vector})) {
        result["bounds_mm"] = bounds_json(*b);
    }
    return OpStatus::success(std::move(result));
}

// --- Duplication, fusion, découpe -----------------------------------------------------

OpStatus op_duplicate(OpContext& ctx) {
    Target target;
    if (auto st = resolve_target(ctx, "object", target); !st.ok) {
        return st;
    }
    Vec2um offset{};
    bool present = false;
    if (auto st = read_point(ctx, "offset_mm", true, offset, present); !st.ok) {
        return st;
    }
    // Rang de l'objet visé parmi les broderies qui partagent son vecteur (ordre du document).
    std::size_t rank = 0;
    for (const auto& e : ctx.project.embroidery_objects) {
        if (e.source_vector == target.vector) {
            if (e.id == target.embroidery) {
                break;
            }
            ++rank;
        }
    }
    const auto fragment = commands::collect_fragment(ctx.project, {target.vector});
    if (fragment.empty()) {
        return refused("rien à dupliquer : fragment vide");
    }
    auto paste = commands::make_paste_command(ctx.project, fragment, offset, "Dupliquer");
    if (!paste.command || paste.pasted.empty()) {
        return refused("duplication impossible");
    }
    const ObjectId new_vector = paste.pasted.front();
    ctx.stack.execute(std::move(paste.command), ctx.project);

    std::vector<std::uint64_t> created;
    for (const auto& e : ctx.project.embroidery_objects) {
        if (e.source_vector == new_vector) {
            created.push_back(e.id.value);
        }
    }
    if (created.empty()) {
        return OpStatus::failure(code::kInternal, "duplication sans objet de broderie résultant");
    }
    const std::uint64_t main_id = created[std::min(rank, created.size() - 1)];
    json result{{"id", main_id}, {"vector_id", new_vector.value}, {"created", created}};
    if (const auto b = commands::objects_bounds(ctx.project, {new_vector})) {
        result["bounds_mm"] = bounds_json(*b);
    }
    return OpStatus::success(std::move(result));
}

OpStatus op_merge(OpContext& ctx) {
    const auto it = ctx.args.find("objects");
    if (it == ctx.args.end() || !it->is_array()) {
        return usage(
            "argument « objects » obligatoire : tableau d'identifiants d'objets de broderie");
    }
    if (it->size() < 2 || it->size() > 1000) {
        return usage("argument « objects » : entre 2 et 1000 objets (" +
                     std::to_string(it->size()) + " donné(s))");
    }
    std::string mode_name = "union";
    bool present = false;
    if (auto st = read_enum(ctx, "mode", {"union", "subtract", "intersect", "exclude"}, false,
                            mode_name, present);
        !st.ok) {
        return st;
    }
    std::vector<ObjectId> vectors;
    std::vector<std::uint64_t> seen;
    for (std::size_t i = 0; i < it->size(); ++i) {
        const auto& item = (*it)[i];
        if (!item.is_number_unsigned()) {
            return usage("objects[" + std::to_string(i) + "] : identifiant entier attendu");
        }
        const std::uint64_t id = item.get<std::uint64_t>();
        const auto* emb = ctx.project.findEmbroidery(ObjectId{id});
        if (emb == nullptr) {
            return OpStatus::failure(code::kUnknownObject,
                                     "objet de broderie " + std::to_string(id) + " introuvable",
                                     "voir `project inspect --json` pour les identifiants");
        }
        if (std::find(seen.begin(), seen.end(), emb->source_vector.value) != seen.end()) {
            return refused(
                "l'objet " + std::to_string(id) +
                " apparaît deux fois ou partage son contour avec un autre objet de la liste");
        }
        seen.push_back(emb->source_vector.value);
        if (ctx.project.findObject(emb->source_vector) == nullptr) {
            return OpStatus::failure(code::kUnknownObject, "l'objet " + std::to_string(id) +
                                                               " n'a pas de vecteur source");
        }
        vectors.push_back(emb->source_vector);
    }
    commands::BooleanOp op = commands::BooleanOp::Union;
    if (mode_name == "subtract") {
        op = commands::BooleanOp::Subtract;
    } else if (mode_name == "intersect") {
        op = commands::BooleanOp::Intersect;
    } else if (mode_name == "exclude") {
        op = commands::BooleanOp::Exclude;
    }

    const auto before_emb = embroidery_ids(ctx.project);
    auto made = commands::make_boolean_command(ctx.project, op, vectors);
    if (!made.command) {
        return refused(made.error.empty() ? "fusion impossible" : made.error);
    }
    ctx.stack.execute(std::move(made.command), ctx.project);

    // Objet conservé : le premier vecteur d'entrée qui existe encore.
    ObjectId kept_vector{};
    for (const auto v : vectors) {
        if (ctx.project.findObject(v) != nullptr) {
            kept_vector = v;
            break;
        }
    }
    json result = json::object();
    const auto after_emb = embroidery_ids(ctx.project);
    std::vector<std::uint64_t> removed;
    for (const auto id : before_emb) {
        if (std::find(after_emb.begin(), after_emb.end(), id) == after_emb.end()) {
            removed.push_back(id);
        }
    }
    result["removed"] = removed;
    result["resulting_objects"] = made.resulting_objects;
    if (kept_vector.valid()) {
        result["vector_id"] = kept_vector.value;
        for (const auto& e : ctx.project.embroidery_objects) {
            if (e.source_vector == kept_vector) {
                result["id"] = e.id.value;
                break;
            }
        }
        if (const auto b = commands::objects_bounds(ctx.project, {kept_vector})) {
            result["bounds_mm"] = bounds_json(*b);
        }
    }
    return OpStatus::success(std::move(result));
}

// Résultat commun de split / break_apart : objets créés (diff des identifiants).
OpStatus pieces_result(const OpContext& ctx, const Target& target,
                       const std::vector<std::uint64_t>& before_emb,
                       const std::vector<std::uint64_t>& before_vec) {
    const auto created = new_ids(before_emb, embroidery_ids(ctx.project));
    const auto created_vec = new_ids(before_vec, vector_ids(ctx.project));
    json result{
        {"original", target.embroidery.value}, {"created", created}, {"vector_ids", created_vec}};
    if (!created.empty()) {
        result["id"] = created.front();
    }
    return OpStatus::success(std::move(result));
}

OpStatus op_split(OpContext& ctx) {
    Target target;
    if (auto st = resolve_target(ctx, "object", target); !st.ok) {
        return st;
    }
    std::vector<Vec2um> line;
    if (auto st = read_points(ctx, "line_mm", 2, 2, line); !st.ok) {
        return st;
    }
    if (length_um(line[1] - line[0]) < 10.0) {
        return refused("ligne de coupe dégénérée : les deux points sont (quasi) confondus");
    }
    const auto before_emb = embroidery_ids(ctx.project);
    const auto before_vec = vector_ids(ctx.project);
    auto made = commands::make_split_command(ctx.project, {target.vector}, line[0], line[1]);
    if (!made.command) {
        return refused(made.error.empty() ? "découpe impossible" : made.error);
    }
    ctx.stack.execute(std::move(made.command), ctx.project);
    return pieces_result(ctx, target, before_emb, before_vec);
}

OpStatus op_break_apart(OpContext& ctx) {
    Target target;
    if (auto st = resolve_target(ctx, "object", target); !st.ok) {
        return st;
    }
    const auto before_emb = embroidery_ids(ctx.project);
    const auto before_vec = vector_ids(ctx.project);
    auto made = commands::make_break_apart_command(ctx.project, target.vector);
    if (!made.command) {
        return refused(made.error.empty() ? "séparation impossible" : made.error);
    }
    ctx.stack.execute(std::move(made.command), ctx.project);
    return pieces_result(ctx, target, before_emb, before_vec);
}

OpStatus op_remove_holes(OpContext& ctx) {
    Target target;
    if (auto st = resolve_target(ctx, "object", target); !st.ok) {
        return st;
    }
    double max_area_mm2 = 0.0;
    bool present = false;
    if (auto st = read_number(ctx, "max_area_mm2", 0.0001, 1.0e6, false, max_area_mm2, present);
        !st.ok) {
        return st;
    }
    auto made = commands::make_remove_holes_command(ctx.project, {target.vector},
                                                    present ? max_area_mm2 * 1.0e6 : 0.0);
    if (!made.command) {
        return refused(made.error.empty() ? "aucun trou à retirer" : made.error);
    }
    const std::size_t removed = made.resulting_objects;
    ctx.stack.execute(std::move(made.command), ctx.project);
    return OpStatus::success(json{{"id", target.embroidery.value},
                                  {"vector_id", target.vector.value},
                                  {"holes_removed", removed}});
}

// --- Auto-satin -----------------------------------------------------------------------

// Angle modulo pi ramené dans (-pi/2 ; pi/2], comme le bureau.
double wrap_half_pi(double a) {
    a = std::fmod(a, std::numbers::pi);
    if (a > std::numbers::pi / 2.0) {
        a -= std::numbers::pi;
    } else if (a <= -std::numbers::pi / 2.0) {
        a += std::numbers::pi;
    }
    return a;
}

OpStatus read_guides(const OpContext& ctx, std::vector<document::AutoSatinGuide>& out) {
    const auto it = ctx.args.find("guides");
    if (it == ctx.args.end() || it->is_null()) {
        return OpStatus::success();
    }
    if (!it->is_array()) {
        return usage("argument « guides » : tableau d'objets {anchor_mm, angle_deg, absolute?}");
    }
    if (it->size() > 64) {
        return out_of_range("argument « guides » : au plus 64 guides");
    }
    for (std::size_t i = 0; i < it->size(); ++i) {
        const auto& g = (*it)[i];
        const std::string where = "guides[" + std::to_string(i) + "]";
        if (!g.is_object()) {
            return usage(where + " : objet {anchor_mm, angle_deg, absolute?} attendu");
        }
        for (const auto& [key, value] : g.items()) {
            (void)value;
            if (key != "anchor_mm" && key != "angle_deg" && key != "absolute") {
                return usage(where + " : clé inconnue « " + key + " »");
            }
        }
        if (!g.contains("anchor_mm") || !g.contains("angle_deg")) {
            return usage(where + " : « anchor_mm » et « angle_deg » sont obligatoires");
        }
        document::AutoSatinGuide guide;
        if (auto st = point_from(g["anchor_mm"], where + ".anchor_mm", guide.anchor); !st.ok) {
            return st;
        }
        if (!g["angle_deg"].is_number()) {
            return usage(where + ".angle_deg : un nombre est attendu");
        }
        const double deg = g["angle_deg"].get<double>();
        if (!std::isfinite(deg) || deg < -360.0 || deg > 360.0) {
            return out_of_range(where + ".angle_deg hors plage [-360 ; 360]");
        }
        guide.angle = Angle{wrap_half_pi(deg * std::numbers::pi / 180.0)};
        if (g.contains("absolute")) {
            if (!g["absolute"].is_boolean()) {
                return usage(where + ".absolute : booléen attendu");
            }
            guide.absolute = g["absolute"].get<bool>();
        }
        out.push_back(guide);
    }
    return OpStatus::success();
}

OpStatus op_auto_satin(OpContext& ctx) {
    Target target;
    if (auto st = resolve_target(ctx, "object", target); !st.ok) {
        return st;
    }
    AutoSatinPlanRequest request;
    double spacing_mm = 0.0;
    bool present = false;
    if (auto st = read_number(ctx, "spacing_mm", 0.1, 1.5, false, spacing_mm, present); !st.ok) {
        return st;
    }
    if (present) {
        request.spacing = to_micrometers(Millimeters{spacing_mm});
    }
    if (const auto it = ctx.args.find("compact_axis"); it != ctx.args.end() && !it->is_null()) {
        if (!it->is_boolean()) {
            return usage("argument « compact_axis » : booléen attendu");
        }
        request.compact_axis = it->get<bool>();
    }
    if (const auto it = ctx.args.find("strategy"); it != ctx.args.end() && !it->is_null()) {
        const auto parsed = it->is_string()
                                ? document::parse_auto_satin_strategy(it->get<std::string>())
                                : std::nullopt;
        if (!parsed) {
            return usage("argument « strategy » : « skeleton », « parallel_sweep » ou "
                         "« medial_axis » attendu");
        }
        request.strategy = *parsed;
    }
    double sweep_deg = 0.0;
    bool sweep_present = false;
    if (auto st =
            read_number(ctx, "sweep_angle_deg", -360.0, 360.0, false, sweep_deg, sweep_present);
        !st.ok) {
        return st;
    }
    if (sweep_present) {
        request.sweep_angle = Angle{sweep_deg * std::numbers::pi / 180.0};
    }
    if (auto st = read_guides(ctx, request.guides); !st.ok) {
        return st;
    }

    const auto plan = plan_auto_satin(ctx.project, target.vector, request);
    if (!plan) {
        return OpStatus::failure(code::kInternal, plan.error().message);
    }
    if (!plan->accepted) {
        return refused(plan->refusal,
                       "forme compacte : essayer compact_axis=true ; sinon un remplissage tatami");
    }

    // Même construction que le bureau (`setStitchType`, type satin) : conversion du groupe de
    // sections, avec le contour brut de la région quand il existe (un seul pas d'annulation).
    auto convert = std::make_unique<commands::ConvertFillGroupCommand>(
        target.embroidery, plan->params, "Type : satin");
    if (plan->restored_contour) {
        auto group = std::make_unique<commands::CompositeCommand>("Type : satin");
        group->add(std::make_unique<commands::SetVectorPathsCommand>(
            target.vector, *plan->restored_contour, "Contour brut de la région"));
        group->add(std::move(convert));
        ctx.stack.execute(std::move(group), ctx.project);
    } else {
        ctx.stack.execute(std::move(convert), ctx.project);
    }

    json result{{"id", target.embroidery.value},
                {"vector_id", target.vector.value},
                {"columns", plan->columns},
                {"satinability", plan->satinability_status},
                {"contour_restored", plan->restored_contour.has_value()},
                {"messages", plan->messages}};
    if (plan->coverage_measured) {
        result["coverage_ratio"] = plan->coverage_ratio;
    }
    return OpStatus::success(std::move(result));
}

// --- Catalogue ------------------------------------------------------------------------

ArgSpec object_arg(const char* description = "objet de broderie visé") {
    return ArgSpec{"object", "object_id", "", description, true, ""};
}

} // namespace

void register_geometry_ops(std::vector<OpSpec>& out) {
    out.push_back(OpSpec{
        "add_shape",
        "Crée une forme (rectangle, ellipse ou polygone) et son objet de broderie.",
        {
            {"shape", "string", "", "rect | ellipse | polygon (déduit de points_mm si absent)",
             false, "rect|ellipse|polygon"},
            {"points_mm", "array", "mm",
             "sommets [[x,y],...] du polygone (repère modèle, Y vers le haut), sans "
             "auto-intersection",
             false, "3 à 2000 points, |coord| <= 1000"},
            {"width_mm", "number", "mm", "largeur du rect/ellipse", false, "[0.1 ; 2000]"},
            {"height_mm", "number", "mm", "hauteur du rect/ellipse", false, "[0.1 ; 2000]"},
            {"center_mm", "array", "mm",
             "centre [x,y] du rect/ellipse (défaut : centre du canevas)", false, "|coord| <= 1000"},
            {"stitch_type", "string", "", "type de points (défaut tatami)", false,
             "tatami|running|directional"},
            {"rgb", "string", "", "couleur de fil « #RRGGBB » (défaut #5078C8)", false, ""},
            {"name", "string", "", "nom de l'objet", false, "1 à 128 caractères"},
        },
        R"({"op":"add_shape","shape":"rect","width_mm":30,"height_mm":10,"center_mm":[0,0],"stitch_type":"tatami","rgb":"#FF0000","name":"Bande"})",
        op_add_shape});

    out.push_back(
        OpSpec{"duplicate_object",
               "Duplique un objet (forme et broderies) avec un décalage.",
               {object_arg("objet de broderie à dupliquer"),
                {"offset_mm", "array", "mm", "décalage [dx,dy] de la copie (Y vers le haut)", true,
                 "|coord| <= 1000"}},
               R"({"op":"duplicate_object","object":1,"offset_mm":[15,0]})",
               op_duplicate});

    out.push_back(OpSpec{
        "translate",
        "Déplace un objet (forme, rangées, guides, rails).",
        {object_arg(),
         {"dx_mm", "number", "mm", "déplacement horizontal (vers la droite)", true,
          "[-2000 ; 2000]"},
         {"dy_mm", "number", "mm", "déplacement vertical (vers le haut)", true, "[-2000 ; 2000]"}},
        R"({"op":"translate","object":1,"dx_mm":5,"dy_mm":-2.5})",
        op_translate});

    out.push_back(
        OpSpec{"rotate",
               "Fait pivoter un objet (sens anti-horaire pour un angle positif).",
               {object_arg(),
                {"angle_deg", "number", "deg", "angle de rotation", true, "[-360 ; 360]"},
                {"center_mm", "array", "mm", "pivot [x,y] (défaut : centre de la boîte englobante)",
                 false, "|coord| <= 1000"}},
               R"({"op":"rotate","object":1,"angle_deg":45})",
               op_rotate});

    out.push_back(
        OpSpec{"scale",
               "Met un objet à l'échelle (uniforme) autour d'un pivot.",
               {object_arg(),
                {"factor", "number", "", "facteur d'échelle", true, "[0.05 ; 20]"},
                {"center_mm", "array", "mm", "pivot [x,y] (défaut : centre de la boîte englobante)",
                 false, "|coord| <= 1000"}},
               R"({"op":"scale","object":1,"factor":1.5})",
               op_scale});

    out.push_back(OpSpec{"mirror",
                         "Retourne un objet : horizontal = gauche/droite, vertical = haut/bas.",
                         {object_arg(),
                          {"axis", "string", "", "horizontal (gauche/droite) | vertical (haut/bas)",
                           true, "horizontal|vertical"},
                          {"center_mm", "array", "mm",
                           "point de l'axe [x,y] (défaut : centre de la boîte englobante)", false,
                           "|coord| <= 1000"}},
                         R"({"op":"mirror","object":1,"axis":"horizontal"})",
                         op_mirror});

    out.push_back(OpSpec{"merge",
                         "Fusionne des formes (union, soustraction, intersection, exclusion) ; le "
                         "dernier de la liste est l'objet principal.",
                         {{"objects", "array", "",
                           "identifiants d'objets de broderie (au moins 2, contours distincts)",
                           true, "2 à 1000 objets"},
                          {"mode", "string", "", "opération booléenne (défaut union)", false,
                           "union|subtract|intersect|exclude"}},
                         R"({"op":"merge","objects":[1,2],"mode":"union"})",
                         op_merge});

    out.push_back(OpSpec{"split",
                         "Découpe un objet le long d'une droite ; chaque morceau devient un objet.",
                         {object_arg(),
                          {"line_mm", "array", "mm",
                           "droite de coupe [[x1,y1],[x2,y2]] (prolongée au-delà de la forme)",
                           true, "2 points distincts"}},
                         R"({"op":"split","object":1,"line_mm":[[0,-20],[0,20]]})",
                         op_split});

    out.push_back(OpSpec{"break_apart",
                         "Sépare les morceaux disjoints d'un objet en objets distincts.",
                         {object_arg()},
                         R"({"op":"break_apart","object":1})",
                         op_break_apart});

    out.push_back(
        OpSpec{"remove_holes",
               "Retire les trous d'un objet (tous, ou seulement ceux dont l'aire ne dépasse pas un "
               "seuil).",
               {object_arg(),
                {"max_area_mm2", "number", "mm2",
                 "seuil d'aire des trous à combler (défaut : tous)", false, "[0.0001 ; 1000000]"}},
               R"({"op":"remove_holes","object":1,"max_area_mm2":2.0})",
               op_remove_holes});

    out.push_back(
        OpSpec{"set_geometry",
               "Remplace TOUTE la géométrie de l'objet (morceaux et trous) par un polygone simple.",
               {object_arg(),
                {"points_mm", "array", "mm",
                 "sommets [[x,y],...] du nouveau contour, sans auto-intersection", true,
                 "3 à 2000 points, |coord| <= 1000"}},
               R"({"op":"set_geometry","object":1,"points_mm":[[-10,-5],[10,-5],[10,5],[-10,5]]})",
               op_set_geometry});

    out.push_back(OpSpec{
        "auto_satin",
        "Convertit un objet en auto-satin (squelette et traversées orientées) ; refusé comme dans "
        "l'application si le moteur ne construit aucune colonne.",
        {object_arg(),
         {"spacing_mm", "number", "mm", "espacement des traversées (défaut 0.4)", false,
          "[0.1 ; 1.5]"},
         {"compact_axis", "boolean", "", "forme compacte : axe synthétique au lieu d'un refus",
          false, ""},
         {"strategy", "string", "",
          "méthode : skeleton (défaut), parallel_sweep (balayage parallèle) ou medial_axis "
          "(axe médian vectoriel)",
          false, "skeleton | parallel_sweep | medial_axis"},
         {"sweep_angle_deg", "number", "°",
          "balayage parallèle : angle des traversées (défaut : automatique)", false,
          "[-360 ; 360]"},
         {"guides", "array", "", "guides [{anchor_mm:[x,y], angle_deg, absolute?}] d'orientation",
          false, "au plus 64"}},
        R"({"op":"auto_satin","object":1,"spacing_mm":0.4})",
        op_auto_satin});
}

} // namespace openstitch::project_ops::detail
