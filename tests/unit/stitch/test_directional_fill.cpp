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
    void add(Vec2um p, std::size_t tag) { buckets_.push_back({key(p), tag, p}); }
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

TEST_CASE("directional guides: medial skeleton proposes one editable guide") {
    const auto region = rect_mm(0, 0, 40, 10);
    DirectionalGuideOptions options;
    options.node_spacing = Micrometers{5'000};

    const auto guides = directional_guides_from_region(region, options);

    REQUIRE(guides.size() == 1);
    const auto& guide = guides.front();
    REQUIRE_FALSE(guide.closed);
    REQUIRE(guide.nodes.size() >= 2);

    std::int32_t minX = guide.nodes.front().pos.x.value;
    std::int32_t maxX = minX;
    std::int32_t minY = guide.nodes.front().pos.y.value;
    std::int32_t maxY = minY;
    for (const auto& node : guide.nodes) {
        minX = std::min(minX, node.pos.x.value);
        maxX = std::max(maxX, node.pos.x.value);
        minY = std::min(minY, node.pos.y.value);
        maxY = std::max(maxY, node.pos.y.value);
    }
    CHECK(maxX - minX > 25'000);
    CHECK(maxY - minY < 3'000);
}

TEST_CASE("directional guides: minimum length can reject ambiguous tiny skeletons") {
    const auto region = rect_mm(0, 0, 4, 4);
    DirectionalGuideOptions options;
    options.min_path_length = Micrometers{10'000};

    CHECK(directional_guides_from_region(region, options).empty());
}

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
                             geometry::NodeType::Corner,
                             {},
                             {}});
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
    dp.guides.push_back(line_mm(2, 2, 18, 18));           // +45 deg a gauche
    dp.guides.push_back(line_mm(22, 18, 38, 2));          // -45 deg a droite
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
                             geometry::NodeType::Corner,
                             {},
                             {}});
    }
    dp.guides.push_back(arc);
    const auto fill = fill_directional(region, dp);
    REQUIRE(fill.size() > 100);
    CHECK(fill.front().jump);
    const auto jumps =
        std::count_if(fill.begin() + 1, fill.end(), [](const FillStitch& fs) { return fs.jump; });
    CHECK(jumps == 0);
    // Sans trajet caché, les liaisons longues redeviennent des sauts.
    dp.hidden_underpath = false;
    const auto raw = fill_directional(region, dp);
    const auto rawJumps =
        std::count_if(raw.begin() + 1, raw.end(), [](const FillStitch& fs) { return fs.jump; });
    CHECK(rawJumps >= jumps);
}

// --- Dégradé de densité ------------------------------------------------------

namespace {

// Ordonnées (µm) auxquelles les lignes de courant coupent la verticale x = xMm.
std::vector<double> crossings_at_x(const std::vector<DirectionalStreamline>& lines, double xMm) {
    const double target = xMm * 1000.0;
    std::vector<double> ys;
    for (const auto& line : lines) {
        double bestDx = std::numeric_limits<double>::max();
        double y = 0.0;
        for (const Vec2um p : line.points) {
            const double dx = std::abs(static_cast<double>(p.x.value) - target);
            if (dx < bestDx) {
                bestDx = dx;
                y = static_cast<double>(p.y.value);
            }
        }
        if (bestDx < 300.0) {
            ys.push_back(y);
        }
    }
    std::sort(ys.begin(), ys.end());
    return ys;
}

// Écart moyen entre lignes voisines dont les deux ordonnées sont dans [lo ; hi] (mm).
double mean_gap_mm(const std::vector<double>& ys, double loMm, double hiMm) {
    double sum = 0.0;
    int n = 0;
    for (std::size_t i = 1; i < ys.size(); ++i) {
        if (ys[i - 1] >= loMm * 1000.0 && ys[i] <= hiMm * 1000.0) {
            sum += ys[i] - ys[i - 1];
            ++n;
        }
    }
    return n > 0 ? sum / n / 1000.0 : 0.0;
}

document::DensityGradient vertical_gradient(Micrometers from, Micrometers to) {
    document::DensityGradient g;
    g.from = um(15, 0);
    g.to = um(15, 20);
    g.spacing_from = from;
    g.spacing_to = to;
    return g;
}

} // namespace

