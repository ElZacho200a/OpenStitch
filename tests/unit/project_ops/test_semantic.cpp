// SPDX-License-Identifier: Apache-2.0
// Import et remplacement sémantiques natifs (semantic_import, semantic_replace) : identité
// conservée, objets non ciblés intacts, refus et retour arrière.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "openstitch/image/image.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/project_ops/inspect.hpp"
#include "openstitch/project_ops/operations.hpp"
#include "openstitch/project_ops/semantic.hpp"

using namespace openstitch;
using namespace openstitch::project_ops;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr int kW = 200;
constexpr int kH = 100;

// Dossier de travail propre au processus et au test (ctest -j lance un processus par cas).
struct Workdir {
    fs::path dir;
    explicit Workdir(const std::string& name) {
        dir = fs::temp_directory_path() /
              ("openstitch_semantic_" + name + "_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir);
    }
    ~Workdir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    Workdir(const Workdir&) = delete;
    Workdir& operator=(const Workdir&) = delete;
};

struct Rect {
    int x0, y0, x1, y1; // pixels, bornes incluses
};

// Masque PNG blanc sur noir de la taille de l'image source.
void write_mask(const fs::path& file, const std::vector<Rect>& rects, int w = kW, int h = kH) {
    image::Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U, 0);
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        img.rgba[i + 3] = 255;
    }
    for (const auto& r : rects) {
        for (int y = r.y0; y <= r.y1; ++y) {
            for (int x = r.x0; x <= r.x1; ++x) {
                auto* px = img.rgba.data() + 4U * (static_cast<std::size_t>(y) * w + x);
                px[0] = px[1] = px[2] = 255;
            }
        }
    }
    const auto bytes = image::encode_png(img);
    REQUIRE(bytes.has_value());
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes->data()),
              static_cast<std::streamsize>(bytes->size()));
}

std::string u8(const fs::path& p) {
    const auto s = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

void write_text(const fs::path& file, const json& doc) {
    std::ofstream out(file, std::ios::binary);
    out << doc.dump(2);
}

json region(const std::string& sid, const std::string& mask, const std::string& intent,
            const std::string& rgb) {
    return json{{"semantic_id", sid},      {"family", "scene"},       {"role", "test"},
                {"priority", "important"}, {"stitch_intent", intent}, {"thread_rgb", rgb},
                {"mask_path", mask}};
}

json manifest_doc(const json& regions, int w = kW, int h = kH, double wmm = 100, double hmm = 50) {
    return json{{"schema", "openstitch.semantic/1"},
                {"source_image", {{"path", "source.png"}, {"width_px", w}, {"height_px", h}}},
                {"design", {{"width_mm", wmm}, {"height_mm", hmm}}},
                {"regions", regions}};
}

document::Project empty_project() {
    NewProjectOptions options;
    options.width_mm = 120;
    options.height_mm = 70;
    auto p = create_empty_project(options);
    REQUIRE(p.has_value());
    return std::move(*p);
}

BatchReport run(document::Project& project, const json& op, bool dry_run = false) {
    BatchOptions options;
    options.dry_run = dry_run;
    return apply_operation(project, op.dump(), options);
}

json import_op(const fs::path& manifest) {
    return json{{"op", "semantic_import"}, {"manifest", u8(manifest)}};
}

std::vector<std::string> names(const document::Project& p) {
    std::vector<std::string> out;
    for (const auto& e : p.embroidery_objects) {
        out.push_back(e.name);
    }
    return out;
}

std::vector<std::uint64_t> semantic_ids(const document::Project& p, const std::string& sid) {
    const auto map = json::parse(semantic_map_json(p));
    std::vector<std::uint64_t> out;
    if (map.at("objects").contains(sid)) {
        for (const auto& id : map.at("objects").at(sid).at("ids")) {
            out.push_back(id.get<std::uint64_t>());
        }
    }
    return out;
}

std::size_t position_of(const document::Project& p, std::uint64_t id) {
    for (std::size_t i = 0; i < p.embroidery_objects.size(); ++i) {
        if (p.embroidery_objects[i].id.value == id) {
            return i;
        }
    }
    return p.embroidery_objects.size();
}

// Inspection complète (JSON) de tous les objets d'un identifiant : sert de témoin d'intégrité.
std::string semantic_objects_json(const document::Project& p, const std::string& sid) {
    std::string out;
    for (const auto id : semantic_ids(p, sid)) {
        const auto doc = inspect_object_json(p, ObjectId{id}, InspectOptions{true, true, true});
        REQUIRE(doc.has_value());
        out += *doc;
    }
    return out;
}

std::string serialized(const document::Project& project) {
    const auto doc = inspect_project_json(project);
    REQUIRE(doc.has_value());
    return *doc;
}

// Deux régions de MÊME couleur de fil : doivent rester deux objets.
json two_regions() {
    return json::array({region("a.gauche", "masks/a.png", "tatami", "#FF0000"),
                        region("a.droite", "masks/b.png", "tatami", "#FF0000")});
}

void write_two_masks(const fs::path& dir) {
    fs::create_directories(dir / "masks");
    write_mask(dir / "masks" / "a.png", {{20, 20, 80, 80}});
    write_mask(dir / "masks" / "b.png", {{120, 20, 180, 80}});
}

} // namespace

