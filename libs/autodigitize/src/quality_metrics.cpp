// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/quality_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <utility>
#include <variant>

#include "openstitch/auto_satin/guided_satin.hpp"
#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/moments.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::autodigitize {

namespace {

double net_area_um2(const geometry::PathSet& set) {
    double area = std::abs(geometry::signed_area_um2(set.outer));
    for (const auto& hole : set.holes) {
        area -= std::abs(geometry::signed_area_um2(hole));
    }
    return std::max(0.0, area);
}

} // namespace

std::optional<BandShape> measure_band_shape(const geometry::PathSet& region,
                                            const AutoOptions& options) {
    const auto_satin::SkeletonSatinParameters params;
    const auto analysis = auto_satin::analyze_region(region, params.analysis);
    if (!analysis) {
        return std::nullopt;
    }
    const auto& r = analysis->report;
    // Le rapport de satinabilité ignore les trous dans le périmètre et n'a pas d'axe pour un
    // anneau (squelette sans arête) : on mesure ici la bande elle-même. Une bande a deux bords de
    // longueur ~L, d'où L = périmètre total / 2 ; W = diamètre inscrit maximal ; largeur moyenne
    // = aire / L.
    double perimeterUm = 0.0;
    const auto addPerimeter = [&perimeterUm](const geometry::Path& path) {
        for (std::size_t i = 0; i < path.nodes.size(); ++i) {
            perimeterUm +=
                length_um(path.nodes[(i + 1) % path.nodes.size()].pos - path.nodes[i].pos);
        }
    };
    addPerimeter(region.outer);
    for (const auto& hole : region.holes) {
        addPerimeter(hole);
    }
    BandShape shape;
    shape.length_um = perimeterUm / 2.0;
    shape.max_width_um = r.maximum_width_mm * 1000.0;
    shape.mean_width_um = shape.length_um > 0.0 ? r.area_mm2 * 1e6 / shape.length_um : 0.0;
    shape.min_satin_width_um =
        static_cast<double>(params.analysis.thresholds.min_satin_width.value);
    shape.min_elongation = params.analysis.thresholds.min_elongation;
    shape.band_like = shape.mean_width_um >= shape.min_satin_width_um &&
                      shape.max_width_um <= static_cast<double>(options.satin_max_width.value) &&
                      shape.length_um >= shape.min_elongation * shape.max_width_um;
    return shape;
}

// --- Satin guidé sur formes larges (HP-ENG-009) ---

namespace {

struct P2 {
    double x{0.0};
    double y{0.0};
};

P2 operator-(P2 a, P2 b) {
    return {a.x - b.x, a.y - b.y};
}
double dot(P2 a, P2 b) {
    return a.x * b.x + a.y * b.y;
}
double cross2(P2 a, P2 b) {
    return a.x * b.y - a.y * b.x;
}
P2 to_p2(Vec2um v) {
    return {static_cast<double>(v.x.value), static_cast<double>(v.y.value)};
}
Vec2um to_um(P2 p) {
    return {{static_cast<std::int32_t>(std::lround(p.x))},
            {static_cast<std::int32_t>(std::lround(p.y))}};
}

struct Edge {
    P2 a;
    P2 b;
};

// Arêtes du contour aplati (extérieur et trous).
std::vector<Edge> boundary_edges(const geometry::PathSet& region) {
    const geometry::PathSet flat = geometry::flatten_to_polygon(region, Micrometers{50});
    std::vector<Edge> edges;
    const auto add = [&edges](const geometry::Path& path) {
        const auto n = path.nodes.size();
        for (std::size_t i = 0; i < n && n >= 3; ++i) {
            edges.push_back({to_p2(path.nodes[i].pos), to_p2(path.nodes[(i + 1) % n].pos)});
        }
    };
    add(flat.outer);
    for (const auto& hole : flat.holes) {
        add(hole);
    }
    return edges;
}

// Direction unitaire de l'arête la plus proche de `p`.
P2 nearest_edge_direction(const std::vector<Edge>& edges, P2 p) {
    double best = std::numeric_limits<double>::max();
    P2 dir{1.0, 0.0};
    for (const Edge& e : edges) {
        const P2 ab = e.b - e.a;
        const double len2 = dot(ab, ab);
        if (len2 <= 0.0) {
            continue;
        }
        const double t = std::clamp(dot(p - e.a, ab) / len2, 0.0, 1.0);
        const P2 q{e.a.x + ab.x * t, e.a.y + ab.y * t};
        const double d2 = dot(p - q, p - q);
        if (d2 < best) {
            best = d2;
            const double len = std::sqrt(len2);
            dir = {ab.x / len, ab.y / len};
        }
    }
    return dir;
}

bool segments_cross(P2 a, P2 b, P2 c, P2 d) {
    const double abC = cross2(b - a, c - a);
    const double abD = cross2(b - a, d - a);
    const double cdA = cross2(d - c, a - c);
    const double cdB = cross2(d - c, b - c);
    return ((abC > 0 && abD < 0) || (abC < 0 && abD > 0)) &&
           ((cdA > 0 && cdB < 0) || (cdA < 0 && cdB > 0));
}

constexpr double kGlancingCos = 0.8660254037844386; // cos 30°
constexpr std::size_t kCrossWindow = 16;
constexpr std::size_t kMaxRows = 1500;
constexpr double kNominalSpacingUm = 400.0;
// Écart admis entre deux fils consécutifs d'un satin tournant (côté concave / côté convexe) :
// en dessous de 0,16 mm le fil casse (densité > 2,5x le nominal), au-dessus de 0,7 mm le jour
// devient visible.
constexpr double kMinPitchUm = 160.0;
constexpr double kMaxPitchUm = 700.0;

constexpr std::size_t kSmoothHalfWindow = 10; // x 100 µm : ±1 mm

double closed_length(const std::vector<P2>& poly) {
    double sum = 0.0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const P2 d = poly[(i + 1) % poly.size()] - poly[i];
        sum += std::hypot(d.x, d.y);
    }
    return sum;
}

