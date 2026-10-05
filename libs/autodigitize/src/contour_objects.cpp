// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/contour_objects.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/clean.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/satin_planning/satin_sections.hpp"

namespace openstitch::autodigitize {

namespace {

std::string fmt_mm(double um) {
    std::ostringstream s;
    s.setf(std::ios::fixed);
    s.precision(2);
    s << um / 1000.0;
    return s.str();
}

} // namespace

SegmentPlan classify_segment(const ContourSegment& seg, ContourTechnique technique,
                             const ContourLimits& lim) {
    SegmentPlan plan;
    const double minLen = static_cast<double>(lim.min_element_length.value);
    if (seg.length_um < minLen) {
        plan.reason = "segment trop court (" + fmt_mm(seg.length_um) + " mm < " + fmt_mm(minLen) +
                      " mm) : aucun point possible";
        return plan;
    }
    if (seg.closed && seg.length_um < static_cast<double>(lim.min_loop_perimeter.value)) {
        plan.reason = "boucle trop petite (" + fmt_mm(seg.length_um) + " mm de perimetre)";
        return plan;
    }
    const double minSatin = static_cast<double>(lim.min_satin_width.value);
    const double maxSatin = static_cast<double>(lim.max_satin_width.value);
    const double w = seg.mean_width_um;
    // Point droit : simple sous la moitie de la largeur satin minimale, triple
    // au-dessus (un trait plus large demande plus de matiere).
    const ContourStrategy run =
        w < minSatin / 2.0 ? ContourStrategy::SingleRun : ContourStrategy::TripleRun;

    if (technique == ContourTechnique::Running) {
        plan.strategy = run;
        return plan;
    }
    if (w < minSatin) {
        plan.strategy = run;
        if (technique == ContourTechnique::Satin) {
            plan.fallback = true;
            plan.reason = "largeur " + fmt_mm(w) + " mm < minimum satin " + fmt_mm(minSatin) +
                          " mm : point droit";
        }
        return plan;
    }
    if (w > maxSatin) {
        plan.strategy = ContourStrategy::TripleRun;
        plan.fallback = true;
        plan.reason = "largeur " + fmt_mm(w) + " mm > maximum satin " + fmt_mm(maxSatin) +
                      " mm : point triple sur la ligne mediane (un remplissage serait plus adapte)";
        return plan;
    }
    if (technique == ContourTechnique::Automatic) {
        const double variation = seg.mean_width_um > 0.0
                                     ? (seg.max_width_um - seg.min_width_um) / seg.mean_width_um
                                     : 0.0;
        if (variation > lim.max_width_variation) {
            plan.strategy = run;
            plan.fallback = true;
            plan.reason =
                "largeur irreguliere (variation " + fmt_mm(variation * 1000.0) + ") : point droit";
            return plan;
        }
        if (seg.max_turn_deg > lim.max_turn_deg) {
            plan.strategy = run;
            plan.fallback = true;
            plan.reason = "virage brusque (" + std::to_string(static_cast<int>(seg.max_turn_deg)) +
                          " deg) : point droit";
            return plan;
        }
    }
    plan.strategy = ContourStrategy::Satin;
    return plan;
}

namespace {

using geometry::Path;
using geometry::PathNode;

PathNode corner(Vec2um p) {
    return {p, geometry::NodeType::Corner, std::nullopt, std::nullopt};
}

// Bande autour d'une ligne mediane (largeur ~ largeur locale), prolongee d'un
// demi-trait aux bouts pour recouvrir la confluence.
Path strip_polygon(const std::vector<Vec2um>& pts, const std::vector<double>& half) {
    std::vector<double> sorted = half;
    std::sort(sorted.begin(), sorted.end());
    const double med = sorted[sorted.size() / 2];
    const std::size_t n = pts.size();
    std::vector<double> nx(n), ny(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2um a = pts[i == 0 ? 0 : i - 1];
        const Vec2um b = pts[i + 1 < n ? i + 1 : n - 1];
        const double dx = static_cast<double>(b.x.value - a.x.value);
        const double dy = static_cast<double>(b.y.value - a.y.value);
        const double l = std::max(1.0, std::hypot(dx, dy));
        nx[i] = -dy / l;
        ny[i] = dx / l;
    }
    std::vector<Vec2um> left;
    std::vector<Vec2um> right;
    for (std::size_t i = 0; i < n; ++i) {
        const double h = std::clamp(half[i], 0.7 * med, 1.3 * med) * 1.25 + 50.0;
        double ex = 0.0;
        double ey = 0.0;
        if (i == 0 || i + 1 == n) {
            const Vec2um a = pts[i == 0 ? 1 : n - 2];
            const Vec2um b = pts[i];
            const double dx = static_cast<double>(b.x.value - a.x.value);
            const double dy = static_cast<double>(b.y.value - a.y.value);
            const double l = std::max(1.0, std::hypot(dx, dy));
            ex = dx / l * med; // vers l'exterieur du bout
            ey = dy / l * med;
        }
        const auto mk = [&](double s) {
            return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(
                              static_cast<double>(pts[i].x.value) + s * nx[i] * h + ex))},
                          Micrometers{static_cast<std::int32_t>(std::lround(
                              static_cast<double>(pts[i].y.value) + s * ny[i] * h + ey))}};
        };
        left.push_back(mk(1.0));
        right.push_back(mk(-1.0));
    }
    Path poly;
    poly.closed = true;
    for (const Vec2um p : left) {
        poly.nodes.push_back(corner(p));
    }
    for (std::size_t i = right.size(); i-- > 0;) {
        poly.nodes.push_back(corner(right[i]));
    }
    return poly;
}