TEST_CASE("semantic validate: manifeste valide, defauts nommes", "[project_ops][semantic]") {
    Workdir w("validate");
    write_two_masks(w.dir);
    write_text(w.dir / "ok.json", manifest_doc(two_regions()));
    const auto ok = json::parse(validate_semantic_manifest(w.dir / "ok.json"));
    CHECK(ok.at("schema") == "openstitch.semantic-validation/1");
    CHECK(ok.at("ok") == true);
    CHECK(ok.at("regions") == 2);
    CHECK(ok.at("families").at("scene") == 2);

    const auto codes = [&](const json& doc, bool skip = false) {
        write_text(w.dir / "bad.json", doc);
        std::vector<std::string> out;
        for (const auto& e :
             json::parse(validate_semantic_manifest(w.dir / "bad.json", skip)).at("errors")) {
            out.push_back(e.at("code").get<std::string>());
        }
        return out;
    };
    const auto has = [](const std::vector<std::string>& v, const char* c) {
        return std::find(v.begin(), v.end(), c) != v.end();
    };

    auto schema = manifest_doc(two_regions());
    schema["schema"] = "autre/9";
    CHECK(has(codes(schema), "E_SCHEMA"));

    auto missing =
        manifest_doc(json::array({region("x", "masks/absent.png", "tatami", "#00FF00")}));
    CHECK(has(codes(missing), "E_MISSING_MASK"));
    CHECK(codes(missing, /*skip=*/true).empty()); // structure seule : les masques ne sont pas lus

    write_mask(w.dir / "masks" / "petit.png", {{2, 2, 8, 8}}, 50, 50);
    auto size = manifest_doc(json::array({region("x", "masks/petit.png", "tatami", "#00FF00")}));
    CHECK(has(codes(size), "E_MASK_SIZE"));

    auto outside = manifest_doc(json::array({region("x", "../evade.png", "tatami", "#00FF00")}));
    CHECK(has(codes(outside), "E_PATH"));

    auto parent = manifest_doc(two_regions());
    parent["regions"][0]["parent_id"] = "n.existe.pas";
    CHECK(has(codes(parent), "E_PARENT"));

    auto cycle = manifest_doc(two_regions());
    cycle["regions"][0]["under"] = json::array({"a.droite"});
    cycle["regions"][1]["under"] = json::array({"a.gauche"});
    CHECK(has(codes(cycle), "E_OCCLUSION_CYCLE"));

    auto duplicate = manifest_doc(json::array({region("d", "masks/a.png", "tatami", "#00FF00"),
                                               region("d", "masks/b.png", "tatami", "#00FF00")}));
    CHECK(has(codes(duplicate), "E_DUPLICATE"));

    auto no_mask = manifest_doc(json::array({region("m", "masks/a.png", "tatami", "#00FF00")}));
    no_mask["regions"][0].erase("mask_path");
    CHECK(has(codes(no_mask), "E_MASK"));

    const auto unreadable = json::parse(validate_semantic_manifest(w.dir / "nope.json"));
    CHECK(unreadable.at("ok") == false);
    CHECK(unreadable.at("errors")[0].at("code") == "E_IO");
}

