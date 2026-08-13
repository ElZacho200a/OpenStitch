// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/region_routing.hpp"

#include <sstream>

namespace openstitch::satin_planning {

namespace {

Vec2um midpoint(Vec2um a, Vec2um b) {
    return Vec2um{Micrometers{(a.x.value + b.x.value) / 2}, Micrometers{(a.y.value + b.y.value) / 2}};
}

// Reduit une colonne (rail_a/rail_b, communs a SatinColumnGeometry et
// ParametricSatinObject) a ses deux extremites : le centre du barreau
// d'about a chaque bout (les deux rails progressent dans le meme sens par
// construction, cf. la correspondance en echelle).
template <typename Column>
bool column_endpoints(const Column& col, Vec2um& start, Vec2um& end) {
    if (col.rail_a.nodes.empty() || col.rail_b.nodes.empty()) return false;
    start = midpoint(col.rail_a.nodes.front().pos, col.rail_b.nodes.front().pos);
    end = midpoint(col.rail_a.nodes.back().pos, col.rail_b.nodes.back().pos);
    return true;
}

}  // namespace

RegionRoutingReport route_regions(const RegionSplitReport& split, const RegionRoutingParams& params) {
    RegionRoutingReport report;

    std::vector<stitch_generation::RouteColumn> columns;
    std::vector<std::size_t> columnToRegion;  // index dans columns -> index dans report.regions

    for (const auto& region : split.regions) {
        RoutedRegion routed;
        routed.path_index = region.path_index;

        const auto built = auto_satin::build_satin_columns(region.region, params.genParams);
        Vec2um start{};
        Vec2um end{};
        bool haveEndpoints = false;
        if (built.refusal.empty()) {
            if (!built.parametric_columns.empty()) {
                haveEndpoints = column_endpoints(built.parametric_columns.front(), start, end);
            } else if (!built.columns.empty()) {
                haveEndpoints = column_endpoints(built.columns.front(), start, end);
            }
        }

        if (haveEndpoints) {
            routed.build_succeeded = true;
            routed.column.id = ObjectId{region.path_index + 1};
            routed.column.start = start;
            routed.column.end = end;
            // start_junction/end_junction laisses a nullopt : cf. limite
            // documentee dans region_routing.hpp (numerotation de jonctions
            // non partagee entre regions reanalysees independamment).
            columnToRegion.push_back(report.regions.size());
            columns.push_back(routed.column);
        }

        report.regions.push_back(routed);
    }

    const stitch_generation::RoutePlan plan = stitch_generation::route_columns(columns, params.origin, params.routingConfig);
    report.plan = plan;
    // Remappe les indices de colonnes (relatifs a `columns`) vers les
    // indices de `report.regions` pour que l'appelant n'ait pas a refaire
    // cette correspondance lui-meme.
    for (auto& step : report.plan.steps) {
        if (step.column_index < columnToRegion.size()) step.column_index = columnToRegion[step.column_index];
    }

    return report;
}

std::string format_region_routing_report(const RegionRoutingReport& report) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);

    out << "[SGSD phase 9 -- routage multi-regions]\n\n";
    for (const auto& r : report.regions) {
        out << "chemin " << r.path_index << " : " << (r.build_succeeded ? "construit" : "echec") << "\n";
    }

    out << "\nPlan :\n";
    for (const auto& step : report.plan.steps) {
        const char* kind = step.connector == stitch_generation::ConnectorKind::Start   ? "START"
                            : step.connector == stitch_generation::ConnectorKind::Underpath ? "UNDERPATH"
                                                                                              : "JUMP";
        out << "    region " << report.regions[step.column_index].path_index << " (" << (step.reversed ? "inverse" : "direct")
            << ") <- " << kind << "\n";
    }
    out << "\nTotal : deplacement=" << (report.plan.travel_um / 1000.0) << "mm sauts=" << report.plan.jumps
        << " trajets_caches=" << report.plan.underpaths << " liaisons_jonction=" << report.plan.junction_links << "\n";
    return out.str();
}

}  // namespace openstitch::satin_planning
