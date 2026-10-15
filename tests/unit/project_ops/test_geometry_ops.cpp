// SPDX-License-Identifier: Apache-2.0
// Les handlers sont testés DIRECTEMENT (OpContext construit ici) : aucune dépendance au
// dispatcher `apply_batch`.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <numbers>

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "../../../libs/project_ops/src/op_registry.hpp"
#include "openstitch/commands/transform_ops.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/primitives.hpp"

using namespace openstitch;
using nlohmann::json;
using project_ops::detail::OpContext;
using project_ops::detail::OpSpec;
using project_ops::detail::OpStatus;

namespace {

const std::vector<OpSpec>& registry() {
    static const std::vector<OpSpec> specs = [] {
        std::vector<OpSpec> v;
        project_ops::detail::register_geometry_ops(v);
        return v;
    }();
    return specs;
}

struct Env {
    document::Project project;
    commands::UndoStack stack;

    Env() { stack.setMergeWindow(std::chrono::milliseconds{0}); }

    OpStatus run(const std::string& op, const json& args) {
        const OpSpec* found = nullptr;
        for (const auto& spec : registry()) {
            if (spec.name == op) {
                found = &spec;
            }
        }
        INFO("operation : " << op);
        REQUIRE(found != nullptr);
        OpContext ctx{project, stack, args};
        return found->apply(ctx);
    }
};

// Empreinte exacte de l'état géométrique et des types de points du document.
std::string signature(const document::Project& project) {
    std::ostringstream out;
    for (const auto& v : project.vector_objects) {
        out << "V" << v.id.value << ":";
        for (const auto& set : v.paths) {
            out << "[";
            for (const auto& n : set.outer.nodes) {
                out << n.pos.x.value << "," << n.pos.y.value << ";";
            }
            for (const auto& h : set.holes) {
                out << "h";
                for (const auto& n : h.nodes) {
                    out << n.pos.x.value << "," << n.pos.y.value << ";";
                }
            }
            out << "]";
        }
    }
    for (const auto& e : project.embroidery_objects) {
        out << "E" << e.id.value << ":" << e.source_vector.value << ":" << e.params.index() << ":"
            << e.name << ":" << int(e.rgb[0]) << int(e.rgb[1]) << int(e.rgb[2]) << ";";
    }
    return out.str();
}

ObjectId add_rect(Env& env, double cx, double cy, double w, double h,
                  const std::string& type = "tatami") {
    const auto st = env.run("add_shape", json{{"shape", "rect"},
                                              {"width_mm", w},
                                              {"height_mm", h},
                                              {"center_mm", json::array({cx, cy})},
                                              {"stitch_type", type}});
    REQUIRE(st.ok);
    return ObjectId{st.result["id"].get<std::uint64_t>()};
}

commands::BoundsUm bounds_of(const Env& env, ObjectId emb) {
    const auto* e = env.project.findEmbroidery(emb);
    REQUIRE(e != nullptr);
    const auto b = commands::objects_bounds(env.project, {e->source_vector});
    REQUIRE(b.has_value());
    return *b;
}

double mm(Micrometers v) {
    return to_millimeters(v).value;
}

} // namespace

TEST_CASE("geometry_ops catalogue complet et exemples valides", "[project_ops][geometry]") {
    const std::vector<std::string> expected{
        "add_shape",   "duplicate_object", "translate",    "rotate",
        "scale",       "mirror",           "merge",        "split",
        "break_apart", "remove_holes",     "set_geometry", "auto_satin"};
    REQUIRE(registry().size() == expected.size());
    for (const auto& name : expected) {
        const OpSpec* found = nullptr;
        for (const auto& s : registry()) {
            if (s.name == name) {
                found = &s;
            }
        }
        REQUIRE(found != nullptr);
        CHECK_FALSE(found->summary.empty());
        const json example = json::parse(found->example); // doit etre du JSON valide
        CHECK(example.at("op") == name);
        for (const auto& arg : found->args) {
            CHECK_FALSE(arg.description.empty());
            CHECK_FALSE(arg.type.empty());
            if (arg.required) {
                CHECK(example.contains(arg.name));
            }
        }
        CHECK(static_cast<bool>(found->apply));
    }
}