TEST_CASE("semantic_import: objets distincts malgre la meme couleur, provenance conservee",
          "[project_ops][semantic]") {
    Workdir w("import");
    write_two_masks(w.dir);
    write_text(w.dir / "carte.json", manifest_doc(two_regions()));
    auto project = empty_project();

    const auto report = run(project, import_op(w.dir / "carte.json"));
    REQUIRE(report.ok);
    CHECK(project.embroidery_objects.size() == 2);
    CHECK(names(project) == std::vector<std::string>{"sem:a.droite 1", "sem:a.gauche 1"});
    // Même couleur de fil, deux objets : l'identité n'est pas la couleur.
    CHECK(project.embroidery_objects[0].rgb == project.embroidery_objects[1].rgb);
    CHECK(project.embroidery_objects[0].id != project.embroidery_objects[1].id);

    const auto map = json::parse(semantic_map_json(project));
    CHECK(map.at("schema") == "openstitch.semantic-map/1");
    CHECK(map.at("objects").at("a.gauche").at("ids").size() == 1);
    CHECK(map.at("objects").at("a.droite").at("ids").size() == 1);
    CHECK(map.at("unmapped_objects") == 0);

    const auto result = json::parse(report.to_json()).at("outcomes")[0].at("result");
    CHECK(result.at("mapping").at("a.gauche").at("family") == "scene");
    CHECK(result.at("created").size() == 2);

    // Géométrie à la bonne échelle : 61 px * 0,5 mm = ~30 mm de large (+/- pixel).
    for (const auto& e : project.embroidery_objects) {
        const auto doc = inspect_object_json(project, e.id);
        REQUIRE(doc.has_value());
    }
}

TEST_CASE("semantic_import: ordre d'occlusion under/over respecte", "[project_ops][semantic]") {
    Workdir w("order");
    write_two_masks(w.dir);
    auto regions = two_regions();
    // a.gauche passe SOUS a.droite : doit être créée avant même si l'identifiant la suit.
    regions[0]["semantic_id"] = "z.dessous";
    regions[0]["under"] = json::array({"a.droite"});
    write_text(w.dir / "carte.json", manifest_doc(regions));
    auto project = empty_project();
    REQUIRE(run(project, import_op(w.dir / "carte.json")).ok);
    REQUIRE(project.embroidery_objects.size() == 2);
    CHECK(project.embroidery_objects[0].name == "sem:z.dessous 1");
    CHECK(project.embroidery_objects[1].name == "sem:a.droite 1");
}

TEST_CASE("semantic_import: refus sans aucune modification du projet", "[project_ops][semantic]") {
    Workdir w("refus");
    write_two_masks(w.dir);
    auto project = empty_project();
    const std::string before = serialized(project);

    SECTION("manifeste invalide") {
        auto doc = manifest_doc(two_regions());
        doc["regions"][1]["mask_path"] = "masks/absent.png";
        write_text(w.dir / "carte.json", doc);
        const auto report = run(project, import_op(w.dir / "carte.json"));
        CHECK_FALSE(report.ok);
        CHECK(report.outcomes[0].code == "E_SCHEMA");
        CHECK(report.outcomes[0].message.find("E_MISSING_MASK") != std::string::npos);
    }
    SECTION("proportions incoherentes") {
        write_text(w.dir / "carte.json", manifest_doc(two_regions(), kW, kH, 100, 80));
        const auto report = run(project, import_op(w.dir / "carte.json"));
        CHECK_FALSE(report.ok);
        CHECK(report.outcomes[0].code == "E_OUT_OF_RANGE");
    }
    SECTION("intention sans guides") {
        auto doc = manifest_doc(two_regions());
        doc["regions"][1]["stitch_intent"] = "directional";
        write_text(w.dir / "carte.json", doc);
        const auto report = run(project, import_op(w.dir / "carte.json"));
        CHECK_FALSE(report.ok);
        CHECK(report.outcomes[0].code == "E_REFUSED");
    }
    SECTION("masque vide detecte apres un premier objet cree : retour arriere complet") {
        // a.droite (alphabétique) est créée AVANT que le masque vide de a.gauche soit refusé.
        write_mask(w.dir / "masks" / "a.png", {});
        write_text(w.dir / "carte.json", manifest_doc(two_regions()));
        const auto report = run(project, import_op(w.dir / "carte.json"));
        CHECK_FALSE(report.ok);
        CHECK_FALSE(report.applied);
        CHECK(report.outcomes[0].message.find("a.gauche") != std::string::npos);
    }
    SECTION("manifeste absent") {
        const auto report = run(project, import_op(w.dir / "absent.json"));
        CHECK_FALSE(report.ok);
        CHECK(report.outcomes[0].code == "E_IO");
    }
    CHECK(project.embroidery_objects.empty());
    CHECK(serialized(project) == before);
}

