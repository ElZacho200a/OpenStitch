// SPDX-License-Identifier: Apache-2.0
// Dispatcher transactionnel : lots, validation, alias, dry-run, rapports, catalogue.
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "openstitch/document/project.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/errors.hpp"
#include "openstitch/project_ops/operations.hpp"

namespace {

using json = nlohmann::json;
using namespace openstitch;
using Catch::Matchers::ContainsSubstring;

document::VectorObject square(ObjectId id, int x_mm, int y_mm, int size_mm) {
    document::VectorObject v;
    v.id = id;
    v.name = "Forme " + std::to_string(id.value);
    geometry::PathSet set;
    const auto pt = [&](int x, int y) {
        geometry::PathNode n;
        n.pos = Vec2um{Micrometers{x * 1000}, Micrometers{y * 1000}};
        return n;
    };
    set.outer.nodes = {pt(x_mm, y_mm), pt(x_mm + size_mm, y_mm), pt(x_mm + size_mm, y_mm + size_mm),
                       pt(x_mm, y_mm + size_mm)};
    set.outer.closed = true;
    v.paths.push_back(set);
    return v;
}

// Projet de test : ids 1..6 (vecteur, broderie) x 3. Broderies : 2 tatami bleu, 4 contour rouge,
// 6 tatami vert.
document::Project make_project() {
    document::Project p;
    const auto add = [&](int x_mm, std::array<std::uint8_t, 3> rgb, document::StitchParams params,
                         const char* name) {
        const ObjectId v = p.object_ids.next();
        p.vector_objects.push_back(square(v, x_mm, 0, 20));
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.name = name;
        e.source_vector = v;
        e.rgb = rgb;
        e.params = std::move(params);
        p.embroidery_objects.push_back(std::move(e));
    };
    add(0, {0, 0, 255}, document::TatamiParams{}, "Fond");
    add(30, {255, 0, 0}, document::RunningStitchParams{}, "Contour");
    add(60, {0, 255, 0}, document::TatamiParams{}, "Feuille");
    return p;
}

// Sérialisation .osp (fichier temporaire) : comparaison stricte avant/après.
std::string serialize(const document::Project& project) {
    // `ctest -j` lance chaque cas dans son propre processus : le compteur seul donne le même nom
    // à tous (collision sur le fichier). Le nonce horaire est propre au processus.
    static const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    static std::atomic<int> counter{0};
    const auto path =
        std::filesystem::temp_directory_path() /
        ("openstitch_ops_test_" + std::to_string(nonce) + "_" + std::to_string(counter++) + ".osp");
    const auto saved = project_io::save_project(path, project);
    REQUIRE(saved.has_value());
    std::ifstream in(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::filesystem::remove(path);
    return bytes;
}

json report_json(const project_ops::BatchReport& r) {
    return json::parse(r.to_json());
}

std::string batch(const std::string& operations) {
    return R"({"schema":"openstitch.batch/1","operations":[)" + operations + "]}";
}

} // namespace

TEST_CASE("ops serialization is deterministic for the comparison helper", "[project_ops][ops]") {
    const auto p = make_project();
    REQUIRE(serialize(p) == serialize(p));
}

TEST_CASE("ops batch applies operations in order and reports each one", "[project_ops][ops]") {
    auto project = make_project();
    const auto report = project_ops::apply_batch(
        project, batch(R"({"op":"rename","object":2,"name":"Ciel"},)"
                       R"({"op":"set_visible","object":4,"visible":false})"));
    REQUIRE(report.ok);
    REQUIRE(report.applied);
    REQUIRE_FALSE(report.dry_run);
    REQUIRE(report.operation_count == 2);
    REQUIRE(report.outcomes.size() == 2);
    CHECK(report.outcomes[0].op == "rename");
    CHECK(report.outcomes[1].op == "set_visible");
    CHECK(project.findEmbroidery(ObjectId{2})->name == "Ciel");
    CHECK_FALSE(project.findEmbroidery(ObjectId{4})->visible);

    const auto j = report_json(report);
    CHECK(j["schema"] == "openstitch.batch-report/1");
    CHECK(j["ok"] == true);
    CHECK(j["applied"] == true);
    CHECK(j["error"].is_null());
    CHECK(j["outcomes"].size() == 2);
    CHECK(j["outcomes"][0]["result"]["id"] == 2);
}

TEST_CASE("ops batch is transactional when the third operation is refused", "[project_ops][ops]") {
    auto project = make_project();
    const std::string before = serialize(project);
    const auto report = project_ops::apply_batch(
        project, batch(R"({"op":"rename","object":2,"name":"A"},)"
                       R"({"op":"set_visible","object":4,"visible":false},)"
                       R"({"op":"rename","object":999,"name":"B"},)"
                       R"({"op":"rename","object":6,"name":"C"})"));
    REQUIRE_FALSE(report.ok);
    REQUIRE_FALSE(report.applied);
    REQUIRE(report.outcomes.size() == 3); // la 4e n'est pas examinée
    CHECK(report.outcomes[0].ok);
    CHECK(report.outcomes[1].ok);
    CHECK_FALSE(report.outcomes[2].ok);
    CHECK(report.outcomes[2].index == 2);
    CHECK(report.outcomes[2].op == "rename");
    CHECK(report.outcomes[2].code == project_ops::code::kUnknownObject);
    CHECK_FALSE(report.outcomes[2].message.empty());
    CHECK(serialize(project) == before);
    CHECK(project.findEmbroidery(ObjectId{2})->name == "Fond");
    CHECK(project.findEmbroidery(ObjectId{4})->visible);

    const auto j = report_json(report);
    CHECK(j["ok"] == false);
    CHECK(j["outcomes"][2]["error"]["code"] == "E_UNKNOWN_OBJECT");
}

TEST_CASE("ops batch validates everything before applying anything", "[project_ops][ops]") {
    auto project = make_project();
    const std::string before = serialize(project);
    // La 1re opération est valide, la 3e porte un argument inconnu : rien ne doit être appliqué.
    const auto report = project_ops::apply_batch(
        project, batch(R"({"op":"rename","object":2,"name":"A"},)"
                       R"({"op":"set_visible","object":4,"visible":false},)"
                       R"({"op":"rename","object":6,"name":"C","colour":"red"})"));
    REQUIRE_FALSE(report.ok);
    REQUIRE_FALSE(report.applied);
    REQUIRE(report.outcomes.size() == 1);
    CHECK(report.outcomes[0].index == 2);
    CHECK(report.outcomes[0].op == "rename");
    CHECK(report.outcomes[0].code == project_ops::code::kUsage);
    CHECK_THAT(report.outcomes[0].message, ContainsSubstring("colour"));
    CHECK_THAT(report.outcomes[0].hint, ContainsSubstring("name"));
    CHECK(serialize(project) == before);
}

TEST_CASE("ops validation refuses unknown ops, missing and mistyped arguments",
          "[project_ops][ops]") {
    auto project = make_project();
    const std::string before = serialize(project);
    const auto run = [&](const std::string& op) {
        return project_ops::apply_batch(project, batch(op));
    };
    SECTION("unknown operation lists the available ones") {
        const auto r = run(R"({"op":"explode","object":2})");
        REQUIRE(r.outcomes.size() == 1);
        CHECK(r.outcomes[0].code == project_ops::code::kUnknownOp);
        CHECK_THAT(r.outcomes[0].hint, ContainsSubstring("rename"));
    }
    SECTION("missing op field") {
        const auto r = run(R"({"object":2})");
        REQUIRE(r.outcomes.size() == 1);
        CHECK(r.outcomes[0].code == project_ops::code::kSchema);
    }
    SECTION("missing required argument") {
        const auto r = run(R"({"op":"rename","object":2})");
        REQUIRE(r.outcomes.size() == 1);
        CHECK(r.outcomes[0].code == project_ops::code::kUsage);
        CHECK_THAT(r.outcomes[0].message, ContainsSubstring("name"));
    }
    SECTION("wrong argument type") {
        const auto r = run(R"({"op":"set_visible","object":2,"visible":"yes"})");
        REQUIRE(r.outcomes.size() == 1);
        CHECK(r.outcomes[0].code == project_ops::code::kUsage);
        CHECK_THAT(r.outcomes[0].message, ContainsSubstring("boolean"));
    }
    SECTION("negative identifier is not an object id") {
        const auto r = run(R"({"op":"rename","object":-3,"name":"x"})");
        REQUIRE(r.outcomes.size() == 1);
        CHECK(r.outcomes[0].code == project_ops::code::kUsage);
    }
    SECTION("operation that is not an object") {
        const auto r = run("42");
        REQUIRE(r.outcomes.size() == 1);
        CHECK(r.outcomes[0].code == project_ops::code::kSchema);
    }
    CHECK(serialize(project) == before);
}

TEST_CASE("ops dry run simulates without modifying the project", "[project_ops][ops]") {
    auto project = make_project();
    const std::string before = serialize(project);
    const project_ops::BatchOptions dry{.dry_run = true};

    const auto good = project_ops::apply_batch(
        project, batch(R"({"op":"rename","object":2,"name":"Simule"})"), dry);
    REQUIRE(good.ok);
    CHECK_FALSE(good.applied);
    CHECK(good.dry_run);
    CHECK(report_json(good)["dry_run"] == true);
    CHECK(serialize(project) == before);

    const auto bad = project_ops::apply_batch(
        project,
        batch(
            R"({"op":"rename","object":2,"name":"Simule"},{"op":"rename","object":77,"name":"x"})"),
        dry);
    REQUIRE_FALSE(bad.ok);
    CHECK_FALSE(bad.applied);
    REQUIRE(bad.outcomes.size() == 2);
    CHECK(bad.outcomes[1].code == project_ops::code::kUnknownObject);
    CHECK(serialize(project) == before);
}

TEST_CASE("ops aliases bind the produced id and resolve later references", "[project_ops][ops]") {
    auto project = make_project();
    const auto report = project_ops::apply_batch(
        project, batch(R"({"op":"rename","object":2,"name":"Premier","as":"first"},)"
                       R"({"op":"set_visible","object":"$first","visible":false})"));
    REQUIRE(report.ok);
    CHECK_FALSE(project.findEmbroidery(ObjectId{2})->visible);
    CHECK(project.findEmbroidery(ObjectId{4})->visible);

    SECTION("an alias never defined is refused at validation") {
        auto p2 = make_project();
        const std::string before = serialize(p2);
        const auto r = project_ops::apply_batch(
            p2, batch(R"({"op":"rename","object":2,"name":"X"},)"
                      R"({"op":"set_visible","object":"$ghost","visible":false})"));
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.outcomes.size() == 1);
        CHECK(r.outcomes[0].index == 1);
        CHECK(r.outcomes[0].code == project_ops::code::kUnknownAlias);
        CHECK(serialize(p2) == before);
    }
    SECTION("an alias used before its definition is refused") {
        auto p2 = make_project();
        const auto r = project_ops::apply_batch(
            p2, batch(R"({"op":"set_visible","object":"$late","visible":false},)"
                      R"({"op":"rename","object":2,"name":"X","as":"late"})"));
        REQUIRE_FALSE(r.ok);
        CHECK(r.outcomes[0].code == project_ops::code::kUnknownAlias);
    }
    SECTION("an alias defined twice is refused") {
        auto p2 = make_project();
        const auto r = project_ops::apply_batch(
            p2, batch(R"({"op":"rename","object":2,"name":"X","as":"a"},)"
                      R"({"op":"rename","object":4,"name":"Y","as":"a"})"));
        REQUIRE_FALSE(r.ok);
        CHECK(r.outcomes[0].code == project_ops::code::kUsage);
    }
    SECTION("an invalid alias name is refused") {
        auto p2 = make_project();
        const auto r = project_ops::apply_batch(
            p2, batch(R"({"op":"rename","object":2,"name":"X","as":"1bad"})"));
        REQUIRE_FALSE(r.ok);
        CHECK(r.outcomes[0].code == project_ops::code::kUsage);
    }
    SECTION("as on an operation that produces no id is refused and nothing is applied") {
        auto p2 = make_project();
        const std::string before = serialize(p2);
        const auto r =
            project_ops::apply_batch(p2, batch(R"({"op":"set_canvas","width_mm":120,"as":"c"})"));
        REQUIRE_FALSE(r.ok);
        CHECK(r.outcomes[0].code == project_ops::code::kUsage);
        CHECK(serialize(p2) == before);
    }
}

