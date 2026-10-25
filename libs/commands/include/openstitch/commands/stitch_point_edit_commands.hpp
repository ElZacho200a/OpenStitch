// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// HP-EDIT-001/002/003 : retouches de points GROUPEES d'un objet de broderie (un seul pas d'undo
// pour une selection entiere). Memes conventions que `MoveStitchPointCommand`
// (project_commands.hpp) : `raw_fingerprint`/`raw_point_count` decrivent la vue brute actuelle
// (`stitch_generation::raw_slice`), calcules par l'appelant ; un objet Dirty ou un index hors vue
// font de la commande un no-op complet (aucune mutation partielle). Eligibilite fine de chaque
// point (TopStitch, Stitch/Jump) jugee par `apply_manual_overrides`, jamais ici.
class EditStitchPointsCommand final : public ICommand {
public:
    enum class Kind : std::uint8_t { Move, Delete, Restore, Insert, SetType, SetTrim };

    struct Item {
        std::size_t base_index{};
        Vec2um pos{}; // Move : nouvelle position ; Insert : point insere apres base_index
    };

    [[nodiscard]] static EditStitchPointsCommand
    move_points(ObjectId id, std::vector<Item> items, std::uint64_t fp, std::uint32_t count) {
        return EditStitchPointsCommand(Kind::Move, id, std::move(items),
                                       document::StitchPointType::Stitch, false, fp, count);
    }
    [[nodiscard]] static EditStitchPointsCommand delete_points(ObjectId id,
                                                               std::vector<std::size_t> indices,
                                                               std::uint64_t fp,
                                                               std::uint32_t count) {
        return EditStitchPointsCommand(Kind::Delete, id, to_items(indices),
                                       document::StitchPointType::Stitch, false, fp, count);
    }
    [[nodiscard]] static EditStitchPointsCommand restore_points(ObjectId id,
                                                                std::vector<std::size_t> indices,
                                                                std::uint64_t fp,
                                                                std::uint32_t count) {
        return EditStitchPointsCommand(Kind::Restore, id, to_items(indices),
                                       document::StitchPointType::Stitch, false, fp, count);
    }
    // Insere `pos` (point Stitch) juste apres le point `base_index`.
    [[nodiscard]] static EditStitchPointsCommand insert_point(ObjectId id, std::size_t base_index,
                                                              Vec2um pos, std::uint64_t fp,
                                                              std::uint32_t count) {
        return EditStitchPointsCommand(Kind::Insert, id, {Item{base_index, pos}},
                                       document::StitchPointType::Stitch, false, fp, count);
    }
    // Plusieurs insertions en un pas d'annulation (un point apres chaque `Item::base_index`).
    [[nodiscard]] static EditStitchPointsCommand
    insert_points(ObjectId id, std::vector<Item> items, std::uint64_t fp, std::uint32_t count) {
        return EditStitchPointsCommand(Kind::Insert, id, std::move(items),
                                       document::StitchPointType::Stitch, false, fp, count);
    }
    [[nodiscard]] static EditStitchPointsCommand set_type(ObjectId id,
                                                          std::vector<std::size_t> indices,
                                                          document::StitchPointType type,
                                                          std::uint64_t fp, std::uint32_t count) {
        return EditStitchPointsCommand(Kind::SetType, id, to_items(indices), type, false, fp,
                                       count);
    }
    [[nodiscard]] static EditStitchPointsCommand set_trim(ObjectId id,
                                                          std::vector<std::size_t> indices,
                                                          bool trim, std::uint64_t fp,
                                                          std::uint32_t count) {
        return EditStitchPointsCommand(Kind::SetTrim, id, to_items(indices),
                                       document::StitchPointType::Stitch, trim, fp, count);
    }

