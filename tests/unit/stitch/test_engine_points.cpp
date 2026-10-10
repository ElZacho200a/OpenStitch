// SPDX-License-Identifier: Apache-2.0
//
// Moteur de points -- lot « moteur » : compensation du tirage du tatami (HP-ENG-001),
// sous-couche automatique (HP-ENG-002), entrée/sortie automatiques (HP-ENG-010), longueurs
// maximales de point (HP-ENG-008) et satin de bordure (HP-STI-004).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <vector>

#include "openstitch/geometry/polyline.hpp"
#include "openstitch/stitch_generation/border_satin.hpp"
#include "openstitch/stitch_generation/finish.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/join.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/stitch_generation/tatami.hpp"
#include "openstitch/stitch_generation/underlay_auto.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;
using CmdType = stitch::CommandType;
using Pass = stitch::StitchPass;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

geometry::PathNode node(std::int32_t x, std::int32_t y) {
    return geometry::PathNode{um(x, y), geometry::NodeType::Corner, std::nullopt, std::nullopt};
}

geometry::Path rect_at(std::int32_t x0, std::int32_t y0, std::int32_t w, std::int32_t h) {
    geometry::Path p;
    p.closed = true;
    p.nodes = {node(x0, y0), node(x0 + w, y0), node(x0 + w, y0 + h), node(x0, y0 + h)};
    return p;
}

geometry::Path circle(std::int32_t cx, std::int32_t cy, double radius, int n, bool ccw = true) {
    geometry::Path p;
    p.closed = true;
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * std::numbers::pi * i / n * (ccw ? 1.0 : -1.0);
        p.nodes.push_back(node(cx + static_cast<std::int32_t>(std::lround(radius * std::cos(a))),
                               cy + static_cast<std::int32_t>(std::lround(radius * std::sin(a)))));
    }
    return p;
}

document::TatamiParams tatami(std::int32_t spacing = 400, std::int32_t len = 3'000) {
    document::TatamiParams p;
    p.row_spacing = Micrometers{spacing};
    p.stitch_length = Micrometers{len};
    p.inset = Micrometers{0};
    return p;
}

// Ajoute un objet vectoriel + un objet de broderie au projet ; renvoie l'id de broderie.
ObjectId add_object(document::Project& project, geometry::PathSet region,
                    document::StitchParams params, std::array<std::uint8_t, 3> rgb = {10, 20, 30}) {
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(std::move(region));
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.name = "obj";
    emb.source_vector = vec.id;
    emb.rgb = rgb;
    emb.params = std::move(params);
    project.embroidery_objects.push_back(emb);
    return emb.id;
}

std::vector<Vec2um> pass_points(const stitch::StitchSequence& s, Pass pass) {
    std::vector<Vec2um> out;
    for (const auto& c : s.commands) {
        if (c.type == CmdType::Stitch && c.pass == pass) {
            out.push_back(c.pos);
        }
    }
    return out;
}

std::size_t count_pass(const stitch::StitchSequence& s, Pass pass) {
    return pass_points(s, pass).size();
}

double dist_um(Vec2um a, Vec2um b) {
    return length_um(a - b);
}

bool same_sequence(const stitch::StitchSequence& a, const stitch::StitchSequence& b) {
    if (a.commands.size() != b.commands.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.commands.size(); ++i) {
        const auto& x = a.commands[i];
        const auto& y = b.commands[i];
        if (x.pos != y.pos || x.type != y.type || x.pass != y.pass || x.source != y.source) {
            return false;
        }
    }
    return true;
}

// Distance d'un point à une polyligne (fermée ou non).
double dist_to_polyline(const std::vector<Vec2um>& poly, bool closed, Vec2um p) {
    double best = 1e30;
    const std::size_t n = poly.size();
    const std::size_t segs = closed ? n : n - 1;
    for (std::size_t i = 0; i < segs; ++i) {
        const Vec2um a = poly[i];
        const Vec2um b = poly[(i + 1) % n];
        const double dx = static_cast<double>(b.x.value - a.x.value);
        const double dy = static_cast<double>(b.y.value - a.y.value);
        const double l2 = dx * dx + dy * dy;
        double t = l2 > 0 ? ((p.x.value - a.x.value) * dx + (p.y.value - a.y.value) * dy) / l2 : 0;
        t = std::clamp(t, 0.0, 1.0);
        const double ex = a.x.value + t * dx - p.x.value;
        const double ey = a.y.value + t * dy - p.y.value;
        best = std::min(best, std::sqrt(ex * ex + ey * ey));
    }
    return best;
}

double orient(Vec2um o, Vec2um a, Vec2um b) {
    return static_cast<double>(a.x.value - o.x.value) * static_cast<double>(b.y.value - o.y.value) -
           static_cast<double>(a.y.value - o.y.value) * static_cast<double>(b.x.value - o.x.value);
}

bool segments_cross(Vec2um a, Vec2um b, Vec2um c, Vec2um d) {
    const double o1 = orient(a, b, c);
    const double o2 = orient(a, b, d);
    const double o3 = orient(c, d, a);
    const double o4 = orient(c, d, b);
    return ((o1 > 0) != (o2 > 0)) && ((o3 > 0) != (o4 > 0)) && o1 != 0 && o2 != 0 && o3 != 0 &&
           o4 != 0;
}

