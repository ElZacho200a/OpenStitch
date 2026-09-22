// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

#include "openstitch/stitch_generation/directional_fill.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

constexpr double kPi = std::numbers::pi;

Vec2um um(double xMm, double yMm) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(xMm * 1000.0))},
                  Micrometers{static_cast<std::int32_t>(std::lround(yMm * 1000.0))}};
}

geometry::Path polygon(const std::vector<Vec2um>& pts, bool closed = true) {
    geometry::Path p;
    p.closed = closed;
    for (const Vec2um v : pts) {
        p.nodes.push_back({v, geometry::NodeType::Corner, {}, {}});
    }
    return p;
}

geometry::PathSet rect_mm(double x0, double y0, double x1, double y1) {
    return geometry::PathSet{polygon({um(x0, y0), um(x1, y0), um(x1, y1), um(x0, y1)}), {}};
}

geometry::Path line_mm(double x0, double y0, double x1, double y1) {
    return polygon({um(x0, y0), um(x1, y1)}, false);
}

document::DirectionalFillParams base_params() {
    document::DirectionalFillParams p;
    p.row_spacing = Micrometers{400};
    p.stitch_length = Micrometers{3'000};
    p.inset = Micrometers{0};
    p.seed = 1234;
    return p;
}

double seg_len(Vec2um a, Vec2um b) {
    return length_um(b - a);
}

// Écart entre deux orientations (modulo pi), en degrés, dans [0, 90].
double orientation_gap_deg(double a, double b) {
    double d = std::fmod(std::abs(a - b), kPi);
    d = std::min(d, kPi - d);
    return d * 180.0 / kPi;
}

double chord_angle(Vec2um a, Vec2um b) {
    return std::atan2(static_cast<double>(b.y.value - a.y.value),
                      static_cast<double>(b.x.value - a.x.value));
}

// Fil de la couche supérieure : somme des segments arrivant sur un point cousu
// (ni saut, ni déplacement caché).
double sewn_length(const std::vector<FillStitch>& fill) {
    double total = 0.0;
    for (std::size_t i = 1; i < fill.size(); ++i) {
        if (!fill[i].jump && !fill[i].travel) {
            total += seg_len(fill[i - 1].pos, fill[i].pos);
        }
    }
    return total;
}

// Grille de voisinage pour les mesures de distance entre points (tests).
class PointBuckets {
public:
    PointBuckets(double cell) : cell_(cell) {}
    void add(Vec2um p, std::size_t tag) {
        buckets_.push_back({key(p), tag, p});
    }
    void finalize() {
        std::sort(buckets_.begin(), buckets_.end(),
                  [](const Entry& a, const Entry& b) { return a.k < b.k; });
    }
    // Distance minimale de q aux points (d'étiquette != exclude) à moins de r.
    [[nodiscard]] double nearest(Vec2um q, double r, std::optional<std::size_t> exclude) const {
        double best = std::numeric_limits<double>::max();
        const auto [ci, cj] = cellOf(q);
        const int reach = static_cast<int>(std::ceil(r / cell_));
        for (int dj = -reach; dj <= reach; ++dj) {
            for (int di = -reach; di <= reach; ++di) {
                const std::int64_t k = pack(ci + di, cj + dj);
                auto it = std::lower_bound(buckets_.begin(), buckets_.end(), k,
                                           [](const Entry& e, std::int64_t v) { return e.k < v; });
                for (; it != buckets_.end() && it->k == k; ++it) {
                    if (exclude && it->tag == *exclude) {
                        continue;
                    }
                    best = std::min(best, seg_len(q, it->p));
                }
            }
        }
        return best;
    }

private:
    struct Entry {
        std::int64_t k;
        std::size_t tag;
        Vec2um p;
    };
    [[nodiscard]] std::pair<int, int> cellOf(Vec2um p) const {
        return {static_cast<int>(std::floor(p.x.value / cell_)),
                static_cast<int>(std::floor(p.y.value / cell_))};
    }
    static std::int64_t pack(int i, int j) {
        return (static_cast<std::int64_t>(i) << 32) ^ static_cast<std::uint32_t>(j);
    }
    [[nodiscard]] std::int64_t key(Vec2um p) const {
        const auto [i, j] = cellOf(p);
        return pack(i, j);
    }
    double cell_;
    std::vector<Entry> buckets_;
};

} // namespace

