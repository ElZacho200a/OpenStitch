// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_ops/inspect.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <variant>

#include <nlohmann/json.hpp>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::project_ops {

namespace {

using json = nlohmann::ordered_json;

// Conversions de frontière : le modèle est en µm entiers, le JSON en mm (réel, 3 décimales
// exactes puisque 1 µm = 0,001 mm) et en degrés.
double mm(Micrometers um) {
    return static_cast<double>(um.value) / 1000.0;
}
double mm_of(double um) {
    return um / 1000.0;
}
double deg(Angle a) {
    return a.radians * 180.0 / std::numbers::pi;
}
json point_mm(Vec2um p) {
    return json::array({mm(p.x), mm(p.y)});
}
json polyline_mm(const geometry::Path& path) {
    json out = json::array();
    for (const auto& n : path.nodes) {
        out.push_back(point_mm(n.pos));
    }
    return out;
}
json paths_mm(const std::vector<geometry::Path>& paths) {
    json out = json::array();
    for (const auto& p : paths) {
        out.push_back(polyline_mm(p));
    }
    return out;
}
json rgb_json(const std::array<std::uint8_t, 3>& rgb) {
    return rgb_to_hex(rgb);
}

const char* underlay_mode_name(document::UnderlayMode m) {
    return m == document::UnderlayMode::Auto ? "auto" : "manual";
}
const char* density_curve_name(document::DensityCurve c) {
    switch (c) {
    case document::DensityCurve::Smooth:
        return "smooth";
    case document::DensityCurve::Steep:
        return "steep";
    case document::DensityCurve::Linear:
        break;
    }
    return "linear";
}
const char* short_stitch_name(document::SatinShortStitch v) {
    switch (v) {
    case document::SatinShortStitch::Disabled:
        return "disabled";
    case document::SatinShortStitch::RemoveAndRedistribute:
        return "remove_and_redistribute";
    case document::SatinShortStitch::SingleInset:
        return "single_inset";
    case document::SatinShortStitch::MultiLevelInset:
        return "multi_level_inset";
    }
    return "disabled";
}
const char* split_name(document::SatinSplit v) {
    switch (v) {
    case document::SatinSplit::Disabled:
        return "disabled";
    case document::SatinSplit::Simple:
        return "simple";
    case document::SatinSplit::Staggered:
        return "staggered";
    case document::SatinSplit::DeterministicJitter:
        return "deterministic_jitter";
    }
    return "disabled";
}
const char* corner_name(document::SatinCorner v) {
    switch (v) {
    case document::SatinCorner::None:
        return "none";
    case document::SatinCorner::Miter:
        return "miter";
    case document::SatinCorner::Overlap:
        return "overlap";
    case document::SatinCorner::Cut:
        return "cut";
    }
    return "none";
}
const char* cap_name(document::SatinCap v) {
    switch (v) {
    case document::SatinCap::Flat:
        return "flat";
    case document::SatinCap::Rounded:
        return "rounded";
    case document::SatinCap::Tapered:
        return "tapered";
    case document::SatinCap::Automatic:
        return "automatic";
    }
    return "flat";
}
const char* lock_name(document::SatinLock v) {
    switch (v) {
    case document::SatinLock::None:
        return "none";
    case document::SatinLock::BackAndForth:
        return "back_and_forth";
    case document::SatinLock::Triangle:
        return "triangle";
    case document::SatinLock::MicroZigzag:
        return "micro_zigzag";
    }
    return "none";
}
const char* project_lock_name(document::LockStitch v) {
    switch (v) {
    case document::LockStitch::None:
        return "none";
    case document::LockStitch::BackAndForth:
        return "backforth";
    case document::LockStitch::Triangle:
        return "triangle";
    case document::LockStitch::MicroZigzag:
        return "zigzag";
    }
    return "none";
}
const char* intent_name(document::EmbroideryIntent v) {
    return v == document::EmbroideryIntent::ForcedUserChoice ? "forced_user_choice" : "auto_choice";
}
const char* join_name(document::JoinMode v) {
    switch (v) {
    case document::JoinMode::Inherit:
        return "inherit";
    case document::JoinMode::Auto:
        return "auto";
    case document::JoinMode::Off:
        return "off";
    }
    return "inherit";
}
const char* connector_name(document::ConnectorMode v) {
    switch (v) {
    case document::ConnectorMode::Inherit:
        return "inherit";
    case document::ConnectorMode::AlwaysTrim:
        return "always_trim";
    case document::ConnectorMode::NeverTrim:
        return "never_trim";
    }
    return "inherit";
}
const char* severity_name(stitch_analysis::Severity s) {
    switch (s) {
    case stitch_analysis::Severity::Info:
        return "info";
    case stitch_analysis::Severity::Warning:
        return "warning";
    case stitch_analysis::Severity::Error:
        return "error";
    }
    return "warning";
}

// Champs scalaires de chaque type de points : noms snake_case, suffixe `_mm` / `_deg` pour
// les grandeurs physiques, énumérations en chaîne. Mêmes noms que l'opération `set_params`.
json params_json(const document::StitchParams& params, bool full) {
    json j = json::object();
    std::visit(
        [&](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, document::RunningStitchParams>) {
                j["stitch_length_mm"] = mm(p.stitch_length);
                j["min_length_mm"] = mm(p.min_length);
                j["repeats"] = p.repeats;
            } else if constexpr (std::is_same_v<T, document::TatamiParams>) {
                j["angle_deg"] = deg(p.angle);
                j["row_spacing_mm"] = mm(p.row_spacing);
                j["stitch_length_mm"] = mm(p.stitch_length);
                j["inset_mm"] = mm(p.inset);
                j["stagger"] = p.stagger;
                j["underlay_edge"] = p.underlay_edge;
                j["underlay_parallel"] = p.underlay_parallel;
                j["underlay_inset_mm"] = mm(p.underlay_inset);
                j["underlay_spacing_mm"] = mm(p.underlay_spacing);
                j["hidden_underpath"] = p.hidden_underpath;
                j["pull_compensation_mm"] = mm(p.pull_compensation);
                j["underlay_mode"] = underlay_mode_name(p.underlay_mode);
                j["row_end_clip_mm"] = mm(p.row_end_clip);
                j["edge_outline"] = p.edge_outline;
                if (p.entry_point) {
                    j["entry_point_mm"] = point_mm(*p.entry_point);
                }
            } else if constexpr (std::is_same_v<T, document::SatinParams>) {
                j["density_mm"] = mm(p.density);
                j["pull_compensation_mm"] = mm(p.pull_compensation);
                j["center_underlay"] = p.center_underlay;
                j["max_width_mm"] = mm(p.max_width);
                j["max_width_hard_mm"] = mm(p.max_width_hard);
                j["short_stitch"] = short_stitch_name(p.short_stitch);
                j["split_stitch"] = split_name(p.split_stitch);
                j["cap_start"] = cap_name(p.cap_start);
                j["cap_end"] = cap_name(p.cap_end);
                j["max_stitch_length_mm"] = mm(p.max_stitch_length);
                j["underlay_edge"] = p.underlay_edge;
                j["underlay_zigzag"] = p.underlay_zigzag;
                j["pull_left_mm"] = mm(p.pull_left);
                j["pull_right_mm"] = mm(p.pull_right);
                j["push_start_mm"] = mm(p.push_start);
                j["push_end_mm"] = mm(p.push_end);
                j["lock_start"] = lock_name(p.lock_start);
                j["lock_end"] = lock_name(p.lock_end);
                j["lock_length_mm"] = mm(p.lock_length);
                j["lock_passes"] = p.lock_passes;
                j["underlay_mode"] = underlay_mode_name(p.underlay_mode);
                j["auto_spacing"] = p.auto_spacing;
                j["corner_mode"] = corner_name(p.corner_mode);
                j["rail_a"] = polyline_mm(p.rail_a);
                j["rail_b"] = polyline_mm(p.rail_b);
                json rungs = json::array();
                for (const auto& r : p.rungs) {
                    rungs.push_back(json{{"a_mm", point_mm(r.a)}, {"b_mm", point_mm(r.b)}});
                }
                j["rungs"] = rungs;
                if (p.border) {
                    j["border_width_mm"] = mm(p.border->width);
                }
            } else if constexpr (std::is_same_v<T, document::DirectionalFillParams>) {
                j["row_spacing_mm"] = mm(p.row_spacing);
                j["spacing_regularity"] = p.spacing_regularity;
                j["stitch_length_mm"] = mm(p.stitch_length);
                j["edge_weight"] = p.edge_weight;
                j["inset_mm"] = mm(p.inset);
                j["stagger"] = p.stagger;
                j["underlay_edge"] = p.underlay_edge;
                j["underlay_parallel"] = p.underlay_parallel;
                j["underlay_inset_mm"] = mm(p.underlay_inset);
                j["underlay_spacing_mm"] = mm(p.underlay_spacing);
                j["hidden_underpath"] = p.hidden_underpath;
                j["sector_overlap_mm"] = mm(p.sector_overlap);
                j["handmade"] = p.handmade;
                j["handmade_intensity"] = p.handmade_intensity;
                j["seed"] = p.seed;
                j["underlay_mode"] = underlay_mode_name(p.underlay_mode);
                j["edge_outline"] = p.edge_outline;
                j["guides"] = paths_mm(p.guides);
                j["break_lines"] = paths_mm(p.break_lines);
                if (p.density_gradient) {
                    const auto& g = *p.density_gradient;
                    j["density_gradient"] = json{{"from_mm", point_mm(g.from)},
                                                 {"to_mm", point_mm(g.to)},
                                                 {"spacing_from_mm", mm(g.spacing_from)},
                                                 {"spacing_to_mm", mm(g.spacing_to)},
                                                 {"interpolate_density", g.interpolate_density},
                                                 {"curve", density_curve_name(g.curve)}};
                } else {
                    j["density_gradient"] = nullptr;
                }
            } else if constexpr (std::is_same_v<T, document::AutoSatinParams>) {
                j["spacing_mm"] = mm(p.spacing);
                j["split_stitch"] = split_name(p.split_stitch);
                j["split_threshold_mm"] = mm(p.split_threshold);
                j["split_length_mm"] = mm(p.split_length);
                j["short_stitch"] = short_stitch_name(p.short_stitch);
                j["pull_compensation_mm"] = mm(p.pull_compensation);
                j["center_underlay"] = p.center_underlay;
                j["underlay_edge"] = p.underlay_edge;
                j["underlay_zigzag"] = p.underlay_zigzag;
                j["pull_left_mm"] = mm(p.pull_left);
                j["pull_right_mm"] = mm(p.pull_right);
                j["push_start_mm"] = mm(p.push_start);
                j["push_end_mm"] = mm(p.push_end);
                j["cap_start"] = cap_name(p.cap_start);
                j["cap_end"] = cap_name(p.cap_end);
                j["lock_start"] = lock_name(p.lock_start);
                j["lock_end"] = lock_name(p.lock_end);
                j["lock_length_mm"] = mm(p.lock_length);
                j["lock_passes"] = p.lock_passes;
                j["underlay_mode"] = underlay_mode_name(p.underlay_mode);
                j["compact_axis"] = p.compact_axis;
                j["auto_spacing"] = p.auto_spacing;
                j["corner_mode"] = corner_name(p.corner_mode);
                j["strategy"] = std::string(document::auto_satin_strategy_id(p.strategy));
                j["sweep_angle_deg"] = p.sweep_angle ? json(deg(*p.sweep_angle)) : json(nullptr);
                json guides = json::array();
                for (const auto& g : p.guides) {
                    guides.push_back(json{{"anchor_mm", point_mm(g.anchor)},
                                          {"angle_deg", deg(g.angle)},
                                          {"absolute", g.absolute}});
                }
                j["guides"] = guides;
            }
        },
        params);
    (void)full;
    return j;
}

