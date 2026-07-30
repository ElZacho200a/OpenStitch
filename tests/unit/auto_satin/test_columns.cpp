// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/auto_satin/shapes.hpp"

using namespace openstitch;
using namespace openstitch::auto_satin;

namespace {

SatinColumnsResult columns_of(const std::string& shape) {
    const auto region = make_shape(shape);
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};  // 0,1 mm : rapide
    return build_satin_columns(*region, params);
}

// Point dans polygone (even-odd), coordonnées µm (Y vers le haut).
bool point_in_poly(const geometry::Path& poly, Vec2um p) {
    bool inside = false;
    const auto& n = poly.nodes;
    const std::size_t m = n.size();
    for (std::size_t i = 0, j = m - 1; i < m; j = i++) {
        const auto ax = n[i].pos.x.value, ay = n[i].pos.y.value;
        const auto bx = n[j].pos.x.value, by = n[j].pos.y.value;
        if (((ay > p.y.value) != (by > p.y.value)) &&
            (static_cast<double>(p.x.value) <
             static_cast<double>(bx - ax) * static_cast<double>(p.y.value - ay) /
                     static_cast<double>(by - ay) +
                 static_cast<double>(ax))) {
            inside = !inside;
        }
    }
    return inside;
}

bool in_region(const geometry::PathSet& region, Vec2um p) {
    if (!point_in_poly(region.outer, p)) {
        return false;
    }
    for (const auto& h : region.holes) {
        if (point_in_poly(h, p)) {
            return false;
        }
    }
    return true;
}

Vec2um mid(Vec2um a, Vec2um b) {
    return {Micrometers{(a.x.value + b.x.value) / 2}, Micrometers{(a.y.value + b.y.value) / 2}};
}

}  // namespace

// --- Formes simples : colonnes cohérentes ------------------------------------

TEST_CASE("colonnes : formes simples produisent au moins une colonne") {
    for (const char* s : {"rectangle", "capsule", "ribbon", "s"}) {
        const auto r = columns_of(s);
        INFO("forme = " << s << " statut = " << to_string(r.status)
                        << " refus = " << r.refusal);
        CHECK(r.refusal.empty());
        REQUIRE(r.columns.size() >= 1);
        const auto& c = r.columns.front();
        CHECK(c.rail_a.nodes.size() >= 2);
        CHECK(c.rail_b.nodes.size() == c.rail_a.nodes.size());
        CHECK(c.rungs.size() >= 2);
        CHECK(c.mean_width_um > 0.0);
    }
}

TEST_CASE("colonnes : milieu des barreaux interieur a la region") {
    const auto region = make_shape("capsule");
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    const auto r = build_satin_columns(*region, params);
    REQUIRE(r.columns.size() >= 1);
    int outside = 0;
    for (const auto& col : r.columns) {
        for (const auto& rung : col.rungs) {
            if (!in_region(*region, mid(rung.a, rung.b))) {
                ++outside;
            }
        }
    }
    CHECK(outside == 0);
}

TEST_CASE("colonnes : rails et barreaux finis, non degeneres") {
    const auto r = columns_of("ribbon");
    REQUIRE(r.columns.size() >= 1);
    for (const auto& col : r.columns) {
        for (const auto& n : col.rail_a.nodes) {
            CHECK(std::isfinite(static_cast<double>(n.pos.x.value)));
            CHECK(std::isfinite(static_cast<double>(n.pos.y.value)));
        }
        for (const auto& rung : col.rungs) {
            CHECK(length_um(rung.a - rung.b) > 0.0);  // largeur non nulle
        }
    }
}

// --- Décomposition et refus --------------------------------------------------

TEST_CASE("colonnes : Y produit plusieurs colonnes (decomposition)") {
    const auto r = columns_of("y");
    INFO("statut = " << to_string(r.status) << " colonnes = " << r.columns.size());
    // RequiresDecomposition : une colonne par branche, ou refus clair.
    CHECK((r.columns.size() >= 2 || !r.refusal.empty()));
}

TEST_CASE("colonnes : cercle refuse (direction ambigue)") {
    const auto r = columns_of("circle");
    CHECK(r.columns.empty());
    CHECK_FALSE(r.refusal.empty());
}

TEST_CASE("colonnes : anneau refuse (trou)") {
    const auto r = columns_of("ring");
    CHECK(r.columns.empty());
    CHECK_FALSE(r.refusal.empty());
}

TEST_CASE("colonnes : forme large refusee") {
    const auto r = columns_of("wide");
    CHECK(r.columns.empty());
    CHECK_FALSE(r.refusal.empty());
}

// --- Déterminisme ------------------------------------------------------------

TEST_CASE("colonnes : deterministe (memes rails a chaque execution)") {
    const auto a = columns_of("s");
    const auto b = columns_of("s");
    REQUIRE(a.columns.size() == b.columns.size());
    REQUIRE(a.columns.size() >= 1);
    CHECK(a.columns.front().rail_a == b.columns.front().rail_a);
    CHECK(a.columns.front().rail_b == b.columns.front().rail_b);
}
