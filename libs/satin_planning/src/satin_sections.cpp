// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/satin_sections.hpp"

#include <algorithm>
#include <cmath>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::satin_planning {

namespace {

// Aire nette d'un PathSet (exterieur moins trous), en um².
double net_area_um2(const geometry::PathSet& set) {
    double area = std::abs(geometry::signed_area_um2(set.outer));
    for (const auto& hole : set.holes) {
        area -= std::abs(geometry::signed_area_um2(hole));
    }
    return std::max(0.0, area);
}

// Polygone approximatif de la bande couverte par une colonne satin (rail A
// aller, rail B retour). Les rails sont aplatis (`geometry::flatten`) : un
// rail parametrique est une courbe de Bezier eparse, jamais une polyligne
// dense.
template <typename ColumnLike>
geometry::Path column_strip(const ColumnLike& column) {
    constexpr Micrometers kFlattenTolerance{30};  // 0,03 mm : sous la resolution DST
    const auto flatA = geometry::flatten(column.rail_a, kFlattenTolerance);
    const auto flatB = geometry::flatten(column.rail_b, kFlattenTolerance);
    geometry::Path strip;
    strip.closed = true;
    strip.nodes.reserve(flatA.points.size() + flatB.points.size());
    for (const auto& pt : flatA.points) {
        strip.nodes.push_back({pt, geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    for (auto it = flatB.points.rbegin(); it != flatB.points.rend(); ++it) {
        strip.nodes.push_back({*it, geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    return strip;
}

template <typename ColumnLike>
BuiltSatinSection make_section(const ColumnLike& col, Micrometers density, Micrometers pullCompensation,
                               bool centerUnderlay, Micrometers maxWidth) {
    BuiltSatinSection out;
    out.params = satin_params_from_column(col, density, pullCompensation, centerUnderlay, maxWidth);
    out.strip = column_strip(col);
    return out;
}

std::vector<BuiltSatinSection> sections_from_result(const auto_satin::SatinColumnsResult& built, Micrometers density,
                                                    Micrometers pullCompensation, bool centerUnderlay,
                                                    Micrometers maxWidth) {
    std::vector<BuiltSatinSection> out;
    if (!built.parametric_columns.empty()) {
        out.reserve(built.parametric_columns.size());
        for (const auto& c : built.parametric_columns) {
            out.push_back(make_section(c, density, pullCompensation, centerUnderlay, maxWidth));
        }
    } else {
        out.reserve(built.columns.size());
        for (const auto& c : built.columns) {
            out.push_back(make_section(c, density, pullCompensation, centerUnderlay, maxWidth));
        }
    }
    return out;
}

}  // namespace

SatinBuildReport build_satin_sections(const geometry::PathSet& region,
                                      const auto_satin::SatinColumnsParameters& genParams, Micrometers density,
                                      Micrometers pullCompensation, bool centerUnderlay, Micrometers maxWidth,
                                      const std::string& warningLabel) {
    SatinBuildReport report;
    const std::string prefix = warningLabel.empty() ? std::string() : (warningLabel + " : ");

    const auto analysis = auto_satin::analyze_region(region, genParams.analysis);
    if (analysis) {
        report.whole_region_report = analysis->report;
    }

    // Planner recursif unifie (§32 du plan de refonte satin, 2026-08-14 ;
    // deplace ici §4 de la mission de durcissement du contrat, 2026-08-17) :
    // seul point d'appel vers `create_satin_plan` -- ce module GERE lui-meme
    // la recursion, la mesure de couverture et la reparation de residu.
    satin_planning::SatinPlanConfig planConfig;
    planConfig.genParams = genParams;
    planConfig.density = density;
    const satin_planning::SatinPlan plan = satin_planning::create_satin_plan(region, planConfig);

    report.status = plan.status;
    report.diagnostics = plan.diagnostics;

    for (const auto& w : plan.warnings) {
        report.warnings.push_back(prefix + w);
    }

    report.sections.reserve(plan.regions.size());
    for (const auto& r : plan.regions) {
        if (r.depth > 0 || r.from_residual_repair) {
            report.used_sgsd = true;
        }
        auto secs = sections_from_result(r.columns, density, pullCompensation, centerUnderlay, maxWidth);
        for (auto& s : secs) report.sections.push_back(std::move(s));
    }

    // Le residu reste une geometrie BRUTE, complete et JAMAIS filtree -- c'est
    // a l'appelant de decider quoi en faire (§12 du plan de refonte : « aucun
    // fallback silencieux vers tatami »). `create_satin_plan` ne filtre deja
    // plus les composantes individuellement negligeables hors de ce residu
    // (defaut reel trouve et corrige le 2026-08-14 : de nombreux petits
    // reliquats "negligeables" un par un peuvent s'additionner en un vrai
    // trou de plusieurs centaines de mm² sur une forme complexe, § docs/
    // source/satin.md) -- il ne fait plus que decider quelles composantes
    // meritent une TENTATIVE de reparation individuelle, jamais ce qui est
    // rapporte.
    report.unresolved_residual = plan.unresolved_residual;
    report.aggregate_coverage = plan.aggregate_coverage;

    // `structural_gap` est un raccourci booleen pour les appelants qui ne
    // veulent pas inspecter `unresolved_residual` en detail : significatif
    // au sens AGREGE (somme de toutes les composantes manquantes, jamais une
    // composante isolee), avec le meme seuil mixte fixe+proportionnel que le
    // reste du pipeline (tolere le reliquat naturel d'une pointe/jonction,
    // meme minuscule, sans le confondre avec un vrai trou -- mais une SOMME
    // de nombreux petits reliquats reste, elle, correctement signalee).
    double residualAreaMm2 = 0.0;
    for (const auto& piece : report.unresolved_residual) residualAreaMm2 += net_area_um2(piece) / 1e6;
    const double totalAreaMm2 = net_area_um2(region) / 1e6;
    constexpr double kGapThresholdFloorMm2 = 1.0;
    constexpr double kGapThresholdRatio = 0.03;
    report.structural_gap = residualAreaMm2 > std::max(kGapThresholdFloorMm2, kGapThresholdRatio * totalAreaMm2);

    if (report.sections.empty()) {
        report.refusal = "Aucune colonne satin n'a pu etre construite sur cette region.";
    }

    return report;
}

}  // namespace openstitch::satin_planning
