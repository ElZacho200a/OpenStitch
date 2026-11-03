// SPDX-License-Identifier: Apache-2.0
#pragma once

// Commandes annulables sur les calques du document (HP-OBJ-024). Un calque est un niveau de
// classement au-dessus des couches d'une forme (cf. `document::Calque`). Toute mutation passe par
// ici : un geste d'interface = un pas d'annulation.

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// Ajoute un calque au-dessus de tous les autres. Sur un document sans calque, le calque implicite
// du fond (« Calque 1 ») est matérialisé d'abord : toutes les formes existantes y restent.
// Les identifiants sont fournis par l'appelant (project.object_ids) AVANT la commande, pour que
// redo réutilise les mêmes.
class AddCalqueCommand final : public ICommand {
public:
    AddCalqueCommand(ObjectId id, std::string name, ObjectId implicitFirstId = {},
                     std::string implicitFirstName = "Calque 1")
        : id_(id), name_(std::move(name)), firstId_(implicitFirstId),
          firstName_(std::move(implicitFirstName)) {}

    void apply(document::Project& project) override {
        materialisedFirst_ = project.calques.empty() && firstId_.valid();
        if (materialisedFirst_) {
            project.calques.push_back({firstId_, firstName_, true, false});
        }
        project.calques.push_back({id_, name_, true, false});
    }
    void revert(document::Project& project) override {
        project.calques.pop_back();
        if (materialisedFirst_) {
            project.calques.pop_back();
        }
        if (project.active_calque == id_) {
            project.active_calque = {};
        }
    }
    [[nodiscard]] std::string name() const override { return "Ajouter un calque"; }

private:
    ObjectId id_;
    std::string name_;
    ObjectId firstId_;
    std::string firstName_;
    bool materialisedFirst_{false};
};

// Supprime un calque. Ses formes passent dans le calque situé juste en dessous (au-dessus s'il
// était au fond) : rien n'est perdu. Refusé (sans effet) s'il ne reste qu'un calque.
class RemoveCalqueCommand final : public ICommand {
public:
    explicit RemoveCalqueCommand(ObjectId id) : id_(id) {}

    void apply(document::Project& project) override {
        applied_ = false;
        moved_.clear();
        auto& list = project.calques;
        if (list.size() <= 1) {
            return;
        }
        const auto it =
            std::find_if(list.begin(), list.end(), [&](const auto& c) { return c.id == id_; });
        if (it == list.end()) {
            return;
        }
        index_ = static_cast<std::size_t>(it - list.begin());
        removed_ = *it;
        const ObjectId target = index_ > 0 ? list[index_ - 1].id : list[1].id;
        for (auto& shape : project.vector_objects) {
            if (shape.calque == id_) {
                shape.calque = target;
                moved_.push_back(shape.id);
            }
        }
        wasActive_ = project.active_calque == id_;
        if (wasActive_) {
            project.active_calque = {};
        }
        list.erase(it);
        applied_ = true;
    }
    void revert(document::Project& project) override {
        if (!applied_) {
            return;
        }
        project.calques.insert(project.calques.begin() + static_cast<std::ptrdiff_t>(index_),
                               removed_);
        for (const ObjectId shapeId : moved_) {
            if (auto* shape = project.findObject(shapeId)) {
                shape->calque = id_;
            }
        }
        if (wasActive_) {
            project.active_calque = id_;
        }
        applied_ = false;
    }
    [[nodiscard]] std::string name() const override { return "Supprimer le calque"; }

private:
    ObjectId id_;
    std::size_t index_{};
    document::Calque removed_{};
    std::vector<ObjectId> moved_;
    bool wasActive_{false};
    bool applied_{false};
};

class RenameCalqueCommand final : public ICommand {
public:
    RenameCalqueCommand(ObjectId id, std::string name) : id_(id), name_(std::move(name)) {}

    void apply(document::Project& project) override {
        applied_ = false;
        for (auto& calque : project.calques) {
            if (calque.id == id_) {
                before_ = calque.name;
                calque.name = name_;
                applied_ = true;
                return;
            }
        }
    }
    void revert(document::Project& project) override {
        if (!applied_) {
            return;
        }
        for (auto& calque : project.calques) {
            if (calque.id == id_) {
                calque.name = before_;
            }
        }
    }
    [[nodiscard]] std::string name() const override { return "Renommer le calque"; }

private:
    ObjectId id_;
    std::string name_;
    std::string before_;
    bool applied_{false};
};