TEST_CASE("add_shape cree rectangle ellipse et polygone", "[project_ops][geometry]") {
    Env env;
    const auto r = env.run("add_shape", json{{"shape", "rect"},
                                             {"width_mm", 30.0},
                                             {"height_mm", 10.0},
                                             {"center_mm", json::array({5.0, -2.0})},
                                             {"rgb", "#FF0000"},
                                             {"name", "Bande"}});
    REQUIRE(r.ok);
    CHECK(env.project.vector_objects.size() == 1);
    CHECK(env.project.embroidery_objects.size() == 1);
    const auto& emb = env.project.embroidery_objects[0];
    CHECK(emb.id.value == r.result["id"].get<std::uint64_t>());
    CHECK(emb.source_vector.value == r.result["vector_id"].get<std::uint64_t>());
    CHECK(emb.is_tatami());
    CHECK(emb.rgb[0] == 255);
    CHECK(emb.name == "Bande");
    const auto b = bounds_of(env, emb.id);
    CHECK(mm(b.min.x) == -10.0);
    CHECK(mm(b.max.x) == 20.0);
    CHECK(mm(b.min.y) == -7.0);
    CHECK(mm(b.max.y) == 3.0);

    const auto e = env.run("add_shape", json{{"shape", "ellipse"},
                                             {"width_mm", 8.0},
                                             {"height_mm", 8.0},
                                             {"stitch_type", "running"}});
    REQUIRE(e.ok);
    CHECK(
        env.project.findEmbroidery(ObjectId{e.result["id"].get<std::uint64_t>()})->params.index() ==
        0);

    const auto p = env.run(
        "add_shape", json{{"points_mm", json::array({json::array({0, 0}), json::array({10, 0}),
                                                     json::array({5, 8})})},
                          {"stitch_type", "directional"}});
    REQUIRE(p.ok);
    CHECK(env.project.findEmbroidery(ObjectId{p.result["id"].get<std::uint64_t>()})
              ->is_directional());
    CHECK(env.project.embroidery_objects.size() == 3);
    // Ids strictement croissants et distincts.
    CHECK(r.result["id"].get<std::uint64_t>() < e.result["id"].get<std::uint64_t>());
}

TEST_CASE("add_shape annulation exacte", "[project_ops][geometry]") {
    Env env;
    add_rect(env, 0, 0, 10, 10);
    const std::string before = signature(env.project);
    add_rect(env, 20, 0, 10, 10);
    CHECK(env.project.embroidery_objects.size() == 2);
    REQUIRE(env.stack.undo(env.project));
    CHECK(signature(env.project) == before);
}

TEST_CASE("add_shape refus nommes", "[project_ops][geometry]") {
    Env env;
    const auto bowtie =
        env.run("add_shape",
                json{{"points_mm", json::array({json::array({0, 0}), json::array({10, 10}),
                                                json::array({10, 0}), json::array({0, 10})})}});
    CHECK_FALSE(bowtie.ok);
    CHECK(bowtie.error.code == "E_REFUSED");

    const auto two =
        env.run("add_shape",
                json{{"points_mm", json::array({json::array({0, 0}), json::array({10, 10})})}});
    CHECK(two.error.code == "E_USAGE");

    const auto flat = env.run(
        "add_shape", json{{"points_mm", json::array({json::array({0, 0}), json::array({10, 0}),
                                                     json::array({20, 0})})}});
    CHECK(flat.error.code == "E_REFUSED");

    CHECK(env.run("add_shape", json{{"shape", "rect"}, {"width_mm", 10.0}}).error.code ==
          "E_USAGE");
    CHECK(env.run("add_shape", json{{"shape", "rect"}, {"width_mm", 0.0}, {"height_mm", 5.0}})
              .error.code == "E_OUT_OF_RANGE");
    CHECK(env.run("add_shape", json{{"shape", "star"}, {"width_mm", 5.0}, {"height_mm", 5.0}})
              .error.code == "E_OUT_OF_RANGE");
    CHECK(env.run("add_shape", json{{"shape", "rect"},
                                    {"width_mm", 5.0},
                                    {"height_mm", 5.0},
                                    {"stitch_type", "satin"}})
              .error.code == "E_OUT_OF_RANGE");
    CHECK(env.run("add_shape",
                  json{{"shape", "rect"}, {"width_mm", 5.0}, {"height_mm", 5.0}, {"rgb", "rouge"}})
              .error.code == "E_USAGE");
    CHECK(env.run("add_shape", json{{"width_mm", 5.0}}).error.code == "E_USAGE");
    CHECK(env.run("add_shape", json{{"shape", "rect"},
                                    {"width_mm", 5.0},
                                    {"height_mm", 5.0},
                                    {"points_mm", json::array()}})
              .error.code == "E_USAGE");
    // Aucun refus n'a laisse de trace dans le document ni dans la pile.
    CHECK(env.project.vector_objects.empty());
    CHECK_FALSE(env.stack.canUndo());
}