TEST_CASE("directional density gradient spreads rows towards the sparse end") {
    const auto region = rect_mm(0, 0, 30, 20);
    auto dp = base_params();
    dp.guides.push_back(line_mm(0, 10, 30, 10)); // champ horizontal
    dp.density_gradient = vertical_gradient(Micrometers{400}, Micrometers{1'600});

    const auto ys = crossings_at_x(trace_directional_streamlines(region, dp), 15.0);
    REQUIRE(ys.size() > 20);
    const double dense = mean_gap_mm(ys, 0.0, 7.0);
    const double sparse = mean_gap_mm(ys, 13.0, 20.0);
    REQUIRE(dense > 0.0);
    REQUIRE(sparse > 0.0);
    // Espacement local moyen attendu ~0,7 mm en bas, ~1,4 mm en haut.
    CHECK(sparse > 1.5 * dense);
    CHECK(dense > 0.4);  // jamais plus serré que l'écart local
    CHECK(sparse < 1.9); // ni plus lâche que le plus grand écart
    // Aucune zone vide : pas d'écart supérieur à 2 x l'écart maximal.
    for (std::size_t i = 1; i < ys.size(); ++i) {
        CHECK(ys[i] - ys[i - 1] < 3'200.0);
    }
}

TEST_CASE("directional density gradient is deterministic") {
    const auto region = rect_mm(0, 0, 30, 20);
    auto dp = base_params();
    dp.guides.push_back(line_mm(0, 10, 30, 10));
    dp.density_gradient = vertical_gradient(Micrometers{500}, Micrometers{1'200});
    const auto a = fill_directional(region, dp);
    const auto b = fill_directional(region, dp);
    REQUIRE(a.size() > 50);
    CHECK(a == b);
}

TEST_CASE("directional uniform gradient equals the plain spacing") {
    const auto region = rect_mm(0, 0, 30, 20);
    auto plain = base_params();
    plain.guides.push_back(line_mm(0, 10, 30, 10));
    auto graded = plain;
    graded.density_gradient = vertical_gradient(Micrometers{400}, Micrometers{400});
    const auto a = trace_directional_streamlines(region, plain);
    const auto b = trace_directional_streamlines(region, graded);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].points == b[i].points);
    }
}

TEST_CASE("directional gradient with a degenerate axis is uniform at spacing_from") {
    const auto region = rect_mm(0, 0, 30, 20);
    auto wide = base_params();
    wide.row_spacing = Micrometers{800};
    wide.guides.push_back(line_mm(0, 10, 30, 10));
    auto degenerate = base_params();
    degenerate.guides = wide.guides;
    document::DensityGradient g;
    g.from = um(5, 5);
    g.to = um(5, 5);
    g.spacing_from = Micrometers{800};
    g.spacing_to = Micrometers{200}; // ignoré : axe nul
    degenerate.density_gradient = g;
    const auto a = trace_directional_streamlines(region, wide);
    const auto b = trace_directional_streamlines(region, degenerate);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].points == b[i].points);
    }
}

