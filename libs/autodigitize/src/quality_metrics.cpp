// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/quality_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <variant>

#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/geometry/boolean.hpp"

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
            if (shape) {
                const double widthMm = shape->max_width_um / 1000.0;
                rep.satin_max_width_mm = std::max(rep.satin_max_width_mm, widthMm);
                const double limit = static_cast<double>(
                    (compact ? options.satin_compact_max_width : options.satin_max_width).value);
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
