// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/vector_convert.hpp"

#include <set>
#include <utility>
#include <variant>
#include <vector>

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
    // Traits superposés (AUTO-LINEWORK-01) : un chemin ouvert n'est pas un aplat. Ils ne passent
    // pas par la classification satin / tatami : point droit simple direct, ajoutés APRÈS les
    // formes du même lot (cousus par-dessus les aplats, l'ordre des calques restant maître).
    std::vector<document::VectorObject> strokes;
    std::erase_if(todo, [&](const document::VectorObject& v) {
        if (!v.linework) {
            return false;
        }
        strokes.push_back(v);
        return true;
    });
    if (!todo.empty()) {
        auto built = auto_digitize_vectors(todo, ids, options);
        if (!built) {
            return std::unexpected(built.error());
        }
        out.embroideries = std::move(built->embroideries);
        out.warnings = std::move(built->warnings);
    }
    for (const auto& v : strokes) {
        document::EmbroideryObject e;
        e.id = ids.next();
        e.name = "Point droit " + v.name;
        e.source_vector = v.id;
        e.rgb = v.rgb;
        e.params = document::RunningStitchParams{};
        out.embroideries.push_back(std::move(e));
    }
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