// `n` points à abscisse curviligne régulière sur le polygone fermé `poly`.
std::vector<P2> resample_closed(const std::vector<P2>& poly, std::size_t n) {
    std::vector<double> lengths{0.0};
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const P2 d = poly[(i + 1) % poly.size()] - poly[i];
        lengths.push_back(lengths.back() + std::hypot(d.x, d.y));
    }
    const double total = lengths.back();
    std::vector<P2> out;
    std::size_t seg = 0;
    for (std::size_t k = 0; k < n; ++k) {
        const double s = total * static_cast<double>(k) / static_cast<double>(n);
        while (seg + 1 < poly.size() && lengths[seg + 1] < s) {
            ++seg;
        }
        const double span = lengths[seg + 1] - lengths[seg];
        const double t = span > 0.0 ? (s - lengths[seg]) / span : 0.0;
        const P2 a = poly[seg];
        const P2 b = poly[(seg + 1) % poly.size()];
        out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
    }
    return out;
}

// Ligne centrale d'un anneau : contour extérieur rentré de la demi-largeur moyenne, fermée en
// répétant son premier point. Vide si l'anneau n'a pas de contour utilisable.
std::vector<Vec2um> ring_guide(const geometry::PathSet& region, double meanWidthUm) {
    geometry::PathSet outerOnly;
    outerOnly.outer = region.outer;
    const auto inset = geometry::inset_path_set(
        outerOnly, Micrometers{static_cast<std::int32_t>(std::lround(meanWidthUm / 2.0))});
    if (!inset || inset->empty()) {
        return {};
    }
    const auto biggest =
        std::max_element(inset->begin(), inset->end(), [](const auto& a, const auto& b) {
            return net_area_um2(a) < net_area_um2(b);
        });
    const geometry::Path flat = geometry::flatten_to_polygon(biggest->outer, Micrometers{5});
    std::vector<P2> base;
    for (const auto& node : flat.nodes) {
        base.push_back(to_p2(node.pos));
    }
    if (base.size() < 3) {
        return {};
    }
    // Le contour vectorisé est un polygone à gros côtés et le décalage laisse des segments très
    // inégaux : la tangente, donc la direction des fils, serait saccadée (fils croisés aux coins).
    // On rééchantillonne finement, on arrondit les coins (moyenne mobile, 2 passes de ±1 mm),
    // puis on rééchantillonne au pas des stations du moteur.
    const double total0 = closed_length(base);
    base = resample_closed(
        base, std::max<std::size_t>(16, static_cast<std::size_t>(std::ceil(total0 / 100.0))));
    for (int pass = 0; pass < 2; ++pass) {
        const std::size_t n = base.size();
        std::vector<P2> next(n);
        for (std::size_t i = 0; i < n; ++i) {
            P2 sum;
            for (std::size_t w = 0; w <= 2 * kSmoothHalfWindow; ++w) {
                const P2 p = base[(i + n + w - kSmoothHalfWindow) % n];
                sum = {sum.x + p.x, sum.y + p.y};
            }
            next[i] = {sum.x / (2 * kSmoothHalfWindow + 1), sum.y / (2 * kSmoothHalfWindow + 1)};
        }
        base = std::move(next);
    }
    // Autant de segments que de stations du moteur guidé (une station tous les `spacing` le long
    // de la ligne, au milieu de chaque segment) : la direction des fils tourne alors d'un pas
    // régulier, sans le sautillement d'une tangente prise sur des segments de longueurs inégales.
    const auto toGuide = [&](std::size_t samples) {
        std::vector<Vec2um> out;
        for (const P2 p : resample_closed(base, samples)) {
            out.push_back(to_um(p));
        }
        out.push_back(out.front());
        return out;
    };
    const auto perimeter = [](const std::vector<Vec2um>& poly) {
        double sum = 0.0;
        for (std::size_t i = 1; i < poly.size(); ++i) {
            sum += std::hypot(to_p2(poly[i]).x - to_p2(poly[i - 1]).x,
                              to_p2(poly[i]).y - to_p2(poly[i - 1]).y);
        }
        return sum;
    };
    std::size_t samples = std::max<std::size_t>(
        16, static_cast<std::size_t>(std::ceil(closed_length(base) / kNominalSpacingUm)));
    std::vector<Vec2um> guide = toGuide(samples);
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto engineCount = std::max<std::size_t>(
            2, static_cast<std::size_t>(std::ceil(perimeter(guide) / kNominalSpacingUm)));
        if (engineCount == samples || engineCount < 16) {
            break;
        }
        samples = engineCount;
        guide = toGuide(samples);
    }
    return guide;
}

} // namespace