TEST_CASE("translate rotate scale mirror modifient la boite englobante",
          "[project_ops][geometry]") {
    Env env;
    const ObjectId obj = add_rect(env, 0, 0, 20, 10);
    const std::string original = signature(env.project);

    REQUIRE(env.run("translate", json{{"object", obj.value}, {"dx_mm", 5.0}, {"dy_mm", -2.5}}).ok);
    auto b = bounds_of(env, obj);
    CHECK(mm(b.min.x) == -5.0);
    CHECK(mm(b.max.x) == 15.0);
    CHECK(mm(b.min.y) == -7.5);
    REQUIRE(env.stack.undo(env.project));
    CHECK(signature(env.project) == original);

    const auto rot = env.run("rotate", json{{"object", obj.value}, {"angle_deg", 90.0}});
    REQUIRE(rot.ok);
    b = bounds_of(env, obj);
    CHECK(std::abs(mm(b.max.x) - mm(b.min.x) - 10.0) < 0.01);
    CHECK(std::abs(mm(b.max.y) - mm(b.min.y) - 20.0) < 0.01);
    CHECK(rot.result.contains("bounds_mm"));
    REQUIRE(env.stack.undo(env.project));
    CHECK(signature(env.project) == original);

    REQUIRE(env.run("scale", json{{"object", obj.value}, {"factor", 2.0}}).ok);
    b = bounds_of(env, obj);
    CHECK(mm(b.max.x) - mm(b.min.x) == 40.0);
    CHECK(mm(b.max.y) - mm(b.min.y) == 20.0);
    REQUIRE(env.stack.undo(env.project));
    CHECK(signature(env.project) == original);

    // Miroir autour d'un axe decale : la forme change de cote.
    REQUIRE(env.run("mirror", json{{"object", obj.value},
                                   {"axis", "horizontal"},
                                   {"center_mm", json::array({20.0, 0.0})}})
                .ok);
    b = bounds_of(env, obj);
    CHECK(mm(b.min.x) == 30.0);
    CHECK(mm(b.max.x) == 50.0);
    REQUIRE(env.stack.undo(env.project));
    CHECK(signature(env.project) == original);
}

TEST_CASE("transformations refus nommes", "[project_ops][geometry]") {
    Env env;
    const ObjectId obj = add_rect(env, 0, 0, 20, 10);
    CHECK(
        env.run("translate", json{{"object", 9999u}, {"dx_mm", 1.0}, {"dy_mm", 1.0}}).error.code ==
        "E_UNKNOWN_OBJECT");
    CHECK(env.run("translate", json{{"object", obj.value}, {"dx_mm", 1.0}}).error.code ==
          "E_USAGE");
    CHECK(env.run("translate", json{{"dx_mm", 1.0}, {"dy_mm", 1.0}}).error.code == "E_USAGE");
    CHECK(env.run("translate", json{{"object", obj.value}, {"dx_mm", 1.0e6}, {"dy_mm", 0.0}})
              .error.code == "E_OUT_OF_RANGE");
    CHECK(env.run("scale", json{{"object", obj.value}, {"factor", 0.0}}).error.code ==
          "E_OUT_OF_RANGE");
    CHECK(env.run("scale", json{{"object", obj.value}, {"factor", 21.0}}).error.code ==
          "E_OUT_OF_RANGE");
    CHECK(env.run("scale", json{{"object", obj.value}, {"factor", 0.01}}).error.code ==
          "E_OUT_OF_RANGE");
    CHECK(env.run("scale", json{{"object", obj.value}, {"factor", "deux"}}).error.code ==
          "E_USAGE");
    CHECK(env.run("rotate", json{{"object", obj.value}, {"angle_deg", 1000.0}}).error.code ==
          "E_OUT_OF_RANGE");
    CHECK(env.run("mirror", json{{"object", obj.value}, {"axis", "diagonal"}}).error.code ==
          "E_OUT_OF_RANGE");
    CHECK(env.run("mirror", json{{"object", obj.value}}).error.code == "E_USAGE");
    CHECK(env.run("rotate", json{{"object", obj.value}, {"angle_deg", 10.0}, {"center_mm", 5}})
              .error.code == "E_USAGE");
    CHECK(env.stack.undoNames().size() == 1);
}

