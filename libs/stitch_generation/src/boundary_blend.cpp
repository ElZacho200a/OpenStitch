// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/boundary_blend.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
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

// Densité minimale d'une couche dans la bande : en dessous, la couche ne coud rien (au-dessus de
// 1 - kMinLayerDensity, elle coud tout). Une rangée gardée sur ≈ 1/d : sans plancher, une couche
// presque vide relie des rangées distantes de plusieurs millimètres par de longues liaisons
// visibles (recette Studio). Avec 0,2, au plus 5 rangées sautées (cf. `kMaskedRowGap`).
constexpr double kMinLayerDensity = 0.2;
// Superposition (grilles différentes) : chaque couche est cousue un peu plus dense que sa part,
// sinon les creux des deux couches indépendantes laissent le tissu nu par endroits.
constexpr double kOverlapBoost = 1.5;
// Mode Interlaced : les rangées en travers d'une frontière courbe s'écartent en éventail du côté
// convexe. Le traceur sème une nouvelle rangée dès que l'écart dépasse 2 × kInterlacedFillSeed ×
// l'écart voulu (au lieu de 1,8 ×) : densité qui s'adapte localement, sans jour entre rangées.
// Écart nominal : fraction du plus petit écart des deux zones ; régularité d'écart du traceur.
constexpr double kInterlacedFillSeed = 0.5;
constexpr double kInterlacedSpacing = 1.0;
constexpr double kInterlacedRegularity = 1.0;

double floored_density(double dA) {
    if (dA < kMinLayerDensity) {
        return 0.0;
    }
    if (dA > 1.0 - kMinLayerDensity) {
        return 1.0;
    }
    return dA;
}

// Masque d'une des deux couches : hors de la boîte de la frontière, le point est dans le cœur
// de la zone de la couche (la prolongation ne sort pas de la bande) : aplat.
class BoundaryRowMask final : public RowMask {
public:
    // `floored` : plancher de densité (cf. `kMinLayerDensity`). Sans objet en mode Interlaced :
    // les rangées traversent la bande, une couche clairsemée n'y relie aucune rangée lointaine, et
    // le plancher alignerait les bouts de 20 % des rangées sur une même ligne.
    BoundaryRowMask(const BoundaryBlendField& field, bool sideA, bool overlap, bool floored = true)
        : field_(field), sideA_(sideA), overlap_(overlap), floored_(floored) {}

    [[nodiscard]] bool keeps(Vec2um p, double threshold) const override {
        if (!field_.near_frontier(p)) {
            return true;
        }
        const double dA = density(p);
        if (overlap_) {
            // Superposition : chaque couche garde la tranche basse de sa part renforcée.
            const double own = sideA_ ? dA : 1.0 - dA;
            return own > 0.0 && threshold < std::min(1.0, own * kOverlapBoost);
        }
        // Partition exacte : A garde [0 ; d_A), B [d_A ; 1) (largeur d_B = 1 - d_A).
        return sideA_ ? threshold < dA : threshold >= dA;
    }

    [[nodiscard]] double coverage(Vec2um p) const override {
        if (!field_.near_frontier(p)) {
            return 1.0;
        }
        const double dA = density(p);
        const double own = sideA_ ? dA : 1.0 - dA;
        return overlap_ && own > 0.0 ? std::min(1.0, own * kOverlapBoost) : own;
    }

private:
    [[nodiscard]] double density(Vec2um p) const {
        const double dA = field_.density_a(p);
        return floored_ ? floored_density(dA) : dA;
    }

    const BoundaryBlendField& field_;
    bool sideA_;
    bool overlap_;
    bool floored_;
};

// Les deux couches partagent la même grille de rangées (ancrage absolu) : tatamis de même angle
// et même écart. Sinon (angles, écarts différents, ou un remplissage directionnel) : superposition.
bool same_row_grid(const BoundaryBlendFillParams& a, const BoundaryBlendFillParams& b) {
    const auto* ta = std::get_if<document::TatamiParams>(&a);
    const auto* tb = std::get_if<document::TatamiParams>(&b);
    return ta != nullptr && tb != nullptr && ta->row_spacing == tb->row_spacing &&
           std::abs(ta->angle.radians - tb->angle.radians) < 1e-9;
}