// Masque / affiche un calque (un calque masqué n'est ni dessiné ni cousu).
class SetCalqueVisibleCommand final : public ICommand {
public:
    SetCalqueVisibleCommand(ObjectId id, bool visible) : id_(id), visible_(visible) {}

    void apply(document::Project& project) override {
        applied_ = false;
        for (auto& calque : project.calques) {
            if (calque.id == id_) {
                before_ = calque.visible;
                calque.visible = visible_;
                applied_ = true;
                return;
            }
        }
    }
    void revert(document::Project& project) override {
        if (!applied_) {
            return;
        }
        for (auto& calque : project.calques) {
            if (calque.id == id_) {
                calque.visible = before_;
            }
        }
    }
    [[nodiscard]] std::string name() const override {
        return visible_ ? "Afficher le calque" : "Masquer le calque";
    }

private:
    ObjectId id_;
    bool visible_;
    bool before_{true};
    bool applied_{false};
};

// Verrouille / déverrouille un calque (ses formes ne se sélectionnent plus au canevas).
class SetCalqueLockedCommand final : public ICommand {
public:
    SetCalqueLockedCommand(ObjectId id, bool locked) : id_(id), locked_(locked) {}

    void apply(document::Project& project) override {
        applied_ = false;
        for (auto& calque : project.calques) {
            if (calque.id == id_) {
                before_ = calque.locked;
                calque.locked = locked_;
                applied_ = true;
                return;
            }
        }
    }
    void revert(document::Project& project) override {
        if (!applied_) {
            return;
        }
        for (auto& calque : project.calques) {
            if (calque.id == id_) {
                calque.locked = before_;
            }
        }
    }
    [[nodiscard]] std::string name() const override {
        return locked_ ? "Verrouiller le calque" : "Déverrouiller le calque";
    }

private:
    ObjectId id_;
    bool locked_;
    bool before_{false};
    bool applied_{false};
};

// Échange un calque avec son voisin (+1 = vers le dessus, donc cousu plus tard). Sans effet au
// bout de la pile.
class MoveCalqueCommand final : public ICommand {
public:
    MoveCalqueCommand(ObjectId id, int direction) : id_(id), direction_(direction) {}

    void apply(document::Project& project) override {
        applied_ = false;
        auto& list = project.calques;
        const auto it =
            std::find_if(list.begin(), list.end(), [&](const auto& c) { return c.id == id_; });
        if (it == list.end() || direction_ == 0) {
            return;
        }
        const std::ptrdiff_t from = it - list.begin();
        const std::ptrdiff_t to = from + (direction_ > 0 ? 1 : -1);
        if (to < 0 || to >= static_cast<std::ptrdiff_t>(list.size())) {
            return;
        }
        std::swap(list[static_cast<std::size_t>(from)], list[static_cast<std::size_t>(to)]);
        from_ = static_cast<std::size_t>(from);
        to_ = static_cast<std::size_t>(to);
        applied_ = true;
    }
    void revert(document::Project& project) override {
        if (applied_ && to_ < project.calques.size() && from_ < project.calques.size()) {
            std::swap(project.calques[from_], project.calques[to_]);
        }
        applied_ = false;
    }
    [[nodiscard]] std::string name() const override {
        return direction_ > 0 ? "Monter le calque" : "Descendre le calque";
    }

private:
    ObjectId id_;
    int direction_;
    std::size_t from_{};
    std::size_t to_{};
    bool applied_{false};
};

// Déplace des formes (et donc toutes leurs couches de couture) vers un calque.
class SetShapesCalqueCommand final : public ICommand {
public:
    SetShapesCalqueCommand(std::vector<ObjectId> shapes, ObjectId target)
        : shapes_(std::move(shapes)), target_(target) {}

    void apply(document::Project& project) override {
        before_.clear();
        for (const ObjectId id : shapes_) {
            if (auto* shape = project.findObject(id)) {
                before_.emplace_back(id, shape->calque);
                shape->calque = target_;
            }
        }
    }
    void revert(document::Project& project) override {
        for (const auto& [id, calque] : before_) {
            if (auto* shape = project.findObject(id)) {
                shape->calque = calque;
            }
        }
        before_.clear();
    }
    [[nodiscard]] std::string name() const override { return "Déplacer vers un calque"; }

private:
    std::vector<ObjectId> shapes_;
    ObjectId target_;
    std::vector<std::pair<ObjectId, ObjectId>> before_;
};

} // namespace openstitch::commands
