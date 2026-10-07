// SPDX-License-Identifier: Apache-2.0
//
// Porte de l'etape 1 de la refonte "decomposition topologique" (cf.
// docs/source/satin.md) : `satin_column_view` doit rester une PROJECTION
// pure de `SatinColumnGeometry`/`ParametricSatinObject`, jamais une
// troisieme representation independante -- ces tests verifient l'identite
// bit-a-bit (memes points de rail, memes barreaux) entre la source et la
// vue projetee, en mode Legacy ET Parametric.
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/auto_satin/shapes.hpp"

using namespace openstitch;
using namespace openstitch::auto_satin;

namespace {

SatinColumnsResult columns_of(const std::string& shape, SatinGeometryMode mode) {
    const auto region = make_shape(shape);
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100}; // 0,1 mm : rapide
    params.geometry_mode = mode;
    return build_satin_columns(*region, params);
}

bool paths_identical(const geometry::Path& a, const geometry::Path& b) {
    if (a.nodes.size() != b.nodes.size() || a.closed != b.closed)
        return false;
    for (std::size_t i = 0; i < a.nodes.size(); ++i) {
        if (a.nodes[i].pos.x.value != b.nodes[i].pos.x.value ||
            a.nodes[i].pos.y.value != b.nodes[i].pos.y.value) {
            return false;
        }
    }
    return true;
}

bool rungs_identical(const std::vector<SatinRung>& a, const std::vector<SatinRung>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].a.x.value != b[i].a.x.value || a[i].a.y.value != b[i].a.y.value ||
            a[i].b.x.value != b[i].b.x.value || a[i].b.y.value != b[i].b.y.value) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("satin_column_view : mode Legacy -- identite bit-a-bit avec SatinColumnGeometry") {
    for (const std::string& name : {"capsule", "t", "y", "cross", "trident"}) {
        INFO("forme = " << name);
        const auto built = columns_of(name, SatinGeometryMode::Legacy);
        REQUIRE_FALSE(built.columns.empty());
        REQUIRE(built.parametric_columns
                    .empty()); // sinon la vue prendrait l'autre vecteur (cf. satin_column_view)

        const auto view = satin_column_view(built);
        REQUIRE(view.size() == built.columns.size());
        for (std::size_t i = 0; i < view.size(); ++i) {
            INFO("colonne " << i);
            const auto& src = built.columns[i];
            const auto& proj = view[i];
            CHECK(paths_identical(proj.rail_a, src.rail_a));
            CHECK(paths_identical(proj.rail_b, src.rail_b));
            CHECK(rungs_identical(proj.rungs, src.rungs));
            CHECK(proj.start_junction == src.start_junction);
            CHECK(proj.end_junction == src.end_junction);
            CHECK(proj.mean_width_um == src.mean_width_um);
            CHECK(proj.length_um == src.length_um);
            CHECK(proj.method == RailConstructionMethod::AxisStation);
        }
    }
}

TEST_CASE("satin_column_view : mode Parametric -- identite bit-a-bit avec ParametricSatinObject") {
    for (const std::string& name : {"capsule", "t", "y", "cross", "trident"}) {
        INFO("forme = " << name);
        const auto built = columns_of(name, SatinGeometryMode::Parametric);
        REQUIRE_FALSE(built.parametric_columns.empty());

        const auto view = satin_column_view(built);
        REQUIRE(view.size() == built.parametric_columns.size());
        for (std::size_t i = 0; i < view.size(); ++i) {
            INFO("objet " << i);
            const auto& src = built.parametric_columns[i];
            const auto& proj = view[i];
            CHECK(paths_identical(proj.rail_a, src.rail_a));
            CHECK(paths_identical(proj.rail_b, src.rail_b));
            CHECK(rungs_identical(proj.rungs, src.rungs));
            CHECK(proj.start_junction == src.start_junction);
            CHECK(proj.end_junction == src.end_junction);
            CHECK(proj.mean_width_um == src.mean_width_um);
            CHECK(proj.length_um == src.length_um);
            CHECK(proj.method == RailConstructionMethod::AxisStation);
        }
    }
}

TEST_CASE("satin_column_view : start_width/end_width derives du premier/dernier barreau reel") {
    const auto built = columns_of("capsule", SatinGeometryMode::Legacy);
    const auto view = satin_column_view(built);
    REQUIRE_FALSE(view.empty());
    const auto& col = view.front();
    REQUIRE_FALSE(col.rungs.empty());

    const auto expectedWidth = [](const SatinRung& r) {
        const double dx = static_cast<double>(r.a.x.value) - static_cast<double>(r.b.x.value);
        const double dy = static_cast<double>(r.a.y.value) - static_cast<double>(r.b.y.value);
        return static_cast<std::int32_t>(std::lround(std::sqrt(dx * dx + dy * dy)));
    };
    CHECK(col.start_width.value == expectedWidth(col.rungs.front()));
    CHECK(col.end_width.value == expectedWidth(col.rungs.back()));
    // Coherent avec la largeur mesuree globalement sur la colonne (jamais un
    // ordre de grandeur different -- capsule est une forme simple, largeur
    // quasi constante sur toute sa longueur).
    CHECK(col.start_width.value > 0);
    CHECK(col.end_width.value > 0);
}

TEST_CASE("satin_column_view : vecteur vide sur un refus (aucune colonne construite)") {
    // "two_holes" (rectangle troue deux fois, refuse `Unsuitable` --
    // hole_count>0 refuse avant meme le test de largeur, cf.
    // satinability.cpp) -- refus explicite, ni columns ni parametric_columns
    // peuples. "wide" (utilise ici avant HP-STI-018 Phase D) ne convient
    // plus : une bande large mais allongee declenche desormais le pelage en
    // anneaux concentriques (§2.3/§4 du plan, cf. test_columns.cpp) et n'est
    // donc plus un exemple de refus -- "two_holes" reste refuse par
    // construction (deux trous, jamais la cible de `build_turning_satin_
    // sections`, qui exige `region.holes.empty()`).
    const auto region = make_shape("two_holes");
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    const auto built = build_satin_columns(*region, params);
    REQUIRE(built.columns.empty());
    REQUIRE(built.parametric_columns.empty());
    CHECK(satin_column_view(built).empty());
}
