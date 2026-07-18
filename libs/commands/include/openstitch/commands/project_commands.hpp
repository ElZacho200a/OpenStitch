// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <utility>

#include "openstitch/commands/command.hpp"
#include "openstitch/image/ops.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::commands {

// Ajoute une opération de prétraitement à la pile du projet.
// Invalide la segmentation (l'image de travail change) et la restaure à
// l'annulation.
class AppendImageOpCommand final : public ICommand {
public:
    explicit AppendImageOpCommand(image::ImageOp op) : op_(std::move(op)) {}

    void apply(document::Project& project) override {
        project.ops.push_back(op_);
        stashedSegmentation_ = std::move(project.segmentation);
        project.segmentation.reset();
    }
    void revert(document::Project& project) override {
        project.ops.pop_back();
        project.segmentation = std::move(stashedSegmentation_);
        stashedSegmentation_.reset();
    }
    [[nodiscard]] std::string name() const override { return image::op_name(op_); }

private:
    image::ImageOp op_;
    std::optional<segmentation::Segmentation> stashedSegmentation_;
};

// Remplace la segmentation du projet (résultat d'un calcul déjà effectué —
// le calcul, qui peut échouer, a lieu AVANT la création de la commande).
class SetSegmentationCommand final : public ICommand {
public:
    explicit SetSegmentationCommand(std::optional<segmentation::Segmentation> next)
        : next_(std::move(next)) {}

    void apply(document::Project& project) override {
        previous_ = std::move(project.segmentation);
        project.segmentation = next_;
    }
    void revert(document::Project& project) override {
        next_ = std::move(project.segmentation);
        project.segmentation = std::move(previous_);
        previous_.reset();
    }
    [[nodiscard]] std::string name() const override { return "Segmentation"; }

private:
    std::optional<segmentation::Segmentation> next_;
    std::optional<segmentation::Segmentation> previous_;
};

// Fusionne une région dans une autre. L'annulation restaure les pixels
// réétiquetés (stockés par indices, pas par copie de la carte entière).
class MergeRegionsCommand final : public ICommand {
public:
    MergeRegionsCommand(RegionId keep, RegionId absorb) : keep_(keep), absorb_(absorb) {}

    void apply(document::Project& project) override {
        auto& seg = *project.segmentation;
        absorbedRegion_ = *seg.find(absorb_);
        auto changed = segmentation::merge_regions(seg, keep_, absorb_);
        changed_ = changed ? std::move(*changed) : std::vector<std::uint32_t>{};
    }
    void revert(document::Project& project) override {
        auto& seg = *project.segmentation;
        for (const std::uint32_t idx : changed_) {
            seg.labels[idx] = static_cast<std::uint32_t>(absorb_.value);
        }
        seg.region_slots[absorb_.value - 1] = absorbedRegion_;
        seg.find(keep_)->pixel_count -= changed_.size();
    }
    [[nodiscard]] std::string name() const override { return "Fusion de régions"; }

private:
    RegionId keep_;
    RegionId absorb_;
    std::vector<std::uint32_t> changed_;
    segmentation::Region absorbedRegion_;
};

// Supprime une région (absorbée par sa voisine majoritaire ou par le fond).
class RemoveRegionCommand final : public ICommand {
public:
    explicit RemoveRegionCommand(RegionId id) : id_(id) {}

    void apply(document::Project& project) override {
        auto& seg = *project.segmentation;
        removedRegion_ = *seg.find(id_);
        auto result = segmentation::remove_region(seg, id_);
        if (result) {
            absorber_ = result->first;
            changed_ = std::move(result->second);
        }
    }
    void revert(document::Project& project) override {
        auto& seg = *project.segmentation;
        for (const std::uint32_t idx : changed_) {
            seg.labels[idx] = static_cast<std::uint32_t>(id_.value);
        }
        seg.region_slots[id_.value - 1] = removedRegion_;
        if (absorber_.valid()) {
            seg.find(absorber_)->pixel_count -= changed_.size();
        }
    }
    [[nodiscard]] std::string name() const override { return "Suppression de région"; }

private:
    RegionId id_;
    RegionId absorber_;
    std::vector<std::uint32_t> changed_;
    segmentation::Region removedRegion_;
};

// Change la couleur représentative d'une région.
class RecolorRegionCommand final : public ICommand {
public:
    RecolorRegionCommand(RegionId id, std::array<std::uint8_t, 3> rgb) : id_(id), rgb_(rgb) {}

    void apply(document::Project& project) override {
        if (auto old = segmentation::recolor_region(*project.segmentation, id_, rgb_)) {
            oldRgb_ = *old;
        }
    }
    void revert(document::Project& project) override {
        [[maybe_unused]] auto restored =
            segmentation::recolor_region(*project.segmentation, id_, oldRgb_);
    }
    [[nodiscard]] std::string name() const override { return "Recoloration de région"; }

private:
    RegionId id_;
    std::array<std::uint8_t, 3> rgb_;
    std::array<std::uint8_t, 3> oldRgb_{};
};

// Ajoute un objet vectoriel (déjà construit — l'id est généré par l'appelant
// via project.object_ids AVANT la création de la commande, pour que redo
// réutilise le même id).
class AddVectorObjectCommand final : public ICommand {
public:
    explicit AddVectorObjectCommand(document::VectorObject object) : object_(std::move(object)) {}

    void apply(document::Project& project) override { project.vector_objects.push_back(object_); }
    void revert(document::Project& project) override { project.vector_objects.pop_back(); }
    [[nodiscard]] std::string name() const override { return "Objet vectoriel"; }

private:
    document::VectorObject object_;
};

// Ajoute un objet de broderie (id généré par l'appelant, comme pour
// AddVectorObjectCommand).
class AddEmbroideryObjectCommand final : public ICommand {
public:
    explicit AddEmbroideryObjectCommand(document::EmbroideryObject object)
        : object_(std::move(object)) {}

    void apply(document::Project& project) override {
        project.embroidery_objects.push_back(object_);
    }
    void revert(document::Project& project) override { project.embroidery_objects.pop_back(); }
    [[nodiscard]] std::string name() const override { return "Objet de broderie"; }

private:
    document::EmbroideryObject object_;
};

// Déplace un nœud d'un objet vectoriel.
class MoveNodeCommand final : public ICommand {
public:
    MoveNodeCommand(ObjectId object, document::NodeRef ref, Vec2um oldPos, Vec2um newPos)
        : object_(object), ref_(ref), oldPos_(oldPos), newPos_(newPos) {}

    void apply(document::Project& project) override { setPos(project, newPos_); }
    void revert(document::Project& project) override { setPos(project, oldPos_); }
    [[nodiscard]] std::string name() const override { return "Déplacement de nœud"; }

private:
    void setPos(document::Project& project, Vec2um pos) {
        if (auto* object = project.findObject(object_)) {
            if (auto* path = document::path_in(*object, ref_.set, ref_.path)) {
                if (ref_.node < path->nodes.size()) {
                    path->nodes[ref_.node].pos = pos;
                }
            }
        }
    }

    ObjectId object_;
    document::NodeRef ref_;
    Vec2um oldPos_;
    Vec2um newPos_;
};

}  // namespace openstitch::commands