ThreadFieldMetrics measure_thread_field(const geometry::PathSet& region,
                                        const std::vector<std::pair<Vec2um, Vec2um>>& threads,
                                        const std::vector<std::size_t>& column_starts) {
    ThreadFieldMetrics m;
    m.thread_count = threads.size();
    if (threads.empty()) {
        return m;
    }
    const auto edges = boundary_edges(region);
    const auto unit = [](P2 v) {
        const double len = std::hypot(v.x, v.y);
        return len > 0.0 ? P2{v.x / len, v.y / len} : P2{1.0, 0.0};
    };

    // Alignement avec le bord : échantillon régulier de ~200 fils.
    const std::size_t stride = std::max<std::size_t>(1, threads.size() / 200);
    double sum = 0.0;
    std::size_t ends = 0;
    std::size_t glancing = 0;
    for (std::size_t i = 0; i < threads.size(); i += stride) {
        const P2 a = to_p2(threads[i].first);
        const P2 b = to_p2(threads[i].second);
        const P2 d = unit(b - a);
        for (const P2 end : {a, b}) {
            const double c = std::abs(dot(d, nearest_edge_direction(edges, end)));
            sum += c;
            glancing += c > kGlancingCos ? 1 : 0;
            ++ends;
        }
    }
    m.edge_misalignment = ends == 0 ? 0.0 : sum / static_cast<double>(ends);
    m.glancing_rate = ends == 0 ? 0.0 : static_cast<double>(glancing) / static_cast<double>(ends);

    // Régularité de la direction (au sein d'une colonne).
    std::vector<bool> starts(threads.size() + 1, false);
    for (const std::size_t s : column_starts) {
        if (s < starts.size()) {
            starts[s] = true;
        }
    }
    for (const auto& t : threads) {
        m.max_thread_um =
            std::max(m.max_thread_um, std::hypot(to_p2(t.second).x - to_p2(t.first).x,
                                                 to_p2(t.second).y - to_p2(t.first).y));
    }
    m.min_pitch_um = std::numeric_limits<double>::max();
    double turnSum = 0.0;
    std::size_t turns = 0;
    for (std::size_t i = 1; i < threads.size(); ++i) {
        if (starts[i]) {
            continue;
        }
        for (const double pitch :
             {std::hypot(to_p2(threads[i].first).x - to_p2(threads[i - 1].first).x,
                         to_p2(threads[i].first).y - to_p2(threads[i - 1].first).y),
              std::hypot(to_p2(threads[i].second).x - to_p2(threads[i - 1].second).x,
                         to_p2(threads[i].second).y - to_p2(threads[i - 1].second).y)}) {
            m.min_pitch_um = std::min(m.min_pitch_um, pitch);
            m.max_pitch_um = std::max(m.max_pitch_um, pitch);
        }
        const P2 d0 = unit(to_p2(threads[i - 1].second) - to_p2(threads[i - 1].first));
        const P2 d1 = unit(to_p2(threads[i].second) - to_p2(threads[i].first));
        const double turn =
            std::acos(std::clamp(std::abs(dot(d0, d1)), 0.0, 1.0)) * 180.0 / std::numbers::pi;
        m.max_turn_deg = std::max(m.max_turn_deg, turn);
        turnSum += turn;
        ++turns;
    }
    m.mean_turn_deg = turns == 0 ? 0.0 : turnSum / static_cast<double>(turns);
    if (turns == 0) {
        m.min_pitch_um = 0.0;
    }

    // Rails croisés : chaque fil contre ses voisins proches, et les deux bouts d'une colonne
    // fermée (anneau) l'un contre l'autre.
    const auto crossPair = [&](std::size_t i, std::size_t j) {
        return segments_cross(to_p2(threads[i].first), to_p2(threads[i].second),
                              to_p2(threads[j].first), to_p2(threads[j].second));
    };
    const std::size_t n = threads.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < std::min(n, i + 1 + kCrossWindow); ++j) {
            m.crossings += crossPair(i, j) ? 1 : 0;
        }
    }
    if (n > 2 * kCrossWindow) {
        for (std::size_t i = 0; i < kCrossWindow; ++i) {
            for (std::size_t j = n - kCrossWindow; j < n; ++j) {
                m.crossings += crossPair(i, j) ? 1 : 0;
            }
        }
    }
    return m;
}

