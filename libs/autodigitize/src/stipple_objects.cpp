// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/stipple_objects.hpp"

namespace openstitch::autodigitize {

Result<AutoResult> auto_digitize_stipple(const image::Image& img, IdGenerator<ObjectId>& ids,
                                         const StippleObjectOptions& options,
                                         StippleMetrics* metricsOut) {
    const auto density = stipple::density_from_image(img, options.density);
    if (!density) {
        return std::unexpected(density.error());
    }
    const auto dots = stipple::stipple_points(*density, options.stipple);
    if (!dots) {
        return std::unexpected(dots.error());
    }
    stipple::PathOptions po;
    po.mm_per_px = options.mm_per_px;
    po.max_link = options.max_link;
    const stipple::StitchPath path = stipple::build_stitch_path(
        *dots, *density, img.width, img.height, po, options.stipple.tour);

    AutoResult result;
    std::vector<geometry::PathSet> paths;
    std::size_t pointCount = 0;
    for (const auto& line : path.polylines) {
        if (line.size() < 2) {
            continue; // un point isolé ne se coud pas
        }
        geometry::PathSet ps;
        ps.outer.closed = false;
        for (const Vec2um p : line) {
            ps.outer.nodes.push_back({p, geometry::NodeType::Corner, std::nullopt, std::nullopt});
        }
        pointCount += line.size();
        paths.push_back(std::move(ps));
    }
    if (paths.empty()) {
        return fail(ErrorCategory::OperationImpossible,
                    "Aucun point de stippling exploitable (image uniformément claire ?)");
    }

    const double toMm = options.mm_per_px.value * density->scale;
    if (metricsOut != nullptr) {
        metricsOut->points = pointCount;
        metricsOut->polylines = paths.size();
        metricsOut->tour_length_mm = path.tour_length_px * toMm;
        metricsOut->initial_tour_length_mm = path.initial_tour_length_px * toMm;
        metricsOut->lloyd_iterations = dots->lloyd_iterations;
    }

    document::VectorObject v;
    v.id = ids.next();
    v.name = "Stippling";
    v.rgb = options.rgb;
    v.paths = std::move(paths);
    document::EmbroideryObject e;
    e.id = ids.next();
    e.source_vector = v.id;
    e.rgb = options.rgb;
    e.intent = document::EmbroideryIntent::AutoChoice;
    // Le point droit rééchantillonne entre les coins : une longueur cible courte
    // garde le semis fidèle (les liaisons longues sont simplement subdivisées).
    document::RunningStitchParams rp;
    rp.stitch_length = Micrometers{1'500};
    rp.min_length = Micrometers{300};
    e.params = rp;
    e.name = "Stippling";
    result.vectors.push_back(std::move(v));
    result.embroideries.push_back(std::move(e));
    return result;
}

} // namespace openstitch::autodigitize