struct Bounds {
    bool any{false};
    Vec2um min{};
    Vec2um max{};
    void add(Vec2um p) {
        if (!any) {
            min = max = p;
            any = true;
            return;
        }
        min.x.value = std::min(min.x.value, p.x.value);
        min.y.value = std::min(min.y.value, p.y.value);
        max.x.value = std::max(max.x.value, p.x.value);
        max.y.value = std::max(max.y.value, p.y.value);
    }
    [[nodiscard]] json to_json() const {
        if (!any) {
            return nullptr;
        }
        return json{{"min_mm", point_mm(min)},
                    {"max_mm", point_mm(max)},
                    {"width_mm", mm(max.x - min.x)},
                    {"height_mm", mm(max.y - min.y)}};
    }
};

// Boîte des nœuds des chemins d'un objet vectoriel (les poignées de Bézier ne comptent pas).
Bounds vector_bounds(const document::VectorObject& v) {
    Bounds b;
    for (const auto& set : v.paths) {
        for (const auto& n : set.outer.nodes) {
            b.add(n.pos);
        }
        for (const auto& hole : set.holes) {
            for (const auto& n : hole.nodes) {
                b.add(n.pos);
            }
        }
    }
    return b;
}

double vector_area_mm2(const document::VectorObject& v) {
    double um2 = 0.0;
    for (const auto& set : v.paths) {
        um2 += geometry::path_set_area_um2(set);
    }
    return um2 / 1.0e6;
}

