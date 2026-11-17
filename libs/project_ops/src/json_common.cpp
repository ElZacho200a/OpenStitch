// SPDX-License-Identifier: Apache-2.0
#include <string>

#include "op_registry.hpp"

namespace openstitch::project_ops::detail {

OpStatus require_embroidery(const OpContext& ctx, const char* key,
                            const document::EmbroideryObject*& out) {
    const auto it = ctx.args.find(key);
    if (it == ctx.args.end() || !it->is_number_unsigned()) {
        return OpStatus::failure(code::kUsage,
                                 std::string("argument « ") + key +
                                     " » obligatoire : identifiant d'objet de broderie (entier)",
                                 "voir `project inspect --json` pour les identifiants");
    }
    const auto* found = ctx.project.findEmbroidery(ObjectId{it->get<std::uint64_t>()});
    if (found == nullptr) {
        return OpStatus::failure(code::kUnknownObject,
                                 "objet de broderie " + std::to_string(it->get<std::uint64_t>()) +
                                     " introuvable",
                                 "voir `project inspect --json` pour les identifiants");
    }
    out = found;
    return OpStatus::success();
}

ObjectId shape_vector_of(const document::Project& project, std::uint64_t id) {
    if (const auto* emb = project.findEmbroidery(ObjectId{id}); emb != nullptr) {
        return project.findObject(emb->source_vector) != nullptr ? emb->source_vector : ObjectId{};
    }
    return project.findObject(ObjectId{id}) != nullptr ? ObjectId{id} : ObjectId{};
}

} // namespace openstitch::project_ops::detail