TEST_CASE("density_gradient_across spans the shape along the requested direction") {
    const std::vector<geometry::PathSet> shape{rect_mm(0, 0, 30, 20)};
    const auto vertical =
        density_gradient_across(shape, Angle{kPi / 2.0}, Micrometers{400}, Micrometers{1'200});
    REQUIRE(vertical.has_value());
    CHECK(vertical->from == um(15, 0));
    CHECK(vertical->to == um(15, 20));
    CHECK(vertical->spacing_from == Micrometers{400});
    CHECK(vertical->spacing_to == Micrometers{1'200});

    const auto horizontal =
        density_gradient_across(shape, Angle{0.0}, Micrometers{400}, Micrometers{800});
    REQUIRE(horizontal.has_value());
    CHECK(horizontal->from == um(0, 10));
    CHECK(horizontal->to == um(30, 10));

    CHECK_FALSE(density_gradient_across({}, Angle{0.0}, Micrometers{400}, Micrometers{800}));
}

// --- Régularité de l'espacement ------------------------------------------------

namespace {

// Dispersion de la distance au voisin le plus proche d'une autre ligne,
// rapportée à l'écart attendu : 0 = espacement parfaitement régulier.
struct SpacingStats {
    double mean{0.0};
    double stddev{0.0};
    std::size_t samples{0};
};

SpacingStats spacing_stats(const std::vector<DirectionalStreamline>& lines, double spacingUm,
                           double marginUm) {
    PointBuckets buckets(spacingUm);
    for (std::size_t li = 0; li < lines.size(); ++li) {
        for (const Vec2um p : lines[li].points) {
            buckets.add(p, li);
        }
    }
    buckets.finalize();
    double sum = 0.0;
    double sum2 = 0.0;
    std::size_t n = 0;
    for (std::size_t li = 0; li < lines.size(); ++li) {
        for (std::size_t k = 0; k < lines[li].points.size(); k += 3) {
            const Vec2um p = lines[li].points[k];
            if (p.x.value < marginUm || p.y.value < marginUm || p.x.value > 30'000 - marginUm ||
                p.y.value > 20'000 - marginUm) {
                continue;
            }
            const double d = buckets.nearest(p, 2.0 * spacingUm, li) / spacingUm;
            if (d > 2.0) {
                continue; // aucune voisine à portée
            }
            sum += d;
            sum2 += d * d;
            ++n;
        }
    }
    SpacingStats s;
    s.samples = n;
    if (n > 0) {
        s.mean = sum / static_cast<double>(n);
        s.stddev = std::sqrt(std::max(0.0, sum2 / static_cast<double>(n) - s.mean * s.mean));
    }
    return s;
}

document::DirectionalFillParams fan_params() {
    auto dp = base_params();
    dp.guides.push_back(line_mm(0, 3, 30, 10));
    dp.guides.push_back(line_mm(0, 17, 30, 10));
    return dp;
}

} // namespace

// Écart moyen (degrés) entre la direction des arêtes tracées et le champ.
double mean_direction_gap_deg(const geometry::PathSet& region,
                              const document::DirectionalFillParams& dp,
                              const std::vector<DirectionalStreamline>& lines) {
    std::vector<Vec2um> mids;
    std::vector<double> angles;
    for (const auto& line : lines) {
        for (std::size_t k = 0; k + 1 < line.points.size(); k += 2) {
            const Vec2um a = line.points[k];
            const Vec2um b = line.points[k + 1];
            mids.push_back(Vec2um{Micrometers{(a.x.value + b.x.value) / 2},
                                  Micrometers{(a.y.value + b.y.value) / 2}});
            angles.push_back(chord_angle(a, b));
        }
    }
    const auto field = directional_field_at(region, dp, mids);
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < mids.size(); ++i) {
        if (field[i]) {
            sum += orientation_gap_deg(angles[i], field[i]->radians);
            ++n;
        }
    }
    return n > 0 ? sum / static_cast<double>(n) : 0.0;
}

TEST_CASE("directional spacing regularity evens out converging rows") {
    const auto region = rect_mm(0, 0, 30, 20);
    auto raw = fan_params();
    auto even = fan_params();
    even.spacing_regularity = 0.5;

    const auto rawLines = trace_directional_streamlines(region, raw);
    const auto evenLines = trace_directional_streamlines(region, even);
    // Même nombre de lignes : la régularisation déplace, elle n'en crée ni n'en retire.
    REQUIRE(rawLines.size() == evenLines.size());

    const auto rawStats = spacing_stats(rawLines, 400.0, 600.0);
    const auto evenStats = spacing_stats(evenLines, 400.0, 600.0);
    REQUIRE(rawStats.samples > 1'000);
    REQUIRE(evenStats.samples > 1'000);
    // Mesuré : 0,232 -> 0,158 (-32 %) ; on exige au moins -20 %.
    CHECK(evenStats.stddev < 0.8 * rawStats.stddev);
    CHECK(std::abs(evenStats.mean - 1.0) < 0.1); // l'écart moyen reste l'écart demandé

    // Le compromis est payé en direction, mais reste faible (mesuré : ~1,9 degré).
    CHECK(mean_direction_gap_deg(region, raw, rawLines) < 1.0);
    CHECK(mean_direction_gap_deg(region, even, evenLines) < 4.0);
}

TEST_CASE("directional density gradient keeps the row count at the integral of the density") {
    // Nombre de lignes théorique le long d'une verticale : l'intégrale de 1/s(y)
    // pour s linéaire de s0 à s1 sur 20 mm. Mesuré : 35/22/15 lignes pour
    // 34,7/23,1/15,5 attendues (écart < 5 %) — c'est ce que garantissent les
    // « graines par divergence » de Liu et al. ; le tracé actuel y suffit déjà
    // (cf. docs/source/directional-fill.md, alternatives écartées).
    const auto region = rect_mm(0, 0, 30, 20);
    for (const double endMm : {0.8, 1.6, 3.0}) {
        for (const double regularity : {0.0, 0.5}) {
            auto dp = base_params();
            dp.guides.push_back(line_mm(0, 10, 30, 10));
            dp.density_gradient = vertical_gradient(
                Micrometers{400}, Micrometers{static_cast<std::int32_t>(endMm * 1000)});
            dp.spacing_regularity = regularity;
            const auto ys = crossings_at_x(trace_directional_streamlines(region, dp), 15.0);
            const double s0 = 0.4;
            const double ideal = 20.0 / (endMm - s0) * std::log(endMm / s0);
            CHECK(std::abs(static_cast<double>(ys.size()) - ideal) <= 0.1 * ideal);
        }
    }
}

TEST_CASE("directional routing leaves at most two jumps on branching shapes") {
    // Mesuré avant d'écarter le parcours « arbre couvrant + profondeur d'abord »
    // de Liu et al. : 0 à 2 sauts (10 à 22 mm) sur U, T et L, que l'arbre
    // remplacerait par la même distance cousue en retour (docs/source/
    // directional-fill.md, alternatives écartées). Garde-fou de non-régression.
    struct Shape {
        const char* name;
        std::vector<Vec2um> outer;
        std::vector<geometry::Path> guides;
    };
    const std::vector<Shape> shapes{
        {"U vertical rows",
         {um(0, 0), um(30, 0), um(30, 20), um(20, 20), um(20, 6), um(10, 6), um(10, 20), um(0, 20)},
         {line_mm(15, 0, 15, 20)}},
        {"U horizontal rows",
         {um(0, 0), um(30, 0), um(30, 20), um(20, 20), um(20, 6), um(10, 6), um(10, 20), um(0, 20)},
         {line_mm(0, 10, 30, 10)}},
        {"T horizontal",
         {um(10, 0), um(20, 0), um(20, 14), um(30, 14), um(30, 20), um(0, 20), um(0, 14),
          um(10, 14)},
         {line_mm(0, 10, 30, 10)}},
        {"T vertical",
         {um(10, 0), um(20, 0), um(20, 14), um(30, 14), um(30, 20), um(0, 20), um(0, 14),
          um(10, 14)},
         {line_mm(15, 0, 15, 20)}},
        {"L horizontal",
         {um(0, 0), um(30, 0), um(30, 8), um(10, 8), um(10, 20), um(0, 20)},
         {line_mm(0, 10, 30, 10)}},
    };
    for (const auto& sh : shapes) {
        for (const bool hidden : {true, false}) {
            document::DirectionalFillParams dp = base_params();
            dp.guides = sh.guides;
            dp.hidden_underpath = hidden;
            const geometry::PathSet region{polygon(sh.outer), {}};
            const auto fill = fill_directional(region, dp);
            std::size_t jumps = 0;
            std::size_t travel = 0;
            double jumpLen = 0.0;
            for (std::size_t i = 1; i < fill.size(); ++i) {
                if (fill[i].jump) {
                    ++jumps;
                    jumpLen += seg_len(fill[i - 1].pos, fill[i].pos);
                }
                travel += fill[i].travel ? 1 : 0;
            }
            INFO(sh.name << " hidden=" << hidden);
            CHECK(fill.size() > 200);
            CHECK(jumps <= 2);
            CHECK(jumpLen <= 25'000.0); // µm : au plus 25 mm de saut au total
            CHECK(travel == 0);         // ces formes n'ont pas de trajet caché possible
        }
    }
}

TEST_CASE("directional spacing regularity keeps every row inside the shape") {
    const auto region = rect_mm(0, 0, 30, 20);
    auto dp = fan_params();
    dp.spacing_regularity = 1.0; // réglage maximal : pire cas pour les garde-fous
    for (const auto& line : trace_directional_streamlines(region, dp)) {
        for (const Vec2um p : line.points) {
            CHECK(p.x.value >= -1);
            CHECK(p.x.value <= 30'001);
            CHECK(p.y.value >= -1);
            CHECK(p.y.value <= 20'001);
        }
    }
}

TEST_CASE("directional spacing regularity is deterministic and off by default") {
    const auto region = rect_mm(0, 0, 30, 20);
    auto dp = fan_params();
    CHECK(dp.spacing_regularity == 0.0);
    dp.spacing_regularity = 0.6;
    const auto a = fill_directional(region, dp);
    const auto b = fill_directional(region, dp);
    REQUIRE(a.size() > 100);
    CHECK(a == b);
    // Désactivée, le tracé est celui d'avant (aucun changement de l'existant).
    dp.spacing_regularity = 0.0;
    CHECK(trace_directional_streamlines(region, dp).size() ==
          trace_directional_streamlines(region, fan_params()).size());
}

TEST_CASE("directional spacing regularity works together with a density gradient") {
    const auto region = rect_mm(0, 0, 30, 20);
    auto dp = base_params();
    dp.guides.push_back(line_mm(0, 10, 30, 10));
    dp.density_gradient = vertical_gradient(Micrometers{400}, Micrometers{1'600});
    dp.spacing_regularity = 0.5;
    const auto a = trace_directional_streamlines(region, dp);
    const auto b = trace_directional_streamlines(region, dp);
    REQUIRE(a.size() > 20);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].points == b[i].points);
    }
    const auto ys = crossings_at_x(a, 15.0);
    CHECK(mean_gap_mm(ys, 13.0, 20.0) > 1.5 * mean_gap_mm(ys, 0.0, 7.0));
}

TEST_CASE("directional gradient clamps absurd spacings instead of stalling") {
    const auto region = rect_mm(0, 0, 6, 4);
    auto dp = base_params();
    dp.guides.push_back(line_mm(0, 2, 6, 2));
    dp.density_gradient = vertical_gradient(Micrometers{0}, Micrometers{-50});
    const auto lines = trace_directional_streamlines(region, dp);
    CHECK(!lines.empty());
    // Plancher de 0,1 mm : jamais plus de 41 lignes sur 4 mm de large.
    CHECK(lines.size() <= 41);
}