// Statistiques de points par objet de broderie, rang de sa première commande.
struct ObjectStitches {
    std::size_t first_command{0};
    bool seen{false};
    std::size_t stitches{0}; // toutes passes
    std::size_t top_stitches{0};
    std::size_t underlay_stitches{0};
    std::size_t jumps{0};
    double thread_um{0.0};
    Bounds bounds;
};

std::map<std::uint64_t, ObjectStitches> per_object_stitches(const stitch::StitchSequence& seq) {
    std::map<std::uint64_t, ObjectStitches> out;
    std::map<std::uint64_t, Vec2um> last;
    for (std::size_t i = 0; i < seq.commands.size(); ++i) {
        const auto& c = seq.commands[i];
        if (!c.source.valid()) {
            continue;
        }
        auto& s = out[c.source.value];
        if (!s.seen) {
            s.seen = true;
            s.first_command = i;
        }
        if (c.type == stitch::CommandType::Stitch) {
            ++s.stitches;
            s.top_stitches += c.pass == stitch::StitchPass::TopStitch ? 1 : 0;
            s.underlay_stitches += c.pass == stitch::StitchPass::Underlay ? 1 : 0;
            s.bounds.add(c.pos);
            if (const auto it = last.find(c.source.value); it != last.end()) {
                s.thread_um += length_um(c.pos - it->second);
            }
            last[c.source.value] = c.pos;
        } else if (c.type == stitch::CommandType::Jump) {
            ++s.jumps;
            last[c.source.value] = c.pos;
        }
    }
    return out;
}