std::vector<std::pair<Vec2um, Vec2um>> fill_threads(const geometry::PathSet& region, Angle angle,
                                                    Micrometers spacing) {
    std::vector<std::pair<Vec2um, Vec2um>> out;
    const auto edges = boundary_edges(region);
    if (edges.empty()) {
        return out;
    }
    const P2 d{std::cos(angle.radians), std::sin(angle.radians)};
    const P2 nrm{-d.y, d.x};
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    for (const Edge& e : edges) {
        lo = std::min(lo, dot(e.a, nrm));
        hi = std::max(hi, dot(e.a, nrm));
    }
    double step = std::max(50.0, static_cast<double>(spacing.value));
    step = std::max(step, (hi - lo) / static_cast<double>(kMaxRows));
    std::vector<double> us;
    for (double s = lo + step / 2.0; s < hi; s += step) {
        us.clear();
        for (const Edge& e : edges) {
            const double sa = dot(e.a, nrm) - s;
            const double sb = dot(e.b, nrm) - s;
            if ((sa < 0.0) != (sb < 0.0)) {
                const double t = sa / (sa - sb);
                const P2 q{e.a.x + (e.b.x - e.a.x) * t, e.a.y + (e.b.y - e.a.y) * t};
                us.push_back(dot(q, d));
            }
        }
        std::sort(us.begin(), us.end());
        for (std::size_t k = 0; k + 1 < us.size(); k += 2) {
            if (us[k + 1] - us[k] < 300.0) {
                continue;
            }
            out.emplace_back(to_um({nrm.x * s + d.x * us[k], nrm.y * s + d.y * us[k]}),
                             to_um({nrm.x * s + d.x * us[k + 1], nrm.y * s + d.y * us[k + 1]}));
        }
    }
    return out;
}

Angle natural_fill_angle(const geometry::PathSet& region, const AutoOptions& options) {
    const auto axis = geometry::principal_axis({region});
    return axis && axis->anisotropy >= options.fill_isotropy_ratio ? axis->angle
                                                                   : options.isotropic_fill_angle;
}

std::optional<WideSatinPlan> plan_wide_guided_satin(const geometry::PathSet& region,
                                                    const AutoOptions& options) {
    if (region.holes.size() > 1) {
        return std::nullopt;
    }
    const auto shape = measure_band_shape(region, options);
    if (!shape) {
        return std::nullopt;
    }
    WideSatinPlan plan;
    auto_satin::SkeletonSatinParameters params;
    params.measure_coverage = true;
    if (region.holes.size() == 1) {
        plan.guide = ring_guide(region, shape->mean_width_um);
        if (plan.guide.empty()) {
            return std::nullopt;
        }
        params.guides.push_back({plan.guide.front(), 0.0, false, plan.guide});
    }
    const auto generated = auto_satin::generate_guided_satin(region, params);
    if (!generated || generated->columns.empty() || !generated->diagnostics.coverage_measured) {
        return std::nullopt;
    }
    plan.coverage = generated->diagnostics.coverage_ratio;
    plan.columns = generated->columns.size();

    std::vector<std::pair<Vec2um, Vec2um>> threads;
    std::vector<std::size_t> starts;
    for (const auto& column : generated->columns) {
        starts.push_back(threads.size());
        for (const auto& c : column.crossings) {
            threads.emplace_back(c.a, c.b);
        }
    }
    plan.guided = measure_thread_field(region, threads, starts);
    plan.fill = measure_thread_field(
        region, fill_threads(region, natural_fill_angle(region, options), Micrometers{400}), {});

    const double maxThread = static_cast<double>(
        std::max(options.satin_compact_max_width.value, options.satin_max_width.value));
    const bool pitchOk =
        plan.guided.min_pitch_um >= kMinPitchUm && plan.guided.max_pitch_um <= kMaxPitchUm;
    plan.better = plan.coverage >= options.satin_min_coverage && plan.guided.crossings == 0 &&
                  plan.columns <= 2 && plan.guided.max_turn_deg <= 12.0 &&
                  plan.guided.max_thread_um <= maxThread && pitchOk &&
                  plan.guided.edge_misalignment <= 0.7 * plan.fill.edge_misalignment &&
                  plan.guided.glancing_rate <= plan.fill.glancing_rate;
    return plan;
}

