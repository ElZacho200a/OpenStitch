// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/boundary_blend.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/frontier.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"
#include "openstitch/stitch_generation/row_mask.hpp"

namespace openstitch::stitch_generation {

namespace {

constexpr std::int32_t kMinWidth = 500;    // 0,5 mm
constexpr std::int32_t kMaxWidth = 15'000; // 15 mm

double seg_distance(Vec2um p, Vec2um a, Vec2um b) {
    const double px = p.x.value;
    const double py = p.y.value;
    const double ax = a.x.value;
    const double ay = a.y.value;
    const double dx = static_cast<double>(b.x.value) - ax;
    const double dy = static_cast<double>(b.y.value) - ay;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const double qx = ax + t * dx - px;
    const double qy = ay + t * dy - py;
    return std::sqrt(qx * qx + qy * qy);
}

// Masque d'une des deux couches : hors de la boîte de la frontière, le point est dans le cœur
// de la zone de la couche (la prolongation ne sort pas de la bande) : aplat.
class BoundaryRowMask final : public RowMask {
public:
    BoundaryRowMask(const BoundaryBlendField& field, bool sideA) : field_(field), sideA_(sideA) {}

    [[nodiscard]] bool keeps(Vec2um p, double threshold) const override {
        if (!field_.near_frontier(p)) {
            return true;
        }
        const double dA = field_.density_a(p);
        // A : tranche basse [0 ; d_A). B : tranche haute [d_A ; 1), de largeur d_B = 1 - d_A.
        return sideA_ ? threshold < dA : threshold >= dA;
    }

private:
    const BoundaryBlendField& field_;
    bool sideA_;
};

Result<std::vector<geometry::PathSet>> extended(const geometry::PathSet& own,
                                                const geometry::PathSet& other,
                                                const std::vector<geometry::Polyline>& frontier,
                                                Micrometers otherWidth) {
    auto band = geometry::polyline_band(frontier, otherWidth);
    if (!band) {
        return std::unexpected(band.error());
    }
    auto reach = geometry::intersect_polygons({other}, *band);
    if (!reach) {
        return std::unexpected(reach.error());
    }
    std::vector<geometry::PathSet> parts{own};
    parts.insert(parts.end(), reach->begin(), reach->end());
    return geometry::union_polygons(parts);
}

void append_fill(std::vector<FillStitch>& out, std::vector<FillStitch> part) {
    if (part.empty()) {
        return;
    }
    if (!out.empty()) {
        part.front().jump = true;
        part.front().travel = false;
    }
    out.insert(out.end(), part.begin(), part.end());
}

} // namespace

BoundaryBlendField::BoundaryBlendField(const geometry::PathSet& a,
                                       std::vector<geometry::Polyline> frontier,
                                       const BoundaryBlendSpec& spec, Micrometers reach_margin)
    : frontier_(std::move(frontier)),
      widthA_(static_cast<double>(std::clamp(spec.width_a.value, kMinWidth, kMaxWidth))),
      widthB_(static_cast<double>(std::clamp(spec.width_b.value, kMinWidth, kMaxWidth))),
      curve_(spec.curve) {
    const geometry::PathSet flat = geometry::flatten_to_polygon(a, Micrometers{20});
    const auto addPoly = [&](const geometry::Path& path) {
        std::vector<Vec2um> pts;
        pts.reserve(path.nodes.size());
        for (const auto& node : path.nodes) {
            pts.push_back(node.pos);
        }
        if (pts.size() >= 3) {
            polysA_.push_back(std::move(pts));
        }
    };
    addPoly(flat.outer);
    for (const auto& hole : flat.holes) {
        addPoly(hole);
    }
    double x0 = std::numeric_limits<double>::max();
    double y0 = x0;
    double x1 = std::numeric_limits<double>::lowest();
    double y1 = x1;
    for (const auto& line : frontier_) {
        for (const Vec2um p : line.points) {
            x0 = std::min(x0, static_cast<double>(p.x.value));
            y0 = std::min(y0, static_cast<double>(p.y.value));
            x1 = std::max(x1, static_cast<double>(p.x.value));
            y1 = std::max(y1, static_cast<double>(p.y.value));
        }
    }
    if (x0 <= x1) {
        const double margin = std::max(widthA_, widthB_) +
                              static_cast<double>(std::max<std::int32_t>(0, reach_margin.value)) +
                              1.0;
        boxX0_ = x0 - margin;
        boxY0_ = y0 - margin;
        boxX1_ = x1 + margin;
        boxY1_ = y1 + margin;
    }
}

bool BoundaryBlendField::near_frontier(Vec2um p) const {
    const double x = p.x.value;
    const double y = p.y.value;
    return x >= boxX0_ && x <= boxX1_ && y >= boxY0_ && y <= boxY1_;
}

bool BoundaryBlendField::inside_a(Vec2um p) const {
    // Pair-impair sur extérieur et trous (rayon horizontal), entiers convertis en double.
    bool inside = false;
    const double px = p.x.value;
    const double py = p.y.value;
    for (const auto& poly : polysA_) {
        const std::size_t m = poly.size();
        for (std::size_t i = 0, j = m - 1; i < m; j = i++) {
            const double ax = poly[i].x.value;
            const double ay = poly[i].y.value;
            const double bx = poly[j].x.value;
            const double by = poly[j].y.value;
            if ((ay > py) != (by > py) && px < (bx - ax) * (py - ay) / (by - ay) + ax) {
                inside = !inside;
            }
        }
    }
    return inside;
}

double BoundaryBlendField::signed_distance(Vec2um p) const {
    double best = std::numeric_limits<double>::max();
    for (const auto& line : frontier_) {
        for (std::size_t i = 1; i < line.points.size(); ++i) {
            best = std::min(best, seg_distance(p, line.points[i - 1], line.points[i]));
        }
    }
    return inside_a(p) ? -best : best;
}

double BoundaryBlendField::density_a(Vec2um p) const {
    if (!near_frontier(p)) {
        return inside_a(p) ? 1.0 : 0.0;
    }
    const double s = signed_distance(p);
    const double t = (s + widthA_) / (widthA_ + widthB_);
    return 1.0 - shape_density_curve(curve_, t);
}

Result<BoundaryBlendPlan> plan_boundary_blend(const geometry::PathSet& a,
                                              const geometry::PathSet& b,
                                              const BoundaryBlendSpec& spec,
                                              Micrometers reach_margin) {
    for (const Micrometers w : {spec.width_a, spec.width_b}) {
        if (w.value < kMinWidth || w.value > kMaxWidth) {
            return fail_coded(ErrorCategory::UserInput, "E_BLEND_WIDTH_RANGE",
                              "Largeur de fondu hors limites",
                              "Choisir une demi-bande entre 0,5 et 15 mm de chaque côté.");
        }
    }
    auto frontier = geometry::shared_frontier(a, b, spec.tolerance, spec.min_frontier);
    if (!frontier) {
        return std::unexpected(frontier.error());
    }
    if (frontier->empty()) {
        return fail_coded(ErrorCategory::OperationImpossible, "E_BLEND_NO_FRONTIER",
                          "Les deux zones ne se touchent pas",
                          "Rapprocher les zones ou dessiner la frontière à la main.");
    }
    BoundaryBlendPlan plan;
    plan.frontier = *frontier;
    const std::int32_t margin = std::clamp<std::int32_t>(reach_margin.value, 0, 10'000);
    auto regionA = extended(a, b, plan.frontier, Micrometers{spec.width_b.value + margin});
    if (!regionA) {
        return std::unexpected(regionA.error());
    }
    auto regionB = extended(b, a, plan.frontier, Micrometers{spec.width_a.value + margin});
    if (!regionB) {
        return std::unexpected(regionB.error());
    }
    plan.region_a = std::move(*regionA);
    plan.region_b = std::move(*regionB);
    plan.field = std::make_shared<const BoundaryBlendField>(a, std::move(*frontier), spec,
                                                            Micrometers{margin});
    return plan;
}

Result<BoundaryBlendFills> fill_boundary_blend_tatami(const geometry::PathSet& a,
                                                      const document::TatamiParams& params_a,
                                                      const geometry::PathSet& b,
                                                      const document::TatamiParams& params_b,
                                                      const BoundaryBlendSpec& spec) {
    // Une case de pénétrations au-delà de la demi-bande (cf. `plan_boundary_blend`).
    const Micrometers margin{std::max(params_a.stitch_length.value, params_b.stitch_length.value)};
    auto plan = plan_boundary_blend(a, b, spec, margin);
    if (!plan) {
        return std::unexpected(plan.error());
    }
    const BoundaryRowMask maskA(*plan->field, true);
    const BoundaryRowMask maskB(*plan->field, false);
    BoundaryBlendFills fills;
    for (const auto& region : plan->region_a) {
        append_fill(fills.a, fill_tatami(region, params_a, &maskA));
    }
    for (const auto& region : plan->region_b) {
        append_fill(fills.b, fill_tatami(region, params_b, &maskB));
    }
    return fills;
}

} // namespace openstitch::stitch_generation
