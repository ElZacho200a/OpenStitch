// SPDX-License-Identifier: Apache-2.0
// Manifeste sémantique `openstitch.semantic/1` : validation, ordre de création, provenance
// (nom d'objet `sem:<id> <n>`) et conversion d'un masque en contours. Aucun algorithme de
// points ici : la vectorisation est celle du moteur (`vectorization::vectorize_region`).
#include "openstitch/project_ops/semantic.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <expected>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "openstitch/image/image.hpp"
#include "openstitch/segmentation/segmentation.hpp"
#include "openstitch/vectorization/vectorize.hpp"
#include "semantic_internal.hpp"

namespace openstitch::project_ops {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;
using detail::SemanticManifest;
using detail::SemanticProblem;
using detail::SemanticRegion;

const std::set<std::string> kPriorities{"protected", "important", "secondary", "optional"};
const std::set<std::string> kIntents{"unstitched", "tatami",      "auto_satin",
                                     "running",    "directional", "satin"};
const std::set<std::string> kDecisions{"keep", "merge", "remove", "review"};
const std::set<std::string> kLevels{"family", "instance", "component", "technical"};

bool is_ident(const std::string& s) {
    static const std::regex re("^[a-zA-Z0-9][a-zA-Z0-9_.:-]*$");
    return std::regex_match(s, re);
}

bool is_color(const std::string& s) {
    static const std::regex re("^#[0-9a-fA-F]{6}$");
    return std::regex_match(s, re);
}

// Preuve de cycle (triée, déterministe) ou liste vide.
std::vector<std::string> find_cycle(const std::map<std::string, std::set<std::string>>& edges) {
    std::map<std::string, int> state;
    std::vector<std::string> stack;
    std::vector<std::string> found;
    const auto visit = [&](const auto& self, const std::string& node) -> void {
        if (!found.empty()) {
            return;
        }
        if (state[node] == 1) {
            const auto start = std::find(stack.begin(), stack.end(), node);
            found.assign(start, stack.end());
            found.push_back(node);
            return;
        }
        if (state[node] == 2) {
            return;
        }
        state[node] = 1;
        stack.push_back(node);
        if (const auto it = edges.find(node); it != edges.end()) {
            for (const auto& child : it->second) {
                self(self, child);
            }
        }
        stack.pop_back();
        state[node] = 2;
    };
    for (const auto& [node, ignored] : edges) {
        visit(visit, node);
        if (!found.empty()) {
            break;
        }
    }
    return found;
}

bool is_inside(const fs::path& base, const fs::path& target) {
    std::error_code ec;
    const fs::path rel = fs::relative(target, base, ec);
    if (ec || rel.empty()) {
        return false;
    }
    const auto first = *rel.begin();
    return first != ".." && !rel.is_absolute();
}

std::string text_field(const json& item, const char* key) {
    return item.contains(key) && item.at(key).is_string() ? item.at(key).get<std::string>()
                                                          : std::string{};
}

} // namespace