json finding_json(const stitch_analysis::Finding& f) {
    json j{{"severity", severity_name(f.severity)},
           {"category", f.category},
           {"message", f.message},
           {"x_mm", mm(f.location.x)},
           {"y_mm", mm(f.location.y)}};
    j["object"] = f.object.valid() ? json(f.object.value) : json(nullptr);
    if (!f.hint.empty()) {
        j["hint"] = f.hint;
    }
    return j;
}

json embroidery_json(const document::Project& project, const document::EmbroideryObject& e,
                     std::size_t rank, const InspectOptions& options,
                     const std::map<std::uint64_t, ObjectStitches>* stitches) {
    json j = json::object();
    j["id"] = e.id.value;
    j["name"] = e.name;
    j["order"] = rank;
    j["source_vector"] = e.source_vector.value;
    j["type"] = stitch_type_name(e);
    j["color"] = rgb_json(e.rgb);
    if (e.thread) {
        j["thread"] = json{{"chart", e.thread->chart_id}, {"code", e.thread->code}};
    } else {
        j["thread"] = nullptr;
    }
    j["visible"] = e.visible;
    j["locked"] = e.locked;
    j["intent"] = intent_name(e.intent);
    j["join"] = join_name(e.join);
    j["connector"] = connector_name(e.connector);
    j["params"] = params_json(e.params, options.full_params);
    j["manual_overrides"] = e.overrides.size();
    if (e.color_blend) {
        j["color_blend"] = json{{"partner", e.color_blend->partner.value},
                                {"foreground", e.color_blend->foreground}};
    }
    if (const auto* v = project.findObject(e.source_vector)) {
        j["region_area_mm2"] = vector_area_mm2(*v);
        j["geometry_bounds"] = vector_bounds(*v).to_json();
    } else {
        j["region_area_mm2"] = nullptr;
        j["geometry_bounds"] = nullptr;
    }
    if (stitches != nullptr) {
        const auto it = stitches->find(e.id.value);
        if (it != stitches->end()) {
            const auto& s = it->second;
            j["stitches"] = json{{"total", s.stitches},
                                 {"top", s.top_stitches},
                                 {"underlay", s.underlay_stitches},
                                 {"jumps", s.jumps},
                                 {"thread_mm", mm_of(s.thread_um)},
                                 {"first_command_index", s.first_command},
                                 {"bounds", s.bounds.to_json()}};
        } else {
            j["stitches"] = json{{"total", 0}};
        }
    }
    return j;
}

