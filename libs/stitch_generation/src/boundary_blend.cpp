// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/boundary_blend.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "openstitch/document/boundary_blend.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/frontier.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"
#include "openstitch/stitch_generation/pull_auto.hpp"
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

    [[nodiscard]] double coverage(Vec2um p) const override {
        if (!field_.near_frontier(p)) {
            return 1.0;
        }
        const double dA = field_.density_a(p);
        return sideA_ ? dA : 1.0 - dA;
    }

private:
    const BoundaryBlendField& field_;
    bool sideA_;
};

Result<std::vector<geometry::PathSet>> extended(const std::vector<geometry::PathSet>& own,
                                                const std::vector<geometry::PathSet>& other,
                                                const std::vector<geometry::Polyline>& frontier,
                                                Micrometers otherWidth) {
    auto band = geometry::polyline_band(frontier, otherWidth);
    if (!band) {
        return std::unexpected(band.error());
    }
    auto reach = geometry::intersect_polygons(other, *band);
    if (!reach) {
        return std::unexpected(reach.error());
    }
    std::vector<geometry::PathSet> parts = own;
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

BoundaryBlendField::BoundaryBlendField(const std::vector<geometry::PathSet>& a,
                                       std::vector<geometry::Polyline> frontier,
                                       const BoundaryBlendSpec& spec, Micrometers reach_margin)
    : frontier_(std::move(frontier)),
      widthA_(static_cast<double>(std::clamp(spec.width_a.value, kMinWidth, kMaxWidth))),
      widthB_(static_cast<double>(std::clamp(spec.width_b.value, kMinWidth, kMaxWidth))),
      curve_(spec.curve) {
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
    // Composantes disjointes : le test pair-impair sur la réunion de leurs contours reste juste.
    for (const auto& component : a) {
        const geometry::PathSet flat = geometry::flatten_to_polygon(component, Micrometers{20});
        addPoly(flat.outer);
        for (const auto& hole : flat.holes) {
            addPoly(hole);
        }
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
    return plan_boundary_blend(std::vector<geometry::PathSet>{a}, std::vector<geometry::PathSet>{b},
                               spec, reach_margin);
}

Result<BoundaryBlendPlan> plan_boundary_blend(const std::vector<geometry::PathSet>& a,
                                              const std::vector<geometry::PathSet>& b,
                                              const BoundaryBlendSpec& spec,
                                              Micrometers reach_margin) {
    for (const Micrometers w : {spec.width_a, spec.width_b}) {
        if (w.value < kMinWidth || w.value > kMaxWidth) {
            return fail_coded(ErrorCategory::UserInput, "E_BLEND_WIDTH_RANGE",
                              "Largeur de fondu hors limites",
                              "Choisir une demi-bande entre 0,5 et 15 mm de chaque côté.");
        }
    }
    std::vector<geometry::Polyline> frontier;
    const bool manual = !spec.manual_frontier.empty();
    if (manual) {
        // Frontière tracée : prise telle quelle (les zones peuvent ne pas se toucher, ou se
        // chevaucher). Au moins deux points et `min_frontier` de long.
        if (spec.manual_frontier.size() < 2 || geometry::polyline_length(spec.manual_frontier) <
                                                   static_cast<double>(spec.min_frontier.value)) {
            return fail_coded(ErrorCategory::UserInput, "E_BLEND_FRONTIER_SHORT",
                              "Frontière tracée trop courte",
                              "Tracer une ligne d'au moins 1 mm entre les deux zones.");
        }
        frontier.push_back(geometry::Polyline{spec.manual_frontier, false});
    } else {
        // Zones en plusieurs composantes : réunion des frontières de chaque couple, dans l'ordre.
        for (const auto& ai : a) {
            for (const auto& bj : b) {
                auto part = geometry::shared_frontier(ai, bj, spec.tolerance, spec.min_frontier);
                if (!part) {
                    return std::unexpected(part.error());
                }
                frontier.insert(frontier.end(), part->begin(), part->end());
            }
        }
    }
    if (frontier.empty()) {
        return fail_coded(ErrorCategory::OperationImpossible, "E_BLEND_NO_FRONTIER",
                          "Les deux zones ne se touchent pas",
                          "Rapprocher les zones ou dessiner la frontière à la main.");
    }
    if (manual) {
        // Chaque zone doit être atteinte par sa demi-bande : sinon le fondu n'aurait aucun effet
        // de ce côté (frontière tracée loin d'une des zones).
        const auto reaches = [&](const std::vector<geometry::PathSet>& zones, Micrometers width) {
            const auto band = geometry::polyline_band(frontier, width);
            if (!band) {
                return false;
            }
            const auto common = geometry::intersect_polygons(zones, *band);
            if (!common) {
                return false;
            }
            double area = 0.0;
            for (const auto& set : *common) {
                area += geometry::path_set_area_um2(set);
            }
            return area > 0.0;
        };
        if (!reaches(a, spec.width_a) || !reaches(b, spec.width_b)) {
            return fail_coded(ErrorCategory::UserInput, "E_BLEND_FRONTIER_FAR",
                              "La frontière tracée est trop loin d'une des deux zones",
                              "Tracer la frontière entre les deux zones, ou élargir la bande.");
        }
    }
    BoundaryBlendPlan plan;
    plan.frontier = frontier;
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
    plan.field = std::make_shared<const BoundaryBlendField>(a, std::move(frontier), spec,
                                                            Micrometers{margin});
    return plan;
}

Result<BoundaryBlendFills> fill_boundary_blend_tatami(const geometry::PathSet& a,
                                                      const document::TatamiParams& params_a,
                                                      const geometry::PathSet& b,
                                                      const document::TatamiParams& params_b,
                                                      const BoundaryBlendSpec& spec) {
    return fill_boundary_blend(a, BoundaryBlendFillParams{params_a}, b,
                               BoundaryBlendFillParams{params_b}, spec);
}

Result<BoundaryBlendFills> fill_boundary_blend(const geometry::PathSet& a,
                                               const BoundaryBlendFillParams& params_a,
                                               const geometry::PathSet& b,
                                               const BoundaryBlendFillParams& params_b,
                                               const BoundaryBlendSpec& spec) {
    return fill_boundary_blend(std::vector<geometry::PathSet>{a}, params_a,
                               std::vector<geometry::PathSet>{b}, params_b, spec);
}

Result<BoundaryBlendFills> fill_boundary_blend(const std::vector<geometry::PathSet>& a,
                                               const BoundaryBlendFillParams& params_a,
                                               const std::vector<geometry::PathSet>& b,
                                               const BoundaryBlendFillParams& params_b,
                                               const BoundaryBlendSpec& spec) {
    const auto stitchLength = [](const BoundaryBlendFillParams& p) {
        return std::visit([](const auto& v) { return v.stitch_length.value; }, p);
    };
    // Une case de pénétrations au-delà de la demi-bande (cf. `plan_boundary_blend`).
    const Micrometers margin{std::max(stitchLength(params_a), stitchLength(params_b))};
    auto plan = plan_boundary_blend(a, b, spec, margin);
    if (!plan) {
        return std::unexpected(plan.error());
    }
    const BoundaryRowMask maskA(*plan->field, true);
    const BoundaryRowMask maskB(*plan->field, false);
    const auto fillLayer = [](const geometry::PathSet& region, const BoundaryBlendFillParams& p,
                              const RowMask& mask) {
        if (const auto* tatami = std::get_if<document::TatamiParams>(&p)) {
            return fill_tatami(region, *tatami, &mask);
        }
        return fill_directional(region, std::get<document::DirectionalFillParams>(p), &mask);
    };
    BoundaryBlendFills fills;
    for (const auto& region : plan->region_a) {
        append_fill(fills.a, fillLayer(region, params_a, maskA));
    }
    for (const auto& region : plan->region_b) {
        append_fill(fills.b, fillLayer(region, params_b, maskB));
    }
    return fills;
}

std::vector<geometry::PathSet> boundary_blend_zone(const document::Project& project,
                                                   const document::EmbroideryObject& object) {
    std::vector<geometry::PathSet> zones;
    Micrometers inset{0};
    if (const auto* t = std::get_if<document::TatamiParams>(&object.params)) {
        inset = t->inset;
    } else if (const auto* d = std::get_if<document::DirectionalFillParams>(&object.params)) {
        inset = d->inset;
    } else {
        return zones;
    }
    const auto* source = project.findObject(object.source_vector);
    if (source == nullptr) {
        return zones;
    }
    // Même préparation que `fill_regions_in_sewing_order` (generate.cpp) : aplatissement à
    // 50 µm, retrait de bord, forme brute si le retrait la fait disparaître.
    for (const geometry::PathSet& rawSet : source->paths) {
        const geometry::PathSet set = geometry::flatten_to_polygon(rawSet, Micrometers{50});
        std::vector<geometry::PathSet> filled;
        if (inset.value > 0) {
            if (auto shrunk = geometry::inset_path_set(set, inset); shrunk && !shrunk->empty()) {
                filled = std::move(*shrunk);
            }
        }
        if (filled.empty()) {
            filled.push_back(set);
        }
        zones.insert(zones.end(), filled.begin(), filled.end());
    }
    return zones;
}

std::optional<std::vector<FillStitch>>
boundary_blend_layer(const document::Project& project, const document::EmbroideryObject& object) {
    const auto pair = document::find_boundary_blend(project, object.id);
    if (!pair) {
        return std::nullopt;
    }
    const auto* self = project.findEmbroidery(pair->self);
    const auto* partner = project.findEmbroidery(pair->partner);
    if (self == nullptr || partner == nullptr) {
        return std::nullopt;
    }
    // Sens canonique : la zone d'identifiant le plus petit est A, quelle que soit la zone
    // générée. Les deux couches sortent ainsi du MÊME plan (frontière, champ, régions).
    const bool selfFirst = self->id.value < partner->id.value;
    const document::EmbroideryObject& first = selfFirst ? *self : *partner;
    const document::EmbroideryObject& second = selfFirst ? *partner : *self;
    const auto zonesFirst = boundary_blend_zone(project, first);
    const auto zonesSecond = boundary_blend_zone(project, second);
    if (zonesFirst.empty() || zonesSecond.empty()) {
        return std::nullopt;
    }
    const auto fillParams = [](const document::EmbroideryObject& o,
                               const geometry::PathSet& region) -> BoundaryBlendFillParams {
        if (const auto* t = std::get_if<document::TatamiParams>(&o.params)) {
            return resolve_pull_compensation(*t, region);
        }
        return resolve_pull_compensation(std::get<document::DirectionalFillParams>(o.params),
                                         region);
    };
    BoundaryBlendSpec spec;
    spec.width_a = first.boundary_blend->width_self;
    spec.width_b = second.boundary_blend->width_self;
    spec.curve = first.boundary_blend->curve;
    spec.manual_frontier = first.boundary_blend->manual_frontier;
    auto fills = fill_boundary_blend(zonesFirst, fillParams(first, zonesFirst.front()), zonesSecond,
                                     fillParams(second, zonesSecond.front()), spec);
    if (!fills) {
        return std::nullopt;
    }
    return selfFirst ? std::move(fills->a) : std::move(fills->b);
}

} // namespace openstitch::stitch_generation