// --- Phase 1 : champ et lignes de courant ------------------------------------

TEST_CASE("directional: horizontal guide matches tatami at 0 deg within 5 percent") {
    const auto region = rect_mm(0, 0, 30, 20);
    auto dp = base_params();
    dp.guides.push_back(line_mm(2, 10, 28, 10));

    document::TatamiParams tp;
    tp.row_spacing = Micrometers{400};
    tp.stitch_length = Micrometers{3'000};
    tp.angle = Angle{0.0};
    tp.inset = Micrometers{0};

    const auto directional = fill_directional(region, dp);
    const auto tatami = fill_tatami(region, tp);
    REQUIRE_FALSE(directional.empty());
    const double dLen = sewn_length(directional);
    const double tLen = sewn_length(tatami);
    CHECK(std::abs(dLen - tLen) / tLen <= 0.05);

    // Toutes les lignes sont horizontales.
    for (const auto& line : directional_stitch_lines(region, dp)) {
        for (std::size_t i = 1; i < line.size(); ++i) {
            CHECK(orientation_gap_deg(chord_angle(line[i - 1], line[i]), 0.0) < 1.0);
        }
    }
}

TEST_CASE("directional: arc guide keeps every stitch within 10 deg of the field") {
    const auto region = rect_mm(-15, 0, 15, 20);
    auto dp = base_params();
    // Arc de cercle de centre (0, -30 mm), rayon 40 mm, de 60 à 120 degrés.
    geometry::Path arc;
    arc.closed = false;
    for (int k = 0; k <= 40; ++k) {
        const double a = (60.0 + 60.0 * k / 40.0) * kPi / 180.0;
        arc.nodes.push_back({um(40.0 * std::cos(a), -30.0 + 40.0 * std::sin(a)),
                             geometry::NodeType::Corner, {}, {}});
    }
    dp.guides.push_back(arc);

    const auto lines = directional_stitch_lines(region, dp);
    REQUIRE(lines.size() > 20);
    std::vector<Vec2um> mids;
    std::vector<double> angles;
    for (const auto& line : lines) {
        for (std::size_t i = 1; i < line.size(); ++i) {
            mids.push_back(Vec2um{Micrometers{(line[i - 1].x.value + line[i].x.value) / 2},
                                  Micrometers{(line[i - 1].y.value + line[i].y.value) / 2}});
            angles.push_back(chord_angle(line[i - 1], line[i]));
        }
    }
    const auto field = directional_field_at(region, dp, mids);
    std::size_t checked = 0;
    for (std::size_t i = 0; i < mids.size(); ++i) {
        if (!field[i]) {
            continue; // milieu de corde exactement sur le bord
        }
        ++checked;
        CHECK(orientation_gap_deg(angles[i], field[i]->radians) < 10.0);
    }
    CHECK(checked > mids.size() * 9 / 10);
}

TEST_CASE("directional: converging guides keep lines separated without empty zones") {
    const auto region = rect_mm(0, 0, 40, 20);
    auto dp = base_params();
    dp.guides.push_back(line_mm(0, 18, 40, 12));
    dp.guides.push_back(line_mm(0, 2, 40, 8));
    const double spacing = 400.0;

    const auto lines = trace_directional_streamlines(region, dp);
    REQUIRE(lines.size() > 10);
    PointBuckets buckets(spacing);
    for (std::size_t li = 0; li < lines.size(); ++li) {
        for (const Vec2um p : lines[li].points) {
            buckets.add(p, li);
        }
    }
    buckets.finalize();

    // Aucune paire de lignes plus proche que 0,5 x espacement.
    double minSep = std::numeric_limits<double>::max();
    for (std::size_t li = 0; li < lines.size(); ++li) {
        for (const Vec2um p : lines[li].points) {
            minSep = std::min(minSep, buckets.nearest(p, spacing, li));
        }
    }
    CHECK(minSep >= 0.5 * spacing - 2.0); // 2 µm : arrondi au micromètre

    // Aucune zone vide plus large que 2 x espacement : tout point intérieur
    // est à moins d'un espacement d'une ligne (échantillonnage des lignes au
    // pas 0,2 x espacement : tolérance d'un demi-pas).
    double worst = 0.0;
    for (double y = 0.4; y <= 19.6; y += 0.2) {
        for (double x = 0.4; x <= 39.6; x += 0.2) {
            worst = std::max(worst, buckets.nearest(um(x, y), 3.0 * spacing, std::nullopt));
        }
    }
    CHECK(worst <= spacing + 0.1 * spacing);
}

TEST_CASE("directional: without guides the field follows the principal axis") {
    const auto region = rect_mm(0, 0, 40, 10);
    const auto dp = base_params();
    const auto field = directional_field_at(region, dp, {um(20, 5), um(5, 2), um(35, 8)});
    for (const auto& a : field) {
        REQUIRE(a.has_value());
        CHECK(orientation_gap_deg(a->radians, 0.0) < 1.0);
    }
}

TEST_CASE("directional: edge weight bends the field along the nearest border") {
    const auto region = rect_mm(0, 0, 30, 30);
    auto dp = base_params();
    dp.guides.push_back(line_mm(5, 15, 25, 15)); // horizontal
    dp.edge_weight = 1.0;
    // Tout près du bord gauche (vertical), le bord domine ; au centre, le guide.
    const auto field = directional_field_at(region, dp, {um(0.2, 15), um(15, 15)});
    REQUIRE(field[0].has_value());
    REQUIRE(field[1].has_value());
    CHECK(orientation_gap_deg(field[0]->radians, kPi / 2.0) < 20.0);
    CHECK(orientation_gap_deg(field[1]->radians, 0.0) < 10.0);
}

TEST_CASE("directional: stitches stay inside the region and avoid holes") {
    geometry::PathSet region = rect_mm(0, 0, 30, 20);
    region.holes.push_back(polygon({um(12, 7), um(12, 13), um(18, 13), um(18, 7)}));
    auto dp = base_params();
    dp.guides.push_back(line_mm(0, 0, 30, 20)); // diagonale
    const auto fill = fill_directional(region, dp);
    REQUIRE(fill.size() > 100);
    for (std::size_t i = 1; i < fill.size(); ++i) {
        if (fill[i].jump) {
            continue;
        }
        CHECK(segment_stays_in_region(region, fill[i - 1].pos, fill[i].pos));
    }
}

TEST_CASE("directional: stitch lengths respect the 0.5 mm minimum and 7 mm maximum") {
    const auto region = rect_mm(0, 0, 40, 20);
    auto dp = base_params();
    dp.stitch_length = Micrometers{12'000}; // bornée à 7 mm
    dp.guides.push_back(line_mm(0, 5, 40, 15));
    for (const auto& line : directional_stitch_lines(region, dp)) {
        for (std::size_t i = 1; i < line.size(); ++i) {
            const double l = seg_len(line[i - 1], line[i]);
            CHECK(l >= 500.0 - 2.0);
            CHECK(l <= 7'000.0 + 2.0);
        }
    }
}

TEST_CASE("directional: conversion from tatami keeps the tatami orientation") {
    const auto region = rect_mm(0, 0, 30, 20);
    document::TatamiParams tp;
    tp.angle = Angle{30.0 * kPi / 180.0};
    tp.row_spacing = Micrometers{450};
    tp.underlay_edge = true;
    const auto dp = directional_from_tatami(tp, {region}, 42);
    CHECK(dp.row_spacing == tp.row_spacing);
    CHECK(dp.underlay_edge);
    CHECK(dp.seed == 42);
    REQUIRE(dp.guides.size() == 1);
    const auto field = directional_field_at(region, dp, {um(15, 10), um(3, 3)});
    REQUIRE(field[0].has_value());
    CHECK(orientation_gap_deg(field[0]->radians, tp.angle.radians) < 1.0);
    CHECK(orientation_gap_deg(field[1]->radians, tp.angle.radians) < 1.0);
}

TEST_CASE("directional: underlay reuses tatami underlay across the mean direction") {
    const auto region = rect_mm(0, 0, 30, 20);
    auto dp = base_params();
    dp.guides.push_back(line_mm(2, 10, 28, 10));
    CHECK(directional_underlay(region, dp).empty());
    dp.underlay_parallel = true;
    const auto passes = directional_underlay(region, dp);
    REQUIRE_FALSE(passes.empty());
    // Direction moyenne horizontale -> rangées de sous-couche verticales
    // (points de 3 mm ; les liaisons de rangée, 2 mm, sont ignorées).
    for (const auto& pass : passes) {
        for (std::size_t i = 1; i < pass.size(); ++i) {
            if (seg_len(pass[i - 1], pass[i]) > 2'500.0) {
                CHECK(orientation_gap_deg(chord_angle(pass[i - 1], pass[i]), kPi / 2.0) < 1.0);
            }
        }
    }
}

TEST_CASE("directional: generation is byte for byte deterministic") {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    geometry::PathSet shape = rect_mm(0, 0, 25, 18);
    shape.holes.push_back(polygon({um(10, 6), um(10, 10), um(14, 10), um(14, 6)}));
    vec.paths.push_back(shape);
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    auto dp = base_params();
    dp.inset = Micrometers{200};
    dp.guides.push_back(line_mm(0, 0, 25, 18));
    dp.break_lines.push_back(line_mm(12.5, 0, 12.5, 18));
    dp.handmade = true;
    dp.handmade_intensity = 70;
    dp.underlay_edge = true;
    dp.underlay_parallel = true;
    emb.params = dp;
    project.embroidery_objects.push_back(emb);

    const auto a = generate_sequence(project);
    const auto b = generate_sequence(project);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->commands.size() > 100);
    CHECK(a->commands == b->commands);
}

// --- Phase 2 : secteurs --------------------------------------------------------

TEST_CASE("directional: break line gives independent sectors (chevron)") {
    const auto region = rect_mm(0, 0, 40, 20);
    auto dp = base_params();
    dp.guides.push_back(line_mm(2, 2, 18, 18));  // +45 deg a gauche
    dp.guides.push_back(line_mm(22, 18, 38, 2)); // -45 deg a droite
    dp.break_lines.push_back(line_mm(20, 0.5, 20, 19.5)); // prolongée jusqu'aux bords
    dp.sector_overlap = Micrometers{250};

    // Champ discontinu de part et d'autre de la rupture.
    const auto field = directional_field_at(region, dp, {um(19.5, 10), um(20.5, 10)});
    REQUIRE(field[0].has_value());
    REQUIRE(field[1].has_value());
    CHECK(orientation_gap_deg(field[0]->radians, kPi / 4.0) < 5.0);
    CHECK(orientation_gap_deg(field[1]->radians, 3.0 * kPi / 4.0) < 5.0);

    // Chaque ligne tracée reste dans son secteur, au chevauchement près.
    const auto lines = trace_directional_streamlines(region, dp);
    std::size_t maxSector = 0;
    for (const auto& line : lines) {
        maxSector = std::max(maxSector, line.sector);
        double sumX = 0.0;
        for (const Vec2um p : line.points) {
            sumX += p.x.value;
        }
        const bool left = sumX / static_cast<double>(line.points.size()) < 20'000.0;
        for (const Vec2um p : line.points) {
            if (left) {
                CHECK(p.x.value <= 20'000 + 250 + 60);
            } else {
                CHECK(p.x.value >= 20'000 - 250 - 60);
            }
        }
    }
    CHECK(maxSector == 1); // deux secteurs

    // Directions des points cousus : 45 deg à gauche, 135 deg à droite.
    for (const auto& line : directional_stitch_lines(region, dp)) {
        for (std::size_t i = 1; i < line.size(); ++i) {
            const double mx = (line[i - 1].x.value + line[i].x.value) / 2.0;
            if (mx < 19'000.0) {
                CHECK(orientation_gap_deg(chord_angle(line[i - 1], line[i]), kPi / 4.0) < 10.0);
            } else if (mx > 21'000.0) {
                CHECK(orientation_gap_deg(chord_angle(line[i - 1], line[i]), 3.0 * kPi / 4.0) <
                      10.0);
            }
        }
    }

    // Chaque secteur est parcouru d'un seul tenant : une seule transition
    // gauche -> droite (ou l'inverse) dans l'ordre de couture.
    const auto fill = fill_directional(region, dp);
    int side = 0;
    int transitions = 0;
    for (const auto& fs : fill) {
        const int s = fs.pos.x.value < 19'500 ? -1 : fs.pos.x.value > 20'500 ? 1 : 0;
        if (s != 0 && side != 0 && s != side) {
            ++transitions;
        }
        if (s != 0) {
            side = s;
        }
    }
    CHECK(transitions == 1);
}

TEST_CASE("directional: sector overlap covers the break line") {
    const auto region = rect_mm(0, 0, 40, 20);
    auto dp = base_params();
    dp.guides.push_back(line_mm(2, 2, 18, 18));
    dp.guides.push_back(line_mm(22, 18, 38, 2));
    dp.break_lines.push_back(line_mm(20, 0.5, 20, 19.5));
    dp.sector_overlap = Micrometers{300};
    // Des lignes des deux secteurs franchissent la rupture (chevauchement).
    bool leftCrosses = false;
    bool rightCrosses = false;
    for (const auto& line : trace_directional_streamlines(region, dp)) {
        double sumX = 0.0;
        double minX = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest();
        for (const Vec2um p : line.points) {
            sumX += p.x.value;
            minX = std::min(minX, static_cast<double>(p.x.value));
            maxX = std::max(maxX, static_cast<double>(p.x.value));
        }
        if (sumX / static_cast<double>(line.points.size()) < 20'000.0) {
            leftCrosses = leftCrosses || maxX > 20'150.0;
        } else {
            rightCrosses = rightCrosses || minX < 19'850.0;
        }
    }
    CHECK(leftCrosses);
    CHECK(rightCrosses);
}

// --- Phase 3 : aspect fait main --------------------------------------------------

TEST_CASE("directional: handmade at zero intensity equals regular output") {
    const auto region = rect_mm(0, 0, 30, 20);
    auto dp = base_params();
    dp.guides.push_back(line_mm(0, 0, 30, 20));
    const auto regular = fill_directional(region, dp);
    dp.handmade = true;
    dp.handmade_intensity = 0;
    CHECK(fill_directional(region, dp) == regular);
}

TEST_CASE("directional: handmade varies stitch lengths within 1 to 7 mm") {
    const auto region = rect_mm(0, 0, 40, 20);
    auto dp = base_params();
    dp.guides.push_back(line_mm(2, 10, 38, 10));
    dp.handmade = true;
    dp.handmade_intensity = 100;
    const auto lines = directional_stitch_lines(region, dp);
    REQUIRE(lines.size() > 20);
    double minInterior = std::numeric_limits<double>::max();
    double maxInterior = 0.0;
    std::vector<double> firstLens;
    for (const auto& line : lines) {
        for (std::size_t i = 1; i < line.size(); ++i) {
            const double l = seg_len(line[i - 1], line[i]);
            CHECK(l >= 500.0 - 2.0);
            CHECK(l <= 7'000.0 + 2.0);
            const bool interior = i >= 2 && i + 1 < line.size();
            if (interior) {
                minInterior = std::min(minInterior, l);
                maxInterior = std::max(maxInterior, l);
            }
        }
        if (line.size() >= 4) {
            firstLens.push_back(seg_len(line[0], line[1]));
        }
    }
    CHECK(minInterior >= 1'000.0 - 2.0);
    CHECK(minInterior < 0.8 * 3'000.0);
    CHECK(maxInterior > 1.2 * 3'000.0);
    // Alternance court/long en début de ligne.
    const auto shortCount = std::count_if(firstLens.begin(), firstLens.end(),
                                          [](double l) { return l < 0.6 * 3'000.0; });
    const auto longCount = std::count_if(firstLens.begin(), firstLens.end(),
                                         [](double l) { return l > 0.8 * 3'000.0; });
    CHECK(shortCount > 5);
    CHECK(longCount > 5);
}

TEST_CASE("directional: handmade result depends only on the stored seed") {
    const auto region = rect_mm(0, 0, 30, 20);
    auto dp = base_params();
    dp.guides.push_back(line_mm(0, 5, 30, 15));
    dp.handmade = true;
    dp.handmade_intensity = 80;
    const auto a = fill_directional(region, dp);
    CHECK(fill_directional(region, dp) == a);
    dp.seed = 99;
    CHECK(fill_directional(region, dp) != a);
}

TEST_CASE("directional: handmade direction wobble is small and smooth") {
    const auto region = rect_mm(0, 0, 40, 30);
    auto dp = base_params();
    dp.guides.push_back(line_mm(2, 15, 38, 15));
    std::vector<Vec2um> pts;
    for (double y = 2.0; y <= 28.0; y += 2.0) {
        for (double x = 2.0; x <= 38.0; x += 2.0) {
            pts.push_back(um(x, y));
            pts.push_back(um(x + 0.2, y));
        }
    }
    const auto calm = directional_field_at(region, dp, pts);
    dp.handmade = true;
    dp.handmade_intensity = 100;
    const auto wobbly = directional_field_at(region, dp, pts);
    double maxGap = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        REQUIRE(calm[i].has_value());
        REQUIRE(wobbly[i].has_value());
        const double gap = orientation_gap_deg(calm[i]->radians, wobbly[i]->radians);
        CHECK(gap <= 6.0 + 0.5);
        maxGap = std::max(maxGap, gap);
    }
    CHECK(maxGap > 1.0);
    // Bruit lissé : deux points distants de 0,2 mm ont presque la même
    // perturbation. Pente maximale théorique : 6° x 2 x 1,5 (smoothstep) / 5 mm
    // = 3,6°/mm, soit 0,72° sur 0,2 mm — un tirage point par point atteindrait 12°.
    for (std::size_t i = 0; i + 1 < pts.size(); i += 2) {
        const double d1 = std::remainder(wobbly[i]->radians - calm[i]->radians, kPi);
        const double d2 = std::remainder(wobbly[i + 1]->radians - calm[i + 1]->radians, kPi);
        CHECK(std::abs(d1 - d2) * 180.0 / kPi < 0.75);
    }
}

TEST_CASE("directional: convex shape is sewn without any jump") {
    const auto region = rect_mm(-15, 0, 15, 20);
    auto dp = base_params();
    geometry::Path arc;
    arc.closed = false;
    for (int k = 0; k <= 20; ++k) {
        const double a = (60.0 + 60.0 * k / 20.0) * kPi / 180.0;
        arc.nodes.push_back({um(40.0 * std::cos(a), -30.0 + 40.0 * std::sin(a)),
                             geometry::NodeType::Corner, {}, {}});
    }
    dp.guides.push_back(arc);
    const auto fill = fill_directional(region, dp);
    REQUIRE(fill.size() > 100);
    CHECK(fill.front().jump);
    const auto jumps = std::count_if(fill.begin() + 1, fill.end(),
                                     [](const FillStitch& fs) { return fs.jump; });
    CHECK(jumps == 0);
    // Sans trajet caché, les liaisons longues redeviennent des sauts.
    dp.hidden_underpath = false;
    const auto raw = fill_directional(region, dp);
    const auto rawJumps = std::count_if(raw.begin() + 1, raw.end(),
                                        [](const FillStitch& fs) { return fs.jump; });
    CHECK(rawJumps >= jumps);
}
