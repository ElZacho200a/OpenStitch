// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/contour_objects.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

#include "openstitch/geometry/clean.hpp"
#include "openstitch/geometry/polyline.hpp"

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
    const double w = seg.mean_width_um;
    // Point droit : simple sous la moitie de l'ancien seuil satin minimal,
    // triple au-dessus (un trait plus large demande plus de matiere).
    const ContourStrategy run =
        w < minSatin / 2.0 ? ContourStrategy::SingleRun : ContourStrategy::TripleRun;

    plan.strategy = run;
    if (technique == ContourTechnique::Satin) {
        plan.fallback = true;
        plan.reason = "satin automatique desactive : point droit";
    }
    return plan;
}

namespace {

using geometry::PathNode;

PathNode corner(Vec2um p) {
    return {p, geometry::NodeType::Corner, std::nullopt, std::nullopt};
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