TEST_CASE("duplicate_object copie avec decalage", "[project_ops][geometry]") {
    Env env;
    const ObjectId obj = add_rect(env, 0, 0, 10, 10);
    const std::string original = signature(env.project);
    const auto r = env.run("duplicate_object",
                           json{{"object", obj.value}, {"offset_mm", json::array({15.0, 0.0})}});
    REQUIRE(r.ok);
    CHECK(env.project.vector_objects.size() == 2);
    CHECK(env.project.embroidery_objects.size() == 2);
    const ObjectId copy{r.result["id"].get<std::uint64_t>()};
    CHECK(copy != obj);
    const auto b = bounds_of(env, copy);
    CHECK(mm(b.min.x) == 10.0);
    CHECK(mm(b.max.x) == 20.0);
    // L'original n'a pas bouge.
    CHECK(mm(bounds_of(env, obj).min.x) == -5.0);
    CHECK(env.project.findEmbroidery(copy)->is_tatami());
    REQUIRE(env.stack.undo(env.project));
    CHECK(signature(env.project) == original);

    CHECK(env.run("duplicate_object", json{{"object", obj.value}}).error.code == "E_USAGE");
    CHECK(env.run("duplicate_object", json{{"object", 777u}, {"offset_mm", json::array({1, 1})}})
              .error.code == "E_UNKNOWN_OBJECT");
}

TEST_CASE("merge union soustraction et refus", "[project_ops][geometry]") {
    Env env;
    const ObjectId a = add_rect(env, 0, 0, 10, 10);
    const ObjectId b = add_rect(env, 5, 0, 10, 10);
    const ObjectId far_away = add_rect(env, 100, 0, 5, 5);
    const std::string original = signature(env.project);

    const auto u =
        env.run("merge", json{{"objects", json::array({a.value, b.value})}, {"mode", "union"}});
    REQUIRE(u.ok);
    CHECK(env.project.vector_objects.size() == 2);
    CHECK(u.result["removed"].size() == 1);
    CHECK(u.result["id"].get<std::uint64_t>() == b.value); // le dernier est le principal
    const auto merged = bounds_of(env, ObjectId{u.result["id"].get<std::uint64_t>()});
    CHECK(mm(merged.min.x) == -5.0);
    CHECK(mm(merged.max.x) == 10.0);
    REQUIRE(env.stack.undo(env.project));
    CHECK(signature(env.project) == original);

    const auto s =
        env.run("merge", json{{"objects", json::array({a.value, b.value})}, {"mode", "subtract"}});
    REQUIRE(s.ok);
    CHECK(env.project.vector_objects.size() == 2);
    REQUIRE(env.stack.undo(env.project));
    CHECK(signature(env.project) == original);

    // Intersection vide : refus du moteur, document intact.
    const auto empty = env.run(
        "merge", json{{"objects", json::array({a.value, far_away.value})}, {"mode", "intersect"}});
    CHECK_FALSE(empty.ok);
    CHECK(empty.error.code == "E_REFUSED");
    CHECK(signature(env.project) == original);

    CHECK(env.run("merge", json{{"objects", json::array({a.value})}}).error.code == "E_USAGE");
    CHECK(env.run("merge", json{{"objects", json::array({a.value, a.value})}}).error.code ==
          "E_REFUSED");
    CHECK(env.run("merge", json{{"objects", json::array({a.value, 4242u})}}).error.code ==
          "E_UNKNOWN_OBJECT");
    CHECK(env.run("merge", json{{"objects", json::array({a.value, b.value})}, {"mode", "xor"}})
              .error.code == "E_OUT_OF_RANGE");
}

