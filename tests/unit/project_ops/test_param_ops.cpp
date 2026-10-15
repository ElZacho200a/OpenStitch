// SPDX-License-Identifier: Apache-2.0
// Opérations de style, de paramètres de couture, d'ordre et de projet.
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "openstitch/document/project.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/errors.hpp"
#include "openstitch/project_ops/operations.hpp"
#include "openstitch/thread_palette/thread_library.hpp"

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

// Ids 1..6 : (vecteur 1, broderie 2 tatami bleu), (3, 4 contour rouge), (5, 6 tatami vert).
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

std::string serialize(const document::Project& project) {
    static std::atomic<int> counter{0};
    const auto path = std::filesystem::temp_directory_path() /
                      ("openstitch_param_test_" + std::to_string(counter++) + ".osp");
    const auto saved = project_io::save_project(path, project);
    REQUIRE(saved.has_value());
    std::ifstream in(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::filesystem::remove(path);
    return bytes;
}

// Exécute UNE opération ; renvoie le rapport.
project_ops::BatchReport run(document::Project& project, const std::string& op) {
    return project_ops::apply_operation(project, op);
}

// Refus attendu : code donné, projet strictement inchangé.
project_ops::OperationOutcome expect_refused(document::Project& project, const std::string& op,
                                             const char* code) {
    const std::string before = serialize(project);
    const auto r = run(project, op);
    INFO(op);
    REQUIRE_FALSE(r.ok);
    CHECK_FALSE(r.applied);
    REQUIRE(r.outcomes.size() == 1);
    CHECK(r.outcomes[0].code == code);
    CHECK_FALSE(r.outcomes[0].message.empty());
    CHECK(serialize(project) == before);
    return r.outcomes[0];
}

json result_of(const project_ops::BatchReport& r) {
    REQUIRE(r.outcomes.size() == 1);
    return json::parse(r.outcomes[0].result_json);
}

std::vector<std::uint64_t> order_of(const document::Project& p) {
    std::vector<std::uint64_t> ids;
    for (const auto& e : p.embroidery_objects) {
        ids.push_back(e.id.value);
    }
    return ids;
}

} // namespace

TEST_CASE("param ops set_color with a free rgb color", "[project_ops][params]") {
    auto p = make_project();
    const auto r = run(p, R"({"op":"set_color","object":2,"rgb":"#ff8000"})");
    REQUIRE(r.ok);
    CHECK(p.findEmbroidery(ObjectId{2})->rgb == std::array<std::uint8_t, 3>{255, 128, 0});
    CHECK_FALSE(p.findEmbroidery(ObjectId{2})->thread.has_value());
    const auto j = result_of(r);
    CHECK(j["rgb"] == "#FF8000");
    CHECK(j["previous_rgb"] == "#0000FF");
    CHECK(j["thread"].is_null());
}

TEST_CASE("param ops set_color with a catalog thread", "[project_ops][params]") {
    const auto chart = thread_palette::generic_basic_chart();
    REQUIRE_FALSE(chart.threads.empty());
    const auto& thread = chart.threads.front();
    auto p = make_project();
    const std::string op = R"({"op":"set_color","object":4,"chart":")" + chart.chart_id +
                           R"(","code":")" + thread.key.code + R"("})";
    const auto r = run(p, op);
    REQUIRE(r.ok);
    const auto* e = p.findEmbroidery(ObjectId{4});
    CHECK(e->rgb == thread.rgb);
    REQUIRE(e->thread.has_value());
    CHECK(e->thread->code == thread.key.code);
    CHECK(result_of(r)["thread"]["code"] == thread.key.code);
}