std::vector<Path> strips_for(const ContourSegment& s) {
    std::vector<Path> out;
    if (s.centerline.size() < 2) {
        return out;
    }
    if (s.closed) {
        std::vector<Vec2um> p = s.centerline;
        std::vector<double> h = s.half_width_um;
        p.push_back(p.front());
        h.push_back(h.front());
        const std::size_t mid = p.size() / 2;
        out.push_back(strip_polygon({p.begin(), p.begin() + static_cast<std::ptrdiff_t>(mid) + 1},
                                    {h.begin(), h.begin() + static_cast<std::ptrdiff_t>(mid) + 1}));
        out.push_back(strip_polygon({p.begin() + static_cast<std::ptrdiff_t>(mid), p.end()},
                                    {h.begin() + static_cast<std::ptrdiff_t>(mid), h.end()}));
    } else {
        out.push_back(strip_polygon(s.centerline, s.half_width_um));
    }
    return out;
}

bool point_in_polygon(Vec2um p, const std::vector<Vec2um>& poly) {
    bool in = false;
    const double px = static_cast<double>(p.x.value);
    const double py = static_cast<double>(p.y.value);
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const double xi = static_cast<double>(poly[i].x.value);
        const double yi = static_cast<double>(poly[i].y.value);
        const double xj = static_cast<double>(poly[j].x.value);
        const double yj = static_cast<double>(poly[j].y.value);
        if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) {
            in = !in;
        }
    }
    return in;
}

// Part de la ligne mediane (echantillonnee tous les 0,4 mm) couverte par les
// bandes satin.
double covered_fraction(const ContourSegment& s, const std::vector<std::vector<Vec2um>>& strips) {
    std::vector<Vec2um> p = s.centerline;
    if (s.closed) {
        p.push_back(p.front());
    }
    const auto cum = geometry::cumulative_lengths(p);
    const double total = cum.back();
    const int n = std::max(2, static_cast<int>(total / 400.0));
    int inside = 0;
    for (int i = 0; i <= n; ++i) {
        const Vec2um q = geometry::point_at_length(p, cum, total * i / n);
        for (const auto& poly : strips) {
            if (poly.size() >= 3 && point_in_polygon(q, poly)) {
                ++inside;
                break;
            }
        }
    }
    return static_cast<double>(inside) / static_cast<double>(n + 1);
}

std::vector<Vec2um> oriented(const ContourSegment& s, bool forward) {
    std::vector<Vec2um> p = s.centerline;
    if (!forward) {
        std::reverse(p.begin(), p.end());
    }
    return p;
}

struct ChainOut {
    std::vector<Vec2um> pts;
    bool closed{false};
};

Vec2um direction_from(const ContourSegment& s, int port) {
    const std::vector<Vec2um> p = oriented(s, port == 0);
    const Vec2um a = p.front();
    double acc = 0.0;
    for (std::size_t i = 1; i < p.size(); ++i) {
        acc += length_um(p[i] - p[i - 1]);
        if (acc >= 1000.0 || i + 1 == p.size()) {
            return p[i] - a;
        }
    }
    return Vec2um{};
}