AutoQualityReport measure_auto_quality(const std::vector<document::VectorObject>& vectors,
                                       const std::vector<document::EmbroideryObject>& embroideries,
                                       const AutoOptions& options) {
    std::map<std::uint64_t, const document::VectorObject*> byId;
    for (const auto& v : vectors) {
        byId.emplace(v.id.value, &v);
    }
    AutoQualityReport rep;
    rep.object_count = embroideries.size();
    for (const auto& e : embroideries) {
        const geometry::PathSet* piece = nullptr;
        if (const auto it = byId.find(e.source_vector.value); it != byId.end()) {
            const auto& sets = it->second->paths;
            if (!sets.empty()) {
                piece =
                    &*std::max_element(sets.begin(), sets.end(), [](const auto& a, const auto& b) {
                        return net_area_um2(a) < net_area_um2(b);
                    });
            }
        }
        const double areaMm2 = piece ? net_area_um2(*piece) / 1e6 : 0.0;
        const auto shape = piece ? measure_band_shape(*piece, options) : std::nullopt;

        const bool satin = e.is_auto_satin() || e.is_satin();
        if (satin) {
            ++rep.satin_count;
            rep.satin_area_mm2 += areaMm2;
            const bool compact =
                e.is_auto_satin() && std::get<document::AutoSatinParams>(e.params).compact_axis;
            rep.compact_satin_count += compact ? 1 : 0;
            const bool guided =
                e.is_auto_satin() && std::get<document::AutoSatinParams>(e.params).strategy ==
                                         document::AutoSatinStrategy::Guided;
            rep.guided_satin_count += guided ? 1 : 0;
            if (shape) {
                const double widthMm = shape->max_width_um / 1000.0;
                rep.satin_max_width_mm = std::max(rep.satin_max_width_mm, widthMm);
                // Un satin guidé large (HP-ENG-009) a ses fils bornés par le plus grand des deux
                // maxima (même borne que `plan_wide_guided_satin`).
                const double limit = static_cast<double>(
                    guided ? std::max(options.satin_compact_max_width.value,
                                      options.satin_max_width.value)
                           : (compact ? options.satin_compact_max_width : options.satin_max_width)
                                 .value);
                // Une forme compacte réellement plus large que `satin_max_width` est autorisée
                // jusqu'à `satin_compact_max_width` seulement ; un satin manuel n'est pas jugé.
                if (e.is_auto_satin() && shape->max_width_um > limit) {
                    ++rep.satin_over_max_count;
                }
            }
        } else if (e.is_tatami() || e.is_directional()) {
            ++rep.fill_count;
            rep.fill_area_mm2 += areaMm2;
        } else if (std::holds_alternative<document::RunningStitchParams>(e.params)) {
            ++rep.contour_count;
            rep.contour_area_mm2 += areaMm2;
        } else {
            ++rep.other_count;
        }

        if (shape && shape->band_like && piece && piece->holes.size() <= 1 &&
            areaMm2 >= options.min_fill_area_mm2) {
            ++rep.thin_band_count;
            rep.thin_band_satin_count += satin ? 1 : 0;
        }
    }
    rep.thin_band_satin_rate = rep.thin_band_count == 0
                                   ? 1.0
                                   : static_cast<double>(rep.thin_band_satin_count) /
                                         static_cast<double>(rep.thin_band_count);
    rep.embroidered_area_mm2 = rep.satin_area_mm2 + rep.fill_area_mm2 + rep.contour_area_mm2;
    return rep;
}

} // namespace openstitch::autodigitize