TEST_CASE("param ops set_color refusals are named", "[project_ops][params]") {
    auto p = make_project();
    expect_refused(p, R"({"op":"set_color","object":2,"rgb":"red"})",
                   project_ops::code::kOutOfRange);
    expect_refused(p, R"({"op":"set_color","object":2,"rgb":"#12345"})",
                   project_ops::code::kOutOfRange);
    expect_refused(p, R"({"op":"set_color","object":2})", project_ops::code::kUsage);
    expect_refused(p,
                   R"({"op":"set_color","object":2,"rgb":"#000000","chart":"generic","code":"1"})",
                   project_ops::code::kUsage);
    expect_refused(p, R"({"op":"set_color","object":2,"chart":"generic"})",
                   project_ops::code::kUsage);
    const auto o = expect_refused(p, R"({"op":"set_color","object":2,"chart":"nope","code":"1"})",
                                  project_ops::code::kRefused);
    CHECK_THAT(o.hint, ContainsSubstring("generic"));
    expect_refused(p, R"({"op":"set_color","object":99,"rgb":"#000000"})",
                   project_ops::code::kUnknownObject);
    // Un identifiant de forme vectorielle n'est pas un objet de broderie.
    expect_refused(p, R"({"op":"set_color","object":1,"rgb":"#000000"})",
                   project_ops::code::kUnknownObject);
}

TEST_CASE("param ops rename visible locked join connector", "[project_ops][params]") {
    auto p = make_project();
    REQUIRE(run(p, R"({"op":"rename","object":2,"name":"Nuage"})").ok);
    CHECK(p.findEmbroidery(ObjectId{2})->name == "Nuage");
    expect_refused(p, R"({"op":"rename","object":2,"name":""})", project_ops::code::kRefused);

    const auto hidden = run(p, R"({"op":"set_visible","object":2,"visible":false})");
    REQUIRE(hidden.ok);
    CHECK_FALSE(p.findEmbroidery(ObjectId{2})->visible);
    CHECK(result_of(hidden)["previous_visible"] == true);

    REQUIRE(run(p, R"({"op":"set_locked","object":4,"locked":true})").ok);
    CHECK(p.findEmbroidery(ObjectId{4})->locked);

    REQUIRE(run(p, R"({"op":"set_join","object":2,"mode":"auto"})").ok);
    CHECK(p.findEmbroidery(ObjectId{2})->join == document::JoinMode::Auto);
    const auto bad_join = expect_refused(p, R"({"op":"set_join","object":2,"mode":"sometimes"})",
                                         project_ops::code::kOutOfRange);
    CHECK_THAT(bad_join.hint, ContainsSubstring("inherit"));

    REQUIRE(run(p, R"({"op":"set_connector","object":2,"mode":"never_trim"})").ok);
    CHECK(p.findEmbroidery(ObjectId{2})->connector == document::ConnectorMode::NeverTrim);
    expect_refused(p, R"({"op":"set_connector","object":2,"mode":"cut"})",
                   project_ops::code::kOutOfRange);
}