// Barreaux de la couche supérieure d'un satin : (A_i, B_i) = paires de points consécutifs.
struct Bar {
    Vec2um a;
    Vec2um b;
};
std::vector<Bar> top_bars(const stitch::StitchSequence& s) {
    const auto pts = pass_points(s, Pass::TopStitch);
    std::vector<Bar> bars;
    for (std::size_t i = 0; i + 1 < pts.size(); i += 2) {
        bars.push_back({pts[i], pts[i + 1]});
    }
    return bars;
}

document::Project satin_project(const document::SatinParams& params) {
    document::Project project;
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.name = "bordure";
    emb.rgb = {200, 0, 0};
    emb.params = params;
    project.embroidery_objects.push_back(emb);
    return project;
}

} // namespace

// --- HP-ENG-001 : compensation du tirage ----------------------------------------------------

TEST_CASE("tirage : les rangees depassent du contour d'exactement la compensation") {
    const geometry::PathSet region{rect_at(0, 0, 20'000, 10'000), {}};
    auto p = tatami();
    p.pull_compensation = Micrometers{300};
    const auto fill = fill_tatami(region, p);
    REQUIRE_FALSE(fill.empty());
    std::int32_t minX = 1'000'000;
    std::int32_t maxX = -1'000'000;
    for (const auto& fs : fill) {
        minX = std::min(minX, fs.pos.x.value);
        maxX = std::max(maxX, fs.pos.x.value);
        CHECK(fs.pos.y.value >= 0);
        CHECK(fs.pos.y.value <= 10'000);
    }
    CHECK(minX == -300);
    CHECK(maxX == 20'300);
}

TEST_CASE("tirage : zero compensation = comportement historique, sinon memes pas hors bouts") {
    const geometry::PathSet region{rect_at(0, 0, 20'000, 10'000), {}};
    const auto base = fill_tatami(region, tatami());
    auto p = tatami();
    p.pull_compensation = Micrometers{300};
    const auto pulled = fill_tatami(region, p);
    REQUIRE(base.size() == pulled.size());
    for (std::size_t i = 0; i < base.size(); ++i) {
        CHECK(base[i].pos.y == pulled[i].pos.y);
        CHECK(base[i].jump == pulled[i].jump);
        // Le dépassement ne déplace que les pénétrations d'extrémité de rangée.
        CHECK(std::abs(base[i].pos.x.value - pulled[i].pos.x.value) <= 300);
        if (base[i].pos.x.value > 0 && base[i].pos.x.value < 20'000) {
            CHECK(base[i].pos == pulled[i].pos);
        }
    }
}

TEST_CASE("tirage : la compensation suit l'axe du fil (rangees inclinees)") {
    const geometry::PathSet region{rect_at(0, 0, 20'000, 20'000), {}};
    const double angle = std::numbers::pi / 6.0;
    auto p = tatami();
    p.angle = Angle{angle};
    const auto base = fill_tatami(region, p);
    p.pull_compensation = Micrometers{400};
    const auto pulled = fill_tatami(region, p);
    // Dans le repère des rangées (rotation de -angle), l'étendue en x grandit de 2 x pull.
    const auto extent = [&](const std::vector<FillStitch>& f) {
        double lo = 1e30;
        double hi = -1e30;
        for (const auto& fs : f) {
            const double x = fs.pos.x.value * std::cos(angle) + fs.pos.y.value * std::sin(angle);
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
        return hi - lo;
    };
    CHECK(extent(pulled) - extent(base) == Catch::Approx(800.0).margin(3.0));
}

TEST_CASE("tirage : forme concave avec trou, aucune liaison cousue supplementaire") {
    geometry::PathSet region;
    region.outer.closed = true;
    region.outer.nodes = {node(0, 0),           node(30'000, 0),      node(30'000, 10'000),
                          node(10'000, 10'000), node(10'000, 30'000), node(0, 30'000)};
    region.holes.push_back(rect_at(3'000, 3'000, 4'000, 4'000));
    auto p = tatami();
    p.hidden_underpath = true;
    const auto base = fill_tatami(region, p);
    p.pull_compensation = Micrometers{500};
    const auto pulled = fill_tatami(region, p);
    const auto jumps = [](const std::vector<FillStitch>& f) {
        return std::count_if(f.begin(), f.end(), [](const FillStitch& s) { return s.jump; });
    };
    CHECK(jumps(pulled) == jumps(base));
    CHECK(pulled.size() == base.size());
    // Les rangées s'arrêtant sur le bord du trou dépassent dedans d'au plus `pull` (le trou
    // se rétrécit à la couture comme le reste) : jamais au-delà, le coeur du trou reste vide.
    for (const auto& fs : pulled) {
        const bool inHoleCore = fs.pos.x.value > 3'500 + 10 && fs.pos.x.value < 6'500 - 10 &&
                                fs.pos.y.value > 3'000 && fs.pos.y.value < 7'000;
        CHECK_FALSE(inHoleCore);
    }
}

TEST_CASE("tirage : formes minuscules et borne a 3 mm, deterministe") {
    const geometry::PathSet tiny{rect_at(0, 0, 800, 800), {}};
    auto p = tatami(400, 3'000);
    p.pull_compensation = Micrometers{50'000}; // aberrant : borne a 3 mm
    const auto a = fill_tatami(tiny, p);
    const auto b = fill_tatami(tiny, p);
    REQUIRE_FALSE(a.empty());
    CHECK(a == b);
    for (const auto& fs : a) {
        CHECK(fs.pos.x.value >= -3'000);
        CHECK(fs.pos.x.value <= 3'800);
    }
}

TEST_CASE("tirage : la sous-couche en rangees ne depasse pas du contour") {
    const geometry::PathSet region{rect_at(0, 0, 30'000, 30'000), {}};
    auto p = tatami();
    p.pull_compensation = Micrometers{500};
    p.underlay_parallel = true;
    const auto passes = tatami_underlay(region, p);
    REQUIRE_FALSE(passes.empty());
    for (const auto& pass : passes) {
        for (const Vec2um pt : pass) {
            CHECK(pt.x.value >= 0);
            CHECK(pt.x.value <= 30'000);
            CHECK(pt.y.value >= 0);
            CHECK(pt.y.value <= 30'000);
        }
    }
}

// --- HP-ENG-002 : sous-couche automatique ---------------------------------------------------

TEST_CASE("sous-couche auto : seuils de remplissage par aire et epaisseur") {
    const auto choose = [](std::int32_t w, std::int32_t h) {
        return choose_fill_underlay(measure_shape(geometry::PathSet{rect_at(0, 0, w, h), {}}));
    };
    const auto tiny = choose(2'000, 2'000); // 4 mm2
    CHECK_FALSE(tiny.edge);
    CHECK_FALSE(tiny.parallel);
    const auto thin = choose(40'000, 500); // bande de 0,5 mm
    CHECK_FALSE(thin.edge);
    const auto medium = choose(5'000, 5'000); // 25 mm2
    CHECK(medium.edge);
    CHECK_FALSE(medium.parallel);
    const auto large = choose(20'000, 20'000); // 400 mm2
    CHECK(large.edge);
    CHECK(large.parallel);
    CHECK(large.spacing.value == 2'500);
    const auto big = choose(40'000, 40'000);
    CHECK(big.inset.value == 600); // epaisseur >= 2,4 mm : retrait plafonne
}

TEST_CASE("sous-couche auto : mesure d'une forme (aire, epaisseur)") {
    const auto m = measure_shape(geometry::PathSet{rect_at(0, 0, 20'000, 10'000), {}});
    CHECK(m.area_mm2 == Catch::Approx(200.0));
    CHECK(m.perimeter_mm == Catch::Approx(60.0));
    CHECK(m.mean_width_mm == Catch::Approx(2.0 * 200.0 / 60.0));
    geometry::PathSet ring{rect_at(0, 0, 20'000, 20'000), {rect_at(5'000, 5'000, 10'000, 10'000)}};
    CHECK(measure_shape(ring).area_mm2 == Catch::Approx(300.0));
}

TEST_CASE("sous-couche auto : seuils de satin par largeur") {
    CHECK_FALSE(choose_satin_underlay(0.8).center);
    CHECK(choose_satin_underlay(2.0).center);
    CHECK_FALSE(choose_satin_underlay(2.0).edge);
    CHECK(choose_satin_underlay(5.0).edge);
    CHECK_FALSE(choose_satin_underlay(5.0).center);
    CHECK_FALSE(choose_satin_underlay(5.0).zigzag);
    CHECK(choose_satin_underlay(8.0).edge);
    CHECK(choose_satin_underlay(8.0).zigzag);
}

TEST_CASE("sous-couche auto : mode Manual inchange, mode Auto choisit") {
    document::Project project;
    auto params = tatami();
    add_object(project, {rect_at(0, 0, 30'000, 30'000), {}}, params);
    const auto manual = generate_sequence(project);
    REQUIRE(manual.has_value());
    CHECK(count_pass(*manual, Pass::Underlay) == 0); // defaut : aucune sous-couche

    std::get<document::TatamiParams>(project.embroidery_objects[0].params).underlay_mode =
        document::UnderlayMode::Auto;
    const auto autoSeq = generate_sequence(project);
    REQUIRE(autoSeq.has_value());
    CHECK(count_pass(*autoSeq, Pass::Underlay) > 0);
    // La couche supérieure n'est pas modifiée par la sous-couche.
    CHECK(pass_points(*autoSeq, Pass::TopStitch) == pass_points(*manual, Pass::TopStitch));
    // Les reglages manuels sont ignores en mode Auto.
    auto& tp = std::get<document::TatamiParams>(project.embroidery_objects[0].params);
    tp.underlay_edge = false;
    tp.underlay_parallel = false;
    const auto again = generate_sequence(project);
    REQUIRE(again.has_value());
    CHECK(same_sequence(*again, *autoSeq));

    // Petite forme : rien, meme en Auto.
    document::Project small;
    auto sp = tatami();
    sp.underlay_mode = document::UnderlayMode::Auto;
    add_object(small, {rect_at(0, 0, 2'000, 2'000), {}}, sp);
    const auto smallSeq = generate_sequence(small);
    REQUIRE(smallSeq.has_value());
    CHECK(count_pass(*smallSeq, Pass::Underlay) == 0);
}

TEST_CASE("sous-couche auto : la sous-couche reste dans la forme (concave, trou)") {
    geometry::PathSet region;
    region.outer.closed = true;
    region.outer.nodes = {node(0, 0),           node(40'000, 0),      node(40'000, 15'000),
                          node(15'000, 15'000), node(15'000, 40'000), node(0, 40'000)};
    region.holes.push_back(rect_at(3'000, 3'000, 6'000, 6'000));
    document::Project project;
    auto p = tatami();
    p.underlay_mode = document::UnderlayMode::Auto;
    add_object(project, region, p);
    const auto seq = generate_sequence(project);
    REQUIRE(seq.has_value());
    const auto under = pass_points(*seq, Pass::Underlay);
    REQUIRE_FALSE(under.empty());
    for (const Vec2um pt : under) {
        CHECK(pt.x.value >= 0);
        CHECK(pt.y.value >= 0);
        CHECK(pt.x.value <= 40'000);
        CHECK(pt.y.value <= 40'000);
        const bool inHole =
            pt.x.value > 3'010 && pt.x.value < 8'990 && pt.y.value > 3'010 && pt.y.value < 8'990;
        CHECK_FALSE(inHole);
    }
}

TEST_CASE("sous-couche auto : satin selon la largeur de la colonne") {
    const auto make = [](std::int32_t widthUm) {
        document::SatinParams p;
        p.rail_a.closed = false;
        p.rail_b.closed = false;
        for (int i = 0; i <= 30; ++i) {
            p.rail_a.nodes.push_back(node(i * 1'000, 0));
            p.rail_b.nodes.push_back(node(i * 1'000, widthUm));
            p.rungs.push_back({um(i * 1'000, 0), um(i * 1'000, widthUm), std::nullopt});
        }
        p.center_underlay = false;
        p.underlay_mode = document::UnderlayMode::Auto;
        return p;
    };
    const auto under = [&](std::int32_t w) {
        const auto s = generate_sequence(satin_project(make(w)));
        REQUIRE(s.has_value());
        return count_pass(*s, Pass::Underlay);
    };
    CHECK(under(800) == 0);
    const auto center = under(2'000);
    CHECK(center > 0);
    const auto edge = under(5'000);
    CHECK(edge > center); // deux chemins de bord
    const auto wide = under(8'000);
    CHECK(wide > edge); // bord + zigzag
    CHECK(satin_mean_width_mm(make(5'000).rail_a, make(5'000).rail_b) == Catch::Approx(5.0));
}

// --- HP-ENG-010 : entree/sortie automatiques ------------------------------------------------

namespace {

// Trois tatami empiles ; l'ordre de couture les visite en zigzag vertical.
document::Project stacked_fills(bool autoJoin) {
    document::Project project;
    project.finishing.auto_join = autoJoin;
    add_object(project, {rect_at(0, 0, 10'000, 10'000), {}}, tatami());
    add_object(project, {rect_at(0, -12'000, 10'000, 10'000), {}}, tatami());
    add_object(project, {rect_at(0, -24'000, 10'000, 10'000), {}}, tatami());
    return project;
}

std::multiset<std::pair<std::int32_t, std::int32_t>> stitch_set(const stitch::StitchSequence& s,
                                                                ObjectId source) {
    std::multiset<std::pair<std::int32_t, std::int32_t>> out;
    for (const auto& c : s.commands) {
        if (c.type == CmdType::Stitch && c.source == source) {
            out.insert({c.pos.x.value, c.pos.y.value});
        }
    }
    return out;
}

} // namespace

TEST_CASE("entree/sortie auto : desactive par defaut, sequence strictement inchangee") {
    const auto off = generate_sequence(stacked_fills(false));
    REQUIRE(off.has_value());
    const auto again = generate_sequence(stacked_fills(false));
    REQUIRE(again.has_value());
    CHECK(same_sequence(*off, *again));
}

TEST_CASE("entree/sortie auto : reduit les sauts et conserve les penetrations") {
    const auto project = stacked_fills(false);
    const auto off = generate_sequence(project);
    const auto on = generate_sequence(stacked_fills(true));
    REQUIRE(off.has_value());
    REQUIRE(on.has_value());
    CHECK(total_jump_length_um(*on) < total_jump_length_um(*off));
    for (const auto& obj : project.embroidery_objects) {
        CHECK(stitch_set(*on, obj.id) == stitch_set(*off, obj.id));
    }
    // Deterministe.
    const auto onAgain = generate_sequence(stacked_fills(true));
    REQUIRE(onAgain.has_value());
    CHECK(same_sequence(*on, *onAgain));
}

TEST_CASE("entree/sortie auto : reglage par objet (Off ignore, Auto force)") {
    auto project = stacked_fills(true);
    for (auto& e : project.embroidery_objects) {
        e.join = document::JoinMode::Off;
    }
    const auto allOff = generate_sequence(project);
    const auto natural = generate_sequence(stacked_fills(false));
    REQUIRE(allOff.has_value());
    REQUIRE(natural.has_value());
    CHECK(same_sequence(*allOff, *natural));

    auto forced = stacked_fills(false);
    for (auto& e : forced.embroidery_objects) {
        e.join = document::JoinMode::Auto;
    }
    const auto forcedSeq = generate_sequence(forced);
    const auto globalSeq = generate_sequence(stacked_fills(true));
    REQUIRE(forcedSeq.has_value());
    REQUIRE(globalSeq.has_value());
    CHECK(same_sequence(*forcedSeq, *globalSeq));
}

TEST_CASE("entree/sortie auto : sous-couche et couche superieure restent dans l'ordre") {
    document::Project project;
    project.finishing.auto_join = true;
    auto p = tatami();
    p.underlay_mode = document::UnderlayMode::Auto;
    add_object(project, {rect_at(0, 0, 20'000, 20'000), {}}, p);
    add_object(project, {rect_at(0, -25'000, 20'000, 20'000), {}}, p);
    const auto seq = generate_sequence(project);
    REQUIRE(seq.has_value());
    for (const auto& obj : project.embroidery_objects) {
        bool seenTop = false;
        for (const auto& c : seq->commands) {
            if (c.type != CmdType::Stitch || c.source != obj.id) {
                continue;
            }
            if (c.pass == Pass::TopStitch) {
                seenTop = true;
            } else if (c.pass == Pass::Underlay) {
                CHECK_FALSE(seenTop);
            }
        }
        CHECK(seenTop);
    }
}

TEST_CASE("entree/sortie auto : un point d'entree explicite du tatami est respecte") {
    auto project = stacked_fills(true);
    std::get<document::TatamiParams>(project.embroidery_objects[1].params).entry_point =
        um(10'000, -12'000);
    auto manual = stacked_fills(false);
    std::get<document::TatamiParams>(manual.embroidery_objects[1].params).entry_point =
        um(10'000, -12'000);
    const auto a = generate_sequence(project);
    const auto b = generate_sequence(manual);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(stitch_set(*a, project.embroidery_objects[1].id) ==
          stitch_set(*b, manual.embroidery_objects[1].id));
    // Premiere commande cousue de l'objet : identique (sens impose par l'entree).
    const auto firstOf = [](const stitch::StitchSequence& s, ObjectId id) {
        for (const auto& c : s.commands) {
            if (c.source == id && c.type == CmdType::Jump) {
                return c.pos;
            }
        }
        return Vec2um{};
    };
    CHECK(firstOf(*a, project.embroidery_objects[1].id) ==
          firstOf(*b, manual.embroidery_objects[1].id));
}

TEST_CASE("entree/sortie auto : satin en colonne unique demarre du cote le plus proche") {
    const auto column = [](bool flip) {
        document::SatinParams p;
        p.rail_a.closed = false;
        p.rail_b.closed = false;
        for (int i = 0; i <= 20; ++i) {
            const std::int32_t x = flip ? 60'000 - i * 1'000 : 40'000 + i * 1'000;
            p.rail_a.nodes.push_back(node(x, 0));
            p.rail_b.nodes.push_back(node(x, 2'000));
            p.rungs.push_back({um(x, 0), um(x, 2'000), std::nullopt});
        }
        return p;
    };
    const auto build = [&](bool autoJoin) {
        document::Project project;
        project.finishing.auto_join = autoJoin;
        add_object(project, {rect_at(0, 0, 10'000, 10'000), {}}, tatami());
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.name = "satin";
        emb.rgb = {10, 20, 30};
        emb.params = column(true); // sens naturel : part de x = 60 mm (loin)
        project.embroidery_objects.push_back(emb);
        return project;
    };
    const auto firstTop = [](const stitch::StitchSequence& s, ObjectId id) {
        for (const auto& c : s.commands) {
            if (c.type == CmdType::Stitch && c.source == id && c.pass == Pass::TopStitch) {
                return c.pos;
            }
        }
        return Vec2um{};
    };
    const auto off = build(false);
    const auto on = build(true);
    const auto offSeq = generate_sequence(off);
    const auto onSeq = generate_sequence(on);
    REQUIRE(offSeq.has_value());
    REQUIRE(onSeq.has_value());
    CHECK(firstTop(*offSeq, off.embroidery_objects[1].id).x.value >= 55'000);
    CHECK(firstTop(*onSeq, on.embroidery_objects[1].id).x.value <= 45'000);
    CHECK(total_jump_length_um(*onSeq) < total_jump_length_um(*offSeq));
}

TEST_CASE("entree/sortie auto : orient_chunk laisse intact un troncon de forme inattendue") {
    std::vector<stitch::StitchCommand> odd;
    odd.push_back({um(0, 0), CmdType::Jump, ObjectId{1}, Pass::Travel});
    odd.push_back({um(0, 0), CmdType::Trim, ObjectId{1}, Pass::Travel});
    odd.push_back({um(1'000, 0), CmdType::Stitch, ObjectId{1}, Pass::TopStitch});
    const auto out = orient_chunk(odd, um(5'000, 0), std::nullopt);
    CHECK(out.size() == odd.size());
    CHECK(out.front().pos == odd.front().pos);
    CHECK(orient_chunk({}, um(0, 0), um(1, 1)).empty());
}

// --- HP-ENG-008 : longueurs de point ---------------------------------------------------------

TEST_CASE("longueurs : aucun point cousu au-dela du maximum apres finitions") {
    document::Project project;
    document::RunningStitchParams rp;
    rp.stitch_length = Micrometers{20'000};
    add_object(project, {rect_at(0, 0, 40'000, 40'000), {}}, rp);
    project.finishing.split_long_stitches = false;
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    const auto longBefore = finish_sequence(*raw, project);
    double maxBefore = 0;
    for (std::size_t i = 1; i < longBefore.commands.size(); ++i) {
        const auto& c = longBefore.commands[i];
        if (c.type == CmdType::Stitch && longBefore.commands[i - 1].type == CmdType::Stitch &&
            c.pass != Pass::Lock) {
            maxBefore = std::max(maxBefore, dist_um(c.pos, longBefore.commands[i - 1].pos));
        }
    }
    CHECK(maxBefore > 7'000); // sans le reglage : points de 20 mm conserves

    project.finishing.split_long_stitches = true;
    project.finishing.max_stitch_length = Micrometers{7'000};
    const auto done = finish_sequence(*raw, project);
    for (std::size_t i = 1; i < done.commands.size(); ++i) {
        const auto& c = done.commands[i];
        if (c.type == CmdType::Stitch && done.commands[i - 1].type == CmdType::Stitch) {
            CHECK(dist_um(c.pos, done.commands[i - 1].pos) <= 7'001.0);
        }
    }
    // Memes sommets conserves : le contour n'est pas deforme (decoupe sur la droite).
    for (const auto& c : done.commands) {
        if (c.type == CmdType::Stitch && c.pass == Pass::TopStitch) {
            const bool onEdge = c.pos.x.value == 0 || c.pos.x.value == 40'000 ||
                                c.pos.y.value == 0 || c.pos.y.value == 40'000;
            CHECK(onEdge);
        }
    }
    // Deterministe.
    CHECK(same_sequence(done, finish_sequence(*raw, project)));
}

TEST_CASE("longueurs : finitions desactivees -> aucun decoupage") {
    document::Project project = document::Project{};
    document::RunningStitchParams rp;
    rp.stitch_length = Micrometers{20'000};
    add_object(project, {rect_at(0, 0, 40'000, 40'000), {}}, rp);
    project.finishing = document::SequenceFinishing::legacy();
    project.finishing.split_long_stitches = true;
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    CHECK(same_sequence(finish_sequence(*raw, project), *raw));
}

// --- HP-STI-004 : satin de bordure -----------------------------------------------------------

TEST_CASE("bordure : cercle centre, bande reguliere sans croisement") {
    const double r = 10'000.0;
    const auto ring = circle(0, 0, r, 72);
    document::BorderSatinSpec spec;
    spec.width = Micrometers{3'000};
    const auto params = border_satin_from_path(ring, spec, Micrometers{400}, true);
    REQUIRE(params.has_value());
    REQUIRE(params->border.has_value());
    const auto seq = generate_sequence(satin_project(*params));
    REQUIRE(seq.has_value());
    const auto bars = top_bars(*seq);
    // Densite : circonference / 0,4 mm, a 15 % pres.
    const double expected = 2.0 * std::numbers::pi * r / 400.0;
    CHECK(static_cast<double>(bars.size()) > expected * 0.85);
    CHECK(static_cast<double>(bars.size()) < expected * 1.15);
    for (std::size_t i = 0; i < bars.size(); ++i) {
        const Bar& b = bars[i];
        // Tous les points restent dans la couronne [r - 1,5 ; r + 1,5] mm (+ fleche du polygone).
        for (const Vec2um pt : {b.a, b.b}) {
            const double rad =
                std::hypot(static_cast<double>(pt.x.value), static_cast<double>(pt.y.value));
            CHECK(rad >= r - 1'500.0 - 120.0);
            CHECK(rad <= r + 1'500.0 + 5.0);
        }
        // Barreau radial : perpendiculaire a la tangente.
        const Vec2um mid{Micrometers{(b.a.x.value + b.b.x.value) / 2},
                         Micrometers{(b.a.y.value + b.b.y.value) / 2}};
        const double bx = static_cast<double>(b.b.x.value - b.a.x.value);
        const double by = static_cast<double>(b.b.y.value - b.a.y.value);
        const double bl = std::hypot(bx, by);
        const double ml =
            std::hypot(static_cast<double>(mid.x.value), static_cast<double>(mid.y.value));
        REQUIRE(bl > 100.0);
        const double cosAngle =
            std::abs(bx * mid.x.value + by * mid.y.value) / (bl * std::max(ml, 1.0));
        CHECK(cosAngle > std::cos(8.0 * std::numbers::pi / 180.0));
        // Pas de croisement avec les deux barreaux suivants.
        for (std::size_t k = 1; k <= 2 && i + k < bars.size(); ++k) {
            CHECK_FALSE(segments_cross(b.a, b.b, bars[i + k].a, bars[i + k].b));
        }
    }
    // Longueur des barreaux = largeur demandee (+- fleche).
    for (const Bar& b : bars) {
        CHECK(dist_um(b.a, b.b) == Catch::Approx(3'000.0).margin(150.0));
    }
}

TEST_CASE("bordure : cote interieur et exterieur d'un anneau, sens horaire ou non") {
    const double r = 10'000.0;
    for (const bool ccw : {true, false}) {
        const auto ring = circle(0, 0, r, 72, ccw);
        for (const auto side : {document::BorderSide::Inside, document::BorderSide::Outside}) {
            document::BorderSatinSpec spec;
            spec.width = Micrometers{2'000};
            spec.side = side;
            const bool leftIsInside = ring_left_is_inside(ring, false);
            const auto params = border_satin_from_path(ring, spec, Micrometers{400}, leftIsInside);
            REQUIRE(params.has_value());
            const auto seq = generate_sequence(satin_project(*params));
            REQUIRE(seq.has_value());
            for (const Vec2um pt : pass_points(*seq, Pass::TopStitch)) {
                const double rad =
                    std::hypot(static_cast<double>(pt.x.value), static_cast<double>(pt.y.value));
                if (side == document::BorderSide::Inside) {
                    CHECK(rad <= r + 5.0);
                    CHECK(rad >= r - 2'000.0 - 120.0);
                } else {
                    CHECK(rad >= r - 120.0);
                    CHECK(rad <= r + 2'000.0 + 5.0);
                }
            }
        }
    }
}

TEST_CASE("bordure : trou d'une region, cote matiere") {
    geometry::PathSet region;
    region.outer = rect_at(-20'000, -20'000, 40'000, 40'000);
    region.holes.push_back(circle(0, 0, 8'000.0, 64, false));
    document::BorderSatinSpec spec;
    spec.width = Micrometers{2'000};
    spec.side = document::BorderSide::Inside; // dans la matiere
    const auto all = border_satin_from_region(region, spec);
    REQUIRE(all.size() == 2);
    CHECK(all[0].border->ring == 0);
    CHECK(all[1].border->ring == 1);
    const auto seq = generate_sequence(satin_project(all[1]));
    REQUIRE(seq.has_value());
    for (const Vec2um pt : pass_points(*seq, Pass::TopStitch)) {
        const double rad =
            std::hypot(static_cast<double>(pt.x.value), static_cast<double>(pt.y.value));
        CHECK(rad >= 8'000.0 - 120.0); // jamais dans le trou
        CHECK(rad <= 10'000.0 + 5.0);
    }
}

TEST_CASE("bordure : coins vifs contre arrondis sur un carre") {
    const auto square = rect_at(0, 0, 20'000, 20'000);
    const std::vector<Vec2um> outline = {um(0, 0), um(20'000, 0), um(20'000, 20'000),
                                         um(0, 20'000)};
    const auto maxDist = [&](document::BorderCorner corner) {
        document::BorderSatinSpec spec;
        spec.width = Micrometers{3'000};
        spec.corner = corner;
        const auto params = border_satin_from_path(square, spec, Micrometers{400},
                                                   ring_left_is_inside(square, false));
        REQUIRE(params.has_value());
        const auto seq = generate_sequence(satin_project(*params));
        REQUIRE(seq.has_value());
        double worst = 0.0;
        const auto top = pass_points(*seq, Pass::TopStitch);
        REQUIRE_FALSE(top.empty());
        for (const Vec2um pt : top) {
            worst = std::max(worst, dist_to_polyline(outline, true, pt));
        }
        return worst;
    };
    const double sharp = maxDist(document::BorderCorner::Sharp);
    const double round = maxDist(document::BorderCorner::Round);
    CHECK(sharp == Catch::Approx(1'500.0 * std::sqrt(2.0)).margin(30.0)); // onglet
    CHECK(round <= 1'500.0 + 20.0);                                       // arc de rayon w/2
    // Les rails ne se croisent pas, y compris dans les coins.
    for (const auto corner : {document::BorderCorner::Sharp, document::BorderCorner::Round}) {
        document::BorderSatinSpec spec;
        spec.corner = corner;
        const auto params = border_satin_from_path(square, spec);
        REQUIRE(params.has_value());
        CHECK_FALSE(
            geometry::polylines_cross(geometry::flatten(params->rail_a, Micrometers{50}).points,
                                      geometry::flatten(params->rail_b, Micrometers{50}).points));
    }
}

TEST_CASE("bordure : trace en S ouvert, regulier sans croisement") {
    geometry::Path s;
    s.closed = false;
    std::vector<Vec2um> centre;
    for (int i = 0; i <= 80; ++i) {
        const double x = i * 500.0; // 40 mm de long
        const double y = 6'000.0 * std::sin(x / 6'000.0);
        s.nodes.push_back(node(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)));
        centre.push_back(s.nodes.back().pos);
    }
    document::BorderSatinSpec spec;
    spec.width = Micrometers{2'000};
    const auto params = border_satin_from_path(s, spec);
    REQUIRE(params.has_value());
    const auto seq = generate_sequence(satin_project(*params));
    REQUIRE(seq.has_value());
    const auto bars = top_bars(*seq);
    REQUIRE(bars.size() > 60);
    for (std::size_t i = 0; i < bars.size(); ++i) {
        for (const Vec2um pt : {bars[i].a, bars[i].b}) {
            CHECK(dist_to_polyline(centre, false, pt) <= 1'000.0 + 60.0);
        }
        CHECK(dist_um(bars[i].a, bars[i].b) == Catch::Approx(2'000.0).margin(200.0));
        for (std::size_t k = 1; k <= 2 && i + k < bars.size(); ++k) {
            CHECK_FALSE(segments_cross(bars[i].a, bars[i].b, bars[i + k].a, bars[i + k].b));
        }
    }
    // Extremites : barreau perpendiculaire a la tangente de depart.
    const double tx = static_cast<double>(centre[1].x.value - centre[0].x.value);
    const double ty = static_cast<double>(centre[1].y.value - centre[0].y.value);
    const double bx = static_cast<double>(bars.front().b.x.value - bars.front().a.x.value);
    const double by = static_cast<double>(bars.front().b.y.value - bars.front().a.y.value);
    CHECK(std::abs(tx * bx + ty * by) / (std::hypot(tx, ty) * std::hypot(bx, by)) < 0.12);
}

TEST_CASE("bordure : cas limites (largeur bornee, trace degenere, determinisme)") {
    document::BorderSatinSpec spec;
    spec.width = Micrometers{10}; // sous le minimum
    const auto narrow = border_satin_from_path(rect_at(0, 0, 10'000, 10'000), spec);
    REQUIRE(narrow.has_value());
    CHECK(dist_um(narrow->rungs.front().a, narrow->rungs.front().b) >= 400.0);
    CHECK(clamp_border_width(Micrometers{99'000}).value == 20'000);

    geometry::Path point;
    point.closed = false;
    point.nodes = {node(0, 0)};
    CHECK_FALSE(border_satin_from_path(point, spec).has_value());
    geometry::Path twice;
    twice.closed = true;
    twice.nodes = {node(0, 0), node(5, 5)}; // 2 points distincts a < 20 um : degenere
    CHECK_FALSE(border_satin_from_path(twice, spec).has_value());

    // Petit cercle : bordure plus large que le rayon, aucun plantage et reste fini.
    spec.width = Micrometers{6'000};
    const auto tiny = border_satin_from_path(circle(0, 0, 1'500.0, 24), spec);
    REQUIRE(tiny.has_value());
    const auto seq = generate_sequence(satin_project(*tiny));
    CHECK(seq.has_value());

    // Determinisme octet a octet.
    const auto a = border_satin_from_path(circle(0, 0, 7'000.0, 48), spec);
    const auto b = border_satin_from_path(circle(0, 0, 7'000.0, 48), spec);
    REQUIRE(a.has_value());
    CHECK(*a == *b);
}

TEST_CASE("bordure : regeneration conserve les reglages, change la geometrie") {
    geometry::PathSet region{circle(0, 0, 10'000.0, 48), {}};
    document::BorderSatinSpec spec;
    spec.width = Micrometers{2'000};
    auto created = border_satin_from_paths({region}, spec);
    REQUIRE(created.size() == 1);
    created[0].lock_start = document::SatinLock::BackAndForth;
    created[0].density = Micrometers{350};
    created[0].underlay_edge = true;
    auto wider = spec;
    wider.width = Micrometers{4'000};
    wider.corner = document::BorderCorner::Round;
    const auto regenerated = regenerate_border_satin({region}, created[0], wider);
    REQUIRE(regenerated.has_value());
    CHECK(regenerated->lock_start == document::SatinLock::BackAndForth);
    CHECK(regenerated->density.value == 350);
    CHECK(regenerated->underlay_edge);
    CHECK(regenerated->border->width.value == 4'000);
    CHECK(dist_um(regenerated->rungs.front().a, regenerated->rungs.front().b) ==
          Catch::Approx(4'000.0).margin(100.0));
    // Anneau disparu : echec propre.
    auto hole = created[0];
    hole.border->ring = 3;
    CHECK_FALSE(regenerate_border_satin({region}, hole, wider).has_value());
    CHECK_FALSE(regenerate_border_satin({}, created[0], wider).has_value());
}

TEST_CASE("bordure : un satin de bordure n'est jamais route avec ses voisins") {
    document::Project project;
    geometry::PathSet region;
    region.outer = rect_at(-20'000, -20'000, 40'000, 40'000);
    region.holes.push_back(circle(0, 0, 8'000.0, 48, false));
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(region);
    project.vector_objects.push_back(vec);
    for (auto& sp : border_satin_from_region(region, document::BorderSatinSpec{})) {
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.source_vector = vec.id;
        emb.rgb = {1, 2, 3};
        emb.params = sp;
        project.embroidery_objects.push_back(emb);
    }
    REQUIRE(project.embroidery_objects.size() == 2);
    const auto seq = generate_sequence(project);
    REQUIRE(seq.has_value());
    // Pas de liaison cousue cachee entre les deux anneaux.
    CHECK(count_pass(*seq, Pass::Travel) == 0);
}
