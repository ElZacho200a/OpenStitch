// SPDX-License-Identifier: Apache-2.0
#include "json_serialize.hpp"

#include <array>
#include <limits>
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
                j = {{"type", "brightnessContrast"}, {"brightness", o.brightness},
                     {"contrast", o.contrast}};
            } else if constexpr (std::is_same_v<T, image::MedianDenoiseOp>) {
                j = {{"type", "medianDenoise"}, {"strength", o.strength}};
            } else if constexpr (std::is_same_v<T, image::QuantizeOp>) {
                j = {{"type", "quantize"}, {"colors", o.colors}};
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
                if (p.entry_point) {
                    j["entryPoint"] = {{"x", p.entry_point->x.value}, {"y", p.entry_point->y.value}};
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
                    j["entryPoint"] = {{"x", p.entry_point->x.value}, {"y", p.entry_point->y.value}};
                }
                if (p.exit_point) {
                    j["exitPoint"] = {{"x", p.exit_point->x.value}, {"y", p.exit_point->y.value}};
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
                document::SatinRung rung{
                    Vec2um{Micrometers{r.at("ax")}, Micrometers{r.at("ay")}},
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
                return fail(ErrorCategory::InvalidFile, "Retouche invalide : type inconnu (" + ts + ")");
            }
        }
        if (entry.contains("trimAfter")) {
            const auto& ta = entry.at("trimAfter");
            if (!ta.is_boolean()) {
                return fail(ErrorCategory::InvalidFile, "Retouche invalide : trimAfter non booléen");
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

}  // namespace

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
        vo["paths"] = json::array();
        for (const auto& ps : v.paths) {
            vo["paths"].push_back(path_set_to_json(ps));
        }
        j["vectorObjects"].push_back(vo);
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
        eo["params"] = params_to_json(e.params);
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
            for (const auto& ps : vo.at("paths")) {
                v.paths.push_back(path_set_from_json(ps));
            }
            project.vector_objects.push_back(std::move(v));
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
            auto params = params_from_json(eo.at("params"));
            if (!params) {
                return std::unexpected(params.error());
            }
            e.params = std::move(*params);

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
    } catch (const json::exception& ex) {
        return fail(ErrorCategory::InvalidFile, "Projet illisible (JSON invalide)", ex.what());
    }
    return project;
}

}  // namespace openstitch::project_io::detail
