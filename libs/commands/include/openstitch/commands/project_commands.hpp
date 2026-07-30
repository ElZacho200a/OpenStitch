// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <utility>
#include <variant>

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

// Ajoute en une seule opération plusieurs objets vectoriels et de broderie
// (résultat de l'autonumérisation). L'annulation retire exactement ce lot.
class AddObjectBatchCommand final : public ICommand {
public:
    AddObjectBatchCommand(std::vector<document::VectorObject> vectors,
                          std::vector<document::EmbroideryObject> embroideries)
        : vectors_(std::move(vectors)), embroideries_(std::move(embroideries)) {}

    void apply(document::Project& project) override {
        for (const auto& v : vectors_) {
            project.vector_objects.push_back(v);
        }
        for (const auto& e : embroideries_) {
            project.embroidery_objects.push_back(e);
        }
    }
    void revert(document::Project& project) override {
        project.vector_objects.resize(project.vector_objects.size() - vectors_.size());
        project.embroidery_objects.resize(project.embroidery_objects.size() -
                                          embroideries_.size());
    }
    [[nodiscard]] std::string name() const override { return "Numérisation automatique"; }

private:
    std::vector<document::VectorObject> vectors_;
    std::vector<document::EmbroideryObject> embroideries_;
};

// Réordonne les objets de broderie selon une permutation d'ObjectId.
class ReorderEmbroideryCommand final : public ICommand {
public:
    explicit ReorderEmbroideryCommand(std::vector<ObjectId> newOrder)
        : newOrder_(std::move(newOrder)) {}

    void apply(document::Project& project) override {
        oldOrder_.clear();
        for (const auto& obj : project.embroidery_objects) {
            oldOrder_.push_back(obj.id);
        }
        reorder(project, newOrder_);
    }
    void revert(document::Project& project) override { reorder(project, oldOrder_); }
    [[nodiscard]] std::string name() const override { return "Réordonner la couture"; }

private:
    static void reorder(document::Project& project, const std::vector<ObjectId>& order) {
        std::vector<document::EmbroideryObject> next;
        next.reserve(project.embroidery_objects.size());
        for (const ObjectId id : order) {
            if (auto* obj = project.findEmbroidery(id)) {
                next.push_back(*obj);
            }
        }
        if (next.size() == project.embroidery_objects.size()) {
            project.embroidery_objects = std::move(next);
        }
    }

    std::vector<ObjectId> newOrder_;
    std::vector<ObjectId> oldOrder_;
};

// Verrouille/déverrouille un objet de broderie (position figée à l'optimisation).
class SetEmbroideryLockCommand final : public ICommand {
public:
    SetEmbroideryLockCommand(ObjectId id, bool locked) : id_(id), locked_(locked) {}

    void apply(document::Project& project) override {
        if (auto* obj = project.findEmbroidery(id_)) {
            previous_ = obj->locked;
            obj->locked = locked_;
        }
    }
    void revert(document::Project& project) override {
        if (auto* obj = project.findEmbroidery(id_)) {
            obj->locked = previous_;
        }
    }
    [[nodiscard]] std::string name() const override {
        return locked_ ? "Verrouiller l'objet" : "Déverrouiller l'objet";
    }

private:
    ObjectId id_;
    bool locked_;
    bool previous_{false};
};

// Convertit des objets de broderie en remplissage tatami (conserve id, source,
// couleur, nom). Sert à réparer les satins automatiques naïfs qui débordent :
// le tatami est découpé sur la région, donc ne sort jamais du contour. Chaque
// paramètre d'origine est mémorisé pour un retour exact.
class ConvertFillsToTatamiCommand final : public ICommand {
public:
    explicit ConvertFillsToTatamiCommand(std::vector<ObjectId> targets)
        : targets_(std::move(targets)) {}