TEST_CASE("param ops set_stitch_type follows the desktop rules", "[project_ops][params]") {
    auto p = make_project();

    SECTION("tatami to running uses a triple pass") {
        const auto r = run(p, R"({"op":"set_stitch_type","object":2,"type":"running"})");
        REQUIRE(r.ok);
        const auto* running =
            std::get_if<document::RunningStitchParams>(&p.findEmbroidery(ObjectId{2})->params);
        REQUIRE(running != nullptr);
        CHECK(running->repeats == 3);
        CHECK(p.findEmbroidery(ObjectId{2})->intent ==
              document::EmbroideryIntent::ForcedUserChoice);
        const auto j = result_of(r);
        CHECK(j["from"] == "tatami");
        CHECK(j["to"] == "running");
        CHECK(j["changed"] == true);
    }
    SECTION("running to tatami uses default tatami parameters") {
        REQUIRE(run(p, R"({"op":"set_stitch_type","object":4,"type":"tatami"})").ok);
        const auto* tatami =
            std::get_if<document::TatamiParams>(&p.findEmbroidery(ObjectId{4})->params);
        REQUIRE(tatami != nullptr);
        CHECK(*tatami == document::TatamiParams{});
    }
    SECTION("directional keeps the tatami settings") {
        REQUIRE(run(p, R"({"op":"set_params","object":2,"params":{"row_spacing_mm":0.55}})").ok);
        REQUIRE(run(p, R"({"op":"set_stitch_type","object":2,"type":"directional"})").ok);
        const auto* directional =
            std::get_if<document::DirectionalFillParams>(&p.findEmbroidery(ObjectId{2})->params);
        REQUIRE(directional != nullptr);
        CHECK(directional->row_spacing == Micrometers{550});
    }
    SECTION("same type is a successful no-op") {
        const std::string before = serialize(p);
        const auto r = run(p, R"({"op":"set_stitch_type","object":2,"type":"tatami"})");
        REQUIRE(r.ok);
        CHECK(result_of(r)["changed"] == false);
        CHECK(serialize(p) == before);
    }
    SECTION("satin and auto_satin are refused with the auto_satin hint") {
        for (const char* type : {"satin", "auto_satin"}) {
            const auto o = expect_refused(
                p, std::string(R"({"op":"set_stitch_type","object":2,"type":")") + type + R"("})",
                project_ops::code::kRefused);
            CHECK_THAT(o.hint, ContainsSubstring("auto_satin"));
        }
    }
    SECTION("unknown type") {
        const auto o = expect_refused(p, R"({"op":"set_stitch_type","object":2,"type":"zigzag"})",
                                      project_ops::code::kOutOfRange);
        CHECK_THAT(o.hint, ContainsSubstring("directional"));
    }
    SECTION("directional without a source contour is refused") {
        p.vector_objects.front().paths.clear();
        expect_refused(p, R"({"op":"set_stitch_type","object":2,"type":"directional"})",
                       project_ops::code::kRefused);
    }
}

TEST_CASE("param ops set_params converts units exactly", "[project_ops][params]") {
    auto p = make_project();
    const auto r = run(p, R"({"op":"set_params","object":2,"params":{"row_spacing_mm":0.45,)"
                          R"("angle_deg":30,"underlay_edge":true,"stagger":3,)"
                          R"("underlay_mode":"auto","entry_point_mm":[1.5,-2]}})");
    REQUIRE(r.ok);
    const auto* t = std::get_if<document::TatamiParams>(&p.findEmbroidery(ObjectId{2})->params);
    REQUIRE(t != nullptr);
    CHECK(t->row_spacing == Micrometers{450});
    CHECK(t->angle.radians == 30.0 * std::numbers::pi / 180.0);
    CHECK(t->underlay_edge);
    CHECK(t->stagger == 3);
    CHECK(t->underlay_mode == document::UnderlayMode::Auto);
    REQUIRE(t->entry_point.has_value());
    CHECK(*t->entry_point == Vec2um{Micrometers{1500}, Micrometers{-2000}});
    // Les champs non cités sont intacts.
    CHECK(t->stitch_length == document::TatamiParams{}.stitch_length);
    CHECK(t->inset == document::TatamiParams{}.inset);

    const auto j = result_of(r);
    CHECK(j["type"] == "tatami");
    CHECK(j["changed"]["row_spacing_mm"]["from"] == 0.4);
    CHECK(j["changed"]["row_spacing_mm"]["to"] == 0.45);
    CHECK(j["changed"]["angle_deg"]["to"] == 30.0);
}

TEST_CASE("param ops set_params is one undo step per operation and can clear optionals",
          "[project_ops][params]") {
    auto p = make_project();
    REQUIRE(run(p, R"({"op":"set_params","object":2,"params":{"entry_point_mm":[1,1]}})").ok);
    REQUIRE(run(p, R"({"op":"set_params","object":2,"params":{"entry_point_mm":null}})").ok);
    const auto* t = std::get_if<document::TatamiParams>(&p.findEmbroidery(ObjectId{2})->params);
    REQUIRE(t != nullptr);
    CHECK_FALSE(t->entry_point.has_value());
}

TEST_CASE("param ops set_params refusals leave the project untouched", "[project_ops][params]") {
    auto p = make_project();

    SECTION("out of range names the range") {
        const auto o =
            expect_refused(p, R"({"op":"set_params","object":2,"params":{"row_spacing_mm":9}})",
                           project_ops::code::kOutOfRange);
        CHECK_THAT(o.message, ContainsSubstring("row_spacing_mm"));
        CHECK_THAT(o.message, ContainsSubstring("[0.1 ; 5]"));
    }
    SECTION("field of another stitch type lists the valid fields") {
        const auto o = expect_refused(p, R"({"op":"set_params","object":2,"params":{"repeats":2}})",
                                      project_ops::code::kWrongType);
        CHECK_THAT(o.message, ContainsSubstring("repeats"));
        CHECK_THAT(o.hint, ContainsSubstring("row_spacing_mm"));
        CHECK_THAT(o.hint, ContainsSubstring("running"));
    }
    SECTION("unknown field is never silently ignored") {
        const auto o =
            expect_refused(p, R"({"op":"set_params","object":2,"params":{"colour":"red"}})",
                           project_ops::code::kWrongType);
        CHECK_THAT(o.hint, ContainsSubstring("angle_deg"));
    }
    SECTION("one bad field cancels the whole operation") {
        expect_refused(
            p, R"({"op":"set_params","object":2,"params":{"row_spacing_mm":0.5,"stagger":99}})",
            project_ops::code::kOutOfRange);
        const auto* t = std::get_if<document::TatamiParams>(&p.findEmbroidery(ObjectId{2})->params);
        CHECK(t->row_spacing == document::TatamiParams{}.row_spacing);
    }
    SECTION("wrong value types") {
        expect_refused(p, R"({"op":"set_params","object":2,"params":{"row_spacing_mm":"0.4"}})",
                       project_ops::code::kUsage);
        expect_refused(p, R"({"op":"set_params","object":2,"params":{"stagger":2.5}})",
                       project_ops::code::kUsage);
        expect_refused(p, R"({"op":"set_params","object":2,"params":{"underlay_edge":1}})",
                       project_ops::code::kUsage);
        expect_refused(p, R"({"op":"set_params","object":2,"params":{"underlay_mode":"x"}})",
                       project_ops::code::kOutOfRange);
    }
    SECTION("empty params") {
        expect_refused(p, R"({"op":"set_params","object":2,"params":{}})",
                       project_ops::code::kUsage);
    }
}

TEST_CASE("param ops set_params on running and directional objects", "[project_ops][params]") {
    auto p = make_project();
    REQUIRE(
        run(p, R"({"op":"set_params","object":4,"params":{"repeats":2,"stitch_length_mm":2.5}})")
            .ok);
    const auto* run4 =
        std::get_if<document::RunningStitchParams>(&p.findEmbroidery(ObjectId{4})->params);
    REQUIRE(run4 != nullptr);
    CHECK(run4->repeats == 2);
    CHECK(run4->stitch_length == Micrometers{2500});
    expect_refused(p, R"({"op":"set_params","object":4,"params":{"angle_deg":10}})",
                   project_ops::code::kWrongType);

    REQUIRE(run(p, R"({"op":"set_stitch_type","object":6,"type":"directional"})").ok);
    REQUIRE(run(p, R"({"op":"set_params","object":6,"params":{"edge_weight":0.25,)"
                   R"("handmade":true,"handmade_intensity":40,"seed":7}})")
                .ok);
    const auto* dir =
        std::get_if<document::DirectionalFillParams>(&p.findEmbroidery(ObjectId{6})->params);
    REQUIRE(dir != nullptr);
    CHECK(dir->edge_weight == 0.25);
    CHECK(dir->handmade);
    CHECK(dir->handmade_intensity == 40);
    CHECK(dir->seed == 7U);
    expect_refused(p, R"({"op":"set_params","object":6,"params":{"edge_weight":1.5}})",
                   project_ops::code::kOutOfRange);
}

TEST_CASE("param ops set_params on satin types", "[project_ops][params]") {
    auto p = make_project();
    p.findEmbroidery(ObjectId{2})->params = document::AutoSatinParams{};
    p.findEmbroidery(ObjectId{4})->params = document::SatinParams{};

    REQUIRE(run(p, R"({"op":"set_params","object":2,"params":{"spacing_mm":0.35,)"
                   R"("cap_start":"rounded","lock_end":"triangle","compact_axis":true}})")
                .ok);
    const auto* a = std::get_if<document::AutoSatinParams>(&p.findEmbroidery(ObjectId{2})->params);
    REQUIRE(a != nullptr);
    CHECK(a->spacing == Micrometers{350});
    CHECK(a->cap_start == document::SatinCap::Rounded);
    CHECK(a->lock_end == document::SatinLock::Triangle);
    CHECK(a->compact_axis);
    // Fractionnement : la longueur de segment ne peut pas dépasser le seuil.
    expect_refused(p, R"({"op":"set_params","object":2,"params":{"split_length_mm":10}})",
                   project_ops::code::kOutOfRange);

    REQUIRE(
        run(p,
            R"({"op":"set_params","object":4,"params":{"density_mm":0.3,"underlay_zigzag":true}})")
            .ok);
    const auto* s = std::get_if<document::SatinParams>(&p.findEmbroidery(ObjectId{4})->params);
    REQUIRE(s != nullptr);
    CHECK(s->density == Micrometers{300});
    CHECK(s->underlay_zigzag);
    // La topologie et la bordure ne sont pas modifiables ici : refus nommé.
    expect_refused(p, R"({"op":"set_params","object":4,"params":{"topology":{}}})",
                   project_ops::code::kWrongType);
}

TEST_CASE("param ops set_order modes", "[project_ops][params]") {
    SECTION("to_back and to_front") {
        auto p = make_project();
        const auto r = run(p, R"({"op":"set_order","object":2,"to_back":true})");
        REQUIRE(r.ok);
        CHECK(order_of(p) == std::vector<std::uint64_t>{4, 6, 2});
        const auto j = result_of(r);
        CHECK(j["from_position"] == 0);
        CHECK(j["to_position"] == 2);
        CHECK(j["order_before"] == json::array({2, 4, 6}));
        CHECK(j["order_after"] == json::array({4, 6, 2}));
        REQUIRE(run(p, R"({"op":"set_order","object":6,"to_front":true})").ok);
        CHECK(order_of(p) == std::vector<std::uint64_t>{6, 4, 2});
    }
    SECTION("position is the final index") {
        auto p = make_project();
        REQUIRE(run(p, R"({"op":"set_order","object":6,"position":1})").ok);
        CHECK(order_of(p) == std::vector<std::uint64_t>{2, 6, 4});
    }
    SECTION("before and after") {
        auto p = make_project();
        REQUIRE(run(p, R"({"op":"set_order","object":6,"before":2})").ok);
        CHECK(order_of(p) == std::vector<std::uint64_t>{6, 2, 4});
        REQUIRE(run(p, R"({"op":"set_order","object":6,"after":4})").ok);
        CHECK(order_of(p) == std::vector<std::uint64_t>{2, 4, 6});
    }
    SECTION("already in place is a successful no-op") {
        auto p = make_project();
        const std::string before = serialize(p);
        const auto r = run(p, R"({"op":"set_order","object":2,"position":0})");
        REQUIRE(r.ok);
        CHECK(result_of(r)["changed"] == false);
        CHECK(serialize(p) == before);
    }
    SECTION("refusals") {
        auto p = make_project();
        expect_refused(p, R"({"op":"set_order","object":2})", project_ops::code::kUsage);
        expect_refused(p, R"({"op":"set_order","object":2,"to_back":true,"to_front":true})",
                       project_ops::code::kUsage);
        expect_refused(p, R"({"op":"set_order","object":2,"position":3})",
                       project_ops::code::kOutOfRange);
        expect_refused(p, R"({"op":"set_order","object":2,"position":-1})",
                       project_ops::code::kOutOfRange);
        expect_refused(p, R"({"op":"set_order","object":2,"before":2})",
                       project_ops::code::kRefused);
        expect_refused(p, R"({"op":"set_order","object":2,"after":99})",
                       project_ops::code::kUnknownObject);
        expect_refused(p, R"({"op":"set_order","object":2,"to_back":false})",
                       project_ops::code::kUsage);
    }
}

TEST_CASE("param ops optimize_order groups colors and respects locks", "[project_ops][params]") {
    // Couleurs A, B, A : by_color regroupe les deux A.
    document::Project p = make_project();
    p.findEmbroidery(ObjectId{6})->rgb = p.findEmbroidery(ObjectId{2})->rgb;
    const auto r = run(p, R"({"op":"optimize_order","strategy":"by_color"})");
    REQUIRE(r.ok);
    const auto order = order_of(p);
    CHECK(order == std::vector<std::uint64_t>{2, 6, 4});
    const auto j = result_of(r);
    CHECK(j["changed"] == true);
    CHECK(j["color_changes_before"] == 2);
    CHECK(j["color_changes_after"] == 1);
    CHECK(j["order_after"] == json::array({2, 6, 4}));

    SECTION("a locked object keeps its position") {
        auto q = make_project();
        q.findEmbroidery(ObjectId{6})->rgb = q.findEmbroidery(ObjectId{2})->rgb;
        REQUIRE(run(q, R"({"op":"set_locked","object":4,"locked":true})").ok);
        REQUIRE(run(q, R"({"op":"optimize_order","strategy":"by_color"})").ok);
        CHECK(order_of(q)[1] == 4);
    }
    SECTION("every strategy is accepted and deterministic") {
        for (const char* s :
             {"document", "by_color", "by_proximity", "color_then_proximity", "layered"}) {
            auto a = make_project();
            auto b = make_project();
            const std::string op =
                std::string(R"({"op":"optimize_order","strategy":")") + s + "\"}";
            REQUIRE(run(a, op).ok);
            REQUIRE(run(b, op).ok);
            CHECK(order_of(a) == order_of(b));
        }
    }
    SECTION("document strategy changes nothing") {
        auto q = make_project();
        const auto rd = run(q, R"({"op":"optimize_order","strategy":"document"})");
        REQUIRE(rd.ok);
        CHECK(result_of(rd)["changed"] == false);
        CHECK(order_of(q) == std::vector<std::uint64_t>{2, 4, 6});
    }
    SECTION("unknown strategy") {
        auto q = make_project();
        const auto o = expect_refused(q, R"({"op":"optimize_order","strategy":"random"})",
                                      project_ops::code::kOutOfRange);
        CHECK_THAT(o.hint, ContainsSubstring("by_color"));
    }
}

TEST_CASE("param ops remove_object keeps or removes the source shape", "[project_ops][params]") {
    SECTION("default keeps the vector shape like the desktop") {
        auto p = make_project();
        const auto r = run(p, R"({"op":"remove_object","object":4})");
        REQUIRE(r.ok);
        CHECK(order_of(p) == std::vector<std::uint64_t>{2, 6});
        CHECK(p.findObject(ObjectId{3}) != nullptr);
        CHECK(result_of(r)["source_removed"] == false);
    }
    SECTION("with_source removes an unused source") {
        auto p = make_project();
        const auto r = run(p, R"({"op":"remove_object","object":4,"with_source":true})");
        REQUIRE(r.ok);
        CHECK(p.findObject(ObjectId{3}) == nullptr);
        CHECK(p.findObject(ObjectId{1}) != nullptr);
        CHECK(result_of(r)["source_removed"] == true);
    }
    SECTION("a source still used by another object is kept") {
        auto p = make_project();
        p.findEmbroidery(ObjectId{6})->source_vector = ObjectId{1}; // 2 et 6 partagent la forme 1
        const auto r = run(p, R"({"op":"remove_object","object":2,"with_source":true})");
        REQUIRE(r.ok);
        CHECK(p.findObject(ObjectId{1}) != nullptr);
        const auto j = result_of(r);
        CHECK(j["source_removed"] == false);
        CHECK_THAT(j["source_kept_reason"].get<std::string>(), ContainsSubstring("utilis"));
    }
    SECTION("an invisible proxy source goes with its object") {
        auto p = make_project();
        p.findObject(ObjectId{3})->visible = false;
        REQUIRE(run(p, R"({"op":"remove_object","object":4})").ok);
        CHECK(p.findObject(ObjectId{3}) == nullptr);
    }
    SECTION("refusals") {
        auto p = make_project();
        expect_refused(p, R"({"op":"remove_object","object":99})",
                       project_ops::code::kUnknownObject);
        expect_refused(p, R"({"op":"remove_object","object":3})",
                       project_ops::code::kUnknownObject);
    }
}

TEST_CASE("param ops discard_overrides", "[project_ops][params]") {
    auto p = make_project();
    auto* e = p.findEmbroidery(ObjectId{2});
    e->overrides.push_back(document::StitchOverride{.base_index = 3, .trim_after = true});
    e->edited_fingerprint = 42;
    e->edited_point_count = 10;
    const auto r = run(p, R"({"op":"discard_overrides","object":2})");
    REQUIRE(r.ok);
    CHECK(p.findEmbroidery(ObjectId{2})->overrides.empty());
    CHECK(p.findEmbroidery(ObjectId{2})->edited_fingerprint == 0);
    CHECK(result_of(r)["discarded"] == 1);
    // Idempotent.
    const auto again = run(p, R"({"op":"discard_overrides","object":2})");
    REQUIRE(again.ok);
    CHECK(result_of(again)["discarded"] == 0);
    expect_refused(p, R"({"op":"discard_overrides","object":99})",
                   project_ops::code::kUnknownObject);
}

TEST_CASE("param ops set_canvas", "[project_ops][params]") {
    auto p = make_project();
    const auto r = run(p, R"({"op":"set_canvas","width_mm":160,"height_mm":260.5,"margin_mm":5})");
    REQUIRE(r.ok);
    CHECK(p.canvas.width == Micrometers{160'000});
    CHECK(p.canvas.height == Micrometers{260'500});
    CHECK(p.canvas.margin == Micrometers{5'000});
    const auto j = result_of(r);
    CHECK(j["before"]["width_mm"] == 100.0);
    CHECK(j["after"]["height_mm"] == 260.5);

    // Mise à jour partielle : le reste est conservé.
    REQUIRE(run(p, R"({"op":"set_canvas","margin_mm":0})").ok);
    CHECK(p.canvas.width == Micrometers{160'000});
    CHECK(p.canvas.margin == Micrometers{0});

    expect_refused(p, R"({"op":"set_canvas"})", project_ops::code::kUsage);
    expect_refused(p, R"({"op":"set_canvas","width_mm":0})", project_ops::code::kOutOfRange);
    expect_refused(p, R"({"op":"set_canvas","height_mm":-10})", project_ops::code::kOutOfRange);
    expect_refused(p, R"({"op":"set_canvas","width_mm":5000})", project_ops::code::kOutOfRange);
    expect_refused(p, R"({"op":"set_canvas","margin_mm":-1})", project_ops::code::kOutOfRange);
    expect_refused(p, R"({"op":"set_canvas","width_mm":10,"height_mm":10,"margin_mm":5})",
                   project_ops::code::kOutOfRange);
}

TEST_CASE("param ops set_finishing", "[project_ops][params]") {
    auto p = make_project();
    const auto r = run(p, R"({"op":"set_finishing","trim_threshold_mm":4.5,"lock":"triangle",)"
                          R"("lock_passes":3,"auto_join":true,"hidden_travel":true,)"
                          R"("hidden_travel_max_jump_mm":2.5,"split_long_stitches":true,)"
                          R"("max_stitch_length_mm":6})");
    REQUIRE(r.ok);
    const auto& f = p.finishing;
    CHECK(f.trim_threshold == Micrometers{4500});
    CHECK(f.lock_type == document::LockStitch::Triangle);
    CHECK(f.lock_passes == 3);
    CHECK(f.auto_join);
    CHECK(f.hidden_travel.enabled);
    CHECK(f.hidden_travel.max_hidden_jump == Micrometers{2500});
    CHECK(f.split_long_stitches);
    CHECK(f.max_stitch_length == Micrometers{6000});
    // Réglages non cités : inchangés.
    CHECK(f.min_stitch_length == document::SequenceFinishing{}.min_stitch_length);
    CHECK(result_of(r)["changed"]["lock"]["from"] == "backforth");
    CHECK(result_of(r)["changed"]["lock"]["to"] == "triangle");

    expect_refused(p, R"({"op":"set_finishing"})", project_ops::code::kUsage);
    expect_refused(p, R"({"op":"set_finishing","trim_threshold_mm":0.1})",
                   project_ops::code::kOutOfRange);
    expect_refused(p, R"({"op":"set_finishing","lock":"knot"})", project_ops::code::kOutOfRange);
    expect_refused(p, R"({"op":"set_finishing","lock_passes":9})", project_ops::code::kOutOfRange);
}