json canvas_json(const document::Project& project) {
    const auto& c = project.canvas;
    return json{{"width_mm", mm(c.width)},
                {"height_mm", mm(c.height)},
                {"margin_mm", mm(c.margin)},
                {"usable_width_mm", mm(c.usable_width())},
                {"usable_height_mm", mm(c.usable_height())},
                {"shape", c.shape == document::HoopShape::Ellipse            ? "ellipse"
                          : c.shape == document::HoopShape::RoundedRectangle ? "rounded"
                                                                             : "rectangle"},
                {"corner_radius_mm", mm(c.corner_radius)},
                {"machine", project.machine_id}};
}

json finishing_json(const document::SequenceFinishing& f) {
    return json{{"enabled", f.enabled},
                {"trim_threshold_mm", mm(f.trim_threshold)},
                {"trim_before_color_change", f.trim_before_color_change},
                {"lock", project_lock_name(f.lock_type)},
                {"lock_length_mm", mm(f.lock_length)},
                {"lock_passes", f.lock_passes},
                {"filter_short_stitches", f.filter_short_stitches},
                {"min_stitch_length_mm", mm(f.min_stitch_length)},
                {"split_long_stitches", f.split_long_stitches},
                {"max_stitch_length_mm", mm(f.max_stitch_length)},
                {"auto_join", f.auto_join},
                {"hidden_travel",
                 json{{"enabled", f.hidden_travel.enabled},
                      {"max_bridge_length_mm", mm(f.hidden_travel.max_bridge_length)},
                      {"between_objects", f.hidden_travel.between_objects},
                      {"within_object", f.hidden_travel.within_object},
                      {"reorder_underlay", f.hidden_travel.reorder_underlay},
                      {"reorder_regions", f.hidden_travel.reorder_regions},
                      {"allow_same_color_surface", f.hidden_travel.allow_same_color_surface}}}};
}