    void apply(document::Project& project) override {
        previous_.clear();
        for (const ObjectId id : targets_) {
            if (auto* obj = project.findEmbroidery(id)) {
                previous_.emplace_back(id, obj->params);
                obj->params = document::TatamiParams{};
            }
        }
    }
    void revert(document::Project& project) override {
        for (const auto& [id, params] : previous_) {
            if (auto* obj = project.findEmbroidery(id)) {
                obj->params = params;
            }
        }
        previous_.clear();
    }
    [[nodiscard]] std::string name() const override { return "Conversion en tatami"; }

private:
    std::vector<ObjectId> targets_;
    std::vector<std::pair<ObjectId, document::StitchParams>> previous_;
};

// Remplace le TYPE de points d'un objet de broderie (contour / tatami / satin).
// Les nouveaux paramètres sont construits par l'appelant (le satin exige des
// rails, calculés avant la commande). L'annulation restaure les paramètres exacts.
class SetStitchTypeCommand final : public ICommand {
public:
    SetStitchTypeCommand(ObjectId id, document::StitchParams params, std::string label)
        : id_(id), params_(std::move(params)), label_(std::move(label)) {}

    void apply(document::Project& project) override {
        if (auto* obj = project.findEmbroidery(id_)) {
            previous_ = obj->params;
            obj->params = params_;
        }
    }
    void revert(document::Project& project) override {
        if (auto* obj = project.findEmbroidery(id_)) {
            obj->params = previous_;
        }
    }
    [[nodiscard]] std::string name() const override { return label_; }

private:
    ObjectId id_;
    document::StitchParams params_;
    std::string label_;
    document::StitchParams previous_{document::RunningStitchParams{}};
};

// Change la taille du cadre de broderie (persistée dans le .osp). L'analyse
// « hors cadre » et l'affichage du cadre s'y réfèrent.
class SetCanvasCommand final : public ICommand {
public:
    explicit SetCanvasCommand(document::Canvas canvas) : canvas_(canvas) {}

    void apply(document::Project& project) override {
        previous_ = project.canvas;
        project.canvas = canvas_;
    }
    void revert(document::Project& project) override { project.canvas = previous_; }
    [[nodiscard]] std::string name() const override { return "Taille du cadre"; }

private:
    document::Canvas canvas_;
    document::Canvas previous_{};
};

// Modifie les PARAMÈTRES de couture d'un objet (même type de point), depuis
// l'inspecteur. Remplace les `StitchParams` et mémorise les précédents pour un
// retour exact. Généralise `SetFillAngleCommand` à tous les champs.
class SetStitchParamsCommand final : public ICommand {
public:
    SetStitchParamsCommand(ObjectId id, document::StitchParams params)
        : id_(id), params_(std::move(params)) {}

    void apply(document::Project& project) override {
        if (auto* obj = project.findEmbroidery(id_)) {
            previous_ = obj->params;
            obj->params = params_;
        }
    }
    void revert(document::Project& project) override {
        if (auto* obj = project.findEmbroidery(id_)) {
            obj->params = previous_;
        }
    }
    [[nodiscard]] std::string name() const override { return "Paramètres de couture"; }

private:
    ObjectId id_;
    document::StitchParams params_;
    document::StitchParams previous_{document::RunningStitchParams{}};
};

// Change l'orientation des fils (angle des rangées) d'un remplissage tatami.
// Les points sont régénérés depuis les paramètres (ADR-014), donc modifier
// l'angle suffit à réorienter la couture ; l'annulation restaure l'angle.
class SetFillAngleCommand final : public ICommand {
public:
    SetFillAngleCommand(ObjectId id, Angle angle) : id_(id), angle_(angle) {}

    void apply(document::Project& project) override {
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* tatami = std::get_if<document::TatamiParams>(&obj->params)) {
                previous_ = tatami->angle;
                tatami->angle = angle_;
            }
        }
    }
    void revert(document::Project& project) override {
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* tatami = std::get_if<document::TatamiParams>(&obj->params)) {
                tatami->angle = previous_;
            }
        }
    }
    [[nodiscard]] std::string name() const override { return "Orientation du remplissage"; }

private:
    ObjectId id_;
    Angle angle_;
    Angle previous_{0.0};
};

}  // namespace openstitch::commands