TEST_CASE("split coupe une forme et annule exactement", "[project_ops][geometry]") {
    Env env;
    const ObjectId obj = add_rect(env, 0, 0, 20, 10);
    const std::string original = signature(env.project);
    const auto r = env.run(
        "split",
        json{{"object", obj.value},
             {"line_mm", json::array({json::array({0.0, -20.0}), json::array({0.0, 20.0})})}});
    REQUIRE(r.ok);
    CHECK(env.project.embroidery_objects.size() == 2);
    CHECK(env.project.vector_objects.size() == 2);
    REQUIRE(r.result["created"].size() == 1);
    CHECK(r.result["id"] == r.result["created"][0]);
    REQUIRE(env.stack.undo(env.project));
    CHECK(signature(env.project) == original);

    // Droite qui ne traverse pas la forme.
    const auto miss = env.run(
        "split",
        json{{"object", obj.value},
             {"line_mm", json::array({json::array({50.0, -20.0}), json::array({50.0, 20.0})})}});
    CHECK(miss.error.code == "E_REFUSED");
    CHECK(env.run("split", json{{"object", obj.value},
                                {"line_mm",
                                 json::array({json::array({1.0, 1.0}), json::array({1.0, 1.0})})}})
              .error.code == "E_REFUSED");
    CHECK(env.run("split",
                  json{{"object", obj.value}, {"line_mm", json::array({json::array({1, 1})})}})
              .error.code == "E_USAGE");
    CHECK(signature(env.project) == original);
}

TEST_CASE("break_apart et remove_holes", "[project_ops][geometry]") {
    Env env;
    const ObjectId obj = add_rect(env, 0, 0, 20, 20);
    const ObjectId vec = env.project.findEmbroidery(obj)->source_vector;
    // Geometrie construite a la main : deux morceaux, le premier avec un trou de 4 mm2.
    auto& v = *env.project.findObject(vec);
    v.paths[0].holes.push_back(
        geometry::rectangle_path(Vec2um{Micrometers{-1000}, Micrometers{-1000}},
                                 Vec2um{Micrometers{1000}, Micrometers{1000}}));
    v.paths.push_back(
        geometry::PathSet{geometry::rectangle_path(Vec2um{Micrometers{30000}, Micrometers{0}},
                                                   Vec2um{Micrometers{40000}, Micrometers{10000}}),
                          {}});
    const std::string original = signature(env.project);

    // Seuil plus petit que le trou : rien a retirer -> refus.
    CHECK(env.run("remove_holes", json{{"object", obj.value}, {"max_area_mm2", 1.0}}).error.code ==
          "E_REFUSED");
    CHECK(signature(env.project) == original);

    const auto rh = env.run("remove_holes", json{{"object", obj.value}, {"max_area_mm2", 5.0}});
    REQUIRE(rh.ok);
    CHECK(rh.result["holes_removed"] == 1);
    CHECK(env.project.findObject(vec)->paths[0].holes.empty());
    REQUIRE(env.stack.undo(env.project));
    CHECK(signature(env.project) == original);

    const auto ba = env.run("break_apart", json{{"object", obj.value}});
    REQUIRE(ba.ok);
    CHECK(env.project.vector_objects.size() == 2);
    CHECK(env.project.embroidery_objects.size() == 2);
    CHECK(ba.result["created"].size() == 1);
    REQUIRE(env.stack.undo(env.project));
    CHECK(signature(env.project) == original);

    CHECK(env.run("remove_holes", json{{"object", obj.value}, {"max_area_mm2", -3.0}}).error.code ==
          "E_OUT_OF_RANGE");

    // Un objet d'un seul morceau et sans trou : refus nommes.
    const ObjectId plain = add_rect(env, 100, 0, 5, 5);
    CHECK(env.run("break_apart", json{{"object", plain.value}}).error.code == "E_REFUSED");
    CHECK(env.run("remove_holes", json{{"object", plain.value}}).error.code == "E_REFUSED");
}