Result<std::vector<geometry::PathSet>> extended(const std::vector<geometry::PathSet>& own,
                                                const std::vector<geometry::PathSet>& other,
                                                const std::vector<geometry::Polyline>& frontier,
                                                Micrometers otherWidth, Micrometers tolerance) {
    auto band = geometry::polyline_band(frontier, otherWidth);
    if (!band) {
        return std::unexpected(band.error());
    }
    // Les zones reçues sont rentrées de leur retrait de bord : un couloir de tissu sépare A et B.
    // La fermeture de A ∪ B (écart <= tolérance de contact) le comble, sinon aucune des deux
    // couches ne le coud et un filet de tissu nu suit la frontière (recette Studio).
    std::vector<geometry::PathSet> both = own;
    both.insert(both.end(), other.begin(), other.end());
    auto merged = geometry::union_polygons_closing(both, tolerance);
    if (!merged) {
        return std::unexpected(merged.error());
    }
    auto reach = geometry::intersect_polygons(*merged, *band);
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

// --- Sens des rangées dans la bande (§6.3) ---------------------------------------------------

// Écart d'angles de rangée ramené dans ]-π/2 ; π/2] (une rangée et son opposée sont la même).
double wrap_half_turn(double d) {
    constexpr double kPi = std::numbers::pi;
    double r = std::fmod(d, kPi);
    if (r > 0.5 * kPi) {
        r -= kPi;
    } else if (r <= -0.5 * kPi) {
        r += kPi;
    }
    return r;
}

// Découpe d'un fondu Shared / Rotating : cœur de chaque zone (privé de sa demi-bande) et bande
// commune (élargie du recouvrement sur les cœurs), cousue par les deux couches.
struct BandSplit {
    std::vector<geometry::PathSet> core_a;
    std::vector<geometry::PathSet> core_b;
    std::vector<geometry::PathSet> band;
};

Result<BandSplit> split_band(const std::vector<geometry::PathSet>& a,
                             const std::vector<geometry::PathSet>& b,
                             const std::vector<geometry::Polyline>& frontier,
                             const BoundaryBlendSpec& spec) {
    const std::int32_t wA = std::clamp(spec.width_a.value, kMinWidth, kMaxWidth);
    const std::int32_t wB = std::clamp(spec.width_b.value, kMinWidth, kMaxWidth);
    auto bandA = geometry::polyline_band(frontier, Micrometers{wA});
    auto bandB = geometry::polyline_band(frontier, Micrometers{wB});
    auto growA = geometry::polyline_band(frontier, Micrometers{wA + kBoundaryBlendSeamOverlap});
    auto growB = geometry::polyline_band(frontier, Micrometers{wB + kBoundaryBlendSeamOverlap});
    if (!bandA) {
        return std::unexpected(bandA.error());
    }
    if (!bandB) {
        return std::unexpected(bandB.error());
    }
    if (!growA) {
        return std::unexpected(growA.error());
    }
    if (!growB) {
        return std::unexpected(growB.error());
    }
    BandSplit split;
    auto coreA = geometry::difference_polygons(a, *bandA);
    if (!coreA) {
        return std::unexpected(coreA.error());
    }
    auto coreB = geometry::difference_polygons(b, *bandB);
    if (!coreB) {
        return std::unexpected(coreB.error());
    }
    split.core_a = std::move(*coreA);
    split.core_b = std::move(*coreB);
    // Côté B : fermeture de A ∪ B privée de A (B et le couloir de retrait entre les zones).
    std::vector<geometry::PathSet> both = a;
    both.insert(both.end(), b.begin(), b.end());
    auto merged = geometry::union_polygons_closing(both, spec.tolerance);
    if (!merged) {
        return std::unexpected(merged.error());
    }
    auto restB = geometry::difference_polygons(*merged, a);
    if (!restB) {
        return std::unexpected(restB.error());
    }
    auto partA = geometry::intersect_polygons(a, *growA);
    if (!partA) {
        return std::unexpected(partA.error());
    }
    auto partB = geometry::intersect_polygons(*restB, *growB);
    if (!partB) {
        return std::unexpected(partB.error());
    }
    std::vector<geometry::PathSet> parts = std::move(*partA);
    parts.insert(parts.end(), partB->begin(), partB->end());
    auto band = geometry::union_polygons(parts);
    if (!band) {
        return std::unexpected(band.error());
    }
    split.band = std::move(*band);
    return split;
}

// Remplissage de la bande commune : tatami à l'angle commun (Shared), directionnel à champ
// pivotant (Rotating) ou en travers de la frontière (Interlaced), mêmes rangées pour les deux
// couches quel que soit le masque.
class BandFiller {
public:
    BandFiller(std::shared_ptr<const BoundaryBlendField> field, const document::TatamiParams& pa,
               const document::TatamiParams& pb, document::BoundaryBlendRows rows)
        : field_(std::move(field)), rows_(rows), angleA_(pa.angle.radians),
          angleB_(pb.angle.radians), tatami_(pa) {
        tatami_.angle = Angle{boundary_blend_shared_angle(angleA_, angleB_)};
        tatami_.row_spacing = Micrometers{std::min(pa.row_spacing.value, pb.row_spacing.value)};
        tatami_.entry_point.reset();
        directional_.row_spacing = tatami_.row_spacing;
        directional_.stitch_length = pa.stitch_length;
        directional_.stagger = pa.stagger;
        directional_.inset = Micrometers{0};
        directional_.pull_compensation = pa.pull_compensation;
        directional_.hidden_underpath = false;
        if (rows == document::BoundaryBlendRows::Interlaced) {
            // Rangées en travers d'une frontière courbe : cf. `kInterlacedSpacing`.
            directional_.row_spacing = Micrometers{static_cast<std::int32_t>(
                std::lround(kInterlacedSpacing * directional_.row_spacing.value))};
            directional_.spacing_regularity = kInterlacedRegularity;
        }
    }

    [[nodiscard]] std::vector<FillStitch> fill(const geometry::PathSet& band,
                                               const RowMask& mask) const {
        const BoundaryBlendField& field = *field_;
        if (rows_ == document::BoundaryBlendRows::Interlaced) {
            return fill_directional_field(
                band, directional_,
                [&field](Vec2um p) { return boundary_blend_cross_angle(field, p); }, &mask,
                kInterlacedFillSeed);
        }
        if (rows_ != document::BoundaryBlendRows::Rotating) {
            return fill_tatami(band, tatami_, &mask);
        }
        const double a = angleA_;
        const double b = angleB_;
        return fill_directional_field(
            band, directional_,
            [&field, a, b](Vec2um p) { return boundary_blend_row_angle(field, a, b, p); }, &mask);
    }

private:
    std::shared_ptr<const BoundaryBlendField> field_;
    document::BoundaryBlendRows rows_;
    double angleA_;
    double angleB_;
    document::TatamiParams tatami_;
    document::DirectionalFillParams directional_;
};

Micrometers blend_margin(const BoundaryBlendFillParams& a, const BoundaryBlendFillParams& b) {
    const auto stitchLength = [](const BoundaryBlendFillParams& p) {
        return std::visit([](const auto& v) { return v.stitch_length.value; }, p);
    };
    // Une case de pénétrations au-delà de la demi-bande (cf. `plan_boundary_blend`), et au moins
    // le recouvrement cœur-bande : la bande commune reste dans la boîte du champ.
    return Micrometers{std::max({stitchLength(a), stitchLength(b), kBoundaryBlendSeamOverlap})};
}

} // namespace

