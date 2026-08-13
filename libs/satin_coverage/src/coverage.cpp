// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_coverage/coverage.hpp"

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/clean.hpp"
#include "openstitch/geometry/offset.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>

namespace openstitch::satin_coverage {

namespace {

using stitch_generation::SatinStation;

double cross_um(Vec2um o, Vec2um a, Vec2um b) {
    const double ox = static_cast<double>(o.x.value);
    const double oy = static_cast<double>(o.y.value);
    const double ax = static_cast<double>(a.x.value) - ox;
    const double ay = static_cast<double>(a.y.value) - oy;
    const double bx = static_cast<double>(b.x.value) - ox;
    const double by = static_cast<double>(b.y.value) - oy;
    return ax * by - ay * bx;
}

// Croisement transversal strict entre deux segments -- même test que celui
// utilisé en amont par `stitch_generation::satin.cpp` (dupliqué : fonction
// locale à quelques lignes, ne justifie pas d'exposer une primitive interne
// d'une autre bibliothèque pour si peu).
bool segments_cross_strict(Vec2um a, Vec2um b, Vec2um c, Vec2um d) {
    const double o1 = cross_um(a, b, c);
    const double o2 = cross_um(a, b, d);
    const double o3 = cross_um(c, d, a);
    const double o4 = cross_um(c, d, b);
    return (o1 > 0) != (o2 > 0) && (o3 > 0) != (o4 > 0) && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0;
}

geometry::Path make_triangle(Vec2um p0, Vec2um p1, Vec2um p2) {
    geometry::Path tri;
    tri.closed = true;
    tri.nodes = {
        geometry::PathNode{p0, geometry::NodeType::Corner, std::nullopt, std::nullopt},
        geometry::PathNode{p1, geometry::NodeType::Corner, std::nullopt, std::nullopt},
        geometry::PathNode{p2, geometry::NodeType::Corner, std::nullopt, std::nullopt},
    };
    // Oriente CCW (aire signée positive) avant de rejoindre l'union : contrairement
    // à `intersect_polygons`/`difference_polygons`, `union_nonzero` ne normalise PAS
    // l'orientation d'entrée. Deux triangles qui se chevauchent avec des orientations
    // opposées s'annuleraient localement sous la règle NonZero, créant un trou
    // parasite qui masquerait exactement le genre de défaut que cet analyseur doit
    // détecter -- cf. proposition d'architecture, "éviter qu'une mauvaise
    // correspondance ne cache une zone manquante".
    if (geometry::signed_area_um2(tri) < 0.0) {
        std::reverse(tri.nodes.begin(), tri.nodes.end());
    }
    return tri;
}

struct ColumnTriangles {
    std::vector<geometry::Path> triangles;
    std::size_t degenerate_intervals{0};
};

// Décompose une colonne en triangles de couverture, un quadrilatère
// (Li, Ri, Ri+1, Li+1) par intervalle entre deux stations consécutives,
// chacun scindé en deux triangles selon la diagonale Li-Ri+1 (robuste à un
// quadrilatère non convexe, cf. proposition §3). Une rupture de ruban
// (`jump_before`) coupe la colonne : aucun quadrilatère ne relie deux
// stations de part et d'autre d'un saut.
ColumnTriangles triangles_for_column(const SatinColumnInput& col) {
    ColumnTriangles out;
    const auto stations =
        stitch_generation::satin_stations(col.rail_a, col.rail_b, col.rungs, col.density);
    for (std::size_t i = 0; i + 1 < stations.size(); ++i) {
        if (stations[i + 1].jump_before) {
            continue;
        }
        const SatinStation& s0 = stations[i];
        const SatinStation& s1 = stations[i + 1];
        // Garde-fou défensif : l'appariement ladder amont garantit déjà
        // l'absence de croisement rail A / rail B entre deux stations
        // consécutives, mais cet analyseur ne fait jamais confiance
        // aveuglément à cette garantie amont -- un intervalle suspect est
        // compté puis EXCLU de l'union plutôt que d'y contribuer un
        // quadrilatère éventuellement invalide.
        if (segments_cross_strict(s0.a, s0.b, s1.a, s1.b)) {
            ++out.degenerate_intervals;
            continue;
        }
        out.triangles.push_back(make_triangle(s0.a, s0.b, s1.b));
        out.triangles.push_back(make_triangle(s0.a, s1.b, s1.a));
    }
    return out;
}

double sum_area_mm2(const std::vector<geometry::PathSet>& sets) {
    double total = 0.0;
    for (const auto& set : sets) {
        total += geometry::path_set_area_um2(set);
    }
    return total / 1e6;
}

void compute_centroid_bbox(const geometry::Path& outer, Vec2um& centroid, Vec2um& bboxMin, Vec2um& bboxMax) {
    if (outer.nodes.empty()) {
        centroid = bboxMin = bboxMax = Vec2um{};
        return;
    }
    std::int64_t sumX = 0;
    std::int64_t sumY = 0;
    bboxMin = bboxMax = outer.nodes.front().pos;
    for (const auto& node : outer.nodes) {
        sumX += node.pos.x.value;
        sumY += node.pos.y.value;
        bboxMin.x = std::min(bboxMin.x, node.pos.x);
        bboxMin.y = std::min(bboxMin.y, node.pos.y);
        bboxMax.x = std::max(bboxMax.x, node.pos.x);
        bboxMax.y = std::max(bboxMax.y, node.pos.y);
    }
    const auto n = static_cast<std::int64_t>(outer.nodes.size());
    centroid = Vec2um{Micrometers{static_cast<std::int32_t>(sumX / n)},
                      Micrometers{static_cast<std::int32_t>(sumY / n)}};
}

// Rayon (mm) du plus grand disque inscrit dans `region`, par recherche
// binaire d'érosions successives (`geometry::inset_path_set`) jusqu'à
// disparition de la région -- précision continue bornée par
// `resolutionMm`, sans dépendance raster/OpenCV.
double max_inscribed_radius_mm(const geometry::PathSet& region, double resolutionMm) {
    Vec2um bboxMin{};
    Vec2um bboxMax{};
    Vec2um centroidUnused{};
    compute_centroid_bbox(region.outer, centroidUnused, bboxMin, bboxMax);
    const double dxMm = to_millimeters(bboxMax.x - bboxMin.x).value;
    const double dyMm = to_millimeters(bboxMax.y - bboxMin.y).value;
    double lo = 0.0;
    double hi = std::hypot(dxMm, dyMm) / 2.0 + resolutionMm;
    while (hi - lo > resolutionMm) {
        const double mid = (lo + hi) / 2.0;
        const auto eroded = geometry::inset_path_set(region, to_micrometers(Millimeters{mid}));
        if (eroded && !eroded->empty()) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

std::string format_diagnostic(const SatinCoverageReport& r, const SatinCoverageConfig& cfg) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "AUTO-SATIN COVERAGE " << (r.passed ? "PASSED" : "FAILED") << "\n\n";
    os << "Target area:              " << r.target_area_mm2 << " mm2\n";
    os << "Covered area:             " << r.covered_area_mm2 << " mm2\n";
    os << "Raw coverage:             " << (r.raw_coverage_ratio * 100.0) << " %\n";
    os << "Core coverage:            " << (r.core_coverage_ratio * 100.0) << " %\n\n";
    os << "Missing area:             " << r.missing_area_mm2 << " mm2\n";
    os << "Missing regions:          " << r.missing_regions.size() << "\n";
    if (!r.missing_regions.empty()) {
        const auto& top = r.missing_regions.front();
        os << "\nLargest missing region:\n";
        os << "    area:                 " << top.area_mm2 << " mm2\n";
        os << "    share of target:      " << (top.area_ratio * 100.0) << " %\n";
        os << "    centroid (mm):        (" << to_millimeters(top.centroid.x).value << ", "
           << to_millimeters(top.centroid.y).value << ")\n";
        os << "    maximum gap radius:   " << top.max_gap_radius_mm << " mm\n";
    }
    os << "\nOutside coverage:\n";
    os << "    area:                 " << r.outside_area_mm2 << " mm2\n";
    os << "    ratio:                " << (r.outside_ratio * 100.0) << " %\n";
    if (r.degenerate_interval_count > 0) {
        os << "\n" << r.degenerate_interval_count
           << " intervalle(s) de colonne rejete(s) (rails croises -- exclu(s) du calcul de couverture)\n";
    }
    os << "\nRESULT: " << (r.passed ? "PASSED" : "REJECTED") << "\n";
    if (!r.passed) {
        std::vector<std::string> reasons;
        if (r.core_coverage_ratio < cfg.min_core_coverage) {
            reasons.emplace_back("core coverage below threshold");
        }
        if (r.raw_coverage_ratio < cfg.min_raw_coverage) {
            reasons.emplace_back("raw coverage below threshold");
        }
        if (r.largest_missing_area_mm2 > cfg.max_largest_missing_area_mm2) {
            reasons.emplace_back(
                "large connected region of the target polygon is not covered by any generated satin column");
        }
        if (r.max_gap_radius_mm > cfg.max_gap_radius_mm) {
            reasons.emplace_back("maximum gap radius exceeds threshold");
        }
        if (r.outside_ratio > cfg.max_outside_ratio) {
            reasons.emplace_back("satin overflows the target region beyond tolerance");
        }
        os << "Reason:\n";
        for (const auto& reason : reasons) {
            os << "- " << reason << "\n";
        }
    }
    return os.str();
}

}  // namespace

Result<SatinCoverageReport> analyze_satin_coverage(const geometry::PathSet& target,
                                                    const std::vector<SatinColumnInput>& columns,
                                                    const SatinCoverageConfig& config) {
    if (target.outer.nodes.size() < 3) {
        return fail(ErrorCategory::UserInput, "Région cible dégénérée",
                    "PathSet.outer a moins de 3 sommets");
    }

    std::vector<geometry::Path> allTriangles;
    std::size_t degenerateIntervals = 0;
    for (const auto& col : columns) {
        ColumnTriangles tris = triangles_for_column(col);
        degenerateIntervals += tris.degenerate_intervals;
        for (auto& t : tris.triangles) {
            allTriangles.push_back(std::move(t));
        }
    }

    const auto coveredUnion = geometry::union_nonzero(allTriangles);
    if (!coveredUnion) {
        return std::unexpected(coveredUnion.error());
    }
    const std::vector<geometry::PathSet>& C = *coveredUnion;
    const std::vector<geometry::PathSet> P{target};

    const auto insideResult = geometry::intersect_polygons(P, C);
    if (!insideResult) {
        return std::unexpected(insideResult.error());
    }
    const auto missingResult = geometry::difference_polygons(P, C);
    if (!missingResult) {
        return std::unexpected(missingResult.error());
    }
    const auto outsideResult = geometry::difference_polygons(C, P);
    if (!outsideResult) {
        return std::unexpected(outsideResult.error());
    }

    SatinCoverageReport report;
    report.target_area_mm2 = geometry::path_set_area_um2(target) / 1e6;
    report.covered_area_mm2 = sum_area_mm2(*insideResult);
    report.outside_area_mm2 = sum_area_mm2(*outsideResult);
    report.raw_coverage_ratio =
        report.target_area_mm2 > 1e-9 ? report.covered_area_mm2 / report.target_area_mm2 : 0.0;
    report.outside_ratio =
        report.target_area_mm2 > 1e-9 ? report.outside_area_mm2 / report.target_area_mm2 : 0.0;
    report.degenerate_interval_count = degenerateIntervals;

    report.missing_regions.reserve(missingResult->size());
    for (const auto& ps : *missingResult) {
        MissingRegion mr;
        mr.region = ps;
        mr.area_mm2 = geometry::path_set_area_um2(ps) / 1e6;
        compute_centroid_bbox(ps.outer, mr.centroid, mr.bbox_min, mr.bbox_max);
        mr.max_gap_radius_mm = max_inscribed_radius_mm(ps, config.gap_radius_resolution_mm);
        report.missing_area_mm2 += mr.area_mm2;
        report.missing_regions.push_back(std::move(mr));
    }
    for (auto& mr : report.missing_regions) {
        mr.area_ratio = report.target_area_mm2 > 1e-9 ? mr.area_mm2 / report.target_area_mm2 : 0.0;
    }
    std::sort(report.missing_regions.begin(), report.missing_regions.end(),
             [](const MissingRegion& a, const MissingRegion& b) { return a.area_mm2 > b.area_mm2; });
    if (!report.missing_regions.empty()) {
        report.largest_missing_area_mm2 = report.missing_regions.front().area_mm2;
        report.largest_missing_ratio = report.missing_regions.front().area_ratio;
        for (const auto& mr : report.missing_regions) {
            report.max_gap_radius_mm = std::max(report.max_gap_radius_mm, mr.max_gap_radius_mm);
        }
    }

    // --- Couverture du cœur (érosion de la cible, absorbe le bruit de bord) ---
    const auto coreResult =
        geometry::inset_path_set(target, to_micrometers(Millimeters{config.boundary_tolerance_mm}));
    if (!coreResult) {
        return std::unexpected(coreResult.error());
    }
    if (coreResult->empty()) {
        // Forme trop fine pour survivre à l'érosion : repli documenté sur le
        // ratio brut plutôt qu'une division par zéro / un résultat non défini.
        report.core_coverage_ratio = report.raw_coverage_ratio;
    } else {
        const double coreArea = sum_area_mm2(*coreResult);
        const auto coreMissing = geometry::difference_polygons(*coreResult, C);
        if (!coreMissing) {
            return std::unexpected(coreMissing.error());
        }
        const double coreMissingArea = sum_area_mm2(*coreMissing);
        report.core_coverage_ratio =
            coreArea > 1e-9 ? 1.0 - coreMissingArea / coreArea : report.raw_coverage_ratio;
    }

    report.passed = report.core_coverage_ratio >= config.min_core_coverage &&
                    report.raw_coverage_ratio >= config.min_raw_coverage &&
                    report.largest_missing_area_mm2 <= config.max_largest_missing_area_mm2 &&
                    report.max_gap_radius_mm <= config.max_gap_radius_mm &&
                    report.outside_ratio <= config.max_outside_ratio;

    report.diagnostic = format_diagnostic(report, config);
    return report;
}

}  // namespace openstitch::satin_coverage