TEST_CASE("set_geometry remplace le contour et refuse les polygones invalides",
          "[project_ops][geometry]") {
    Env env;
    const ObjectId obj = add_rect(env, 0, 0, 20, 10);
    const std::string original = signature(env.project);
    const auto r =
        env.run("set_geometry",
                json{{"object", obj.value},
                     {"points_mm", json::array({json::array({-5, -5}), json::array({5, -5}),
                                                json::array({5, 5}), json::array({-5, 5})})}});
    REQUIRE(r.ok);
    const auto b = bounds_of(env, obj);
    CHECK(mm(b.min.x) == -5.0);
    CHECK(mm(b.max.x) == 5.0);
    REQUIRE(env.stack.undo(env.project));
    CHECK(signature(env.project) == original);

    const auto bowtie =
        env.run("set_geometry",
                json{{"object", obj.value},
                     {"points_mm", json::array({json::array({0, 0}), json::array({10, 10}),
                                                json::array({10, 0}), json::array({0, 10})})}});
    CHECK(bowtie.error.code == "E_REFUSED");
    CHECK(env.run("set_geometry",
                  json{{"object", obj.value},
                       {"points_mm", json::array({json::array({0, 0}), json::array({1, 1})})}})
              .error.code == "E_USAGE");
    CHECK(env.run("set_geometry", json{{"object", obj.value}}).error.code == "E_USAGE");
    CHECK(signature(env.project) == original);
}

TEST_CASE("auto_satin convertit une bande et annule exactement",
          "[project_ops][geometry][auto_satin]") {
    Env env;
    const ObjectId obj = add_rect(env, 0, 0, 30, 2);
    const std::string original = signature(env.project);
    const auto r = env.run("auto_satin", json{{"object", obj.value}, {"spacing_mm", 0.5}});
    REQUIRE(r.ok);
    CHECK(r.result["columns"].get<std::size_t>() > 0);
    CHECK(r.result["id"].get<std::uint64_t>() == obj.value);
    const auto* emb = env.project.findEmbroidery(obj);
    REQUIRE(emb != nullptr);
    REQUIRE(emb->is_auto_satin());
    const auto& params = std::get<document::AutoSatinParams>(emb->params);
    CHECK(params.spacing.value == 500);
    CHECK(emb->intent == document::EmbroideryIntent::ForcedUserChoice);
    REQUIRE(env.stack.undo(env.project));
    CHECK(signature(env.project) == original);
    CHECK(env.project.findEmbroidery(obj)->is_tatami());
}

TEST_CASE("auto_satin refuse un disque avec le refus du moteur",
          "[project_ops][geometry][auto_satin]") {
    Env env;
    const auto shape =
        env.run("add_shape", json{{"shape", "ellipse"}, {"width_mm", 10.0}, {"height_mm", 10.0}});
    REQUIRE(shape.ok);
    const std::uint64_t id = shape.result["id"].get<std::uint64_t>();
    const std::string original = signature(env.project);
    const auto r = env.run("auto_satin", json{{"object", id}});
    CHECK_FALSE(r.ok);
    CHECK(r.error.code == "E_REFUSED");
    CHECK(r.error.message.find("Aucune colonne satin") != std::string::npos);
    CHECK(signature(env.project) == original); // jamais de repli silencieux
    CHECK(env.project.findEmbroidery(ObjectId{id})->is_tatami());

    const auto compact = env.run("auto_satin", json{{"object", id}, {"compact_axis", true}});
    REQUIRE(compact.ok);
    CHECK(env.project.findEmbroidery(ObjectId{id})->is_auto_satin());
}