namespace detail {

Result<SemanticManifest> read_semantic_manifest(const fs::path& file, bool skip_files,
                                                std::vector<SemanticProblem>& problems) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return fail(ErrorCategory::InvalidFile,
                    "manifeste sémantique illisible : " + file.string());
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded()) {
        return fail(ErrorCategory::InvalidFile, "manifeste sémantique : JSON invalide");
    }

    SemanticManifest manifest;
    manifest.base_dir = file.parent_path();
    const auto problem = [&](const char* code, std::string where, std::string message) {
        problems.push_back({code, std::move(where), std::move(message)});
    };
    if (!root.is_object()) {
        problem("E_SCHEMA", "$", "racine JSON doit etre un objet");
        return manifest;
    }
    if (root.value("schema", json{}) != kSemanticSchema) {
        problem("E_SCHEMA", "schema", std::string("attendu : ") + kSemanticSchema);
    }

    bool have_size = false;
    if (!root.contains("source_image") || !root.at("source_image").is_object()) {
        problem("E_SOURCE", "source_image", "objet source_image requis");
    } else {
        const auto& src = root.at("source_image");
        const bool ok =
            src.contains("width_px") && src.contains("height_px") &&
            src.at("width_px").is_number_integer() && src.at("height_px").is_number_integer() &&
            src.at("width_px").get<long long>() > 0 && src.at("height_px").get<long long>() > 0 &&
            src.at("width_px").get<long long>() <= 100000 &&
            src.at("height_px").get<long long>() <= 100000;
        if (!ok) {
            problem("E_SOURCE", "source_image", "dimensions width_px/height_px > 0 requises");
        } else {
            manifest.width_px = src.at("width_px").get<int>();
            manifest.height_px = src.at("height_px").get<int>();
            have_size = true;
        }
    }
    if (!root.contains("design") || !root.at("design").is_object()) {
        problem("E_SIZE", "design", "objet design requis");
    } else {
        const auto& design = root.at("design");
        for (const char* key : {"width_mm", "height_mm"}) {
            const bool ok = design.contains(key) && design.at(key).is_number() &&
                            design.at(key).get<double>() > 0.0 &&
                            design.at(key).get<double>() <= 1000.0;
            if (!ok) {
                problem("E_SIZE", std::string("design.") + key,
                        "dimension > 0 et <= 1000 mm requise");
            }
        }
        manifest.width_mm = design.value("width_mm", 0.0);
        manifest.height_mm = design.value("height_mm", 0.0);
    }

    json regions = json::array();
    if (!root.contains("regions") || !root.at("regions").is_array()) {
        problem("E_REGIONS", "regions", "liste de regions requise");
    } else {
        regions = root.at("regions");
    }
    manifest.raw_region_count = regions.size();

    std::map<std::string, std::size_t> indexed; // identifiant -> position dans manifest.regions
    for (std::size_t n = 0; n < regions.size(); ++n) {
        const auto& item = regions[n];
        const std::string where = "regions[" + std::to_string(n) + "]";
        if (!item.is_object()) {
            problem("E_REGION", where, "region doit etre un objet");
            continue;
        }
        const std::string sid = text_field(item, "semantic_id");
        if (!item.contains("semantic_id") || !item.at("semantic_id").is_string() ||
            !is_ident(sid)) {
            problem("E_ID", where + ".semantic_id", "identifiant technique ASCII stable requis");
            continue;
        }
        SemanticRegion region;
        region.semantic_id = sid;
        if (indexed.contains(sid)) {
            problem("E_DUPLICATE", where + ".semantic_id", sid + " apparait plusieurs fois");
        } else {
            indexed[sid] = manifest.regions.size();
        }
        region.family = text_field(item, "family");
        if (region.family.empty()) {
            problem("E_FAMILY", where + ".family", "famille non vide requise");
        }
        region.role = text_field(item, "role");
        if (region.role.empty()) {
            problem("E_ROLE", where + ".role", "role non vide requis");
        }
        if (item.contains("level")) {
            region.level = text_field(item, "level");
            if (!item.at("level").is_string() || !kLevels.contains(region.level)) {
                problem("E_LEVEL", where + ".level",
                        "niveau famille/instance/composant/technique inconnu");
            }
        }
        region.priority = text_field(item, "priority");
        if (!kPriorities.contains(region.priority)) {
            problem("E_PRIORITY", where + ".priority", "priorite inconnue");
        }
        if (item.contains("stitch_intent") && !item.at("stitch_intent").is_null()) {
            const std::string intent = text_field(item, "stitch_intent");
            if (!item.at("stitch_intent").is_string() || !kIntents.contains(intent)) {
                problem("E_STITCH", where + ".stitch_intent", "type de point inconnu");
            } else {
                region.stitch_intent = intent;
            }
        }
        if (item.contains("decision")) {
            region.decision = text_field(item, "decision");
            if (!item.at("decision").is_string() || !kDecisions.contains(region.decision)) {
                problem("E_DECISION", where + ".decision", "decision inconnue");
            }
        }
        if (item.contains("thread_rgb") && !item.at("thread_rgb").is_null()) {
            const std::string rgb = text_field(item, "thread_rgb");
            if (!item.at("thread_rgb").is_string() || !is_color(rgb)) {
                problem("E_THREAD", where + ".thread_rgb", "couleur attendue : #RRGGBB");
            } else {
                region.thread_rgb = rgb;
            }
        }
        if (item.contains("protected_detail") && !item.at("protected_detail").is_boolean()) {
            problem("E_PROTECTED", where + ".protected_detail", "booleen requis");
        }
        if (item.contains("visible_on_fabric") && !item.at("visible_on_fabric").is_boolean()) {
            problem("E_VISIBLE", where + ".visible_on_fabric", "booleen requis");
        }
        const bool has_mask =
            item.contains("mask_path") && !item.at("mask_path").is_null() &&
            !(item.at("mask_path").is_string() && item.at("mask_path").get<std::string>().empty());
        if (region.stitch_intent && *region.stitch_intent != "unstitched" && !has_mask) {
            problem("E_MASK", where + ".mask_path", "masque requis pour un objet cousu");
        }
        if (item.contains("mask_path") && !item.at("mask_path").is_null()) {
            const auto& mask = item.at("mask_path");
            if (!mask.is_string() || mask.get<std::string>().empty()) {
                problem("E_MASK", where + ".mask_path", "chemin relatif non vide requis");
            } else {
                region.mask_path = mask.get<std::string>();
                if (!skip_files) {
                    std::error_code ec;
                    const fs::path base = fs::weakly_canonical(manifest.base_dir, ec);
                    const fs::path target =
                        fs::weakly_canonical(manifest.base_dir / fs::path(*region.mask_path), ec);
                    if (!is_inside(base, target)) {
                        problem("E_PATH", where + ".mask_path",
                                "masque en dehors du dossier du manifeste");
                    } else if (!fs::is_regular_file(target, ec)) {
                        problem("E_MISSING_MASK", where + ".mask_path", "fichier introuvable");
                    } else if (const auto info = image::read_image_info(target); !info) {
                        problem("E_MASK_PNG", where + ".mask_path", info.error().message);
                    } else if (have_size && (info->width_px != manifest.width_px ||
                                             info->height_px != manifest.height_px)) {
                        problem("E_MASK_SIZE", where + ".mask_path",
                                "(" + std::to_string(info->width_px) + ", " +
                                    std::to_string(info->height_px) + ") != dimensions source (" +
                                    std::to_string(manifest.width_px) + ", " +
                                    std::to_string(manifest.height_px) + ")");
                    }
                }
            }
        }
        if (item.contains("parent_id") && !item.at("parent_id").is_null() &&
            item.at("parent_id").is_string()) {
            region.parent_id = item.at("parent_id").get<std::string>();
        }
        for (const char* relation : {"under", "over"}) {
            if (!item.contains(relation)) {
                continue;
            }
            const auto& links = item.at(relation);
            const bool ok =
                links.is_array() && std::all_of(links.begin(), links.end(),
                                                [](const json& x) { return x.is_string(); });
            if (ok) {
                auto& dst = std::string(relation) == "under" ? region.under : region.over;
                for (const auto& x : links) {
                    dst.push_back(x.get<std::string>());
                }
            }
        }
        manifest.regions.push_back(std::move(region));
    }

    std::map<std::string, std::set<std::string>> hierarchy;
    std::map<std::string, std::set<std::string>> occlusions;
    for (const auto& [sid, ignored] : indexed) {
        hierarchy[sid];
        occlusions[sid];
    }
    for (std::size_t n = 0; n < regions.size(); ++n) {
        const auto& item = regions[n];
        if (!item.is_object() || !item.contains("semantic_id") ||
            !item.at("semantic_id").is_string()) {
            continue;
        }
        const std::string sid = item.at("semantic_id").get<std::string>();
        if (!indexed.contains(sid)) {
            continue;
        }
        if (item.contains("parent_id") && !item.at("parent_id").is_null()) {
            const auto& parent = item.at("parent_id");
            if (!parent.is_string() || !indexed.contains(parent.get<std::string>())) {
                problem("E_PARENT", sid,
                        "parent inconnu : " +
                            (parent.is_string() ? parent.get<std::string>() : parent.dump()));
            } else {
                hierarchy[sid].insert(parent.get<std::string>());
            }
        }
        for (const char* relation : {"under", "over"}) {
            if (!item.contains(relation)) {
                continue;
            }
            const auto& links = item.at(relation);
            if (!links.is_array() || !std::all_of(links.begin(), links.end(),
                                                  [](const json& x) { return x.is_string(); })) {
                problem("E_RELATION", sid + "." + relation, "liste d identifiants requise");
                continue;
            }
            for (const auto& link : links) {
                const std::string target = link.get<std::string>();
                if (!indexed.contains(target)) {
                    problem("E_REFERENCE", sid + "." + relation, "objet inconnu : " + target);
                } else if (std::string(relation) == "under") {
                    occlusions[sid].insert(target);
                } else {
                    occlusions[target].insert(sid);
                }
            }
        }
    }
    for (const auto& [graph, code] :
         {std::pair{&hierarchy, "E_PARENT_CYCLE"}, std::pair{&occlusions, "E_OCCLUSION_CYCLE"}}) {
        const auto trace = find_cycle(*graph);
        if (!trace.empty()) {
            std::string joined;
            for (const auto& step : trace) {
                joined += (joined.empty() ? "" : " -> ") + step;
            }
            problem(code, "regions", joined);
        }
    }
    return manifest;
}