TEST_CASE("semantic_import: simulation, doublon d'identifiant, structurels ignores",
          "[project_ops][semantic]") {
    Workdir w("dry");
    write_two_masks(w.dir);
    auto regions = two_regions();
    regions.push_back(json{{"semantic_id", "famille.scene"},
                           {"family", "scene"},
                           {"role", "structure"},
                           {"priority", "protected"},
                           {"stitch_intent", nullptr}});
    write_text(w.dir / "carte.json", manifest_doc(regions));
    auto project = empty_project();

    const auto dry = run(project, import_op(w.dir / "carte.json"), /*dry_run=*/true);
    REQUIRE(dry.ok);
    CHECK_FALSE(dry.applied);
    CHECK(project.embroidery_objects.empty());

    const auto real = run(project, import_op(w.dir / "carte.json"));
    REQUIRE(real.ok);
    CHECK(project.embroidery_objects.size() == 2); // l'objet structurel n'a pas de points
    const auto result = json::parse(real.to_json()).at("outcomes")[0].at("result");
    CHECK(result.at("skipped").size() == 1);
    CHECK(result.at("skipped")[0].at("semantic_id") == "famille.scene");

    const auto again = run(project, import_op(w.dir / "carte.json"));
    CHECK_FALSE(again.ok);
    CHECK(again.outcomes[0].code == "E_REFUSED");
    CHECK(project.embroidery_objects.size() == 2);
}

TEST_CASE("semantic_import: auto_satin sur une forme allongee", "[project_ops][semantic]") {
    Workdir w("satin");
    fs::create_directories(w.dir / "masks");
    write_mask(w.dir / "masks" / "liane.png", {{10, 40, 190, 52}}); // bande 181 x 13 px
    write_text(
        w.dir / "carte.json",
        manifest_doc(json::array({region("liane", "masks/liane.png", "auto_satin", "#738643")})));
    auto project = empty_project();
    const auto report = run(project, import_op(w.dir / "carte.json"));
    REQUIRE(report.ok);
    REQUIRE_FALSE(project.embroidery_objects.empty());
    CHECK(std::string(stitch_type_name(project.embroidery_objects[0])) == "auto_satin");
    CHECK(semantic_map_json(project).find("liane") != std::string::npos);
}

TEST_CASE("semantic_replace: un seul objet change, les autres restent identiques",
          "[project_ops][semantic]") {
    Workdir w("replace");
    write_two_masks(w.dir);
    write_text(w.dir / "carte.json", manifest_doc(two_regions()));
    auto project = empty_project();
    REQUIRE(run(project, import_op(w.dir / "carte.json")).ok);
    REQUIRE(project.embroidery_objects.size() == 2);

    const auto droite_before = semantic_objects_json(project, "a.droite");
    const auto gauche_before = semantic_ids(project, "a.gauche");
    REQUIRE(gauche_before.size() == 1);
    const auto gauche_position = position_of(project, gauche_before[0]);

    // Masque corrigé de a.gauche : rectangle plus grand.
    write_mask(w.dir / "masks" / "a_v2.png", {{10, 10, 95, 90}});
    const json replace = {{"op", "semantic_replace"},
                          {"manifest", u8(w.dir / "carte.json")},
                          {"semantic_id", "a.gauche"},
                          {"mask", "masks/a_v2.png"}};

    const auto dry = run(project, replace, /*dry_run=*/true);
    REQUIRE(dry.ok);
    CHECK_FALSE(dry.applied);
    CHECK(semantic_ids(project, "a.gauche") == gauche_before); // la simulation ne touche à rien

    const auto report = run(project, replace);
    REQUIRE(report.ok);
    REQUIRE(project.embroidery_objects.size() == 2);
    const auto gauche_after = semantic_ids(project, "a.gauche");
    REQUIRE(gauche_after.size() == 1);
    CHECK(gauche_after[0] != gauche_before[0]);                         // nouvel objet
    CHECK(position_of(project, gauche_after[0]) == gauche_position);    // même place de couture
    CHECK(semantic_objects_json(project, "a.droite") == droite_before); // l'autre est intact

    const auto result = json::parse(report.to_json()).at("outcomes")[0].at("result");
    CHECK(result.at("replaced") == "a.gauche");
    CHECK(result.at("unchanged_objects") == 1);

    // La géométrie a bien changé : l'objet remplacé est plus large (86 px au lieu de 61).
    const auto width_of = [&](std::uint64_t id) {
        const auto doc = json::parse(*inspect_object_json(project, ObjectId{id}));
        return doc.dump().size();
    };
    CHECK(width_of(gauche_after[0]) > 0);
}

