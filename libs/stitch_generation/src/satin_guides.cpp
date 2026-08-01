// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/satin_guides.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "openstitch/geometry/polyline.hpp"

namespace openstitch::stitch_generation {
namespace {

struct Projection {
    Vec2um point{};
    double station{};
};

double distance2(Vec2um a, Vec2um b) {
    const double dx = static_cast<double>(a.x.value) - b.x.value;
    const double dy = static_cast<double>(a.y.value) - b.y.value;
    return dx * dx + dy * dy;
}

std::optional<Projection> project(const std::vector<Vec2um>& points,
                                  const std::vector<double>& cumulative, Vec2um desired) {
    if (points.size() < 2 || cumulative.size() != points.size()) {
        return std::nullopt;
    }
    double bestDistance2 = std::numeric_limits<double>::infinity();
    Projection best{};
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const Vec2um p = points[i];
        const Vec2um q = points[i + 1];
        const double vx = static_cast<double>(q.x.value) - p.x.value;
        const double vy = static_cast<double>(q.y.value) - p.y.value;
        const double length2 = vx * vx + vy * vy;
        double t = 0.0;
        if (length2 > 0.0) {
            const double wx = static_cast<double>(desired.x.value) - p.x.value;
            const double wy = static_cast<double>(desired.y.value) - p.y.value;
            t = std::clamp((wx * vx + wy * vy) / length2, 0.0, 1.0);
        }
        const Vec2um snapped{
            Micrometers{static_cast<std::int32_t>(std::llround(p.x.value + t * vx))},
            Micrometers{static_cast<std::int32_t>(std::llround(p.y.value + t * vy))}};
        const double d2 = distance2(snapped, desired);
        if (d2 < bestDistance2) {
            bestDistance2 = d2;
            best.point = snapped;
            best.station = cumulative[i] + t * std::sqrt(length2);
        }
    }
    return best;
}

bool opposite_orientation(const std::vector<Vec2um>& a, const std::vector<Vec2um>& b) {
    return distance2(a.front(), b.back()) + distance2(a.back(), b.front()) <
           distance2(a.front(), b.front()) + distance2(a.back(), b.back());
}

}  // namespace

std::optional<document::SatinRung> move_satin_guide_endpoint(
    const document::SatinParams& satin, std::size_t guide_index, SatinGuideSide side,
    Vec2um desired, Micrometers flatten_tolerance) {
    if (guide_index >= satin.rungs.size() || satin.rungs.size() < 2) {
        return std::nullopt;
    }
    auto railA = geometry::flatten(satin.rail_a, flatten_tolerance).points;
    auto railB = geometry::flatten(satin.rail_b, flatten_tolerance).points;
    if (railA.size() < 2 || railB.size() < 2) {
        return std::nullopt;
    }
    if (opposite_orientation(railA, railB)) {
        std::reverse(railB.begin(), railB.end());
    }
    const auto cumulativeA = geometry::cumulative_lengths(railA);
    const auto cumulativeB = geometry::cumulative_lengths(railB);

    document::SatinRung candidate = satin.rungs[guide_index];
    const auto moved = side == SatinGuideSide::RailA
                           ? project(railA, cumulativeA, desired)
                           : project(railB, cumulativeB, desired);
    if (!moved) {
        return std::nullopt;
    }
    (side == SatinGuideSide::RailA ? candidate.a : candidate.b) = moved->point;

    struct Anchor {
        double sa{};
        double sb{};
    };
    std::vector<Anchor> anchors;
    anchors.reserve(satin.rungs.size());
    for (std::size_t i = 0; i < satin.rungs.size(); ++i) {
        const auto& rung = i == guide_index ? candidate : satin.rungs[i];
        const auto pa = project(railA, cumulativeA, rung.a);
        const auto pb = project(railB, cumulativeB, rung.b);
        if (!pa || !pb) {
            return std::nullopt;
        }
        anchors.push_back({pa->station, pb->station});
    }
    std::stable_sort(anchors.begin(), anchors.end(),
                     [](const Anchor& lhs, const Anchor& rhs) {
                         return lhs.sa + lhs.sb < rhs.sa + rhs.sb;
                     });
    const double minimumGap = std::max(1.0, static_cast<double>(satin.density.value) * 0.5);
    for (std::size_t i = 1; i < anchors.size(); ++i) {
        if (anchors[i].sa <= anchors[i - 1].sa + minimumGap ||
            anchors[i].sb <= anchors[i - 1].sb + minimumGap) {
            return std::nullopt;
        }
    }
    return candidate;
}

