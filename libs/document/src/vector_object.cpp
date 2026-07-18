// SPDX-License-Identifier: Apache-2.0
#include "openstitch/document/vector_object.hpp"

namespace openstitch::document {

const geometry::Path* path_in(const VectorObject& object, std::size_t set, std::size_t path) {
    if (set >= object.paths.size()) {
        return nullptr;
    }
    const geometry::PathSet& ps = object.paths[set];
    if (path == 0) {
        return &ps.outer;
    }
    if (path - 1 < ps.holes.size()) {
        return &ps.holes[path - 1];
    }
    return nullptr;
}

geometry::Path* path_in(VectorObject& object, std::size_t set, std::size_t path) {
    return const_cast<geometry::Path*>(path_in(std::as_const(object), set, path));
}

}  // namespace openstitch::document