std::vector<const SemanticRegion*> creation_order(const SemanticManifest& manifest) {
    std::map<std::string, const SemanticRegion*> indexed;
    for (const auto& r : manifest.regions) {
        indexed.emplace(r.semantic_id, &r);
    }
    std::map<std::string, std::set<std::string>> predecessors;
    for (const auto& [sid, ignored] : indexed) {
        predecessors[sid];
    }
    for (const auto& r : manifest.regions) {
        for (const auto& above : r.under) {
            if (indexed.contains(above)) {
                predecessors[above].insert(r.semantic_id);
            }
        }
        for (const auto& below : r.over) {
            if (indexed.contains(below)) {
                predecessors[r.semantic_id].insert(below);
            }
        }
    }
    std::vector<const SemanticRegion*> order;
    std::set<std::string> done;
    while (done.size() != indexed.size()) {
        const SemanticRegion* next = nullptr;
        for (const auto& [sid, region] : indexed) { // std::map : ordre alphabétique
            if (done.contains(sid)) {
                continue;
            }
            const auto& need = predecessors[sid];
            if (std::all_of(need.begin(), need.end(),
                            [&](const std::string& p) { return done.contains(p); })) {
                next = region;
                break;
            }
        }
        if (next == nullptr) { // cycle : écarté par la validation, filet de sécurité
            break;
        }
        done.insert(next->semantic_id);
        order.push_back(next);
    }
    return order;
}