double boundary_blend_shared_angle(double angle_a, double angle_b) {
    return angle_a + 0.5 * wrap_half_turn(angle_b - angle_a);
}

double boundary_blend_row_angle(const BoundaryBlendField& field, double angle_a, double angle_b,
                                Vec2um p) {
    double t = 0.0;
    if (field.near_frontier(p)) {
        const double s = field.signed_distance(p);
        t = std::clamp((s + field.width_a()) / (field.width_a() + field.width_b()), 0.0, 1.0);
    } else {
        t = field.density_a(p) >= 0.5 ? 0.0 : 1.0; // aplat : cœur de A ou de B
    }
    const double u = t * t * (3.0 - 2.0 * t);
    return angle_a + wrap_half_turn(angle_b - angle_a) * u;
}

double boundary_blend_cross_angle(const BoundaryBlendField& field, Vec2um p) {
    // Perpendiculaire à la tangente LISSÉE de la frontière autour de p : moyenne des directions
    // des segments (angles doublés : un segment et son opposé comptent pareil), pondérée par la
    // longueur et par exp(-((d - d_min) / σ)²), σ = demi-largeur moyenne de la bande. La simple
    // perpendiculaire au point le plus proche convergeait du côté concave d'une frontière courbe
    // (rangées croisées en éventail, recette Studio) et suivait chaque cran d'une frontière
    // vectorisée ; ce champ est continu partout et exact le long d'une frontière droite.
    const double sigma = std::max(1.0, 0.5 * (field.width_a() + field.width_b()));
    double dmin = std::numeric_limits<double>::max();
    for (const auto& line : field.frontier()) {
        for (std::size_t i = 1; i < line.points.size(); ++i) {
            dmin = std::min(dmin, seg_distance(p, line.points[i - 1], line.points[i]));
        }
    }
    if (dmin == std::numeric_limits<double>::max()) {
        return 0.0;
    }
    double c2 = 0.0;
    double s2 = 0.0;
    for (const auto& line : field.frontier()) {
        for (std::size_t i = 1; i < line.points.size(); ++i) {
            const Vec2um a = line.points[i - 1];
            const Vec2um b = line.points[i];
            const double dx = static_cast<double>(b.x.value) - a.x.value;
            const double dy = static_cast<double>(b.y.value) - a.y.value;
            const double len = std::hypot(dx, dy);
            if (len <= 0.0) {
                continue;
            }
            const double u = (seg_distance(p, a, b) - dmin) / sigma;
            if (u > 4.0) {
                continue; // poids < 1e-7
            }
            const double w = len * std::exp(-u * u);
            const double theta = std::atan2(dy, dx);
            c2 += w * std::cos(2.0 * theta);
            s2 += w * std::sin(2.0 * theta);
        }
    }
    return 0.5 * std::atan2(s2, c2) + 0.5 * std::numbers::pi;
}

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
    auto regionA =
        extended(a, b, plan.frontier, Micrometers{spec.width_b.value + margin}, spec.tolerance);
    if (!regionA) {
        return std::unexpected(regionA.error());
    }
    auto regionB =
        extended(b, a, plan.frontier, Micrometers{spec.width_a.value + margin}, spec.tolerance);
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
    auto plan = plan_boundary_blend(a, b, spec, blend_margin(params_a, params_b));
    if (!plan) {
        return std::unexpected(plan.error());
    }
    const auto* tatamiA = std::get_if<document::TatamiParams>(&params_a);
    const auto* tatamiB = std::get_if<document::TatamiParams>(&params_b);
    // Shared / Rotating : cœurs à leur angle, bande commune partagée (§6.3). Une zone
    // directionnelle a ses propres lignes de courant : repli sur PerZone.
    if (spec.rows != document::BoundaryBlendRows::PerZone && tatamiA != nullptr &&
        tatamiB != nullptr) {
        auto split = split_band(a, b, plan->frontier, spec);
        if (!split) {
            return std::unexpected(split.error());
        }
        const BandFiller band(plan->field, *tatamiA, *tatamiB, spec.rows);
        const bool floored = spec.rows != document::BoundaryBlendRows::Interlaced;
        const BoundaryRowMask lower(*plan->field, true, false, floored);
        const BoundaryRowMask upper(*plan->field, false, false, floored);
        BoundaryBlendFills fills;
        fills.frontier = plan->frontier;
        for (const auto& core : split->core_a) {
            append_fill(fills.a, fill_tatami(core, *tatamiA));
        }
        for (const auto& part : split->band) {
            append_fill(fills.a, band.fill(part, lower));
        }
        for (const auto& part : split->band) {
            append_fill(fills.b, band.fill(part, upper));
        }
        for (const auto& core : split->core_b) {
            append_fill(fills.b, fill_tatami(core, *tatamiB));
        }
        return fills;
    }
    const bool overlap = !same_row_grid(params_a, params_b);
    const BoundaryRowMask maskA(*plan->field, true, overlap);
    const BoundaryRowMask maskB(*plan->field, false, overlap);
    const auto fillLayer = [](const geometry::PathSet& region, const BoundaryBlendFillParams& p,
                              const RowMask& mask) {
        if (const auto* tatami = std::get_if<document::TatamiParams>(&p)) {
            return fill_tatami(region, *tatami, &mask);
        }
        return fill_directional(region, std::get<document::DirectionalFillParams>(p), &mask);
    };
    BoundaryBlendFills fills;
    fills.frontier = plan->frontier;
    for (const auto& region : plan->region_a) {
        append_fill(fills.a, fillLayer(region, params_a, maskA));
    }
    for (const auto& region : plan->region_b) {
        append_fill(fills.b, fillLayer(region, params_b, maskB));
    }
    return fills;
}

