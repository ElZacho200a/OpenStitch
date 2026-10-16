// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "openstitch/document/project.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/project_ops/auto_satin_service.hpp"

using namespace openstitch;

namespace {

// Ajoute un objet vectoriel rectangulaire / elliptique centré, de `w` x `h` mm.
ObjectId add_vector(document::Project& project, bool ellipse, double w_mm, double h_mm) {
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = ellipse ? "disque" : "bande";
    const Vec2um c1{to_micrometers(Millimeters{-w_mm / 2}), to_micrometers(Millimeters{-h_mm / 2})};
    const Vec2um c2{to_micrometers(Millimeters{w_mm / 2}), to_micrometers(Millimeters{h_mm / 2})};
    vec.paths.push_back(geometry::PathSet{
        ellipse ? geometry::ellipse_path(c1, c2) : geometry::rectangle_path(c1, c2), {}});
    const ObjectId id = vec.id;
    project.vector_objects.push_back(std::move(vec));
    return id;
}

} // namespace

TEST_CASE("auto_satin_service accepte une bande etroite", "[project_ops][auto_satin]") {
    document::Project project;
    const ObjectId vec = add_vector(project, false, 30.0, 2.0);
    const auto plan = project_ops::plan_auto_satin(project, vec);
    REQUIRE(plan.has_value());
    CHECK(plan->accepted);
    CHECK(plan->columns > 0);
    CHECK(plan->refusal.empty());
    CHECK_FALSE(plan->satinability_status.empty());
    CHECK(plan->coverage_measured);
    CHECK(plan->coverage_ratio > 0.5);
    CHECK(plan->params.spacing.value == 400);
    CHECK_FALSE(plan->restored_contour.has_value()); // pas de segmentation : contour inchange
}

TEST_CASE("auto_satin_service refuse un disque sans repli", "[project_ops][auto_satin]") {
    document::Project project;
    const ObjectId vec = add_vector(project, true, 10.0, 10.0);
    const auto plan = project_ops::plan_auto_satin(project, vec);
    REQUIRE(plan.has_value());
    CHECK_FALSE(plan->accepted);
    CHECK(plan->columns == 0);
    CHECK_FALSE(plan->refusal.empty());
    CHECK(plan->refusal.find("Aucune colonne satin") != std::string::npos);
    CHECK_FALSE(plan->messages.empty()); // le refus est nomme par le moteur
}

TEST_CASE("auto_satin_service axe compact accepte un disque", "[project_ops][auto_satin]") {
    document::Project project;
    const ObjectId vec = add_vector(project, true, 10.0, 10.0);
    project_ops::AutoSatinPlanRequest request;
    request.compact_axis = true;
    const auto plan = project_ops::plan_auto_satin(project, vec, request);
    REQUIRE(plan.has_value());
    CHECK(plan->accepted);
    CHECK(plan->columns > 0);
    CHECK(plan->params.compact_axis);
}

TEST_CASE("auto_satin_service propage espacement et guides", "[project_ops][auto_satin]") {
    document::Project project;
    const ObjectId vec = add_vector(project, false, 30.0, 3.0);
    project_ops::AutoSatinPlanRequest request;
    request.spacing = Micrometers{600};
    request.guides.push_back(document::AutoSatinGuide{Vec2um{}, Angle{0.1}, false});
    const auto plan = project_ops::plan_auto_satin(project, vec, request);
    REQUIRE(plan.has_value());
    REQUIRE(plan->accepted);
    CHECK(plan->params.spacing.value == 600);
    REQUIRE(plan->params.guides.size() == 1);
    CHECK(plan->params.guides[0].angle.radians == 0.1);
}

TEST_CASE("auto_satin_service objet inconnu est une erreur d'appel", "[project_ops][auto_satin]") {
    document::Project project;
    const auto plan = project_ops::plan_auto_satin(project, ObjectId{42});
    CHECK_FALSE(plan.has_value());
}

TEST_CASE("auto_satin_service sans contour est un refus nomme", "[project_ops][auto_satin]") {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    const ObjectId id = vec.id;
    project.vector_objects.push_back(std::move(vec));
    const auto plan = project_ops::plan_auto_satin(project, id);
    REQUIRE(plan.has_value());
    CHECK_FALSE(plan->accepted);
    CHECK_FALSE(plan->refusal.empty());
}

TEST_CASE("auto_satin_service region absente de la segmentation", "[project_ops][auto_satin]") {
    // source_region renseignee mais aucune segmentation : le contour courant sert tel quel.
    document::Project project;
    const ObjectId vec = add_vector(project, false, 30.0, 2.0);
    project.findObject(vec)->source_region = RegionId{1};
    const auto plan = project_ops::plan_auto_satin(project, vec);
    REQUIRE(plan.has_value());
    CHECK(plan->accepted);
    CHECK_FALSE(plan->restored_contour.has_value());
}

TEST_CASE("auto_satin_service est deterministe", "[project_ops][auto_satin]") {
    document::Project project;
    const ObjectId vec = add_vector(project, false, 40.0, 3.0);
    const auto a = project_ops::plan_auto_satin(project, vec);
    const auto b = project_ops::plan_auto_satin(project, vec);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->accepted == b->accepted);
    CHECK(a->columns == b->columns);
    CHECK(a->coverage_ratio == b->coverage_ratio);
    CHECK(a->satinability_status == b->satinability_status);
    CHECK(a->messages == b->messages);
    CHECK(a->params == b->params);
}

TEST_CASE("auto_satin_service : la methode choisie accepte un disque sans axe compact",
          "[project_ops][auto_satin]") {
    for (const auto strategy :
         {document::AutoSatinStrategy::ParallelSweep, document::AutoSatinStrategy::MedialAxis}) {
        INFO(document::auto_satin_strategy_id(strategy));
        document::Project project;
        const ObjectId vec = add_vector(project, true, 10.0, 10.0);
        project_ops::AutoSatinPlanRequest request;
        request.strategy = strategy;
        const auto plan = project_ops::plan_auto_satin(project, vec, request);
        REQUIRE(plan.has_value());
        CHECK(plan->accepted);
        CHECK(plan->columns > 0);
        CHECK(plan->params.strategy == strategy); // la methode est posee sur l'objet
        CHECK(plan->coverage_ratio > 0.9);
    }
}

TEST_CASE("auto_satin_service : l'angle de balayage est transmis a l'objet",
          "[project_ops][auto_satin]") {
    document::Project project;
    const ObjectId vec = add_vector(project, false, 30.0, 6.0);
    project_ops::AutoSatinPlanRequest request;
    request.strategy = document::AutoSatinStrategy::ParallelSweep;
    request.sweep_angle = Angle{0.5};
    const auto plan = project_ops::plan_auto_satin(project, vec, request);
    REQUIRE(plan.has_value());
    REQUIRE(plan->accepted);
    REQUIRE(plan->params.sweep_angle.has_value());
    CHECK(plan->params.sweep_angle->radians == 0.5);
}
