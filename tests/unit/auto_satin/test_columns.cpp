// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>

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

// --- Extension des bouts ouverts jusqu'au bord réel ---------------------------
//
// Défaut trouvé par revue (mission « auto-satin béton ») : un bout OUVERT du
// squelette (sans jonction) s'arrête, par construction de l'amincissement,
// sensiblement avant le bord réel de la région — un embout arrondi ou pointu
// n'était pas couvert du tout par le satin, et même un bout CARRÉ retracte
// (retrait générique proche de la demi-largeur locale, artefact connu de
// l'amincissement). Démontré visuellement sur « capsule » (rect 40 mm + deux
// demi-cercles de rayon 2,5 mm, longueur totale bout-à-bout 45 mm) : avant
// correction, la colonne s'arrêtait à ~38,8 mm ; les deux embouts (2,5 mm de
// rayon chacun, ~11 % de la longueur totale) restaient entièrement hors
// couture. Confirmé aussi sur un bout plat (« rectangle », retrait ~2,55 mm
// par bout avant correction).

TEST_CASE("colonnes : bout ouvert (capsule) atteint le bord reel") {
    const auto r = columns_of("capsule");
    REQUIRE(r.refusal.empty());
    REQUIRE(r.columns.size() == 1);
    const auto& c = r.columns.front();
    // Longueur bout-a-bout reelle = 45 mm (40 mm de corps + 2 x 2,5 mm de
    // demi-cercle). Avant correction : ~38,8 mm (embouts non couverts).
    CHECK(std::abs(c.length_um - 45'000.0) < 1'000.0);
    // Les rails atteignent la pointe reelle de chaque demi-cercle : x = -2500
    // (gauche) et x = 42500 (droite), rayon W = 2500 depuis les centres
    // x=0/x=40000 (cf. shapes.cpp). Marge large : rasterisation 0,1 mm +
    // pas d'echantillonnage 0,5 mm.
    const auto firstX = c.rail_a.nodes.front().pos.x.value;
    const auto lastX = c.rail_a.nodes.back().pos.x.value;
    const auto [minX, maxX] = std::minmax(firstX, lastX);
    CHECK(minX < -1'800);
    CHECK(maxX > 41'800);
}

TEST_CASE("colonnes : extend_open_ends=false conserve l'ancien comportement") {
    const auto region = make_shape("capsule");
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    params.extend_open_ends = false;
    const auto r = build_satin_columns(*region, params);
    REQUIRE(r.columns.size() == 1);
    // Sans extension, la colonne s'arrete nettement avant le bord reel
    // (~38,8 mm avant correction) : verifie que le bascule desactive bien le
    // nouveau comportement plutot que de toujours l'appliquer.
    CHECK(r.columns.front().length_um < 40'000.0);
}

TEST_CASE("colonnes : bout carre (rectangle) atteint aussi le bord reel") {
    // Un bout CARRE (perpendiculaire, sans arrondi) souffre du meme defaut :
    // l'amincissement retracte generiquement le squelette d'un bout plat
    // avant le coin reel (d'environ la demi-largeur locale). Mesure sur
    // "rectangle" (40 mm, bouts plats) : sans extension, la colonne s'arrete a
    // ~34,9 mm (retrait ~2,55 mm par bout, proche de la demi-largeur 2,5 mm) ;
    // l'extension corrige aussi ce cas, pas seulement les embouts arrondis.
    const auto region = make_shape("rectangle");
    REQUIRE(region.has_value());
    SatinColumnsParameters withExt;
    withExt.analysis.raster.pixel_size = Micrometers{100};
    SatinColumnsParameters noExt = withExt;
    noExt.extend_open_ends = false;
    const auto a = build_satin_columns(*region, withExt);
    const auto b = build_satin_columns(*region, noExt);
    REQUIRE(a.columns.size() == 1);
    REQUIRE(b.columns.size() == 1);
    // Longueur reelle bout-a-bout = 40 mm (x de 0 a 40000, cf. shapes.cpp).
    CHECK(std::abs(a.columns.front().length_um - 40'000.0) < 1'000.0);
    // Sans extension, retrait mesurable (regression du defaut d'origine).
    CHECK(a.columns.front().length_um - b.columns.front().length_um > 2'000.0);
}

TEST_CASE("colonnes : bouts de jonction (Y) jamais etendus, arretes ouverts allonges") {
    SatinColumnsParameters withExt;
    withExt.analysis.raster.pixel_size = Micrometers{100};
    SatinColumnsParameters noExt = withExt;
    noExt.extend_open_ends = false;
    const auto region = make_shape("y");
    REQUIRE(region.has_value());
    const auto a = build_satin_columns(*region, withExt);
    const auto b = build_satin_columns(*region, noExt);
    REQUIRE(a.refusal.empty());
    REQUIRE(b.refusal.empty());
    REQUIRE(a.columns.size() == b.columns.size());
    REQUIRE(a.columns.size() >= 2);
    // La jonction partagee reste geometriquement identique (non affectee par
    // l'extension, qui ne touche que le bout SANS jonction de chaque bras).
    std::set<std::uint32_t> junctionsA, junctionsB;
    for (const auto& c : a.columns) {
        if (c.start_junction) junctionsA.insert(*c.start_junction);
        if (c.end_junction) junctionsA.insert(*c.end_junction);
    }
    for (const auto& c : b.columns) {
        if (c.start_junction) junctionsB.insert(*c.start_junction);
        if (c.end_junction) junctionsB.insert(*c.end_junction);
    }
    CHECK(junctionsA.size() == 1);
    CHECK(junctionsB.size() == 1);
    // Chaque bras s'est allonge (bout ouvert etendu jusqu'au bord reel).
    for (std::size_t i = 0; i < a.columns.size(); ++i) {
        CHECK(a.columns[i].length_um > b.columns[i].length_um);
    }
}

TEST_CASE("colonnes : extension deterministe") {
    const auto region = make_shape("capsule");
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    const auto a = build_satin_columns(*region, params);
    const auto b = build_satin_columns(*region, params);
    REQUIRE(a.columns.size() == b.columns.size());
    for (std::size_t i = 0; i < a.columns.size(); ++i) {
        CHECK(a.columns[i].rail_a == b.columns[i].rail_a);
        CHECK(a.columns[i].rail_b == b.columns[i].rail_b);
        REQUIRE(a.columns[i].rungs.size() == b.columns[i].rungs.size());
        for (std::size_t j = 0; j < a.columns[i].rungs.size(); ++j) {
            CHECK(a.columns[i].rungs[j].a == b.columns[i].rungs[j].a);
            CHECK(a.columns[i].rungs[j].b == b.columns[i].rungs[j].b);
        }
    }
}

TEST_CASE("colonnes : aucun barreau degenere apres extension (capsule)") {
    const auto r = columns_of("capsule");
    REQUIRE(r.columns.size() == 1);
    for (const auto& rung : r.columns.front().rungs) {
        CHECK(length_um(rung.a - rung.b) > 0.0);
    }
}

// --- Décomposition et refus --------------------------------------------------

TEST_CASE("colonnes : Y produit plusieurs colonnes (decomposition)") {
    const auto r = columns_of("y");
    INFO("statut = " << to_string(r.status) << " colonnes = " << r.columns.size());
    // RequiresDecomposition : une colonne par branche, ou refus clair.
    CHECK((r.columns.size() >= 2 || !r.refusal.empty()));
}

TEST_CASE("colonnes : un reseau Y identifie ses sections et sa jonction") {
    const auto r = columns_of("y");
    REQUIRE(r.refusal.empty());
    REQUIRE(r.columns.size() >= 2);
    std::set<std::uint32_t> junctions;
    for (std::size_t i = 0; i < r.columns.size(); ++i) {
        const auto& column = r.columns[i];
        CHECK(column.section_index == i);
        CHECK(column.section_count == r.columns.size());
        CHECK(column.start_junction.has_value() != column.end_junction.has_value());
        if (column.start_junction) junctions.insert(*column.start_junction);
        if (column.end_junction) junctions.insert(*column.end_junction);
    }
    CHECK(junctions.size() == 1);
}

TEST_CASE("colonnes : cercle refuse (direction ambigue)") {
    const auto r = columns_of("circle");
    CHECK(r.columns.empty());
    CHECK_FALSE(r.refusal.empty());
}

TEST_CASE("colonnes : anneau decompose en quatre sections ouvertes raccordees") {
    const auto r = columns_of("ring");
    REQUIRE(r.refusal.empty());
    REQUIRE(r.status == SatinabilityStatus::RequiresDecomposition);
    REQUIRE(r.columns.size() == 4);
    for (std::size_t i = 0; i < r.columns.size(); ++i) {
        const auto& current = r.columns[i];
        const auto& next = r.columns[(i + 1) % r.columns.size()];
        REQUIRE_FALSE(current.rail_a.closed);
        REQUIRE_FALSE(current.rail_b.closed);
        REQUIRE(current.rungs.size() >= 2);
        CHECK(current.section_index == i);
        CHECK(current.section_count == 4);
        REQUIRE(current.start_junction.has_value());
        REQUIRE(current.end_junction.has_value());
        CHECK(*current.start_junction == i);
        CHECK(*current.end_junction == (i + 1) % 4);
        CHECK(current.rail_a.nodes.back().pos == next.rail_a.nodes.front().pos);
        CHECK(current.rail_b.nodes.back().pos == next.rail_b.nodes.front().pos);
    }
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