// Enchaine les segments point droit en polylignes maximales : a un noeud de
// degre 2 on continue toujours ; a une jonction on apparie les branches les
// plus alignees (un X donne deux lignes traversantes, jamais quatre).
std::vector<ChainOut> assemble_chains(const ContourComponent& comp,
                                      const std::vector<std::size_t>& segs) {
    struct Port {
        std::size_t seg;
        int port;
    };
    std::map<std::int32_t, std::vector<Port>> atNode;
    for (const std::size_t si : segs) {
        const auto& s = comp.segments[si];
        if (s.start_node >= 0) {
            atNode[s.start_node].push_back({si, 0});
        }
        if (s.end_node >= 0) {
            atNode[s.end_node].push_back({si, 1});
        }
    }
    std::map<std::pair<std::size_t, int>, Port> partner;
    for (auto& [node, ports] : atNode) {
        (void)node;
        std::vector<bool> used(ports.size(), false);
        for (;;) {
            double bestDot = 2.0;
            std::size_t bi = 0;
            std::size_t bj = 0;
            for (std::size_t i = 0; i < ports.size(); ++i) {
                for (std::size_t j = i + 1; j < ports.size(); ++j) {
                    if (used[i] || used[j]) {
                        continue;
                    }
                    const Vec2um da = direction_from(comp.segments[ports[i].seg], ports[i].port);
                    const Vec2um db = direction_from(comp.segments[ports[j].seg], ports[j].port);
                    const double la = std::max(1.0, length_um(da));
                    const double lb = std::max(1.0, length_um(db));
                    const double dot = (static_cast<double>(da.x.value) * db.x.value +
                                        static_cast<double>(da.y.value) * db.y.value) /
                                       (la * lb);
                    if (dot < bestDot - 1e-12) {
                        bestDot = dot;
                        bi = i;
                        bj = j;
                    }
                }
            }
            // Jonction (>= 3) : on exige un virage < 90 deg ; degre 2 : toujours.
            const bool accept = bestDot <= 1.0 && (ports.size() == 2 || bestDot < 0.0);
            if (!accept) {
                break;
            }
            used[bi] = used[bj] = true;
            partner[{ports[bi].seg, ports[bi].port}] = ports[bj];
            partner[{ports[bj].seg, ports[bj].port}] = ports[bi];
        }
    }

    std::vector<ChainOut> chains;
    std::map<std::size_t, bool> visited;
    const auto walk = [&](std::size_t first, int startPort, bool closedExpected) {
        ChainOut chain;
        std::size_t cur = first;
        int enter = startPort;
        for (;;) {
            visited[cur] = true;
            const std::vector<Vec2um> p = oriented(comp.segments[cur], enter == 0);
            chain.pts.insert(chain.pts.end(), chain.pts.empty() ? p.begin() : p.begin() + 1,
                             p.end());
            const int exitPort = 1 - enter;
            const auto it = partner.find({cur, exitPort});
            if (it == partner.end()) {
                break;
            }
            if (visited[it->second.seg]) {
                chain.closed = closedExpected;
                break;
            }
            cur = it->second.seg;
            enter = it->second.port;
        }
        if (chain.closed && chain.pts.size() > 1 && chain.pts.front() == chain.pts.back()) {
            chain.pts.pop_back();
        }
        chains.push_back(std::move(chain));
    };
    for (const std::size_t si : segs) {
        const auto& s = comp.segments[si];
        if (s.closed) {
            visited[si] = true;
            chains.push_back({s.centerline, true});
        }
    }
    for (const std::size_t si : segs) {
        if (visited[si]) {
            continue;
        }
        const bool p0 = partner.contains({si, 0});
        const bool p1 = partner.contains({si, 1});
        if (!p0) {
            walk(si, 0, false);
        } else if (!p1) {
            walk(si, 1, false);
        }
    }
    for (const std::size_t si : segs) {
        if (!visited[si]) {
            walk(si, 0, true);
        }
    }
    return chains;
}

document::VectorObject make_vector(ObjectId id, std::string name, std::array<std::uint8_t, 3> rgb,
                                   std::vector<geometry::PathSet> paths) {
    document::VectorObject v;
    v.id = id;
    v.name = std::move(name);
    v.rgb = rgb;
    v.paths = std::move(paths);
    return v;
}

} // namespace

