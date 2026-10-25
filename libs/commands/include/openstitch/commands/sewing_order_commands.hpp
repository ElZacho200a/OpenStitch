// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// HP-SEQ-002 -- ordre de couture par glisser-déposer. Nouvel ordre de couture (ids) obtenu en
// déplaçant `id` à la position `to_position` (indice final dans l'ordre de couture). `nullopt` si
// l'objet est inconnu, si la position est hors bornes, si rien ne change, ou si le déplacement
// franchirait un calque (le rang de calque reste la clé principale de l'ordre : HP-OBJ-024).
[[nodiscard]] inline std::optional<std::vector<ObjectId>>
moved_sewing_order(const document::Project& project, ObjectId id, std::size_t to_position) {
    std::vector<ObjectId> order;
    for (const std::size_t index : project.sewingOrder()) {
        order.push_back(project.embroidery_objects[index].id);
    }
    const auto it = std::find(order.begin(), order.end(), id);
    if (it == order.end() || to_position >= order.size()) {
        return std::nullopt;
    }
    const auto from = static_cast<std::size_t>(it - order.begin());
    if (from == to_position) {
        return std::nullopt;
    }
    std::vector<ObjectId> next = order;
    next.erase(next.begin() + static_cast<std::ptrdiff_t>(from));
    next.insert(next.begin() + static_cast<std::ptrdiff_t>(to_position), id);
    // Les rangs de calque doivent rester croissants.
    std::size_t previousRank = 0;
    for (const ObjectId oid : next) {
        const auto* obj = project.findEmbroidery(oid);
        if (obj == nullptr) {
            return std::nullopt;
        }
        const std::size_t rank = project.calqueRankOf(*obj);
        if (rank < previousRank) {
            return std::nullopt;
        }
        previousRank = rank;
    }
    return next;
}

// Déplace un objet à une position de l'ordre de couture (un pas d'annulation). Sans effet si le
// déplacement est invalide (cf. `moved_sewing_order`).
class MoveInSewingOrderCommand final : public ICommand {
public:
    MoveInSewingOrderCommand(ObjectId id, std::size_t toPosition) : id_(id), to_(toPosition) {}

    void apply(document::Project& project) override {
        oldOrder_.clear();
        for (const auto& obj : project.embroidery_objects) {
            oldOrder_.push_back(obj.id);
        }
        const auto next = moved_sewing_order(project, id_, to_);
        if (!next) {
            return;
        }
        assign(project, *next);
    }
    void revert(document::Project& project) override { assign(project, oldOrder_); }
    [[nodiscard]] std::string name() const override { return "Déplacer dans l'ordre de couture"; }

private:
    static void assign(document::Project& project, const std::vector<ObjectId>& order) {
        std::vector<document::EmbroideryObject> next;
        next.reserve(project.embroidery_objects.size());
        for (const ObjectId oid : order) {
            if (auto* obj = project.findEmbroidery(oid)) {
                next.push_back(*obj);
            }
        }
        if (next.size() == project.embroidery_objects.size()) {
            project.embroidery_objects = std::move(next);
        }
    }

    ObjectId id_;
    std::size_t to_;
    std::vector<ObjectId> oldOrder_;
};

} // namespace openstitch::commands