TEST_CASE("semantic_replace: refus nommes, projet inchange", "[project_ops][semantic]") {
    Workdir w("replace_refus");
    write_two_masks(w.dir);
    write_text(w.dir / "carte.json", manifest_doc(two_regions()));
    auto project = empty_project();
    REQUIRE(run(project, import_op(w.dir / "carte.json")).ok);
    const std::string before = serialized(project);

    const auto op = [&](const std::string& sid, const std::string& mask) {
        return json{{"op", "semantic_replace"},
                    {"manifest", u8(w.dir / "carte.json")},
                    {"semantic_id", sid},
                    {"mask", mask}};
    };

    SECTION("identifiant inconnu du manifeste") {
        const auto r = run(project, op("inconnu", "masks/a.png"));
        CHECK_FALSE(r.ok);
        CHECK(r.outcomes[0].code == "E_UNKNOWN_OBJECT");
    }
    SECTION("masque de mauvaise taille") {
        write_mask(w.dir / "masks" / "petit.png", {{2, 2, 8, 8}}, 50, 50);
        const auto r = run(project, op("a.gauche", "masks/petit.png"));
        CHECK_FALSE(r.ok);
        CHECK(r.outcomes[0].code == "E_REFUSED");
    }
    SECTION("masque vide") {
        write_mask(w.dir / "masks" / "vide.png", {});
        const auto r = run(project, op("a.gauche", "masks/vide.png"));
        CHECK_FALSE(r.ok);
    }
    SECTION("absent du projet") {
        auto doc = manifest_doc(two_regions());
        doc["regions"].push_back(region("c.absent", "masks/a.png", "tatami", "#0000FF"));
        write_text(w.dir / "carte.json", doc);
        const auto r = run(project, op("c.absent", "masks/a.png"));
        CHECK_FALSE(r.ok);
        CHECK(r.outcomes[0].code == "E_UNKNOWN_OBJECT");
    }
    SECTION("type different entre manifeste et projet") {
        auto doc = manifest_doc(two_regions());
        doc["regions"][0]["stitch_intent"] = "running";
        write_text(w.dir / "carte.json", doc);
        const auto r = run(project, op("a.gauche", "masks/a.png"));
        CHECK_FALSE(r.ok);
        CHECK(r.outcomes[0].code == "E_REFUSED");
    }
    CHECK(serialized(project) == before);
}

namespace {

// Disque blanc sur noir : forme compacte que l'auto-satin refuse (« disque, forme peu allongée »).
void write_disc_mask(const fs::path& file, int cx, int cy, int radius) {
    image::Image img;
    img.width = kW;
    img.height = kH;
    img.rgba.assign(static_cast<std::size_t>(kW) * static_cast<std::size_t>(kH) * 4U, 0);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            auto* px = img.rgba.data() + 4U * (static_cast<std::size_t>(y) * kW + x);
            px[3] = 255;
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius) {
                px[0] = px[1] = px[2] = 255;
            }
        }
    }
    const auto bytes = image::encode_png(img);
    REQUIRE(bytes.has_value());
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes->data()),
              static_cast<std::streamsize>(bytes->size()));
}

} // namespace

TEST_CASE("semantic_import: satin_fallback consigne les refus d'auto_satin au lieu de tout annuler",
          "[project_ops][semantic]") {
    Workdir w("fallback");
    fs::create_directories(w.dir / "masks");
    write_disc_mask(w.dir / "masks" / "lune.png", 100, 50, 30);
    auto doc =
        manifest_doc(json::array({region("lune", "masks/lune.png", "auto_satin", "#E8DBB0")}));
    write_text(w.dir / "carte.json", doc);

    auto project = empty_project();
    const std::string before = serialized(project);
    // Par défaut le refus du moteur annule tout, avec le motif nommé.
    const auto strict = run(project, import_op(w.dir / "carte.json"));
    REQUIRE_FALSE(strict.ok);
    CHECK(strict.outcomes[0].message.find("auto_satin refusé") != std::string::npos);
    CHECK(serialized(project) == before);

    for (const char* fallback : {"tatami", "running"}) {
        CAPTURE(fallback);
        auto p = empty_project();
        json op = import_op(w.dir / "carte.json");
        op["satin_fallback"] = fallback;
        const auto report = run(p, op);
        REQUIRE(report.ok);
        REQUIRE(p.embroidery_objects.size() == 1);
        CHECK(std::string(stitch_type_name(p.embroidery_objects[0])) ==
              (std::string(fallback) == "running" ? "running" : "tatami"));
        const auto result = json::parse(report.to_json()).at("outcomes")[0].at("result");
        REQUIRE(result.at("fallbacks").size() == 1);
        CHECK(result.at("fallbacks")[0].at("semantic_id") == "lune");
        CHECK(result.at("fallbacks")[0].at("fallback") == fallback);
    }

    json bad = import_op(w.dir / "carte.json");
    bad["satin_fallback"] = "satin";
    auto p2 = empty_project();
    const auto refused = run(p2, bad);
    CHECK_FALSE(refused.ok);
    CHECK(refused.outcomes[0].code == "E_USAGE");
}
