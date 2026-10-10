// SPDX-License-Identifier: Apache-2.0
#include "json_serialize.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <unordered_map>

namespace openstitch::project_io::detail {

namespace {

using nlohmann::json;

Result<std::uint32_t> strict_uint32(const json& j, const char* field);

// --- Types géométriques ------------------------------------------------------

json vec_to_json(Vec2um v) {
    return json::array({v.x.value, v.y.value});
}

Vec2um vec_from_json(const json& j) {
    return Vec2um{Micrometers{j.at(0).get<std::int32_t>()},
                  Micrometers{j.at(1).get<std::int32_t>()}};
}

json rgb_to_json(const std::array<std::uint8_t, 3>& rgb) {
    return json::array({rgb[0], rgb[1], rgb[2]});
}

std::array<std::uint8_t, 3> rgb_from_json(const json& j) {
    return {j.at(0).get<std::uint8_t>(), j.at(1).get<std::uint8_t>(), j.at(2).get<std::uint8_t>()};
}

json node_to_json(const geometry::PathNode& n) {
    json j;
    j["pos"] = vec_to_json(n.pos);
    j["smooth"] = (n.type == geometry::NodeType::Smooth);
    if (n.tan_in) {
        j["tanIn"] = vec_to_json(*n.tan_in);
    }
    if (n.tan_out) {
        j["tanOut"] = vec_to_json(*n.tan_out);
    }
    return j;
}

geometry::PathNode node_from_json(const json& j) {
    geometry::PathNode n;
    n.pos = vec_from_json(j.at("pos"));
    n.type = j.value("smooth", false) ? geometry::NodeType::Smooth : geometry::NodeType::Corner;
    if (j.contains("tanIn")) {
        n.tan_in = vec_from_json(j.at("tanIn"));
    }
    if (j.contains("tanOut")) {
        n.tan_out = vec_from_json(j.at("tanOut"));
    }
    return n;
}

json path_to_json(const geometry::Path& p) {
    json j;
    j["closed"] = p.closed;
    j["nodes"] = json::array();
    for (const auto& n : p.nodes) {
        j["nodes"].push_back(node_to_json(n));
    }
    return j;
}

geometry::Path path_from_json(const json& j) {
    geometry::Path p;
    p.closed = j.value("closed", true);
    for (const auto& n : j.at("nodes")) {
        p.nodes.push_back(node_from_json(n));
    }
    return p;
}

json path_set_to_json(const geometry::PathSet& ps) {
    json j;
    j["outer"] = path_to_json(ps.outer);
    j["holes"] = json::array();
    for (const auto& h : ps.holes) {
        j["holes"].push_back(path_to_json(h));
    }
    return j;
}

geometry::PathSet path_set_from_json(const json& j) {
    geometry::PathSet ps;
    ps.outer = path_from_json(j.at("outer"));
    for (const auto& h : j.at("holes")) {
        ps.holes.push_back(path_from_json(h));
    }
    return ps;
}

// --- Opérations d'image ------------------------------------------------------

json op_to_json(const image::ImageOp& op) {
    return std::visit(
        [](const auto& o) -> json {
            using T = std::decay_t<decltype(o)>;
            json j;
            if constexpr (std::is_same_v<T, image::CropOp>) {
                j = {{"type", "crop"}, {"x", o.x}, {"y", o.y}, {"w", o.width}, {"h", o.height}};
            } else if constexpr (std::is_same_v<T, image::FlipOp>) {
                j = {{"type", "flip"}, {"horizontal", o.horizontal}};
            } else if constexpr (std::is_same_v<T, image::Rotate90Op>) {
                j = {{"type", "rotate90"}, {"quarterTurns", o.quarter_turns}};
            } else if constexpr (std::is_same_v<T, image::GrayscaleOp>) {
                j = {{"type", "grayscale"}};
            } else if constexpr (std::is_same_v<T, image::BrightnessContrastOp>) {
                j = {{"type", "brightnessContrast"},
                     {"brightness", o.brightness},
                     {"contrast", o.contrast}};
            } else if constexpr (std::is_same_v<T, image::MedianDenoiseOp>) {
                j = {{"type", "medianDenoise"}, {"strength", o.strength}};
            } else if constexpr (std::is_same_v<T, image::QuantizeOp>) {
                j = {{"type", "quantize"}, {"colors", o.colors}};
            } else if constexpr (std::is_same_v<T, image::BilateralDenoiseOp>) {
                j = {{"type", "bilateralDenoise"}, {"strength", o.strength}};
            }
            return j;
        },
        op);
}

Result<image::ImageOp> op_from_json(const json& j) {
    const std::string type = j.at("type").get<std::string>();
    if (type == "crop") {
        return image::CropOp{j.at("x"), j.at("y"), j.at("w"), j.at("h")};
    }
    if (type == "flip") {
        return image::FlipOp{j.at("horizontal")};
    }
    if (type == "rotate90") {
        return image::Rotate90Op{j.at("quarterTurns")};
    }
    if (type == "grayscale") {
        return image::GrayscaleOp{};
    }
    if (type == "brightnessContrast") {
        return image::BrightnessContrastOp{j.at("brightness"), j.at("contrast")};
    }
    if (type == "medianDenoise") {
        return image::MedianDenoiseOp{j.at("strength")};
    }
    if (type == "quantize") {
        return image::QuantizeOp{j.at("colors")};
    }
    if (type == "bilateralDenoise") {
        return image::BilateralDenoiseOp{j.value("strength", 2)};
    }
    return fail(ErrorCategory::InvalidFile, "Opération d'image inconnue : " + type);
}

// --- Paramètres de points ----------------------------------------------------

json params_to_json(const document::StitchParams& params) {
    return std::visit(
        [](const auto& p) -> json {
            using T = std::decay_t<decltype(p)>;
            json j;
            if constexpr (std::is_same_v<T, document::RunningStitchParams>) {
                j = {{"type", "running"},
                     {"stitchLength", p.stitch_length.value},
                     {"minLength", p.min_length.value},
                     {"repeats", p.repeats}};
            } else if constexpr (std::is_same_v<T, document::TatamiParams>) {
                j = {{"type", "tatami"},
                     {"angle", p.angle.radians},
                     {"rowSpacing", p.row_spacing.value},
                     {"stitchLength", p.stitch_length.value},
                     {"inset", p.inset.value},
                     {"stagger", p.stagger},
                     {"underlayEdge", p.underlay_edge},
                     {"underlayParallel", p.underlay_parallel},
                     {"underlayInset", p.underlay_inset.value},
                     {"underlaySpacing", p.underlay_spacing.value},
                     {"hiddenUnderpath", p.hidden_underpath}};
                // HP-ENG-001/002 : écrits seulement hors défaut, un .osp inchangé reste inchangé.
                if (p.pull_compensation.value != 0) {
                    j["pullCompensation"] = p.pull_compensation.value;
                }
                if (p.underlay_mode != document::UnderlayMode::Manual) {
                    j["underlayMode"] = static_cast<int>(p.underlay_mode);
                }
                if (p.entry_point) {
                    j["entryPoint"] = {{"x", p.entry_point->x.value},
                                       {"y", p.entry_point->y.value}};
                }
            } else if constexpr (std::is_same_v<T, document::SatinParams>) {
                json rungs = json::array();
                for (const auto& r : p.rungs) {
                    json rung = {{"ax", r.a.x.value},
                                 {"ay", r.a.y.value},
                                 {"bx", r.b.x.value},
                                 {"by", r.b.y.value}};
                    if (r.link_id) {
                        rung["linkId"] = *r.link_id;
                    }
                    rungs.push_back(std::move(rung));
                }
                j = {{"type", "satin"},
                     {"railA", path_to_json(p.rail_a)},
                     {"railB", path_to_json(p.rail_b)},
                     {"rungs", std::move(rungs)},
                     {"density", p.density.value},
                     {"pullCompensation", p.pull_compensation.value},
                     {"centerUnderlay", p.center_underlay},
                     {"maxWidth", p.max_width.value},
                     {"maxWidthHard", p.max_width_hard.value},
                     {"shortStitch", static_cast<int>(p.short_stitch)},
                     {"splitStitch", static_cast<int>(p.split_stitch)},
                     {"capStart", static_cast<int>(p.cap_start)},
                     {"capEnd", static_cast<int>(p.cap_end)},
                     {"maxStitchLength", p.max_stitch_length.value},
                     {"underlayEdge", p.underlay_edge},
                     {"underlayZigzag", p.underlay_zigzag},
                     {"pullLeft", p.pull_left.value},
                     {"pullRight", p.pull_right.value},
                     {"pushStart", p.push_start.value},
                     {"pushEnd", p.push_end.value},
                     {"lockStart", static_cast<int>(p.lock_start)},
                     {"lockEnd", static_cast<int>(p.lock_end)},
                     {"lockLength", p.lock_length.value},
                     {"lockPasses", p.lock_passes}};
                if (p.entry_point) {
                    j["entryPoint"] = {{"x", p.entry_point->x.value},
                                       {"y", p.entry_point->y.value}};
                }
                if (p.exit_point) {
                    j["exitPoint"] = {{"x", p.exit_point->x.value}, {"y", p.exit_point->y.value}};
                }
                if (p.underlay_mode != document::UnderlayMode::Manual) {
                    j["underlayMode"] = static_cast<int>(p.underlay_mode);
                }
                if (p.border) {
                    j["border"] = {{"width", p.border->width.value},
                                   {"side", static_cast<int>(p.border->side)},
                                   {"corner", static_cast<int>(p.border->corner)},
                                   {"pathSet", p.border->path_set},
                                   {"ring", p.border->ring}};
                }
                if (p.topology) {
                    json topology = {{"sectionIndex", p.topology->section_index},
                                     {"sectionCount", p.topology->section_count}};
                    if (p.topology->start_junction) {
                        topology["startJunction"] = *p.topology->start_junction;
                    }
                    if (p.topology->end_junction) {
                        topology["endJunction"] = *p.topology->end_junction;
                    }
                    j["topology"] = std::move(topology);
                }
            } else if constexpr (std::is_same_v<T, document::DirectionalFillParams>) {
                json guides = json::array();
                for (const auto& g : p.guides) {
                    guides.push_back(path_to_json(g));
                }
                json breaks = json::array();
                for (const auto& b : p.break_lines) {
                    breaks.push_back(path_to_json(b));
                }
                j = {{"type", "directional"},
                     {"guides", std::move(guides)},
                     {"breakLines", std::move(breaks)},
                     {"rowSpacing", p.row_spacing.value},
                     {"stitchLength", p.stitch_length.value},
                     {"edgeWeight", p.edge_weight},
                     {"inset", p.inset.value},
                     {"stagger", p.stagger},
                     {"underlayEdge", p.underlay_edge},
                     {"underlayParallel", p.underlay_parallel},
                     {"underlayInset", p.underlay_inset.value},
                     {"underlaySpacing", p.underlay_spacing.value},
                     {"hiddenUnderpath", p.hidden_underpath},
                     {"sectorOverlap", p.sector_overlap.value},
                     {"handmade", p.handmade},
                     {"handmadeIntensity", p.handmade_intensity},
                     {"seed", p.seed}};
                if (p.underlay_mode != document::UnderlayMode::Manual) {
                    j["underlayMode"] = static_cast<int>(p.underlay_mode);
                }
                if (p.spacing_regularity != 0.0) {
                    j["spacingRegularity"] = p.spacing_regularity;
                }
                if (p.density_gradient) {
                    const auto& g = *p.density_gradient;
                    j["densityGradient"] = {{"fromX", g.from.x.value},
                                            {"fromY", g.from.y.value},
                                            {"toX", g.to.x.value},
                                            {"toY", g.to.y.value},
                                            {"spacingFrom", g.spacing_from.value},
                                            {"spacingTo", g.spacing_to.value}};
                }
            } else if constexpr (std::is_same_v<T, document::AutoSatinParams>) {
                json guides = json::array();
                for (const auto& g : p.guides) {
                    guides.push_back({{"x", g.anchor.x.value},
                                      {"y", g.anchor.y.value},
                                      {"angle", g.angle.radians},
                                      {"absolute", g.absolute}});
                }
                j = {{"type", "autoSatin"},
                     {"guides", std::move(guides)},
                     {"spacing", p.spacing.value},
                     {"splitStitch", static_cast<int>(p.split_stitch)},
                     {"splitThreshold", p.split_threshold.value},
                     {"splitLength", p.split_length.value},
                     {"shortStitch", static_cast<int>(p.short_stitch)},
                     {"pullCompensation", p.pull_compensation.value},
                     {"centerUnderlay", p.center_underlay},
                     {"underlayEdge", p.underlay_edge},
                     {"underlayZigzag", p.underlay_zigzag},
                     {"pullLeft", p.pull_left.value},
                     {"pullRight", p.pull_right.value},
                     {"pushStart", p.push_start.value},
                     {"pushEnd", p.push_end.value},
                     {"capStart", static_cast<int>(p.cap_start)},
                     {"capEnd", static_cast<int>(p.cap_end)},
                     {"lockStart", static_cast<int>(p.lock_start)},
                     {"lockEnd", static_cast<int>(p.lock_end)},
                     {"lockLength", p.lock_length.value},
                     {"lockPasses", p.lock_passes}};
                if (p.underlay_mode != document::UnderlayMode::Manual) {
                    j["underlayMode"] = static_cast<int>(p.underlay_mode);
                }
                if (p.entry_point) {
                    j["entryPoint"] = {{"x", p.entry_point->x.value},
                                       {"y", p.entry_point->y.value}};
                }
                if (p.exit_point) {
                    j["exitPoint"] = {{"x", p.exit_point->x.value}, {"y", p.exit_point->y.value}};
                }
            }
            return j;
        },
        params);
}

Result<document::StitchParams> params_from_json(const json& j) {
    const std::string type = j.at("type").get<std::string>();
    if (type == "running") {
        document::RunningStitchParams p;
        p.stitch_length = Micrometers{j.at("stitchLength")};
        p.min_length = Micrometers{j.at("minLength")};
        p.repeats = j.at("repeats");
        return document::StitchParams{p};
    }
    if (type == "tatami") {
        document::TatamiParams p;
        p.angle = Angle{j.at("angle")};
        p.row_spacing = Micrometers{j.at("rowSpacing")};
        p.stitch_length = Micrometers{j.at("stitchLength")};
        p.inset = Micrometers{j.at("inset")};
        p.stagger = j.at("stagger");
        // Tatami avancé (Lot 7) : clés optionnelles, rétrocompatibles.
        p.underlay_edge = j.value("underlayEdge", false);
        p.underlay_parallel = j.value("underlayParallel", false);
        p.underlay_inset = Micrometers{j.value("underlayInset", 600)};
        p.underlay_spacing = Micrometers{j.value("underlaySpacing", 2'000)};
        p.hidden_underpath = j.value("hiddenUnderpath", false);
        // HP-ENG-001/002 : absents d'un .osp antérieur -> défauts du modèle. Valeurs bornées :
        // un fichier édité à la main ne doit pas produire une compensation absurde.
        p.pull_compensation = Micrometers{std::clamp(j.value("pullCompensation", 0), 0, 3'000)};
        p.underlay_mode =
            static_cast<document::UnderlayMode>(std::clamp(j.value("underlayMode", 0), 0, 1));
        if (j.contains("entryPoint")) {
            p.entry_point = Vec2um{Micrometers{j.at("entryPoint").at("x")},
                                   Micrometers{j.at("entryPoint").at("y")}};
        }
        return document::StitchParams{p};
    }
    if (type == "satin") {
        document::SatinParams p;
        p.rail_a = path_from_json(j.at("railA"));
        p.rail_b = path_from_json(j.at("railB"));
        // Barreaux : optionnels (projets antérieurs au schéma v2 -> aucun).
        if (j.contains("rungs")) {
            for (const auto& r : j.at("rungs")) {
                document::SatinRung rung{Vec2um{Micrometers{r.at("ax")}, Micrometers{r.at("ay")}},
                                         Vec2um{Micrometers{r.at("bx")}, Micrometers{r.at("by")}}};
                if (r.contains("linkId")) {
                    auto linkId = strict_uint32(r.at("linkId"), "linkId");
                    if (!linkId) {
                        return std::unexpected(linkId.error());
                    }
                    rung.link_id = *linkId;
                }
                p.rungs.push_back(std::move(rung));
            }
        }
        p.density = Micrometers{j.at("density")};
        p.pull_compensation = Micrometers{j.at("pullCompensation")};
        p.center_underlay = j.at("centerUnderlay");
        p.max_width = Micrometers{j.at("maxWidth")};
        p.max_width_hard = Micrometers{j.value("maxWidthHard", 48'000)};
        // Finitions Lot 3 : optionnelles (projets antérieurs -> défauts).
        p.short_stitch = static_cast<document::SatinShortStitch>(j.value("shortStitch", 0));
        p.split_stitch = static_cast<document::SatinSplit>(j.value("splitStitch", 0));
        p.cap_start = static_cast<document::SatinCap>(j.value("capStart", 0));
        p.cap_end = static_cast<document::SatinCap>(j.value("capEnd", 0));
        p.max_stitch_length = Micrometers{j.value("maxStitchLength", 7'000)};
        p.underlay_edge = j.value("underlayEdge", false);
        p.underlay_zigzag = j.value("underlayZigzag", false);
        p.pull_left = Micrometers{j.value("pullLeft", 0)};
        p.pull_right = Micrometers{j.value("pullRight", 0)};
        p.push_start = Micrometers{j.value("pushStart", 0)};
        p.push_end = Micrometers{j.value("pushEnd", 0)};
        p.lock_start = static_cast<document::SatinLock>(j.value("lockStart", 0));
        p.lock_end = static_cast<document::SatinLock>(j.value("lockEnd", 0));
        p.lock_length = Micrometers{j.value("lockLength", 800)};
        p.lock_passes = j.value("lockPasses", 2);
        p.underlay_mode =
            static_cast<document::UnderlayMode>(std::clamp(j.value("underlayMode", 0), 0, 1));
        if (j.contains("border") && j.at("border").is_object()) {
            const auto& bj = j.at("border");
            document::BorderSatinSpec spec;
            spec.width = Micrometers{std::clamp(bj.value("width", 3'000), 500, 20'000)};
            spec.side = static_cast<document::BorderSide>(std::clamp(bj.value("side", 0), 0, 2));
            spec.corner =
                static_cast<document::BorderCorner>(std::clamp(bj.value("corner", 0), 0, 1));
            spec.path_set =
                static_cast<std::uint32_t>(std::clamp(bj.value("pathSet", 0), 0, 100'000));
            spec.ring = static_cast<std::uint32_t>(std::clamp(bj.value("ring", 0), 0, 100'000));
            p.border = spec;
        }
        if (j.contains("entryPoint")) {
            p.entry_point = Vec2um{Micrometers{j.at("entryPoint").at("x")},
                                   Micrometers{j.at("entryPoint").at("y")}};
        }
        if (j.contains("exitPoint")) {
            p.exit_point = Vec2um{Micrometers{j.at("exitPoint").at("x")},
                                  Micrometers{j.at("exitPoint").at("y")}};
        }
        if (j.contains("topology")) {
            const auto& topology = j.at("topology");
            document::SatinSectionTopology section;
            auto sectionIndex = strict_uint32(topology.at("sectionIndex"), "sectionIndex");
            auto sectionCount = strict_uint32(topology.at("sectionCount"), "sectionCount");
            if (!sectionIndex) {
                return std::unexpected(sectionIndex.error());
            }
            if (!sectionCount) {
                return std::unexpected(sectionCount.error());
            }
            section.section_index = *sectionIndex;
            section.section_count = *sectionCount;
            if (section.section_count == 0 || section.section_index >= section.section_count) {
                return fail(ErrorCategory::InvalidFile,
                            "Topologie satin invalide : index de section hors réseau");
            }
            if (topology.contains("startJunction")) {
                auto start = strict_uint32(topology.at("startJunction"), "startJunction");
                if (!start) {
                    return std::unexpected(start.error());
                }
                section.start_junction = *start;
            }
            if (topology.contains("endJunction")) {
                auto end = strict_uint32(topology.at("endJunction"), "endJunction");
                if (!end) {
                    return std::unexpected(end.error());
                }
                section.end_junction = *end;
            }
            p.topology = section;
        }
        return document::StitchParams{p};
    }
    if (type == "directional") {
        // Remplissage directionnel : toutes les clés sauf `type` sont
        // optionnelles (défauts du modèle), pour qu'un fichier écrit par une
        // version ultérieure ajoutant des réglages reste lisible.
        document::DirectionalFillParams p;
        if (j.contains("guides")) {
            for (const auto& g : j.at("guides")) {
                p.guides.push_back(path_from_json(g));
            }
        }
        if (j.contains("breakLines")) {
            for (const auto& b : j.at("breakLines")) {
                p.break_lines.push_back(path_from_json(b));
            }
        }
        p.row_spacing = Micrometers{j.value("rowSpacing", 400)};
        p.stitch_length = Micrometers{j.value("stitchLength", 3'000)};
        p.edge_weight = j.value("edgeWeight", 0.0);
        p.inset = Micrometers{j.value("inset", 200)};
        p.stagger = j.value("stagger", 2);
        p.underlay_edge = j.value("underlayEdge", false);
        p.underlay_parallel = j.value("underlayParallel", false);
        p.underlay_inset = Micrometers{j.value("underlayInset", 600)};
        p.underlay_spacing = Micrometers{j.value("underlaySpacing", 2'000)};
        p.hidden_underpath = j.value("hiddenUnderpath", true);
        p.sector_overlap = Micrometers{j.value("sectorOverlap", 250)};
        p.handmade = j.value("handmade", false);
        p.handmade_intensity = j.value("handmadeIntensity", 50);
        p.underlay_mode =
            static_cast<document::UnderlayMode>(std::clamp(j.value("underlayMode", 0), 0, 1));
        // Valeur hors [0 ; 1] ou non finie : bornée (fichier édité à la main).
        p.spacing_regularity = std::clamp(j.value("spacingRegularity", 0.0), 0.0, 1.0);
        if (j.contains("densityGradient") && j.at("densityGradient").is_object()) {
            const auto& g = j.at("densityGradient");
            document::DensityGradient grad;
            grad.from = Vec2um{Micrometers{g.value("fromX", 0)}, Micrometers{g.value("fromY", 0)}};
            grad.to = Vec2um{Micrometers{g.value("toX", 0)}, Micrometers{g.value("toY", 0)}};
            grad.spacing_from = Micrometers{g.value("spacingFrom", 400)};
            grad.spacing_to = Micrometers{g.value("spacingTo", 400)};
            p.density_gradient = grad;
        }
        if (j.contains("seed")) {
            auto seed = strict_uint32(j.at("seed"), "seed");
            if (!seed) {
                return std::unexpected(seed.error());
            }
            p.seed = *seed;
        }
        return document::StitchParams{p};
    }
    if (type == "autoSatin") {
        // Auto-satin : toutes les clés sauf `type` sont optionnelles (défauts du
        // modèle), pour qu'un fichier écrit par une version ultérieure reste lisible.
        document::AutoSatinParams p;
        // Un fichier édité à la main ou corrompu ne doit jamais figer la génération
        // (espacement nul), tronquer une coordonnée ni fabriquer une énumération invalide.
        const auto badNumber = [](const json& v, const char* name, std::int64_t lo,
                                  std::int64_t hi) -> std::optional<std::string> {
            if (v.is_null()) {
                return std::nullopt; // absent : valeur par défaut
            }
            if (!v.is_number_integer()) {
                return std::string("autoSatin : « ") + name + " » doit être un entier";
            }
            const auto n =
                v.is_number_unsigned()
                    ? static_cast<std::int64_t>(std::min<std::uint64_t>(
                          v.get<std::uint64_t>(),
                          static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))
                    : v.get<std::int64_t>();
            if (n < lo || n > hi) {
                return std::string("autoSatin : « ") + name + " » hors limites";
            }
            return std::nullopt;
        };
        constexpr std::int64_t kCoordMax = 1'000'000'000;
        const auto field = [&](const char* name) {
            return j.contains(name) ? j.at(name) : json(nullptr);
        };
        struct Range {
            const char* name;
            std::int64_t lo, hi;
        };
        for (const Range& r :
             {Range{"spacing", 50, 5'000}, Range{"splitThreshold", 500, 1'000'000},
              Range{"splitLength", 500, 1'000'000}, Range{"splitStitch", 0, 3},
              Range{"shortStitch", 0, 3}, Range{"capStart", 0, 3}, Range{"capEnd", 0, 3},
              Range{"lockStart", 0, 3}, Range{"lockEnd", 0, 3}, Range{"lockPasses", 0, 10},
              Range{"lockLength", 0, 20'000}, Range{"pullCompensation", -5'000, 5'000},
              Range{"pullLeft", -5'000, 5'000}, Range{"pullRight", -5'000, 5'000},
              Range{"pushStart", -5'000, 5'000}, Range{"pushEnd", -5'000, 5'000},
              Range{"underlayMode", 0, 1}}) {
            if (const auto err = badNumber(field(r.name), r.name, r.lo, r.hi)) {
                return fail(ErrorCategory::InvalidFile, *err);
            }
        }
        if (j.contains("guides")) {
            for (const auto& g : j.at("guides")) {
                for (const char* axis : {"x", "y"}) {
                    if (const auto err = badNumber(g.contains(axis) ? g.at(axis) : json(nullptr),
                                                   axis, -kCoordMax, kCoordMax)) {
                        return fail(ErrorCategory::InvalidFile, *err);
                    }
                }
            }
        }
        if (j.contains("guides")) {
            for (const auto& g : j.at("guides")) {
                document::AutoSatinGuide guide;
                guide.anchor = Vec2um{Micrometers{g.at("x")}, Micrometers{g.at("y")}};
                guide.angle = Angle{g.value("angle", 0.0)};
                guide.absolute = g.value("absolute", false);
                p.guides.push_back(guide);
            }
        }
        p.spacing = Micrometers{j.value("spacing", 400)};
        p.split_stitch = static_cast<document::SatinSplit>(
            j.value("splitStitch", static_cast<int>(p.split_stitch)));
        p.split_threshold = Micrometers{j.value("splitThreshold", 7'000)};
        p.split_length = Micrometers{j.value("splitLength", 4'000)};
        p.short_stitch = static_cast<document::SatinShortStitch>(
            j.value("shortStitch", static_cast<int>(p.short_stitch)));
        p.pull_compensation = Micrometers{j.value("pullCompensation", 0)};
        p.center_underlay = j.value("centerUnderlay", true);
        p.underlay_edge = j.value("underlayEdge", false);
        p.underlay_zigzag = j.value("underlayZigzag", false);
        p.pull_left = Micrometers{j.value("pullLeft", 0)};
        p.pull_right = Micrometers{j.value("pullRight", 0)};
        p.push_start = Micrometers{j.value("pushStart", 0)};
        p.push_end = Micrometers{j.value("pushEnd", 0)};
        p.cap_start = static_cast<document::SatinCap>(j.value("capStart", 0));
        p.cap_end = static_cast<document::SatinCap>(j.value("capEnd", 0));
        p.lock_start = static_cast<document::SatinLock>(j.value("lockStart", 0));
        p.lock_end = static_cast<document::SatinLock>(j.value("lockEnd", 0));
        p.lock_length = Micrometers{j.value("lockLength", 800)};
        p.lock_passes = j.value("lockPasses", 2);
        p.underlay_mode = static_cast<document::UnderlayMode>(j.value("underlayMode", 0));
        if (j.contains("entryPoint")) {
            p.entry_point = Vec2um{Micrometers{j.at("entryPoint").at("x")},
                                   Micrometers{j.at("entryPoint").at("y")}};
        }
        if (j.contains("exitPoint")) {
            p.exit_point = Vec2um{Micrometers{j.at("exitPoint").at("x")},
                                  Micrometers{j.at("exitPoint").at("y")}};
        }
        return document::StitchParams{p};
    }
    return fail(ErrorCategory::InvalidFile, "Type de point inconnu : " + type);
}

// --- Retouches manuelles (Lot 8.1, ADR-014) ----------------------------------
//
// Validation stricte : un JSON malformé ou hors bornes renvoie une erreur
// utile (`ErrorCategory::InvalidFile`), jamais un comportement indéfini
// (troncature silencieuse d'un entier hors plage, par exemple). `nlohmann`
// stocke un entier positif dépassant l'`int64` comme `number_unsigned`
// (`std::uint64_t` exact, pas de conversion par `double`) : `editedFingerprint`
// peut donc dépasser 2^53 sans perte de bits, comme les `ObjectId`/`RegionId`
// existants (cf. `vo["id"]`/`ObjectId{...get<std::uint64_t>()}` ci-dessus).

Result<std::int32_t> strict_int32(const json& j, const char* field) {
    if (j.is_number_unsigned()) {
        const auto v = j.get<std::uint64_t>();
        if (v > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
            return fail(ErrorCategory::InvalidFile,
                        std::string("Retouche invalide : coordonnée hors limites (") + field + ")");
        }
        return static_cast<std::int32_t>(v);
    }
    if (j.is_number_integer()) {
        const auto v = j.get<std::int64_t>();
        if (v < std::numeric_limits<std::int32_t>::min() ||
            v > std::numeric_limits<std::int32_t>::max()) {
            return fail(ErrorCategory::InvalidFile,
                        std::string("Retouche invalide : coordonnée hors limites (") + field + ")");
        }
        return static_cast<std::int32_t>(v);
    }
    return fail(ErrorCategory::InvalidFile,
                std::string("Retouche invalide : coordonnée non entière (") + field + ")");
}

Result<std::size_t> strict_index(const json& j) {
    // Verifie explicitement la borne avant cast : sur une plateforme ou
    // size_t < uint64_t (32 bits), un index JSON valide en uint64 pourrait
    // sinon tronquer silencieusement lors du static_cast.
    constexpr auto kMaxIndex = static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
    if (j.is_number_unsigned()) {
        const auto v = j.get<std::uint64_t>();
        if (v > kMaxIndex) {
            return fail(ErrorCategory::InvalidFile, "Retouche invalide : index hors limites");
        }
        return static_cast<std::size_t>(v);
    }
    if (j.is_number_integer()) {
        const auto v = j.get<std::int64_t>();
        if (v < 0) {
            return fail(ErrorCategory::InvalidFile, "Retouche invalide : index négatif");
        }
        if (static_cast<std::uint64_t>(v) > kMaxIndex) {
            return fail(ErrorCategory::InvalidFile, "Retouche invalide : index hors limites");
        }
        return static_cast<std::size_t>(v);
    }
    return fail(ErrorCategory::InvalidFile, "Retouche invalide : index non entier");
}

Result<std::uint64_t> strict_uint64(const json& j, const char* field) {
    if (j.is_number_unsigned()) {
        return j.get<std::uint64_t>();
    }
    if (j.is_number_integer()) {
        const auto v = j.get<std::int64_t>();
        if (v < 0) {
            return fail(ErrorCategory::InvalidFile,
                        std::string("Champ invalide (négatif) : ") + field);
        }
        return static_cast<std::uint64_t>(v);
    }
    return fail(ErrorCategory::InvalidFile, std::string("Champ non entier : ") + field);
}

Result<std::uint32_t> strict_uint32(const json& j, const char* field) {
    auto v = strict_uint64(j, field);
    if (!v) {
        return std::unexpected(v.error());
    }
    if (*v > std::numeric_limits<std::uint32_t>::max()) {
        return fail(ErrorCategory::InvalidFile, std::string("Champ hors limites : ") + field);
    }
    return static_cast<std::uint32_t>(*v);
}

Result<Vec2um> override_pos_from_json(const json& j) {
    if (!j.is_object() || !j.contains("x") || !j.contains("y")) {
        return fail(ErrorCategory::InvalidFile, "Retouche invalide : position malformée");
    }
    auto x = strict_int32(j.at("x"), "x");
    if (!x) {
        return std::unexpected(x.error());
    }
    auto y = strict_int32(j.at("y"), "y");
    if (!y) {
        return std::unexpected(y.error());
    }
    return Vec2um{Micrometers{*x}, Micrometers{*y}};
}

json override_to_json(const document::StitchOverride& ov) {
    json j;
    j["index"] = static_cast<std::uint64_t>(ov.base_index);
    if (ov.moved_to) {
        j["pos"] = {{"x", ov.moved_to->x.value}, {"y", ov.moved_to->y.value}};
    }
    if (ov.forced_type) {
        j["type"] = (*ov.forced_type == document::StitchPointType::Jump) ? "jump" : "stitch";
    }
    j["trimAfter"] = ov.trim_after;
    return j;
}

// Un `index` en double dans le tableau JSON est refusé (InvalidFile), il
// n'est PAS fusionné champ à champ avec l'entrée déjà lue. Décision revue
// (2026-08-01, audit) : le cadrage Lot 8 (SS4) proposait initialement de
// fusionner pour tolérer un outil tiers scindant les champs d'un même
// `base_index` sur plusieurs entrées JSON -- mais aucun producteur réel de ce
// format ne fait ça : les commandes (`begin_stitch_edit`,
// `project_commands.hpp`) et `override_to_json` ci-dessus garantissent chacun
// UNE seule entrée JSON par `base_index`. Un doublon ne peut donc venir que
// d'une écriture manuelle ou d'une corruption -- le rejeter explicitement
// suit la même politique de validation stricte que le reste de ce fichier
// (jamais de réinterprétation silencieuse d'un JSON suspect). Voir
// `docs/lot8-manual-editing-design.md` §4 (exemple mis à jour en conséquence).
Result<std::vector<document::StitchOverride>> overrides_from_json(const json& arr) {
    std::vector<document::StitchOverride> result;
    std::unordered_map<std::size_t, bool> seen_index;

    for (const auto& entry : arr) {
        if (!entry.is_object() || !entry.contains("index")) {
            return fail(ErrorCategory::InvalidFile, "Retouche invalide : index manquant");
        }
        auto idx = strict_index(entry.at("index"));
        if (!idx) {
            return std::unexpected(idx.error());
        }
        if (!seen_index.emplace(*idx, true).second) {
            return fail(ErrorCategory::InvalidFile,
                        "Retouche invalide : index en double (" + std::to_string(*idx) + ")");
        }

        document::StitchOverride ov;
        ov.base_index = *idx;

        if (entry.contains("pos")) {
            auto pos = override_pos_from_json(entry.at("pos"));
            if (!pos) {
                return std::unexpected(pos.error());
            }
            ov.moved_to = *pos;
        }
        if (entry.contains("type")) {
            const auto& t = entry.at("type");
            if (!t.is_string()) {
                return fail(ErrorCategory::InvalidFile, "Retouche invalide : type non textuel");
            }
            const std::string ts = t.get<std::string>();
            if (ts == "stitch") {
                ov.forced_type = document::StitchPointType::Stitch;
            } else if (ts == "jump") {
                ov.forced_type = document::StitchPointType::Jump;
            } else {
                return fail(ErrorCategory::InvalidFile,
                            "Retouche invalide : type inconnu (" + ts + ")");
            }
        }
        if (entry.contains("trimAfter")) {
            const auto& ta = entry.at("trimAfter");
            if (!ta.is_boolean()) {
                return fail(ErrorCategory::InvalidFile,
                            "Retouche invalide : trimAfter non booléen");
            }
            ov.trim_after = ta.get<bool>();
        }

        // Une entrée qui ne porte aucune modification effective (ni position,
        // ni type forcé, ni coupe demandée) est un no-op déguisé en retouche :
        // aucune commande du Lot 8.1 n'en produit (cf. Point 5,
        // `SetStitchTrimCommand`), donc seul un JSON malformé/hand-écrit peut
        // en contenir un -- refusé plutôt que silencieusement absorbé.
        if (!ov.moved_to.has_value() && !ov.forced_type.has_value() && !ov.trim_after) {
            return fail(ErrorCategory::InvalidFile,
                        "Retouche invalide : entrée sans modification effective (index " +
                            std::to_string(*idx) + ")");
        }

        result.push_back(ov);
    }
    return result;
}

// --- Lettrage (HP-TXT-001) ---------------------------------------------------

json text_to_json(const document::TextObject& t) {
    json j;
    j["id"] = t.id.value;
    j["text"] = t.text;
    j["fontFamily"] = t.font.family;
    j["fontFile"] = t.font.file;
    j["fontBuiltin"] = t.font.builtin;
    j["faceIndex"] = t.font.face_index;
    j["capHeight"] = t.cap_height.value;
    j["letterSpacing"] = t.letter_spacing.value;
    j["wordSpacing"] = t.word_spacing.value;
    j["lineSpacing"] = t.line_spacing;
    j["kerning"] = t.kerning;
    j["align"] = static_cast<int>(t.align);
    j["justifyWidth"] = t.justify_width.value;
    j["origin"] = vec_to_json(t.origin);
    j["rotation"] = t.rotation.radians;
    j["rgb"] = rgb_to_json(t.rgb);
    j["fill"] = static_cast<int>(t.fill);
    j["maxSatinWidth"] = t.max_satin_width.value;
    j["density"] = t.density.value;
    return j;
}

document::TextObject text_from_json(const json& j) {
    const document::TextObject d;
    document::TextObject t;
    t.id = ObjectId{j.at("id").get<std::uint64_t>()};
    t.text = j.value("text", std::string{});
    t.font.family = j.value("fontFamily", std::string{});
    t.font.file = j.value("fontFile", std::string{});
    t.font.builtin = j.value("fontBuiltin", std::string{});
    t.font.face_index = j.value("faceIndex", 0);
    t.cap_height = Micrometers{j.value("capHeight", d.cap_height.value)};
    t.letter_spacing = Micrometers{j.value("letterSpacing", d.letter_spacing.value)};
    t.word_spacing = Micrometers{j.value("wordSpacing", d.word_spacing.value)};
    t.line_spacing = j.value("lineSpacing", d.line_spacing);
    t.kerning = j.value("kerning", d.kerning);
    t.align = static_cast<document::TextAlign>(std::clamp(j.value("align", 0), 0, 3));
    t.justify_width = Micrometers{j.value("justifyWidth", d.justify_width.value)};
    if (j.contains("origin")) {
        t.origin = vec_from_json(j.at("origin"));
    }
    t.rotation = Angle{j.value("rotation", 0.0)};
    if (j.contains("rgb")) {
        t.rgb = rgb_from_json(j.at("rgb"));
    }
    t.fill = static_cast<document::TextFill>(std::clamp(j.value("fill", 0), 0, 3));
    t.max_satin_width = Micrometers{j.value("maxSatinWidth", d.max_satin_width.value)};
    t.density = Micrometers{j.value("density", d.density.value)};
    return t;
}

} // namespace

json project_to_json(const document::Project& project) {
    json j;
    j["mmPerPx"] = project.mm_per_px.value;
    j["objectIdLast"] = project.object_ids.last();
    j["canvas"] = {{"width", project.canvas.width.value}, {"height", project.canvas.height.value}};

    j["ops"] = json::array();
    for (const auto& op : project.ops) {
        j["ops"].push_back(op_to_json(op));
    }

    if (project.segmentation) {
        const auto& seg = *project.segmentation;
        json s;
        s["width"] = seg.width;
        s["height"] = seg.height;
        s["regions"] = json::array();
        for (const auto& slot : seg.region_slots) {
            if (slot) {
                s["regions"].push_back({{"id", slot->id.value},
                                        {"rgb", rgb_to_json(slot->rgb)},
                                        {"pixels", slot->pixel_count}});
            } else {
                s["regions"].push_back(nullptr);
            }
        }
        j["segmentation"] = s;
    }

    j["vectorObjects"] = json::array();
    for (const auto& v : project.vector_objects) {
        json vo;
        vo["id"] = v.id.value;
        vo["name"] = v.name;
        vo["rgb"] = rgb_to_json(v.rgb);
        vo["visible"] = v.visible;
        if (v.source_region) {
            vo["sourceRegion"] = v.source_region->value;
        }
        if (v.text_owner) {
            vo["textOwner"] = v.text_owner->value;
        }
        vo["paths"] = json::array();
        for (const auto& ps : v.paths) {
            vo["paths"].push_back(path_set_to_json(ps));
        }
        j["vectorObjects"].push_back(vo);
    }

    // Objets texte (schéma v6) : clé absente quand le projet n'en a pas.
    if (!project.text_objects.empty()) {
        j["textObjects"] = json::array();
        for (const auto& t : project.text_objects) {
            j["textObjects"].push_back(text_to_json(t));
        }
    }

    j["embroideryObjects"] = json::array();
    for (const auto& e : project.embroidery_objects) {
        json eo;
        eo["id"] = e.id.value;
        eo["name"] = e.name;
        eo["sourceVector"] = e.source_vector.value;
        eo["rgb"] = rgb_to_json(e.rgb);
        eo["visible"] = e.visible;
        // §21/§24 du plan de refonte satin (2026-08-14) : AutoChoice=0,
        // ForcedUserChoice=1 -- même convention que les autres enums de ce
        // fichier (int brut, jamais une table de correspondance texte).
        eo["intent"] = static_cast<int>(e.intent);
        if (e.join != document::JoinMode::Inherit) {
            eo["join"] = static_cast<int>(e.join); // HP-ENG-010 : absent = Inherit
        }
        eo["params"] = params_to_json(e.params);
        // Schéma v6 (HP-THR-004) : fil de nuancier assigné, écrit seulement s'il existe.
        if (e.thread) {
            eo["thread"] = {{"chart", e.thread->chart_id}, {"code", e.thread->code}};
        }
        if (!e.overrides.empty()) {
            eo["overrides"] = json::array();
            for (const auto& ov : e.overrides) {
                eo["overrides"].push_back(override_to_json(ov));
            }
            eo["editedFingerprint"] = e.edited_fingerprint;
            eo["editedPointCount"] = e.edited_point_count;
        }
        j["embroideryObjects"].push_back(eo);
    }

    // Finitions de la séquence (Lots E/F). Toujours écrites ; absentes à la
    // lecture (projet antérieur) -> SequenceFinishing::legacy().
    const auto& f = project.finishing;
    j["finishing"] = {{"enabled", f.enabled},
                      {"trimThreshold", f.trim_threshold.value},
                      {"trimBeforeColorChange", f.trim_before_color_change},
                      {"lockType", static_cast<int>(f.lock_type)},
                      {"lockLength", f.lock_length.value},
                      {"lockPasses", f.lock_passes},
                      {"filterShortStitches", f.filter_short_stitches},
                      {"minStitchLength", f.min_stitch_length.value},
                      {"splitLongStitches", f.split_long_stitches},
                      {"maxStitchLength", f.max_stitch_length.value},
                      {"autoJoin", f.auto_join}};

    // AD-04 : design importé (DST aujourd'hui) -- donnée source immuable,
    // absente d'un projet qui n'en a pas (comportement historique inchangé).
    // `source` de chaque commande est délibérément omis : toujours
    // `ObjectId{}` pour un design importé (0 = manuel/importé, cf.
    // `stitch/sequence.hpp`), jamais relu.
    if (project.imported_design) {
        const auto& im = *project.imported_design;
        json id;
        id["sourceFormat"] = im.source_format;
        id["sequence"] = json::array();
        for (const auto& cmd : im.sequence.commands) {
            id["sequence"].push_back({{"pos", vec_to_json(cmd.pos)},
                                      {"type", static_cast<int>(cmd.type)},
                                      {"pass", static_cast<int>(cmd.pass)}});
        }
        id["colorBlocks"] = json::array();
        for (const auto& b : im.color_blocks) {
            json bj{{"rgb", rgb_to_json(b.rgb)}, {"start", b.start}, {"end", b.end}};
            if (b.thread_key) {
                bj["threadChart"] = b.thread_key->chart_id;
                bj["threadCode"] = b.thread_key->code;
            }
            id["colorBlocks"].push_back(bj);
        }
        j["importedDesign"] = id;
    }

    return j;
}

Result<document::Project> project_from_json(const json& j) {
    document::Project project;
    try {
        project.mm_per_px = Millimeters{j.at("mmPerPx").get<double>()};
        project.object_ids.reset(j.value("objectIdLast", std::uint64_t{0}));
        // Cadre : optionnel (les projets antérieurs n'en avaient pas -> 100x100).
        if (j.contains("canvas")) {
            const auto& c = j.at("canvas");
            project.canvas.width = Micrometers{c.value("width", 100'000)};
            project.canvas.height = Micrometers{c.value("height", 100'000)};
        }

        for (const auto& op : j.at("ops")) {
            auto parsed = op_from_json(op);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            project.ops.push_back(std::move(*parsed));
        }

        if (j.contains("segmentation")) {
            const auto& s = j.at("segmentation");
            segmentation::Segmentation seg;
            seg.width = s.at("width");
            seg.height = s.at("height");
            for (const auto& r : s.at("regions")) {
                if (r.is_null()) {
                    seg.region_slots.push_back(std::nullopt);
                } else {
                    segmentation::Region region;
                    region.id = RegionId{r.at("id").get<std::uint64_t>()};
                    region.rgb = rgb_from_json(r.at("rgb"));
                    region.pixel_count = r.at("pixels").get<std::size_t>();
                    seg.region_slots.push_back(region);
                }
            }
            // labels remplis plus tard par project_io (binaire).
            project.segmentation = std::move(seg);
        }

        for (const auto& vo : j.at("vectorObjects")) {
            document::VectorObject v;
            v.id = ObjectId{vo.at("id").get<std::uint64_t>()};
            v.name = vo.at("name");
            v.rgb = rgb_from_json(vo.at("rgb"));
            v.visible = vo.value("visible", true);
            if (vo.contains("sourceRegion")) {
                v.source_region = RegionId{vo.at("sourceRegion").get<std::uint64_t>()};
            }
            if (vo.contains("textOwner")) {
                v.text_owner = ObjectId{vo.at("textOwner").get<std::uint64_t>()};
            }
            for (const auto& ps : vo.at("paths")) {
                v.paths.push_back(path_set_from_json(ps));
            }
            project.vector_objects.push_back(std::move(v));
        }

        if (j.contains("textObjects")) {
            for (const auto& tj : j.at("textObjects")) {
                project.text_objects.push_back(text_from_json(tj));
            }
        }

        for (const auto& eo : j.at("embroideryObjects")) {
            document::EmbroideryObject e;
            e.id = ObjectId{eo.at("id").get<std::uint64_t>()};
            e.name = eo.at("name");
            e.source_vector = ObjectId{eo.at("sourceVector").get<std::uint64_t>()};
            e.rgb = rgb_from_json(eo.at("rgb"));
            e.visible = eo.value("visible", true);
            // Absent (projets antérieurs au §21/§24, 2026-08-14) -> AutoChoice
            // (valeur 0, comportement historique implicite désormais explicite).
            e.intent = static_cast<document::EmbroideryIntent>(eo.value("intent", 0));
            e.join = static_cast<document::JoinMode>(std::clamp(eo.value("join", 0), 0, 2));
            auto params = params_from_json(eo.at("params"));
            if (!params) {
                return std::unexpected(params.error());
            }
            e.params = std::move(*params);

            // Fil de nuancier (schéma v6) : absent dans un projet v1..v5 -> couleur libre.
            if (eo.contains("thread")) {
                const auto& tj = eo.at("thread");
                if (!tj.is_object() || !tj.contains("chart") || !tj.contains("code") ||
                    !tj.at("chart").is_string() || !tj.at("code").is_string()) {
                    return fail(ErrorCategory::InvalidFile,
                                "Fil invalide : « thread » doit porter « chart » et « code »");
                }
                e.thread = thread_palette::ThreadKey{tj.at("chart").get<std::string>(),
                                                     tj.at("code").get<std::string>()};
            }

            // Retouches manuelles (Lot 8.1) : champs absents = Clean, comportement
            // v1/v2 inchangé (overrides vide, fingerprint/compteur à zéro).
            if (eo.contains("overrides")) {
                const auto& overridesJson = eo.at("overrides");
                if (!overridesJson.is_array()) {
                    return fail(ErrorCategory::InvalidFile,
                                "Retouche invalide : overrides doit être un tableau");
                }
                auto overrides = overrides_from_json(overridesJson);
                if (!overrides) {
                    return std::unexpected(overrides.error());
                }
                e.overrides = std::move(*overrides);
                if (!e.overrides.empty()) {
                    // Présence explicite exigée : un tableau non vide sans ces
                    // deux clés est un document malformé (une empreinte/compteur
                    // à zéro par défaut masquerait la corruption au lieu de la
                    // signaler -- cf. §1 de la revue). Contraste volontaire avec
                    // le tableau vide (Clean), où ces mêmes clés sont ignorées
                    // sans erreur si présentes (normalisation Clean déterministe,
                    // voir test dédié).
                    if (!eo.contains("editedFingerprint") || !eo.contains("editedPointCount")) {
                        return fail(ErrorCategory::InvalidFile,
                                    "Retouche invalide : editedFingerprint/editedPointCount "
                                    "manquant pour un tableau overrides non vide");
                    }
                    auto fp = strict_uint64(eo.at("editedFingerprint"), "editedFingerprint");
                    if (!fp) {
                        return std::unexpected(fp.error());
                    }
                    auto count = strict_uint32(eo.at("editedPointCount"), "editedPointCount");
                    if (!count) {
                        return std::unexpected(count.error());
                    }
                    e.edited_fingerprint = *fp;
                    e.edited_point_count = *count;
                }
            }

            project.embroidery_objects.push_back(std::move(e));
        }

        // Finitions (Lots E/F) : bloc absent = projet antérieur, aucune
        // finition (séquence identique à avant). Champ absent dans un bloc
        // présent = valeur par défaut actuelle.
        if (j.contains("finishing")) {
            const auto& fj = j.at("finishing");
            const document::SequenceFinishing d;
            auto& f = project.finishing;
            f.enabled = fj.value("enabled", d.enabled);
            f.trim_threshold = Micrometers{fj.value("trimThreshold", d.trim_threshold.value)};
            f.trim_before_color_change =
                fj.value("trimBeforeColorChange", d.trim_before_color_change);
            f.lock_type = static_cast<document::LockStitch>(
                std::clamp(fj.value("lockType", static_cast<int>(d.lock_type)), 0, 3));
            f.lock_length = Micrometers{fj.value("lockLength", d.lock_length.value)};
            f.lock_passes = fj.value("lockPasses", d.lock_passes);
            f.filter_short_stitches = fj.value("filterShortStitches", d.filter_short_stitches);
            f.min_stitch_length =
                Micrometers{fj.value("minStitchLength", d.min_stitch_length.value)};
            // HP-ENG-008/010 : absents d'un projet antérieur -> désactivés (défauts du modèle).
            f.split_long_stitches = fj.value("splitLongStitches", d.split_long_stitches);
            f.max_stitch_length = Micrometers{
                std::clamp(fj.value("maxStitchLength", d.max_stitch_length.value), 1'000, 12'100)};
            f.auto_join = fj.value("autoJoin", d.auto_join);
        } else {
            project.finishing = document::SequenceFinishing::legacy();
        }

        // Design importé (AD-04) : absent d'un projet antérieur -> nullopt,
        // comportement historique inchangé (régénération normale depuis
        // `embroidery_objects`). `source` de chaque commande reconstruite
        // est toujours `ObjectId{}` (0 = manuel/importé) : jamais lu du JSON.
        if (j.contains("importedDesign")) {
            const auto& idj = j.at("importedDesign");
            document::ImportedDesign imported;
            imported.source_format = idj.value("sourceFormat", std::string{});
            for (const auto& cj : idj.at("sequence")) {
                stitch::StitchCommand cmd;
                cmd.pos = vec_from_json(cj.at("pos"));
                cmd.type = static_cast<stitch::CommandType>(cj.at("type").get<int>());
                cmd.pass = static_cast<stitch::StitchPass>(cj.value("pass", 0));
                cmd.source = ObjectId{};
                imported.sequence.commands.push_back(cmd);
            }
            for (const auto& bj : idj.at("colorBlocks")) {
                stitch::ColorBlock block;
                block.rgb = rgb_from_json(bj.at("rgb"));
                block.start = bj.at("start").get<std::size_t>();
                block.end = bj.at("end").get<std::size_t>();
                if (bj.contains("threadChart") && bj.contains("threadCode")) {
                    block.thread_key =
                        thread_palette::ThreadKey{bj.at("threadChart"), bj.at("threadCode")};
                }
                imported.color_blocks.push_back(block);
            }
            project.imported_design = std::move(imported);
        }
    } catch (const json::exception& ex) {
        return fail(ErrorCategory::InvalidFile, "Projet illisible (JSON invalide)", ex.what());
    }
    return project;
}

} // namespace openstitch::project_io::detail