TEST_CASE("ops report is deterministic", "[project_ops][ops]") {
    const std::string text =
        batch(R"({"op":"rename","object":2,"name":"Ciel"},)"
              R"({"op":"set_params","object":2,"params":{"row_spacing_mm":0.45}},)"
              R"({"op":"rename","object":123,"name":"x"})");
    auto a = make_project();
    auto b = make_project();
    const auto ra = project_ops::apply_batch(a, text);
    const auto rb = project_ops::apply_batch(b, text);
    CHECK(ra.to_json() == rb.to_json());

    const std::string good = batch(R"({"op":"rename","object":2,"name":"Ciel"})");
    auto c = make_project();
    auto d = make_project();
    CHECK(project_ops::apply_batch(c, good).to_json() ==
          project_ops::apply_batch(d, good).to_json());
    CHECK(serialize(c) == serialize(d));
}

TEST_CASE("ops malformed or unknown batches give a global error without touching the project",
          "[project_ops][ops]") {
    auto project = make_project();
    const std::string before = serialize(project);
    const auto expect_schema_error = [&](const std::string& text) {
        const auto r = project_ops::apply_batch(project, text);
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.applied);
        CHECK(r.error_code == project_ops::code::kSchema);
        CHECK_FALSE(r.error_message.empty());
        CHECK(r.outcomes.empty());
        const auto j = report_json(r);
        CHECK(j["error"]["code"] == "E_SCHEMA");
        CHECK(j["ok"] == false);
        CHECK(serialize(project) == before);
    };
    SECTION("malformed json") {
        expect_schema_error("{\"schema\": ");
    }
    SECTION("empty text") {
        expect_schema_error("");
    }
    SECTION("unknown schema") {
        expect_schema_error(R"({"schema":"openstitch.batch/9","operations":[{"op":"rename"}]})");
    }
    SECTION("missing schema") {
        expect_schema_error(R"({"operations":[{"op":"rename","object":2,"name":"x"}]})");
    }
    SECTION("operations is not an array") {
        expect_schema_error(R"({"schema":"openstitch.batch/1","operations":{}})");
    }
    SECTION("empty batch") {
        expect_schema_error(R"({"schema":"openstitch.batch/1","operations":[]})");
    }
    SECTION("unknown top-level key") {
        expect_schema_error(R"({"schema":"openstitch.batch/1","operations":[],"x":1})");
    }
    SECTION("array at top level") {
        expect_schema_error("[]");
    }
    SECTION("bare operation refused by apply_batch") {
        expect_schema_error(R"({"op":"rename","object":2,"name":"x"})");
    }
}

