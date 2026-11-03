// SPDX-License-Identifier: Apache-2.0
// Opérations sémantiques : `semantic_import` (masques d'un manifeste `openstitch.semantic/1` ->
// objets distincts qui gardent leur identité `sem:<id>`) et `semantic_replace` (remplace UN
// objet logique sans toucher aux autres). Aucun algorithme de points : contours par la
// vectorisation du moteur, objets ajoutés par `AddObjectBatchCommand`, satin par l'opération
// `auto_satin`, suppression par `remove_object`, ordre par `set_order`. Comme toute opération,
// elles sont transactionnelles dans un lot (`project apply`, MCP `project_apply`) : un refus
// laisse le projet strictement inchangé.
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "op_registry.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/project_ops/inspect.hpp"
#include "openstitch/project_ops/semantic.hpp"
#include "semantic_internal.hpp"

namespace openstitch::project_ops::detail {

namespace {

using nlohmann::json;
namespace fs = std::filesystem;

ArgSpec arg(std::string name, std::string type, std::string description, bool required = false,
            std::string unit = {}, std::string range = {}) {
    return ArgSpec{std::move(name),        std::move(type), std::move(unit),
                   std::move(description), required,        std::move(range)};
}

OpStatus usage(const std::string& message, const std::string& hint = {}) {
    return OpStatus::failure(code::kUsage, message, hint);
}

OpStatus string_arg(const OpContext& ctx, const char* key, bool required, std::string& out) {
    const auto it = ctx.args.find(key);
    if (it == ctx.args.end() || it->is_null()) {
        return required ? usage(std::string("argument « ") + key + " » obligatoire (chaîne)")
                        : OpStatus::success();
    }
    if (!it->is_string() || it->get<std::string>().empty()) {
        return usage(std::string("argument « ") + key + " » : chaîne non vide attendue");
    }
    out = it->get<std::string>();
    return OpStatus::success();
}

fs::path utf8_path(const std::string& text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

// Lit un manifeste ; refus nommé si illisible ou invalide (premiers défauts dans le message).
OpStatus load_manifest(const fs::path& file, bool skip_files, SemanticManifest& out) {
    std::vector<SemanticProblem> problems;
    auto read = read_semantic_manifest(file, skip_files, problems);
    if (!read) {
        return OpStatus::failure(code::kIo, read.error().message,
                                 "chemin du manifeste openstitch.semantic/1");
    }
    if (!problems.empty()) {
        return OpStatus::failure(code::kSchema,
                                 "manifeste sémantique invalide : " + problems_text(problems),
                                 "validate_semantic_manifest / MCP semantic_validate donnent la "
                                 "liste complète");
    }
    out = std::move(*read);
    return OpStatus::success();
}

// Échelle image -> mm ; refuse un manifeste dont les proportions image / design divergent.
OpStatus resolve_scale(const SemanticManifest& m, double& mm_per_px) {
    const double sx = m.width_mm / static_cast<double>(m.width_px);
    const double sy = m.height_mm / static_cast<double>(m.height_px);
    if (std::abs(sx - sy) > 0.02 * sx) {
        return OpStatus::failure(
            code::kOutOfRange,
            "proportions du design (" + std::to_string(m.width_mm) + " x " +
                std::to_string(m.height_mm) + " mm) différentes de celles de l'image source (" +
                std::to_string(m.width_px) + " x " + std::to_string(m.height_px) + " px)",
            "corrigez design.width_mm / height_mm : pas de déformation "
            "silencieuse");
    }
    mm_per_px = sx;
    return OpStatus::success();
}

bool stitchable(const SemanticRegion& r) {
    return r.decision == "keep" && r.stitch_intent && *r.stitch_intent != "unstitched";
}

OpStatus check_intent(const SemanticRegion& r) {
    const std::string& intent = *r.stitch_intent;
    if (intent != "tatami" && intent != "running" && intent != "auto_satin") {
        return OpStatus::failure(code::kRefused,
                                 "intention « " + intent + " » de " + r.semantic_id +
                                     " non importable sans guides",
                                 "intentions prises en charge : tatami, running, auto_satin ; "
                                 "poser ensuite directional_guides / un satin manuel par "
                                 "project_apply");
    }
    if (("sem:" + r.semantic_id).size() > 110) {
        return OpStatus::failure(code::kOutOfRange,
                                 "identifiant sémantique trop long : " + r.semantic_id,
                                 "au plus 106 caractères");
    }
    return OpStatus::success();
}

struct Created {
    std::vector<std::uint64_t> ids;
    std::vector<std::uint64_t> vector_ids;
    json fallbacks = json::array(); // objets dont l'auto_satin a été refusé puis repliés
};

// Réglages de conversion : stratégie auto_satin et conduite en cas de refus du moteur.
struct Policy {
    std::string strategy{"skeleton"};
    std::string satin_fallback{"fail"}; // fail | tatami | running
    double mm_per_px{25.4 / 96.0};      // échelle image -> mm du manifeste
};

// Crée les objets d'un identifiant sémantique (un par contour extérieur du masque) puis, pour
// auto_satin, convertit chacun par l'opération `auto_satin`. Un refus du moteur est propagé
// (`satin_fallback=fail`, défaut) ou l'objet reste en tatami / passe en contour, et le repli est
// consigné dans `Created::fallbacks` (jamais de conversion silencieuse).
OpStatus create_objects(OpContext& ctx, const SemanticRegion& region,
                        const std::array<std::uint8_t, 3>& rgb,
                        std::vector<geometry::PathSet> paths, const Policy& policy, Created& out) {
    const std::string& intent = *region.stitch_intent;
    std::vector<document::VectorObject> vectors;
    std::vector<document::EmbroideryObject> embroideries;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        document::VectorObject v;
        v.id = ctx.project.object_ids.next();
        v.name = semantic_object_name(region.semantic_id, i + 1);
        v.rgb = rgb;
        v.paths.push_back(std::move(paths[i]));
        document::EmbroideryObject e;
        e.id = ctx.project.object_ids.next();
        e.name = v.name;
        e.source_vector = v.id;
        e.rgb = rgb;
        e.intent = document::EmbroideryIntent::ForcedUserChoice;
        if (intent == "running") {
            e.params = document::RunningStitchParams{};
        } else {
            e.params = document::TatamiParams{};
        }
        out.ids.push_back(e.id.value);
        out.vector_ids.push_back(v.id.value);
        vectors.push_back(std::move(v));
        embroideries.push_back(std::move(e));
    }
    if (intent != "running") {
        // Angle de rangée naturel (axe principal de la forme) et réglages de tatami du moteur,
        // comme `digitize` : sans cela tous les remplissages auraient le même angle (bandes
        // horizontales sur toute la scène).
        autodigitize::AutoOptions options;
        options.mm_per_px = Millimeters{policy.mm_per_px};
        const auto built =
            autodigitize::auto_digitize_vectors(vectors, ctx.project.object_ids, options);
        if (built) {
            for (auto& e : embroideries) {
                for (const auto& b : built->embroideries) {
                    if (b.source_vector == e.source_vector &&
                        std::holds_alternative<document::TatamiParams>(b.params)) {
                        e.params = b.params;
                    }
                }
            }
        }
    }
    ctx.stack.execute(std::make_unique<commands::AddObjectBatchCommand>(
                          std::move(vectors), std::move(embroideries),
                          "Importer l'objet sémantique " + region.semantic_id),
                      ctx.project);
    if (intent == "auto_satin") {
        for (const auto id : out.ids) {
            const auto st = run_nested_op(ctx, "auto_satin",
                                          json{{"object", id}, {"strategy", policy.strategy}});
            if (!st.ok && policy.satin_fallback != "fail") {
                if (policy.satin_fallback == "running") {
                    const auto to_running = run_nested_op(
                        ctx, "set_stitch_type", json{{"object", id}, {"type", "running"}});
                    if (!to_running.ok) {
                        return to_running;
                    }
                }
                out.fallbacks.push_back(json{{"semantic_id", region.semantic_id},
                                             {"object", id},
                                             {"fallback", policy.satin_fallback},
                                             {"reason", st.error.message}});
                continue;
            }
            if (!st.ok) {
                return OpStatus::failure(st.error.code,
                                         region.semantic_id +
                                             " : auto_satin refusé : " + st.error.message,
                                         "essayez une autre stratégie (auto_satin_strategy : "
                                         "skeleton | parallel_sweep | medial_axis) ou tatami");
            }
        }
    }
    return OpStatus::success();
}

OpStatus read_policy(const OpContext& ctx, Policy& policy) {
    if (auto st = string_arg(ctx, "auto_satin_strategy", false, policy.strategy); !st.ok) {
        return st;
    }
    if (auto st = string_arg(ctx, "satin_fallback", false, policy.satin_fallback); !st.ok) {
        return st;
    }
    if (policy.satin_fallback != "fail" && policy.satin_fallback != "tatami" &&
        policy.satin_fallback != "running") {
        return usage("argument « satin_fallback » : fail | tatami | running");
    }
    return OpStatus::success();
}

OpStatus read_rgb(const SemanticRegion& region, const std::array<std::uint8_t, 3>& fallback,
                  std::array<std::uint8_t, 3>& out) {
    out = fallback;
    if (region.thread_rgb) {
        const auto parsed = rgb_from_hex(*region.thread_rgb);
        if (!parsed) {
            return OpStatus::failure(code::kOutOfRange,
                                     "thread_rgb invalide : " + *region.thread_rgb,
                                     "format attendu : #RRGGBB");
        }
        out = *parsed;
    }
    return OpStatus::success();
}

json region_entry(const SemanticRegion& r, const Created& c) {
    return json{{"ids", c.ids},
                {"vector_ids", c.vector_ids},
                {"family", r.family},
                {"role", r.role},
                {"parent_id", r.parent_id ? json(*r.parent_id) : json(nullptr)},
                {"mask_path", r.mask_path ? json(*r.mask_path) : json(nullptr)},
                {"stitch_intent", *r.stitch_intent}};
}

// --- semantic_import ----------------------------------------------------------------------

OpStatus semantic_import(OpContext& ctx) {
    std::string manifest_arg;
    if (auto st = string_arg(ctx, "manifest", true, manifest_arg); !st.ok) {
        return st;
    }
    Policy policy;
    if (auto st = read_policy(ctx, policy); !st.ok) {
        return st;
    }
    SemanticManifest manifest;
    if (auto st = load_manifest(utf8_path(manifest_arg), false, manifest); !st.ok) {
        return st;
    }
    double mm_per_px = 0.0;
    if (auto st = resolve_scale(manifest, mm_per_px); !st.ok) {
        return st;
    }
    policy.mm_per_px = mm_per_px;

    // Image source facultative : vérifiée avant toute mutation, posée en fin d'opération avec
    // l'échelle du manifeste (les contours importés et l'image restent ainsi alignés).
    std::string source_path;
    if (auto st = string_arg(ctx, "source_path", false, source_path); !st.ok) {
        return st;
    }
    SourceImagePlan source;
    if (!source_path.empty()) {
        if (auto st = load_source_image(ctx.project, source_path, mm_per_px, source); !st.ok) {
            return st;
        }
        if (source.image.width != manifest.width_px || source.image.height != manifest.height_px) {
            return OpStatus::failure(
                code::kRefused,
                "source_path : l'image fait " + std::to_string(source.image.width) + " x " +
                    std::to_string(source.image.height) + " px, le manifeste déclare " +
                    std::to_string(manifest.width_px) + " x " + std::to_string(manifest.height_px) +
                    " px",
                "passer l'image source du manifeste (source_image.path)");
        }
    }

    const auto order = creation_order(manifest);
    for (const auto* region : order) { // tout est vérifié AVANT la première mutation
        if (!stitchable(*region)) {
            continue;
        }
        if (auto st = check_intent(*region); !st.ok) {
            return st;
        }
        if (!semantic_objects(ctx.project, region->semantic_id).empty()) {
            return OpStatus::failure(code::kRefused,
                                     "l'identifiant sémantique " + region->semantic_id +
                                         " existe déjà dans le projet",
                                     "semantic_replace remplace un objet existant ; sinon "
                                     "choisissez un autre identifiant");
        }
    }

    json mapping = json::object();
    json skipped = json::array();
    json created = json::array();
    json fallbacks = json::array();
    for (const auto* region : order) {
        if (!stitchable(*region)) {
            skipped.push_back(json{{"semantic_id", region->semantic_id},
                                   {"reason", region->decision != "keep"
                                                  ? "decision=" + region->decision
                                                  : "structurel ou unstitched (aucun point)"}});
            continue;
        }
        std::array<std::uint8_t, 3> rgb{};
        if (auto st = read_rgb(*region, {80, 120, 200}, rgb); !st.ok) {
            return st;
        }
        auto paths = mask_to_paths(manifest.base_dir / utf8_path(*region->mask_path),
                                   manifest.width_px, manifest.height_px, mm_per_px);
        if (!paths) {
            return OpStatus::failure(code::kRefused,
                                     region->semantic_id + " : " + paths.error().message,
                                     "masque PNG blanc (> 127) sur fond noir, aux dimensions de "
                                     "l'image source");
        }
        Created c;
        if (auto st = create_objects(ctx, *region, rgb, std::move(*paths), policy, c); !st.ok) {
            return st;
        }
        for (const auto id : c.ids) {
            created.push_back(id);
        }
        for (const auto& f : c.fallbacks) {
            fallbacks.push_back(f);
        }
        mapping[region->semantic_id] = region_entry(*region, c);
    }
    if (created.empty()) {
        return OpStatus::failure(code::kNothingToDo, "aucune région à coudre dans le manifeste",
                                 "chaque région cousue porte une stitch_intent et un mask_path "
                                 "avec decision=keep");
    }
    json result{{"id", created.front()},
                {"created", created},
                {"mapping", mapping},
                {"skipped", skipped},
                {"fallbacks", fallbacks}};
    if (!source_path.empty()) {
        auto applied = apply_source_image(ctx, std::move(source));
        result["source_image"] = std::move(applied.result);
    }
    return OpStatus::success(std::move(result));
}

// --- semantic_replace ---------------------------------------------------------------------

OpStatus semantic_replace(OpContext& ctx) {
    std::string manifest_arg;
    std::string sid;
    std::string mask_arg;
    if (auto st = string_arg(ctx, "manifest", true, manifest_arg); !st.ok) {
        return st;
    }
    if (auto st = string_arg(ctx, "semantic_id", true, sid); !st.ok) {
        return st;
    }
    if (auto st = string_arg(ctx, "mask", false, mask_arg); !st.ok) {
        return st;
    }
    Policy policy;
    if (auto st = read_policy(ctx, policy); !st.ok) {
        return st;
    }
    SemanticManifest manifest;
    // Structure seule : les masques des AUTRES objets ne sont pas lus (ni requis).
    if (auto st = load_manifest(utf8_path(manifest_arg), true, manifest); !st.ok) {
        return st;
    }
    double mm_per_px = 0.0;
    if (auto st = resolve_scale(manifest, mm_per_px); !st.ok) {
        return st;
    }
    policy.mm_per_px = mm_per_px;
    const auto found = std::find_if(manifest.regions.begin(), manifest.regions.end(),
                                    [&](const SemanticRegion& r) { return r.semantic_id == sid; });
    if (found == manifest.regions.end()) {
        return OpStatus::failure(code::kUnknownObject,
                                 "identifiant sémantique inconnu du manifeste : " + sid,
                                 "voir regions[].semantic_id du manifeste");
    }
    const SemanticRegion& region = *found;
    if (!stitchable(region)) {
        return OpStatus::failure(code::kRefused, sid + " n'est pas une région cousue",
                                 "stitch_intent tatami|running|auto_satin et decision=keep");
    }
    if (auto st = check_intent(region); !st.ok) {
        return st;
    }
    const auto previous = semantic_objects(ctx.project, sid);
    if (previous.empty()) {
        return OpStatus::failure(code::kUnknownObject,
                                 "aucun objet « sem:" + sid + " » dans le projet",
                                 "semantic_import crée les objets d'un manifeste ; la carte de "
                                 "provenance liste les identifiants présents");
    }
    const std::string& intent = *region.stitch_intent;
    if (intent != "auto_satin") {
        for (const auto* e : previous) {
            if (intent != stitch_type_name(*e)) {
                return OpStatus::failure(code::kRefused,
                                         "le projet porte « " + std::string(stitch_type_name(*e)) +
                                             " » pour " + sid + " mais le manifeste demande « " +
                                             intent + " »",
                                         "mettre le manifeste à jour, ou convertir l'objet par "
                                         "project_apply avant le remplacement");
            }
        }
    }
    std::size_t first_order = ctx.project.embroidery_objects.size();
    for (std::size_t i = 0; i < ctx.project.embroidery_objects.size(); ++i) {
        if (std::any_of(previous.begin(), previous.end(), [&](const document::EmbroideryObject* p) {
                return p->id == ctx.project.embroidery_objects[i].id;
            })) {
            first_order = std::min(first_order, i);
        }
    }
    const std::string mask_text = mask_arg.empty() ? region.mask_path.value_or("") : mask_arg;
    if (mask_text.empty()) {
        return OpStatus::failure(code::kUsage, "aucun masque pour " + sid,
                                 "renseignez « mask » ou mask_path dans le manifeste");
    }
    fs::path mask_file = utf8_path(mask_text);
    if (mask_file.is_relative()) {
        mask_file = manifest.base_dir / mask_file;
    }
    std::array<std::uint8_t, 3> rgb{};
    if (auto st = read_rgb(region, previous.front()->rgb, rgb); !st.ok) {
        return st;
    }
    auto paths = mask_to_paths(mask_file, manifest.width_px, manifest.height_px, mm_per_px);
    if (!paths) {
        return OpStatus::failure(code::kRefused, sid + " : " + paths.error().message,
                                 "masque PNG blanc (> 127) sur fond noir, aux dimensions de "
                                 "l'image source");
    }

    std::vector<std::uint64_t> old_ids;
    for (const auto* e : previous) {
        old_ids.push_back(e->id.value);
    }
    const std::size_t other_objects = ctx.project.embroidery_objects.size() - old_ids.size();
    for (const auto id : old_ids) {
        const auto st =
            run_nested_op(ctx, "remove_object", json{{"object", id}, {"with_source", true}});
        if (!st.ok) {
            return st;
        }
    }
    Created c;
    if (auto st = create_objects(ctx, region, rgb, std::move(*paths), policy, c); !st.ok) {
        return st;
    }
    // Les nouveaux objets reprennent la place des anciens dans l'ordre de couture.
    for (std::size_t i = 0; i < c.ids.size(); ++i) {
        const auto st = run_nested_op(ctx, "set_order",
                                      json{{"object", c.ids[i]}, {"position", first_order + i}});
        if (!st.ok) {
            return st;
        }
    }
    const auto after = semantic_objects(ctx.project, sid);
    std::set<std::uint64_t> now;
    for (const auto* e : after) {
        now.insert(e->id.value);
    }
    if (now != std::set<std::uint64_t>(c.ids.begin(), c.ids.end()) ||
        ctx.project.embroidery_objects.size() != other_objects + c.ids.size()) {
        return OpStatus::failure(code::kInternal,
                                 "remplacement incohérent : identité sémantique non conservée");
    }
    return OpStatus::success({{"id", c.ids.front()},
                              {"replaced", sid},
                              {"old_ids", old_ids},
                              {"new_ids", c.ids},
                              {"fallbacks", c.fallbacks},
                              {"unchanged_objects", other_objects}});
}

} // namespace

void register_semantic_ops(std::vector<OpSpec>& out) {
    OpSpec import;
    import.name = "semantic_import";
    import.summary =
        "Importe les masques PNG d'un manifeste openstitch.semantic/1 : un objet distinct par "
        "contour et par identifiant sémantique (jamais fusionné sur la seule couleur), nommé "
        "« sem:<id> <n> », créé dans l'ordre d'occlusion (under/over). Intentions : tatami, "
        "running, auto_satin. Refuse tout manifeste invalide avant de modifier le projet.";
    import.args = {
        arg("manifest", "string", "fichier manifeste .json (masques relatifs à son dossier)", true),
        arg("auto_satin_strategy", "string", "stratégie des objets auto_satin (défaut skeleton)",
            false, {}, "skeleton | parallel_sweep | medial_axis"),
        arg("satin_fallback", "string",
            "si le moteur refuse l'auto_satin d'un objet : fail (défaut, tout est annulé), tatami "
            "ou running (l'objet est gardé dans ce type et le repli est consigné dans « fallbacks "
            "»)",
            false, {}, "fail | tatami | running"),
        arg("source_path", "string",
            "image source (mêmes dimensions que le manifeste) : posée comme par set_source_image, "
            "à l'échelle du manifeste, pour permettre la mesure de fidélité. Absent : projet "
            "inchangé côté image")};
    import.example = R"({"op":"semantic_import","manifest":"travail/carte.json"})";
    import.apply = semantic_import;
    out.push_back(std::move(import));

    OpSpec replace;
    replace.name = "semantic_replace";
    replace.summary =
        "Remplace UN objet logique (identifiant sémantique) à partir d'un masque corrigé, à la "
        "même place dans l'ordre de couture ; les autres objets ne sont pas touchés. Les "
        "identifiants numériques de l'objet remplacé changent, son identité sémantique reste.";
    replace.args = {
        arg("manifest", "string", "manifeste openstitch.semantic/1 décrivant l'objet", true),
        arg("semantic_id", "string", "identifiant sémantique à remplacer", true),
        arg("mask", "string",
            "masque PNG corrigé (relatif au dossier du manifeste ; défaut : mask_path du "
            "manifeste)"),
        arg("auto_satin_strategy", "string", "stratégie des objets auto_satin (défaut skeleton)",
            false, {}, "skeleton | parallel_sweep | medial_axis"),
        arg("satin_fallback", "string",
            "si le moteur refuse l'auto_satin d'un objet : fail (défaut, tout est annulé), tatami "
            "ou running (l'objet est gardé dans ce type et le repli est consigné dans « fallbacks "
            "»)",
            false, {}, "fail | tatami | running")};
    replace.example =
        R"({"op":"semantic_replace","manifest":"travail/carte.json","semantic_id":"voiles.grand","mask":"masks/grand_v2.png"})";
    replace.apply = semantic_replace;
    out.push_back(std::move(replace));
}

} // namespace openstitch::project_ops::detail