Result<std::vector<FillStitch>> boundary_blend_band_rows(const geometry::PathSet& a,
                                                         const document::TatamiParams& params_a,
                                                         const geometry::PathSet& b,
                                                         const document::TatamiParams& params_b,
                                                         const BoundaryBlendSpec& spec) {
    std::vector<FillStitch> out;
    if (spec.rows == document::BoundaryBlendRows::PerZone) {
        return out;
    }
    const std::vector<geometry::PathSet> za{a};
    const std::vector<geometry::PathSet> zb{b};
    auto plan = plan_boundary_blend(
        za, zb, spec,
        blend_margin(BoundaryBlendFillParams{params_a}, BoundaryBlendFillParams{params_b}));
    if (!plan) {
        return std::unexpected(plan.error());
    }
    auto split = split_band(za, zb, plan->frontier, spec);
    if (!split) {
        return std::unexpected(split.error());
    }
    const BandFiller band(plan->field, params_a, params_b, spec.rows);
    const DensityRowMask full([](Vec2um) { return 1.0; }, DensityRowMask::Slice::Lower);
    for (const auto& part : split->band) {
        append_fill(out, band.fill(part, full));
    }
    return out;
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

std::optional<BoundaryBlendLayer> boundary_blend_layer(const document::Project& project,
                                                       const document::EmbroideryObject& object) {
    // Lot 1 (plusieurs fondus par zone) : seul le PREMIER couple valide (plus petit partenaire)
    // est cousu ; une zone à un seul fondu garde exactement le comportement antérieur.
    const auto pairs = document::find_boundary_blends(project, object.id);
    if (pairs.empty()) {
        return std::nullopt;
    }
    const auto* pair = &pairs.front();
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
    // Liens réciproques garantis par `find_boundary_blends`.
    const auto* linkFirst = document::boundary_blend_link(first, second.id);
    const auto* linkSecond = document::boundary_blend_link(second, first.id);
    const auto* linkSelf = selfFirst ? linkFirst : linkSecond;
    BoundaryBlendSpec spec;
    spec.width_a = linkFirst->width_self;
    spec.width_b = linkSecond->width_self;
    spec.curve = linkFirst->curve;
    spec.manual_frontier = linkFirst->manual_frontier;
    spec.rows = linkFirst->rows;
    auto fills = fill_boundary_blend(zonesFirst, fillParams(first, zonesFirst.front()), zonesSecond,
                                     fillParams(second, zonesSecond.front()), spec);
    if (!fills) {
        return std::nullopt;
    }
    BoundaryBlendLayer layer;
    layer.stitches = selfFirst ? std::move(fills->a) : std::move(fills->b);
    const auto& ownZones = selfFirst ? zonesFirst : zonesSecond;
    const auto band = geometry::polyline_band(fills->frontier, linkSelf->width_self);
    if (band) {
        if (auto core = geometry::difference_polygons(ownZones, *band)) {
            layer.underlay_zones = std::move(*core);
        }
    } else {
        layer.underlay_zones = ownZones; // bande introuvable : sous-couche habituelle
    }
    return layer;
}

} // namespace openstitch::stitch_generation