    void apply(document::Project& project) override {
        valid_ = false;
        auto* obj = project.findEmbroidery(id_);
        if (obj == nullptr || items_.empty()) {
            return;
        }
        for (const Item& it : items_) {
            if (it.base_index >= count_) {
                return;
            }
        }
        const bool wasEmpty = obj->overrides.empty();
        if (!wasEmpty && (obj->edited_point_count != count_ || obj->edited_fingerprint != fp_)) {
            return; // Dirty : pas de transition Dirty -> ManuallyEdited
        }
        prevOverrides_ = obj->overrides;
        prevFingerprint_ = obj->edited_fingerprint;
        prevCount_ = obj->edited_point_count;
        valid_ = true;

        for (const Item& it : items_) {
            auto found = std::find_if(
                obj->overrides.begin(), obj->overrides.end(),
                [&](const document::StitchOverride& o) { return o.base_index == it.base_index; });
            if (found == obj->overrides.end()) {
                if (kind_ == Kind::Restore || (kind_ == Kind::SetTrim && !trim_)) {
                    continue; // rien a retirer : ne pas creer d'entree vide
                }
                document::StitchOverride fresh;
                fresh.base_index = it.base_index;
                obj->overrides.push_back(fresh);
                found = obj->overrides.end() - 1;
            }
            switch (kind_) {
            case Kind::Move:
                found->moved_to = it.pos;
                break;
            case Kind::Delete:
                found->deleted = true;
                break;
            case Kind::Restore:
                found->deleted = false;
                break;
            case Kind::Insert:
                found->inserted_after.push_back(it.pos);
                break;
            case Kind::SetType:
                found->forced_type = type_;
                break;
            case Kind::SetTrim:
                found->trim_after = trim_;
                break;
            }
        }
        // Aucune entree vide persistee (invalide a la relecture).
        std::erase_if(obj->overrides, [](const document::StitchOverride& o) {
            return !o.moved_to && !o.forced_type && !o.trim_after && !o.deleted &&
                   o.inserted_after.empty();
        });
        if (wasEmpty && !obj->overrides.empty()) {
            obj->edited_fingerprint = fp_;
            obj->edited_point_count = count_;
        } else if (obj->overrides.empty()) {
            obj->edited_fingerprint = 0;
            obj->edited_point_count = 0;
        }
    }

    void revert(document::Project& project) override {
        if (!valid_) {
            return;
        }
        if (auto* obj = project.findEmbroidery(id_)) {
            obj->overrides = prevOverrides_;
            obj->edited_fingerprint = prevFingerprint_;
            obj->edited_point_count = prevCount_;
        }
    }

    [[nodiscard]] std::string name() const override {
        switch (kind_) {
        case Kind::Move:
            return "Deplacer des points";
        case Kind::Delete:
            return "Supprimer des points";
        case Kind::Restore:
            return "Restaurer des points";
        case Kind::Insert:
            return "Inserer un point";
        case Kind::SetType:
            return "Changer le type de points";
        case Kind::SetTrim:
            return "Modifier la coupe de fil";
        }
        return {};
    }

    // Vrai si le dernier apply() a muté le document (pour les tests et l'IHM).
    [[nodiscard]] bool applied() const { return valid_; }

private:
    static std::vector<Item> to_items(const std::vector<std::size_t>& indices) {
        std::vector<Item> items;
        items.reserve(indices.size());
        for (std::size_t i : indices) {
            items.push_back(Item{i, {}});
        }
        return items;
    }

    EditStitchPointsCommand(Kind kind, ObjectId id, std::vector<Item> items,
                            document::StitchPointType type, bool trim, std::uint64_t fp,
                            std::uint32_t count)
        : kind_(kind), id_(id), items_(std::move(items)), type_(type), trim_(trim), fp_(fp),
          count_(count) {}
    Kind kind_;
    ObjectId id_;
    std::vector<Item> items_;
    document::StitchPointType type_;
    bool trim_;
    std::uint64_t fp_;
    std::uint32_t count_;

    bool valid_{false};
    std::vector<document::StitchOverride> prevOverrides_;
    std::uint64_t prevFingerprint_{0};
    std::uint32_t prevCount_{0};
};

// HP-EDIT-004 : edition point par point d'un design machine importe (`Project::imported_design`),
// donnee SOURCE restituee telle quelle par `generate_sequence` : la retouche modifie directement
// cette sequence (il n'y a pas de generation a laquelle appliquer un delta) et reste annulable
// (instantane du design). Les bornes des blocs de couleur sont recalees a l'insertion/suppression.
// Les points modifies passent en `StitchPass::Manual`.
class EditImportedPointsCommand final : public ICommand {
public:
    enum class Kind : std::uint8_t { Move, Delete, Insert, SetType };

    struct Item {
        std::size_t index{};
        Vec2um pos{};
    };