Result<json> build_project_doc(const document::Project& project, const InspectOptions& options) {
    json doc = json::object();
    doc["schema"] = kInspectSchema;
    doc["osp_schema_version"] = project_io::kSchemaVersion;
    doc["canvas"] = canvas_json(project);
    const double mmPerPx = project.mm_per_px.value;
    doc["resolution"] = json{{"mm_per_px", mmPerPx}, {"dpi", mmPerPx > 0.0 ? 25.4 / mmPerPx : 0.0}};
    doc["image"] = json{{"present", project.hasImage()},
                        {"width_px", project.original.width},
                        {"height_px", project.original.height}};
    doc["finishing"] = finishing_json(project.finishing);
    doc["imported_design"] = project.imported_design.has_value();

    json regions = json::array();
    if (project.segmentation) {
        for (const auto& slot : project.segmentation->region_slots) {
            if (slot) {
                regions.push_back(json{{"id", slot->id.value},
                                       {"color", rgb_json(slot->rgb)},
                                       {"pixels", slot->pixel_count}});
            }
        }
    }
    doc["segmentation"] = json{{"region_count", regions.size()}, {"regions", regions}};

    json vectors = json::array();
    for (const auto& v : project.vector_objects) {
        json vj{{"id", v.id.value},
                {"name", v.name},
                {"visible", v.visible},
                {"color", rgb_json(v.rgb)},
                {"pieces", v.paths.size()},
                {"area_mm2", vector_area_mm2(v)},
                {"bounds", vector_bounds(v).to_json()}};
        vj["source_region"] = v.source_region ? json(v.source_region->value) : json(nullptr);
        vj["text_owner"] = v.text_owner ? json(v.text_owner->value) : json(nullptr);
        std::size_t holes = 0;
        std::size_t nodes = 0;
        for (const auto& set : v.paths) {
            holes += set.holes.size();
            nodes += set.outer.nodes.size();
            for (const auto& h : set.holes) {
                nodes += h.nodes.size();
            }
        }
        vj["holes"] = holes;
        vj["nodes"] = nodes;
        vectors.push_back(std::move(vj));
    }
    doc["vector_objects"] = vectors;

    json texts = json::array();
    for (const auto& t : project.text_objects) {
        json letters = json::array();
        for (const auto& v : project.vector_objects) {
            if (v.text_owner && *v.text_owner == t.id) {
                letters.push_back(v.id.value);
            }
        }
        const char* align = t.align == document::TextAlign::Center    ? "center"
                            : t.align == document::TextAlign::Right   ? "right"
                            : t.align == document::TextAlign::Justify ? "justify"
                                                                      : "left";
        const char* fill = t.fill == document::TextFill::Satin     ? "satin"
                           : t.fill == document::TextFill::Tatami  ? "tatami"
                           : t.fill == document::TextFill::Contour ? "contour"
                                                                   : "auto";
        const auto base_name = [](document::TextBaseKind k) {
            return k == document::TextBaseKind::Arc      ? "arc"
                   : k == document::TextBaseKind::Circle ? "circle"
                   : k == document::TextBaseKind::Path   ? "path"
                                                         : "line";
        };
        const auto connector_name = [](document::ConnectorMode m) {
            return m == document::ConnectorMode::AlwaysTrim  ? "trim"
                   : m == document::ConnectorMode::NeverTrim ? "no_trim"
                                                             : "inherit";
        };
        texts.push_back(json{{"id", t.id.value},
                             {"text", t.text},
                             {"font", json{{"family", t.font.family},
                                           {"file", t.font.file},
                                           {"builtin", t.font.builtin},
                                           {"face_index", t.font.face_index}}},
                             {"cap_height_mm", mm(t.cap_height)},
                             {"letter_spacing_mm", mm(t.letter_spacing)},
                             {"word_spacing_mm", mm(t.word_spacing)},
                             {"line_spacing", t.line_spacing},
                             {"kerning", t.kerning},
                             {"align", align},
                             {"origin_mm", point_mm(t.origin)},
                             {"rotation_deg", deg(t.rotation)},
                             {"color", rgb_json(t.rgb)},
                             {"fill", fill},
                             {"density_mm", mm(t.density)},
                             {"base", base_name(t.base.kind)},
                             {"base_radius_mm", mm(t.base.radius)},
                             {"base_inside", t.base.inside},
                             {"base_path_points", t.base.path.size()},
                             {"letter_connector", connector_name(t.letter_connector)},
                             {"word_connector", connector_name(t.word_connector)},
                             {"glyph_edit_count", t.glyph_edits.size()},
                             {"letter_vector_ids", letters}});
    }
    doc["text_objects"] = texts;

    std::map<std::uint64_t, ObjectStitches> stitches;
    std::optional<stitch::StitchStats> stats;
    // `effective_sequence` refuse un projet sans objet visible : inspecter un projet vide (ou
    // dont tout est masqué) reste légitime, la séquence est alors simplement vide.
    const bool anyVisible =
        std::any_of(project.embroidery_objects.begin(), project.embroidery_objects.end(),
                    [](const auto& e) { return e.visible; });
    if (options.with_stitches) {
        stitch::StitchSequence generated;
        if (anyVisible) {
            auto seq = stitch_generation::effective_sequence(project);
            if (!seq) {
                return std::unexpected(seq.error());
            }
            generated = std::move(*seq);
        }
        const auto* seqPtr = &generated;
        const stitch::StitchSequence& seqRef = *seqPtr;
        stitches = per_object_stitches(seqRef);
        stats = stitch::compute_stats(seqRef);
        doc["sequence"] = json{{"commands", seqRef.commands.size()},
                               {"stitches", stats->stitches},
                               {"jumps", stats->jumps},
                               {"trims", stats->trims},
                               {"color_changes", stats->color_changes},
                               {"thread_m", stats->thread_length_um / 1.0e6},
                               {"width_mm", mm(stats->bounds.max.x - stats->bounds.min.x)},
                               {"height_mm", mm(stats->bounds.max.y - stats->bounds.min.y)}};
    } else {
        doc["sequence"] = nullptr;
    }

    json embroideries = json::array();
    for (std::size_t i = 0; i < project.embroidery_objects.size(); ++i) {
        embroideries.push_back(embroidery_json(project, project.embroidery_objects[i], i, options,
                                               options.with_stitches ? &stitches : nullptr));
    }
    doc["embroidery_objects"] = embroideries;

    // Coloris (HP-THR-006) et tissu (HP-THR-008) : clés présentes seulement si le document les
    // utilise, pour ne pas modifier la sortie des documents existants.
    if (!project.colorways.empty()) {
        json colorways = json::array();
        for (std::size_t i = 0; i < project.colorways.size(); ++i) {
            colorways.push_back(json{{"name", project.colorways[i].name},
                                     {"active", static_cast<int>(i) == project.active_colorway},
                                     {"objects", project.colorways[i].entries.size()}});
        }
        doc["colorways"] = colorways;
    }
    if (!(project.fabric == document::FabricSettings{})) {
        doc["fabric"] = json{{"rgb", json::array({project.fabric.color[0], project.fabric.color[1],
                                                  project.fabric.color[2]})},
                             {"name", project.fabric.name}};
    }

    json warnings = json::array();
    for (const auto& f : stitch_analysis::check_stitch_limits(project)) {
        warnings.push_back(finding_json(f));
    }
    doc["warnings"] = warnings;
    doc["counts"] = json{{"vector_objects", project.vector_objects.size()},
                         {"embroidery_objects", project.embroidery_objects.size()},
                         {"text_objects", project.text_objects.size()}};
    return doc;
}

} // namespace