std::string semantic_object_name(std::string_view semantic_id, std::size_t n) {
    return "sem:" + std::string(semantic_id) + " " + std::to_string(n);
}

std::optional<std::string> semantic_id_of_name(std::string_view name) {
    constexpr std::string_view kPrefix = "sem:";
    if (!name.starts_with(kPrefix)) {
        return std::nullopt;
    }
    name.remove_prefix(kPrefix.size());
    const auto space = name.find(' ');
    const std::string id(name.substr(0, space));
    if (id.empty() || !is_ident(id)) {
        return std::nullopt;
    }
    return id;
}

std::vector<const document::EmbroideryObject*> semantic_objects(const document::Project& project,
                                                                std::string_view semantic_id) {
    std::vector<const document::EmbroideryObject*> out;
    for (const auto& e : project.embroidery_objects) {
        auto id = semantic_id_of_name(e.name);
        if (!id) {
            if (const auto* v = project.findObject(e.source_vector); v != nullptr) {
                id = semantic_id_of_name(v->name);
            }
        }
        if (id && *id == semantic_id) {
            out.push_back(&e);
        }
    }
    return out;
}

std::string problems_text(const std::vector<SemanticProblem>& problems, std::size_t limit) {
    std::string out;
    for (std::size_t i = 0; i < problems.size() && i < limit; ++i) {
        out += (out.empty() ? "" : " ; ") + problems[i].code + " " + problems[i].path + " : " +
               problems[i].message;
    }
    if (problems.size() > limit) {
        out += " ; … (" + std::to_string(problems.size() - limit) + " autre(s))";
    }
    return out;
}

