// SPDX-License-Identifier: Apache-2.0
#pragma once

// Commandes annulables sur les groupes d'objets de l'utilisateur (HP-OBJ-009). Un groupe réunit
// des formes et/ou d'autres groupes (cf. `document::ObjectGroup`) ; il ne change jamais l'ordre de
// couture. Un geste d'interface = un pas d'annulation.

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// Réunit `shapes` dans un nouveau groupe `id` (identifiant fourni par l'appelant AVANT la commande,
// pour que redo réutilise le même). Une forme déjà groupée n'est pas extraite de son groupe : c'est
// son groupe de premier niveau qui devient enfant du nouveau (imbrication). Sans effet s'il y a
// moins de deux éléments distincts (formes isolées ou groupes de premier niveau).
class GroupShapesCommand final : public ICommand {
public:
    GroupShapesCommand(ObjectId id, std::string name, std::vector<ObjectId> shapes)
        : id_(id), name_(std::move(name)), shapes_(std::move(shapes)) {}

    void apply(document::Project& project) override {
        applied_ = false;
        shapeBefore_.clear();
        groupBefore_.clear();
        std::vector<ObjectId> loneShapes;
        std::vector<ObjectId> topGroups;
        for (const ObjectId shapeId : shapes_) {
            if (project.findObject(shapeId) == nullptr) {
                continue;
            }
            const ObjectId top = project.topGroupOfShape(shapeId);
            if (top.valid()) {
                if (std::find(topGroups.begin(), topGroups.end(), top) == topGroups.end()) {
                    topGroups.push_back(top);
                }
            } else if (std::find(loneShapes.begin(), loneShapes.end(), shapeId) ==
                       loneShapes.end()) {
                loneShapes.push_back(shapeId);
            }
        }
        if (!id_.valid() || project.findGroup(id_) != nullptr ||
            loneShapes.size() + topGroups.size() < 2) {
            return;
        }
        for (const ObjectId shapeId : loneShapes) {
            auto* shape = project.findObject(shapeId);
            shapeBefore_.emplace_back(shapeId, shape->group);
            shape->group = id_;
        }
        for (auto& group : project.groups) {
            if (std::find(topGroups.begin(), topGroups.end(), group.id) != topGroups.end()) {
                groupBefore_.emplace_back(group.id, group.parent);
                group.parent = id_;
            }
        }
        project.groups.push_back({id_, name_, ObjectId{}});
        applied_ = true;
    }
    void revert(document::Project& project) override {
        if (!applied_) {
            return;
        }
        std::erase_if(project.groups, [&](const document::ObjectGroup& g) { return g.id == id_; });
        for (const auto& [shapeId, previous] : shapeBefore_) {
            if (auto* shape = project.findObject(shapeId)) {
                shape->group = previous;
            }
        }
        for (const auto& [groupId, previous] : groupBefore_) {
            for (auto& group : project.groups) {
                if (group.id == groupId) {
                    group.parent = previous;
                }
            }
        }
        applied_ = false;
    }
    [[nodiscard]] std::string name() const override { return "Grouper"; }
    [[nodiscard]] bool applied() const { return applied_; }

private:
    ObjectId id_;
    std::string name_;
    std::vector<ObjectId> shapes_;
    std::vector<std::pair<ObjectId, ObjectId>> shapeBefore_; // (forme, groupe précédent)
    std::vector<std::pair<ObjectId, ObjectId>> groupBefore_; // (groupe, parent précédent)
    bool applied_{false};
};

// Dissout le groupe `id` : ses formes et sous-groupes passent au parent du groupe (premier niveau
// s'il n'en a pas). Les formes ne bougent pas et la couture non plus. Sans effet si le groupe est
// inconnu.
class UngroupCommand final : public ICommand {
public:
    explicit UngroupCommand(ObjectId id) : id_(id) {}

    void apply(document::Project& project) override {
        applied_ = false;
        shapes_.clear();
        children_.clear();
        const auto it = std::find_if(project.groups.begin(), project.groups.end(),
                                     [&](const document::ObjectGroup& g) { return g.id == id_; });
        if (it == project.groups.end()) {
            return;
        }
        index_ = static_cast<std::size_t>(it - project.groups.begin());
        removed_ = *it;
        for (auto& shape : project.vector_objects) {
            if (shape.group == id_) {
                shapes_.push_back(shape.id);
                shape.group = removed_.parent;
            }
        }
        for (auto& group : project.groups) {
            if (group.parent == id_ && group.id != id_) {
                children_.push_back(group.id);
                group.parent = removed_.parent;
            }
        }
        project.groups.erase(project.groups.begin() + static_cast<std::ptrdiff_t>(index_));
        applied_ = true;
    }
    void revert(document::Project& project) override {
        if (!applied_) {
            return;
        }
        project.groups.insert(project.groups.begin() + static_cast<std::ptrdiff_t>(index_),
                              removed_);
        for (const ObjectId shapeId : shapes_) {
            if (auto* shape = project.findObject(shapeId)) {
                shape->group = id_;
            }
        }
        for (auto& group : project.groups) {
            if (std::find(children_.begin(), children_.end(), group.id) != children_.end()) {
                group.parent = id_;
            }
        }
        applied_ = false;
    }
    [[nodiscard]] std::string name() const override { return "Dégrouper"; }
    [[nodiscard]] bool applied() const { return applied_; }

private:
    ObjectId id_;
    std::size_t index_{};
    document::ObjectGroup removed_{};
    std::vector<ObjectId> shapes_;
    std::vector<ObjectId> children_;
    bool applied_{false};
};

} // namespace openstitch::commands