TEST_CASE("ops apply_operation accepts a bare operation and an envelope", "[project_ops][ops]") {
    auto project = make_project();
    const auto bare =
        project_ops::apply_operation(project, R"({"op":"rename","object":2,"name":"Seule"})");
    REQUIRE(bare.ok);
    CHECK(bare.applied);
    CHECK(project.findEmbroidery(ObjectId{2})->name == "Seule");

    const auto env =
        project_ops::apply_operation(project, batch(R"({"op":"rename","object":4,"name":"Env"})"));
    REQUIRE(env.ok);
    CHECK(project.findEmbroidery(ObjectId{4})->name == "Env");

    const auto bad = project_ops::apply_operation(project, R"({"op":"rename","object":2})");
    CHECK_FALSE(bad.ok);
    CHECK(bad.outcomes.size() == 1);

    const auto garbage = project_ops::apply_operation(project, "not json");
    CHECK(garbage.error_code == project_ops::code::kSchema);
}

TEST_CASE("ops catalog is generated from the dispatcher and every example is valid",
          "[project_ops][ops]") {
    const json catalog = json::parse(project_ops::operations_catalog_json());
    CHECK(catalog["schema"] == "openstitch.operations/1");
    CHECK(catalog["batch_schema"] == "openstitch.batch/1");
    CHECK(project_ops::operations_catalog_json() == project_ops::operations_catalog_json());

    std::set<std::string> names;
    std::string previous;
    for (const auto& op : catalog["operations"]) {
        const std::string name = op["name"].get<std::string>();
        INFO("operation " << name);
        CHECK(names.insert(name).second); // pas de doublon
        CHECK(name > previous);           // ordre alphabétique stable
        previous = name;
        CHECK_FALSE(op["summary"].get<std::string>().empty());
        REQUIRE_FALSE(op.contains("example_invalid"));
        const json& example = op["example"];
        REQUIRE(example.is_object());
        CHECK(example["op"] == name);

        std::set<std::string> arg_names{"op", "as"};
        for (const auto& a : op["args"]) {
            arg_names.insert(a["name"].get<std::string>());
            CHECK_FALSE(a["description"].get<std::string>().empty());
        }
        for (const auto& [key, value] : example.items()) {
            CHECK(arg_names.count(key) == 1);
        }
        for (const auto& a : op["args"]) {
            const std::string an = a["name"].get<std::string>();
            const std::string type = a["type"].get<std::string>();
            if (a["required"].get<bool>()) {
                CHECK(example.contains(an));
            }
            if (!example.contains(an)) {
                continue;
            }
            const json& v = example[an];
            if (type == "object_id") {
                CHECK((v.is_number_unsigned() || v.is_string()));
            } else if (type == "number") {
                CHECK(v.is_number());
            } else if (type == "integer") {
                CHECK(v.is_number_integer());
            } else if (type == "string") {
                CHECK(v.is_string());
            } else if (type == "boolean") {
                CHECK(v.is_boolean());
            } else if (type == "array") {
                CHECK(v.is_array());
            } else if (type == "object") {
                CHECK(v.is_object());
            }
        }

        // Le dispatcher accepte l'exemple : jamais d'opération/schéma/argument refusé à la
        // validation (un refus métier sur un projet de test est légitime).
        auto project = make_project();
        const auto r = project_ops::apply_operation(project, example.dump(),
                                                    project_ops::BatchOptions{.dry_run = true});
        CHECK(r.error_code.empty());
        for (const auto& o : r.outcomes) {
            CHECK(o.code != project_ops::code::kUnknownOp);
            CHECK(o.code != project_ops::code::kSchema);
            CHECK(o.code != project_ops::code::kInternal);
        }
    }
    // Les opérations de style/paramètres/ordre/projet sont toutes au catalogue.
    for (const char* expected :
         {"set_color", "rename", "set_visible", "set_locked", "set_join", "set_connector",
          "set_stitch_type", "set_params", "set_order", "optimize_order", "remove_object",
          "discard_overrides", "set_canvas", "set_finishing"}) {
        CHECK(names.count(expected) == 1);
    }
}

TEST_CASE("ops each example of this family really applies on the test project",
          "[project_ops][ops]") {
    const json catalog = json::parse(project_ops::operations_catalog_json());
    const std::set<std::string> mine{"set_color",       "rename",        "set_visible",
                                     "set_locked",      "set_join",      "set_connector",
                                     "set_stitch_type", "set_params",    "set_order",
                                     "optimize_order",  "remove_object", "discard_overrides",
                                     "set_canvas",      "set_finishing"};
    for (const auto& op : catalog["operations"]) {
        const std::string name = op["name"].get<std::string>();
        if (mine.count(name) == 0) {
            continue;
        }
        INFO("operation " << name);
        auto project = make_project();
        const auto r = project_ops::apply_operation(project, op["example"].dump());
        CHECK(r.ok);
        CHECK(r.applied);
    }
}