Result<std::vector<geometry::PathSet>> mask_to_paths(const fs::path& mask, int width_px,
                                                     int height_px, double mm_per_px) {
    auto img = image::load_image(mask);
    if (!img) {
        return std::unexpected(img.error());
    }
    if (img->width != width_px || img->height != height_px) {
        return fail(ErrorCategory::UserInput, "masque de dimensions " + std::to_string(img->width) +
                                                  "x" + std::to_string(img->height) +
                                                  " au lieu de " + std::to_string(width_px) + "x" +
                                                  std::to_string(height_px));
    }
    segmentation::Segmentation seg;
    seg.width = width_px;
    seg.height = height_px;
    seg.labels.assign(static_cast<std::size_t>(width_px) * static_cast<std::size_t>(height_px), 0U);
    // Canal alpha utilisé seulement s'il porte une information (un PNG RGBA entièrement opaque
    // est un masque en noir et blanc : on lit alors le gris, sinon tout serait « blanc »).
    bool use_alpha = false;
    if (img->source_had_alpha) {
        for (std::size_t i = 0; i < seg.labels.size() && !use_alpha; ++i) {
            use_alpha = img->rgba[4 * i + 3] != 255;
        }
    }
    std::size_t count = 0;
    for (std::size_t i = 0; i < seg.labels.size(); ++i) {
        const std::uint8_t* px = img->rgba.data() + 4 * i;
        const int value = use_alpha ? px[3] : (299 * px[0] + 587 * px[1] + 114 * px[2]) / 1000;
        if (value > 127) {
            seg.labels[i] = 1U;
            ++count;
        }
    }
    if (count == 0) {
        return fail(ErrorCategory::UserInput, "masque vide : aucun pixel > 127");
    }
    segmentation::Region region;
    region.id = RegionId{1};
    region.rgb = {255, 255, 255};
    region.pixel_count = count;
    seg.region_slots.emplace_back(region);

    vectorization::VectorizeOptions options;
    options.mm_per_px = Millimeters{mm_per_px};
    // Même finesse que le pont Python (approxPolyDP 0,65 px), bornée à [0,05 ; 0,4] mm.
    const double tolerance_um = std::clamp(std::round(0.65 * mm_per_px * 1000.0), 50.0, 400.0);
    options.simplify_tolerance = Micrometers{static_cast<std::int32_t>(tolerance_um)};
    auto paths = vectorization::vectorize_region(seg, RegionId{1}, options);
    if (!paths) {
        return std::unexpected(paths.error());
    }
    if (paths->empty()) {
        return fail(ErrorCategory::UserInput, "masque sans contour exploitable");
    }
    return paths;
}

} // namespace detail

std::string validate_semantic_manifest(const fs::path& manifest, bool skip_files) {
    std::vector<SemanticProblem> problems;
    json out{{"schema", kSemanticValidationSchema}};
    const auto read = detail::read_semantic_manifest(manifest, skip_files, problems);
    if (!read) {
        problems.push_back({"E_IO", manifest.string(), read.error().message});
    }
    out["ok"] = problems.empty();
    if (read) {
        std::map<std::string, int> families;
        std::set<std::string> unique;
        for (const auto& r : read->regions) {
            unique.insert(r.semantic_id);
            if (!r.family.empty()) {
                ++families[r.family];
            }
        }
        out["regions"] = read->raw_region_count;
        out["unique_regions"] = unique.size();
        out["families"] = families;
    }
    json errors = json::array();
    for (const auto& p : problems) {
        errors.push_back(json{{"code", p.code}, {"path", p.path}, {"message", p.message}});
    }
    out["errors"] = errors;
    return out.dump();
}

std::string semantic_map_json(const document::Project& project) {
    std::map<std::string, json> objects;
    std::size_t unmapped = 0;
    for (std::size_t i = 0; i < project.embroidery_objects.size(); ++i) {
        const auto& e = project.embroidery_objects[i];
        auto id = detail::semantic_id_of_name(e.name);
        if (!id) {
            if (const auto* v = project.findObject(e.source_vector); v != nullptr) {
                id = detail::semantic_id_of_name(v->name);
            }
        }
        if (!id) {
            ++unmapped;
            continue;
        }
        auto& entry = objects[*id];
        if (entry.is_null()) {
            entry = json{{"ids", json::array()}, {"vector_ids", json::array()}, {"first_order", i}};
        }
        entry["ids"].push_back(e.id.value);
        entry["vector_ids"].push_back(e.source_vector.value);
    }
    return json{
        {"schema", kSemanticMapSchema}, {"objects", objects}, {"unmapped_objects", unmapped}}
        .dump(2);
}

} // namespace openstitch::project_ops