const char* stitch_type_name(const document::EmbroideryObject& object) {
    switch (object.params.index()) {
    case 0:
        return "running";
    case 1:
        return "tatami";
    case 2:
        return "satin";
    case 3:
        return "directional";
    case 4:
        return "auto_satin";
    case 5:
        return "blanket";
    case 6:
        return "contour_fill";
    case 7:
        return "motif_run";
    case 8:
        return "motif_fill";
    case 9:
        return "radial_fill";
    case 10:
        return "spiral_fill";
    case 11:
        return "crosshatch_fill";
    default:
        return "unknown";
    }
}

std::string rgb_to_hex(const std::array<std::uint8_t, 3>& rgb) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out = "#";
    for (const auto c : rgb) {
        out += kDigits[c >> 4];
        out += kDigits[c & 0x0F];
    }
    return out;
}

std::optional<std::array<std::uint8_t, 3>> rgb_from_hex(std::string_view text) {
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != 6) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 3> rgb{};
    for (std::size_t i = 0; i < 3; ++i) {
        int value = 0;
        for (std::size_t k = 0; k < 2; ++k) {
            const char ch = text[i * 2 + k];
            int digit = 0;
            if (ch >= '0' && ch <= '9') {
                digit = ch - '0';
            } else if (ch >= 'a' && ch <= 'f') {
                digit = ch - 'a' + 10;
            } else if (ch >= 'A' && ch <= 'F') {
                digit = ch - 'A' + 10;
            } else {
                return std::nullopt;
            }
            value = value * 16 + digit;
        }
        rgb[i] = static_cast<std::uint8_t>(value);
    }
    return rgb;
}

Result<std::string> inspect_project_json(const document::Project& project,
                                         const InspectOptions& options) {
    auto doc = build_project_doc(project, options);
    if (!doc) {
        return std::unexpected(doc.error());
    }
    return doc->dump(2);
}