    [[nodiscard]] static EditImportedPointsCommand move_points(std::vector<Item> items) {
        return EditImportedPointsCommand(Kind::Move, std::move(items),
                                         document::StitchPointType::Stitch);
    }
    [[nodiscard]] static EditImportedPointsCommand delete_points(std::vector<std::size_t> indices) {
        return EditImportedPointsCommand(Kind::Delete, to_items(indices),
                                         document::StitchPointType::Stitch);
    }
    // Insere un point Stitch juste apres la commande `index`.
    [[nodiscard]] static EditImportedPointsCommand insert_point(std::size_t index, Vec2um pos) {
        return EditImportedPointsCommand(Kind::Insert, {Item{index, pos}},
                                         document::StitchPointType::Stitch);
    }
    // Plusieurs insertions en un pas d'annulation (un point apres chaque index).
    [[nodiscard]] static EditImportedPointsCommand insert_points(std::vector<Item> items) {
        return EditImportedPointsCommand(Kind::Insert, std::move(items),
                                         document::StitchPointType::Stitch);
    }
    [[nodiscard]] static EditImportedPointsCommand set_type(std::vector<std::size_t> indices,
                                                            document::StitchPointType type) {
        return EditImportedPointsCommand(Kind::SetType, to_items(indices), type);
    }

    void apply(document::Project& project) override {
        applied_ = false;
        if (!project.imported_design || items_.empty()) {
            return;
        }
        auto& design = *project.imported_design;
        auto& cmds = design.sequence.commands;
        for (const Item& it : items_) {
            if (it.index >= cmds.size()) {
                return;
            }
            const auto t = cmds[it.index].type;
            if (t != stitch::CommandType::Stitch && t != stitch::CommandType::Jump) {
                return; // Trim/ColorChange/Stop/End : non editables point par point
            }
        }
        before_ = design;
        applied_ = true;

        switch (kind_) {
        case Kind::Move:
            for (const Item& it : items_) {
                cmds[it.index].pos = it.pos;
                cmds[it.index].pass = stitch::StitchPass::Manual;
            }
            break;
        case Kind::SetType:
            for (const Item& it : items_) {
                cmds[it.index].type = type_ == document::StitchPointType::Jump
                                          ? stitch::CommandType::Jump
                                          : stitch::CommandType::Stitch;
                cmds[it.index].pass = stitch::StitchPass::Manual;
            }
            break;
        case Kind::Insert: {
            // Indices decroissants (tri stable) : une insertion ne decale pas celles a venir.
            auto ordered = items_;
            std::stable_sort(ordered.begin(), ordered.end(),
                             [](const Item& a, const Item& b) { return a.index > b.index; });
            for (const Item& it : ordered) {
                const std::size_t at = it.index + 1;
                stitch::StitchCommand fresh{it.pos, stitch::CommandType::Stitch,
                                            cmds[it.index].source, stitch::StitchPass::Manual};
                cmds.insert(cmds.begin() + static_cast<std::ptrdiff_t>(at), fresh);
                for (auto& b : design.color_blocks) {
                    b.start += (b.start >= at) ? 1 : 0;
                    b.end += (b.end >= at) ? 1 : 0;
                }
            }
            break;
        }
        case Kind::Delete: {
            std::vector<std::size_t> idx;
            idx.reserve(items_.size());
            for (const Item& it : items_) {
                idx.push_back(it.index);
            }
            std::sort(idx.begin(), idx.end());
            idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
            const auto shift = [&idx](std::size_t v) {
                return v - static_cast<std::size_t>(std::lower_bound(idx.begin(), idx.end(), v) -
                                                    idx.begin());
            };
            for (auto& b : design.color_blocks) {
                b.start = shift(b.start);
                b.end = shift(b.end);
            }
            for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
                cmds.erase(cmds.begin() + static_cast<std::ptrdiff_t>(*it));
            }
            break;
        }
        }
    }

    void revert(document::Project& project) override {
        if (applied_ && before_) {
            project.imported_design = std::move(before_);
            before_.reset();
            applied_ = false;
        }
    }

    [[nodiscard]] std::string name() const override {
        switch (kind_) {
        case Kind::Move:
            return "Deplacer des points importes";
        case Kind::Delete:
            return "Supprimer des points importes";
        case Kind::Insert:
            return "Inserer un point importe";
        case Kind::SetType:
            return "Changer le type de points importes";
        }
        return {};
    }

    [[nodiscard]] bool applied() const { return applied_; }

private:
    static std::vector<Item> to_items(const std::vector<std::size_t>& indices) {
        std::vector<Item> items;
        items.reserve(indices.size());
        for (std::size_t i : indices) {
            items.push_back(Item{i, {}});
        }
        return items;
    }
    EditImportedPointsCommand(Kind kind, std::vector<Item> items, document::StitchPointType type)
        : kind_(kind), items_(std::move(items)), type_(type) {}

    Kind kind_;
    std::vector<Item> items_;
    document::StitchPointType type_;
    bool applied_{false};
    std::optional<document::ImportedDesign> before_;
};

} // namespace openstitch::commands
