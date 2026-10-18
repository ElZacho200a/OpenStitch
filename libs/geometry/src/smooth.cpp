// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/smooth.hpp"

#include "openstitch/geometry/curve_fit.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::geometry {

Path smooth_path(const Path& path, Micrometers tolerance) {
    if (tolerance.value <= 0 || path.nodes.size() < 4) {
        return path;
    }
    const Path polygon = flatten_to_polygon(path, tolerance);
    CurveFitOptions options;
    options.tolerance = tolerance;
    Path out = fit_curves(polygon, options);
    if (out.nodes.empty()) {
        return path;
    }
    if (!path.closed) {
        // Les extrémités d'un tracé ouvert sont une contrainte dure.
        out.nodes.front().pos = path.nodes.front().pos;
        out.nodes.back().pos = path.nodes.back().pos;
    }
    return out;
}

PathSet smooth_path_set(const PathSet& set, Micrometers tolerance) {
    PathSet out;
    out.outer = smooth_path(set.outer, tolerance);
    out.holes.reserve(set.holes.size());
    for (const Path& hole : set.holes) {
        out.holes.push_back(smooth_path(hole, tolerance));
    }
    return out;
}

} // namespace openstitch::geometry