Result<AutoResult> build_contour_objects(const ContourNetwork& net, IdGenerator<ObjectId>& ids,
                                         const ContourOptions& options,
                                         ContourMetrics* metricsOut) {
    ContourMetrics m;
    AutoResult result;
    const ContourLimits lim = contour_limits();
    m.components = net.components.size();
    m.rejected = net.failed_components;
    m.removed_small_elements = net.removed_isolated;
    for (const auto& d : net.diagnostics) {
        result.warnings.push_back(d);
    }

    // Groupes de couleur : clairs d'abord, le plus sombre en dernier.
    std::vector<std::size_t> order(net.components.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const auto& ca = net.components[a].rgb;
        const auto& cb = net.components[b].rgb;
        if (ca == cb) {
            return false;
        }
        const double la = segmentation::cielab_lightness(ca);
        const double lb = segmentation::cielab_lightness(cb);
        return la != lb ? la > lb : ca < cb;
    });

    double widthMin = 1e18;
    double widthMax = 0.0;
    double widthSum = 0.0;
    double widthLen = 0.0;
    std::size_t counter = 0;

    for (const std::size_t ci : order) {
        const ContourComponent& comp = net.components[ci];
        const std::string label = "Contour " + std::to_string(++counter);
        m.segments += comp.segments.size();
        m.junctions += comp.junction_count();
        m.endpoints += comp.endpoint_count();
        m.removed_short_branches += comp.removed_short_branches;
        m.removed_small_elements += comp.removed_small_loops;

        std::vector<SegmentPlan> plans;
        plans.reserve(comp.segments.size());
        for (std::size_t si = 0; si < comp.segments.size(); ++si) {
            plans.push_back(classify_segment(comp.segments[si], options.technique, lim));
        }

        // --- Satin : sous-reseau satinable ---------------------------------
        std::vector<std::size_t> satinSegs;
        bool wholeRegion = true;
        for (std::size_t si = 0; si < plans.size(); ++si) {
            if (plans[si].strategy == ContourStrategy::Satin) {
                satinSegs.push_back(si);
            } else if (plans[si].strategy != ContourStrategy::Rejected ||
                       comp.segments[si].length_um >=
                           static_cast<double>(lim.min_element_length.value)) {
                wholeRegion = false;
            }
        }
        std::vector<geometry::PathSet> satinRegions;
        if (!satinSegs.empty()) {
            // Region entiere seulement si rien n'a ete elague : les moignons
            // elagues restent dans le polygone et feraient refuser les colonnes
            // voisines ("rail hors region") ; on les decoupe avec les bandes.
            if (wholeRegion && comp.removed_short_branches == 0) {
                satinRegions.push_back(comp.region);
            } else {
                std::vector<Path> strips;
                for (const std::size_t si : satinSegs) {
                    for (auto& p : strips_for(comp.segments[si])) {
                        strips.push_back(std::move(p));
                    }
                }
                const auto uni = geometry::union_nonzero(strips);
                if (uni) {
                    const auto cut = geometry::intersect_polygons({comp.region}, *uni);
                    if (cut) {
                        satinRegions = *cut;
                    }
                }
            }
        }
        std::vector<satin_planning::BuiltSatinSection> sections;
        std::vector<std::size_t> sectionRegion;
        std::vector<std::vector<Vec2um>> flatStrips;
        if (!satinRegions.empty()) {
            auto_satin::SatinColumnsParameters sp;
            sp.analysis.cleanup.minimum_branch_length = net.thresholds.min_branch_length;
            sp.analysis.thresholds.min_satin_width = lim.min_satin_width;
            sp.analysis.thresholds.max_satin_width = lim.max_satin_width;
            sp.geometry_mode = auto_satin::SatinGeometryMode::Parametric;
            const document::SatinParams defaults;
            for (std::size_t ri = 0; ri < satinRegions.size(); ++ri) {
                auto built = satin_planning::build_satin_sections(
                    satinRegions[ri], sp, defaults.density, defaults.pull_compensation,
                    defaults.center_underlay, lim.max_satin_width, label);
                for (auto& w : built.warnings) {
                    result.warnings.push_back(std::move(w));
                }
                for (auto& s : built.sections) {
                    flatStrips.push_back(geometry::flatten(s.strip, Micrometers{50}).points);
                    sectionRegion.push_back(ri);
                    sections.push_back(std::move(s));
                }
            }
        }
        // Couverture reelle par segment : un segment satin mal couvert retombe
        // en point droit, avec diagnostic.
        for (const std::size_t si : satinSegs) {
            const double frac = covered_fraction(comp.segments[si], flatStrips);
            if (frac < 0.7) {
                plans[si].strategy = comp.segments[si].mean_width_um <
                                             static_cast<double>(lim.min_satin_width.value) / 2.0
                                         ? ContourStrategy::SingleRun
                                         : ContourStrategy::TripleRun;
                plans[si].fallback = true;
                plans[si].reason = "couverture satin insuffisante (" +
                                   std::to_string(static_cast<int>(frac * 100.0)) +
                                   " %) : point droit";
            }
        }

        // Diagnostics et metriques par segment.
        for (std::size_t si = 0; si < plans.size(); ++si) {
            const auto& pl = plans[si];
            const auto& s = comp.segments[si];
            if (pl.fallback || pl.strategy == ContourStrategy::Rejected) {
                result.warnings.push_back(label + " segment " + std::to_string(si + 1) + " (" +
                                          fmt_mm(s.length_um) + " mm) : " + pl.reason);
            }
            if (pl.fallback) {
                ++m.fallbacks;
            }
            if (pl.strategy == ContourStrategy::Rejected) {
                ++m.rejected;
                continue;
            }
            widthMin = std::min(widthMin, s.min_width_um);
            widthMax = std::max(widthMax, s.max_width_um);
            widthSum += s.mean_width_um * s.length_um;
            widthLen += s.length_um;
            if (pl.strategy == ContourStrategy::Satin) {
                m.satin_length_mm += s.length_um / 1000.0;
            }
        }

        // --- Emission satin -------------------------------------------------
        bool anySatin = false;
        for (const auto& pl : plans) {
            anySatin = anySatin || pl.strategy == ContourStrategy::Satin;
        }
        if (anySatin) {
            std::map<std::size_t, ObjectId> vecOf;
            std::size_t emitted = 0;
            for (std::size_t k = 0; k < sections.size(); ++k) {
                const std::size_t ri = sectionRegion[k];
                if (!vecOf.contains(ri)) {
                    const ObjectId vid = ids.next();
                    vecOf[ri] = vid;
                    result.vectors.push_back(
                        make_vector(vid, label + " (satin)", comp.rgb, {satinRegions[ri]}));
                }
                document::EmbroideryObject e;
                e.id = ids.next();
                e.source_vector = vecOf[ri];
                e.rgb = comp.rgb;
                e.intent = document::EmbroideryIntent::AutoChoice;
                e.name = "Satin " + label + " - section " + std::to_string(++emitted);
                e.params = std::move(sections[k].params);
                result.embroideries.push_back(std::move(e));
            }
        }

        // --- Emission point droit ------------------------------------------
        for (const ContourStrategy kind :
             {ContourStrategy::SingleRun, ContourStrategy::TripleRun}) {
            std::vector<std::size_t> segs;
            for (std::size_t si = 0; si < plans.size(); ++si) {
                if (plans[si].strategy == kind) {
                    segs.push_back(si);
                    m.running_length_mm += comp.segments[si].length_um / 1000.0;
                }
            }
            if (segs.empty()) {
                continue;
            }
            std::vector<geometry::PathSet> paths;
            for (auto& chain : assemble_chains(comp, segs)) {
                geometry::PathSet ps;
                ps.outer.closed = chain.closed;
                for (const Vec2um p : chain.pts) {
                    ps.outer.nodes.push_back(corner(p));
                }
                if (ps.outer.nodes.size() >= 2) {
                    paths.push_back(std::move(ps));
                }
            }
            if (paths.empty()) {
                continue;
            }
            const ObjectId vid = ids.next();
            result.vectors.push_back(make_vector(
                vid, label + (kind == ContourStrategy::SingleRun ? " (ligne)" : " (ligne triple)"),
                comp.rgb, std::move(paths)));
            document::EmbroideryObject e;
            e.id = ids.next();
            e.source_vector = vid;
            e.rgb = comp.rgb;
            e.intent = document::EmbroideryIntent::AutoChoice;
            document::RunningStitchParams rp;
            rp.repeats = kind == ContourStrategy::SingleRun ? 1 : 3;
            e.params = rp;
            e.name = "Contour " + label + (rp.repeats == 1 ? " (simple)" : " (triple)");
            result.embroideries.push_back(std::move(e));
        }
    }

    if (widthLen > 0.0) {
        m.min_width_mm = widthMin / 1000.0;
        m.max_width_mm = widthMax / 1000.0;
        m.mean_width_mm = widthSum / widthLen / 1000.0;
    }
    if (metricsOut != nullptr) {
        *metricsOut = m;
    }
    if (result.embroideries.empty()) {
        return fail(ErrorCategory::OperationImpossible,
                    "Aucun contour exploitable (tout est sous les seuils de detail ou les "
                    "garde-fous physiques)");
    }
    return result;
}

Result<AutoResult> auto_digitize_contours(const segmentation::Segmentation& seg,
                                          IdGenerator<ObjectId>& ids, const ContourOptions& options,
                                          ContourMetrics* metrics) {
    auto net = analyze_contours(seg, options);
    if (!net) {
        return std::unexpected(net.error());
    }
    return build_contour_objects(*net, ids, options, metrics);
}

} // namespace openstitch::autodigitize