Result<std::string> inspect_project_text(const document::Project& project,
                                         const InspectOptions& options) {
    const auto doc = build_project_doc(project, options);
    if (!doc) {
        return std::unexpected(doc.error());
    }
    std::string out;
    const auto& canvas = (*doc)["canvas"];
    out += "Cadre : " + std::to_string(canvas["width_mm"].get<double>()) + " x " +
           std::to_string(canvas["height_mm"].get<double>()) + " mm (marge " +
           std::to_string(canvas["margin_mm"].get<double>()) + " mm)\n";
    out += "Schéma .osp : v" + std::to_string((*doc)["osp_schema_version"].get<int>()) + "\n";
    const auto& counts = (*doc)["counts"];
    out += "Objets : " + std::to_string(counts["embroidery_objects"].get<std::size_t>()) +
           " de broderie, " + std::to_string(counts["vector_objects"].get<std::size_t>()) +
           " vectoriels\n";
    if (!(*doc)["sequence"].is_null()) {
        const auto& q = (*doc)["sequence"];
        out += "Points : " + std::to_string(q["stitches"].get<std::size_t>()) + ", sauts " +
               std::to_string(q["jumps"].get<std::size_t>()) + ", coupes " +
               std::to_string(q["trims"].get<std::size_t>()) + ", changements de fil " +
               std::to_string(q["color_changes"].get<std::size_t>()) + "\n";
    }
    out += "Ordre  id  type         couleur  points  nom\n";
    for (const auto& o : (*doc)["embroidery_objects"]) {
        char line[200];
        const std::size_t stitches =
            o.contains("stitches") ? o["stitches"]["total"].get<std::size_t>() : 0;
        std::snprintf(line, sizeof line, "%5zu %4llu  %-11s  %s %7zu  %s\n",
                      o["order"].get<std::size_t>(),
                      static_cast<unsigned long long>(o["id"].get<std::uint64_t>()),
                      o["type"].get<std::string>().c_str(), o["color"].get<std::string>().c_str(),
                      stitches, o["name"].get<std::string>().c_str());
        out += line;
    }
    for (const auto& w : (*doc)["warnings"]) {
        out += "! " + w["message"].get<std::string>() + "\n";
    }
    return out;
}

Result<std::string> inspect_object_json(const document::Project& project, ObjectId embroideryId,
                                        const InspectOptions& options) {
    const auto it =
        std::find_if(project.embroidery_objects.begin(), project.embroidery_objects.end(),
                     [&](const auto& e) { return e.id == embroideryId; });
    if (it == project.embroidery_objects.end()) {
        return fail(ErrorCategory::OperationImpossible,
                    "objet de broderie " + std::to_string(embroideryId.value) + " introuvable",
                    "voir `project inspect --json` pour les identifiants");
    }
    const auto rank = static_cast<std::size_t>(it - project.embroidery_objects.begin());
    std::map<std::uint64_t, ObjectStitches> stitches;
    if (options.with_stitches) {
        const auto seq = stitch_generation::effective_sequence(project);
        if (!seq) {
            return std::unexpected(seq.error());
        }
        stitches = per_object_stitches(*seq);
    }
    json doc = json::object();
    doc["schema"] = kObjectSchema;
    doc["object"] =
        embroidery_json(project, *it, rank, options, options.with_stitches ? &stitches : nullptr);
    if (const auto* v = project.findObject(it->source_vector)) {
        json geometry = json::object();
        geometry["pieces"] = v->paths.size();
        json pieces = json::array();
        for (const auto& set : v->paths) {
            const auto ring = [](const geometry::Path& path) {
                json r = json::object();
                r["closed"] = path.closed;
                json nodes = json::array();
                for (const auto& n : path.nodes) {
                    nodes.push_back(point_mm(n.pos));
                }
                r["nodes_mm"] = nodes;
                return r;
            };
            json piece = json::object();
            piece["outer"] = ring(set.outer);
            json holes = json::array();
            for (const auto& h : set.holes) {
                holes.push_back(ring(h));
            }
            piece["holes"] = holes;
            pieces.push_back(std::move(piece));
        }
        if (options.with_geometry) {
            geometry["paths"] = pieces;
        }
        doc["geometry"] = geometry;
    }
    json warnings = json::array();
    for (const auto& f : stitch_analysis::check_stitch_limits(project)) {
        if (f.object == embroideryId) {
            warnings.push_back(finding_json(f));
        }
    }
    doc["warnings"] = warnings;
    return doc.dump(2);
}

} // namespace openstitch::project_ops