std::optional<SatinGuideInsertion> make_satin_guide_in_largest_gap(
    const document::SatinParams& satin, Micrometers flatten_tolerance) {
    if (satin.rungs.size() < 2) {
        return std::nullopt;
    }
    auto railA = geometry::flatten(satin.rail_a, flatten_tolerance).points;
    auto railB = geometry::flatten(satin.rail_b, flatten_tolerance).points;
    if (railA.size() < 2 || railB.size() < 2) {
        return std::nullopt;
    }
    if (opposite_orientation(railA, railB)) {
        std::reverse(railB.begin(), railB.end());
    }
    const auto cumulativeA = geometry::cumulative_lengths(railA);
    const auto cumulativeB = geometry::cumulative_lengths(railB);
    struct Anchor {
        double sa{};
        double sb{};
        std::size_t original_index{};
    };
    std::vector<Anchor> anchors;
    anchors.reserve(satin.rungs.size());
    for (std::size_t i = 0; i < satin.rungs.size(); ++i) {
        const auto& rung = satin.rungs[i];
        const auto pa = project(railA, cumulativeA, rung.a);
        const auto pb = project(railB, cumulativeB, rung.b);
        if (!pa || !pb) {
            return std::nullopt;
        }
        anchors.push_back({pa->station, pb->station, i});
    }
    std::stable_sort(anchors.begin(), anchors.end(),
                     [](const Anchor& lhs, const Anchor& rhs) {
                         return lhs.sa + lhs.sb < rhs.sa + rhs.sb;
                     });

    std::size_t best = 0;
    double bestMedialGap = -1.0;
    for (std::size_t i = 0; i + 1 < anchors.size(); ++i) {
        const double da = anchors[i + 1].sa - anchors[i].sa;
        const double db = anchors[i + 1].sb - anchors[i].sb;
        const double medialGap = 0.5 * (da + db);
        if (da > 0.0 && db > 0.0 && medialGap > bestMedialGap) {
            best = i;
            bestMedialGap = medialGap;
        }
    }
    const double minimumGap = std::max(1.0, static_cast<double>(satin.density.value) * 0.5);
    if (bestMedialGap < 0.0 ||
        anchors[best + 1].sa - anchors[best].sa <= 2.0 * minimumGap ||
        anchors[best + 1].sb - anchors[best].sb <= 2.0 * minimumGap) {
        return std::nullopt;
    }
    const double sa = 0.5 * (anchors[best].sa + anchors[best + 1].sa);
    const double sb = 0.5 * (anchors[best].sb + anchors[best + 1].sb);
    // Les colonnes produites par l'auto-satin stockent les guides dans l'ordre
    // de progression (croissant ou décroissant). Insérer entre les deux indices
    // d'origine préserve cet ordre et garde front()/back() représentatifs pour
    // le routage textile. Le générateur reste lui-même tolérant aux imports non
    // triés, mais l'éditeur ne doit pas en créer de nouveaux.
    const std::size_t insertionIndex =
        std::max(anchors[best].original_index, anchors[best + 1].original_index);
    return SatinGuideInsertion{
        document::SatinRung{geometry::point_at_length(railA, cumulativeA, sa),
                            geometry::point_at_length(railB, cumulativeB, sb)},
        insertionIndex};
}

}  // namespace openstitch::stitch_generation