TEST_CASE("auto_satin valide ses arguments", "[project_ops][geometry][auto_satin]") {
    Env env;
    const ObjectId obj = add_rect(env, 0, 0, 30, 2);
    CHECK(env.run("auto_satin", json{{"object", obj.value}, {"spacing_mm", 5.0}}).error.code ==
          "E_OUT_OF_RANGE");
    CHECK(env.run("auto_satin", json{{"object", obj.value}, {"spacing_mm", 0.01}}).error.code ==
          "E_OUT_OF_RANGE");
    CHECK(env.run("auto_satin", json{{"object", obj.value}, {"compact_axis", "oui"}}).error.code ==
          "E_USAGE");
    CHECK(env.run("auto_satin", json{{"object", obj.value}, {"guides", 3}}).error.code ==
          "E_USAGE");
    CHECK(env.run("auto_satin",
                  json{{"object", obj.value},
                       {"guides", json::array({json{{"anchor_mm", json::array({0, 0})}}})}})
              .error.code == "E_USAGE");
    CHECK(env.run("auto_satin", json{{"object", 31415u}}).error.code == "E_UNKNOWN_OBJECT");
    CHECK(env.stack.undoNames().size() == 1);

    const auto guided =
        env.run("auto_satin", json{{"object", obj.value},
                                   {"guides", json::array({json{{"anchor_mm", json::array({0, 0})},
                                                                {"angle_deg", 10.0},
                                                                {"absolute", true}}})}});
    REQUIRE(guided.ok);
    const auto& params =
        std::get<document::AutoSatinParams>(env.project.findEmbroidery(obj)->params);
    REQUIRE(params.guides.size() == 1);
    CHECK(params.guides[0].absolute);
}

TEST_CASE("geometry_ops sont deterministes", "[project_ops][geometry]") {
    const auto scenario = [] {
        Env env;
        const ObjectId a = add_rect(env, 0, 0, 30, 2);
        const ObjectId b = add_rect(env, 40, 0, 12, 12);
        REQUIRE(env.run("auto_satin", json{{"object", a.value}}).ok);
        REQUIRE(env.run("rotate", json{{"object", b.value}, {"angle_deg", 33.0}}).ok);
        REQUIRE(env.run("scale", json{{"object", b.value}, {"factor", 1.7}}).ok);
        REQUIRE(env.run("duplicate_object",
                        json{{"object", b.value}, {"offset_mm", json::array({0.0, 20.0})}})
                    .ok);
        return env.project;
    };
    const auto p1 = scenario();
    const auto p2 = scenario();
    CHECK(signature(p1) == signature(p2));
    const auto* s1 = p1.findEmbroidery(ObjectId{2});
    const auto* s2 = p2.findEmbroidery(ObjectId{2});
    REQUIRE(s1 != nullptr);
    REQUIRE(s2 != nullptr);
    CHECK(s1->params == s2->params);
}

TEST_CASE("auto_satin accepte une methode et la pose sur l'objet",
          "[project_ops][geometry][auto_satin]") {
    Env env;
    const auto shape =
        env.run("add_shape", json{{"shape", "ellipse"}, {"width_mm", 10.0}, {"height_mm", 10.0}});
    REQUIRE(shape.ok);
    const std::uint64_t id = shape.result["id"].get<std::uint64_t>();
    const std::string original = signature(env.project);

    // Methode inconnue ou mal typee : erreur d'usage, document intact.
    CHECK(env.run("auto_satin", json{{"object", id}, {"strategy", "voronoi_v9"}}).error.code ==
          "E_USAGE");
    CHECK(env.run("auto_satin", json{{"object", id}, {"strategy", 3}}).error.code == "E_USAGE");
    CHECK(env.run("auto_satin", json{{"object", id}, {"sweep_angle_deg", 900.0}}).error.code ==
          "E_OUT_OF_RANGE");
    CHECK(signature(env.project) == original);

    const auto r =
        env.run("auto_satin",
                json{{"object", id}, {"strategy", "parallel_sweep"}, {"sweep_angle_deg", 30.0}});
    REQUIRE(r.ok);
    const auto& params =
        std::get<document::AutoSatinParams>(env.project.findEmbroidery(ObjectId{id})->params);
    CHECK(params.strategy == document::AutoSatinStrategy::ParallelSweep);
    REQUIRE(params.sweep_angle.has_value());
    CHECK(params.sweep_angle->radians == std::numbers::pi / 6.0);
    REQUIRE(env.stack.undo(env.project));
    CHECK(signature(env.project) == original);

    // Le meme champ se regle ensuite par set_params, et se relit par inspect.
    const auto again = env.run("auto_satin", json{{"object", id}, {"strategy", "medial_axis"}});
    REQUIRE(again.ok);
    CHECK(std::get<document::AutoSatinParams>(env.project.findEmbroidery(ObjectId{id})->params)
              .strategy == document::AutoSatinStrategy::MedialAxis);
}
