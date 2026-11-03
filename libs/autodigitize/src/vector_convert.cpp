// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/vector_convert.hpp"

#include <set>
#include <utility>
#include <variant>

namespace openstitch::autodigitize {

Result<VectorConversion> convert_vectors_to_embroidery(const document::Project& project,
                                                       IdGenerator<ObjectId>& ids,
                                                       const AutoOptions& options) {
    std::set<ObjectId> sewn;
    for (const auto& e : project.embroidery_objects) {
        sewn.insert(e.source_vector);
    }
    VectorConversion out;
    std::vector<document::VectorObject> todo;
    for (const auto& v : project.vector_objects) {
        if (sewn.count(v.id) != 0) {
            ++out.already_embroidered;
        } else {
            todo.push_back(v);
        }
    }
    out.considered = todo.size();
    if (todo.empty()) {
        return std::unexpected(Error{
            ErrorCategory::UserInput, "aucune forme vectorielle sans broderie à convertir", {}});
    }
    auto built = auto_digitize_vectors(todo, ids, options);
    if (!built) {
        return std::unexpected(built.error());
    }
    out.embroideries = std::move(built->embroideries);
    out.warnings = std::move(built->warnings);
    for (const auto& e : out.embroideries) {
        if (e.is_tatami()) {
            ++out.tatami;
        } else if (std::holds_alternative<document::RunningStitchParams>(e.params)) {
            ++out.contour;
        } else {
            ++out.other;
        }
    }
    return out;
}

} // namespace openstitch::autodigitize
