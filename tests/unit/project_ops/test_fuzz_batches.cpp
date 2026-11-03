// SPDX-License-Identifier: Apache-2.0
// HP-QA-014 : fuzzing déterministe (graine fixe) des lots d'opérations `apply`.
// Contrat : jamais de crash, d'exception ni de blocage ; tout refus porte un code E_* ; un lot
// non appliqué laisse le projet strictement inchangé.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fuzz_mutator.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_ops/errors.hpp"
#include "openstitch/project_ops/operations.hpp"

namespace {

using json = nlohmann::json;
using namespace openstitch;
namespace po = openstitch::project_ops;

// Nombre de mutations par graine (chaque exemple du catalogue + lots composés).
constexpr std::size_t kMutationsPerSeed = 14;
constexpr std::size_t kComposedMutations = 300;
constexpr std::uint64_t kBaseSeed = 0x0517C4F00D2026ULL;
constexpr auto kPerMutationLimit = std::chrono::seconds(20);

document::VectorObject square(ObjectId id, int x_mm, int size_mm) {
    document::VectorObject v;
    v.id = id;
    v.name = "Forme " + std::to_string(id.value);
    geometry::PathSet set;
    const auto pt = [&](int x, int y) {
        geometry::PathNode n;
        n.pos = Vec2um{Micrometers{x * 1000}, Micrometers{y * 1000}};
        return n;
    };
    set.outer.nodes = {pt(x_mm, 0), pt(x_mm + size_mm, 0), pt(x_mm + size_mm, size_mm),
                       pt(x_mm, size_mm)};
    set.outer.closed = true;
    v.paths.push_back(set);
    return v;
}

// Projet de test : ids 1..6 (vecteur, broderie) x 3.
document::Project make_project() {
    document::Project p;
    const auto add = [&](int x_mm, std::array<std::uint8_t, 3> rgb, document::StitchParams params,
                         const char* name) {
        const ObjectId v = p.object_ids.next();
        p.vector_objects.push_back(square(v, x_mm, 20));
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

// Empreinte bon marché de l'état modifiable par un lot (détecte une mutation partielle).
std::string fingerprint(const document::Project& p) {
    std::string out = std::to_string(p.vector_objects.size()) + "/" +
                      std::to_string(p.embroidery_objects.size()) + "/" +
                      std::to_string(p.object_ids.last());
    for (const auto& e : p.embroidery_objects) {
        out += "|" + std::to_string(e.id.value) + ":" + e.name + ":" + std::to_string(e.rgb[0]) +
               "," + std::to_string(e.rgb[1]) + "," + std::to_string(e.rgb[2]) +
               (e.visible ? "v" : "h") + (e.locked ? "l" : "u") + std::to_string(e.params.index());
    }
    return out;
}

bool is_coded(const std::string& code) {
    return code.size() > 2 && code.rfind("E_", 0) == 0;
}

void check_report(const po::BatchReport& report, const std::string& input) {
    INFO("entree : " << input.substr(0, 400));
    const json doc = json::parse(report.to_json(), nullptr, /*allow_exceptions=*/false);
    REQUIRE_FALSE(doc.is_discarded());
    CHECK(doc.at("schema") == "openstitch.batch-report/1");
    if (report.ok) {
        CHECK(report.error_code.empty());
        for (const auto& o : report.outcomes) {
            CHECK(o.ok);
        }
        return;
    }
    // Refus : un code global, ou au moins une opération fautive codée.
    bool coded = is_coded(report.error_code);
    for (const auto& o : report.outcomes) {
        if (!o.ok) {
            coded = coded || is_coded(o.code);
            CHECK(is_coded(o.code));
            CHECK_FALSE(o.message.empty());
        }
    }
    CHECK(coded);
    CHECK_FALSE(report.applied);
}

std::string wrap(const json& operations) {
    return json{{"schema", po::kBatchSchema}, {"operations", operations}}.dump();
}

void run_mutation(const std::string& mutated, bool dry_run) {
    auto project = make_project();
    const std::string before = fingerprint(project);
    const auto start = std::chrono::steady_clock::now();
    po::BatchOptions options;
    options.dry_run = dry_run;
    const auto report = po::apply_batch(project, mutated, options);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    INFO("entree : " << mutated.substr(0, 400));
    CHECK(elapsed < kPerMutationLimit);
    check_report(report, mutated);
    if (!report.applied) {
        CHECK(fingerprint(project) == before);
    }
}

} // namespace

TEST_CASE("fuzz apply: mutations de chaque exemple du catalogue (graine fixe)",
          "[project_ops][fuzz]") {
    const json catalog = json::parse(po::operations_catalog_json());
    std::size_t total = 0;
    std::uint64_t index = 0;
    for (const auto& op : catalog["operations"]) {
        const std::string seed = wrap(json::array({op["example"]}));
        openstitch::qa::Rng rng(kBaseSeed + index++);
        for (std::size_t i = 0; i < kMutationsPerSeed; ++i) {
            run_mutation(openstitch::qa::mutate(seed, rng), /*dry_run=*/i % 3 == 0);
            ++total;
        }
    }
    CHECK(total >= 500);
}

TEST_CASE("fuzz apply: mutations de lots composes avec alias (graine fixe)",
          "[project_ops][fuzz]") {
    const json catalog = json::parse(po::operations_catalog_json());
    json all = json::array();
    for (const auto& op : catalog["operations"]) {
        all.push_back(op["example"]);
    }
    const std::string composed = wrap(all);
    const std::string aliased =
        wrap(json::array({{{"op", "add_shape"},
                           {"shape", "rect"},
                           {"width_mm", 10},
                           {"height_mm", 5},
                           {"center_mm", json::array({10, 10})},
                           {"stitch_type", "tatami"},
                           {"as", "r"}},
                          {{"op", "rename"}, {"object", "$r"}, {"name", "Carre"}},
                          {{"op", "duplicate_object"}, {"object", "$r"}, {"as", "d"}},
                          {{"op", "set_color"}, {"object", "$d"}, {"rgb", "#112233"}}}));
    openstitch::qa::Rng rng(kBaseSeed ^ 0xA5A5A5A5ULL);
    for (std::size_t i = 0; i < kComposedMutations; ++i) {
        run_mutation(openstitch::qa::mutate(i % 2 == 0 ? composed : aliased, rng),
                     /*dry_run=*/true);
    }
}

TEST_CASE("fuzz apply: la meme graine donne les memes mutations", "[project_ops][fuzz]") {
    const std::string seed = wrap(json::array({{{"op", "rename"}, {"object", 2}, {"name", "x"}}}));
    openstitch::qa::Rng a(42);
    openstitch::qa::Rng b(42);
    for (int i = 0; i < 50; ++i) {
        CHECK(openstitch::qa::mutate(seed, a) == openstitch::qa::mutate(seed, b));
    }
}

TEST_CASE("fuzz apply: entrees degenerees refusees avec un code", "[project_ops][fuzz]") {
    for (const std::string& input :
         {std::string{}, std::string("null"), std::string("[]"), std::string("{}"),
          std::string("\"x\""), std::string(R"({"schema":"openstitch.batch/1"})"),
          std::string(R"({"schema":"openstitch.batch/1","operations":{}})"),
          std::string(R"({"schema":"openstitch.batch/1","operations":[null,1,"a",[]]})"),
          std::string(R"({"schema":"openstitch.batch/9","operations":[]})"), std::string(1000, '['),
          std::string(1000, '{'), std::string("\0\0\0", 3)}) {
        run_mutation(input, /*dry_run=*/false);
    }
}
