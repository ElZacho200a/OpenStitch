// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/geometry/path.hpp"
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

// Supprime un objet de broderie seul (garde l'objet vectoriel source, qui
// reste convertible en un autre type). Undo réinsère à l'index d'origine.
class RemoveEmbroideryObjectCommand final : public ICommand {
public:
    explicit RemoveEmbroideryObjectCommand(ObjectId id) : id_(id) {}

    void apply(document::Project& project) override {
        applied_ = false;
        auto& objs = project.embroidery_objects;
        for (std::size_t i = 0; i < objs.size(); ++i) {
            if (objs[i].id == id_) {
                index_ = i;
                removed_ = objs[i];
                objs.erase(objs.begin() + static_cast<std::ptrdiff_t>(i));
                applied_ = true;
                return;
            }
        }
    }
    void revert(document::Project& project) override {
        if (!applied_) {
            return;
        }
        const std::size_t pos = std::min(index_, project.embroidery_objects.size());
        project.embroidery_objects.insert(
            project.embroidery_objects.begin() + static_cast<std::ptrdiff_t>(pos), removed_);
        applied_ = false;
    }
    [[nodiscard]] std::string name() const override { return "Supprimer l'objet de broderie"; }

private:
    ObjectId id_;
    std::size_t index_{};
    document::EmbroideryObject removed_{};
    bool applied_{false};
};

// Supprime un objet vectoriel ET tout objet de broderie qui en dépend
// (source_vector == id) en une seule transaction : sans cela, un objet de
// broderie se retrouverait avec un source_vector orphelin, jamais un état
// valide dans ce document. Undo restaure les deux exactement à leurs index
// d'origine.
class RemoveVectorObjectCommand final : public ICommand {
public:
    explicit RemoveVectorObjectCommand(ObjectId id) : id_(id) {}

    void apply(document::Project& project) override {
        applied_ = false;
        removedEmbroideries_.clear();
        auto& vecs = project.vector_objects;
        std::size_t vecIndex = vecs.size();
        for (std::size_t i = 0; i < vecs.size(); ++i) {
            if (vecs[i].id == id_) {
                vecIndex = i;
                break;
            }
        }
        if (vecIndex == vecs.size()) {
            return;  // introuvable : no-op
        }
        removedVector_ = vecs[vecIndex];
        vectorIndex_ = vecIndex;
        vecs.erase(vecs.begin() + static_cast<std::ptrdiff_t>(vecIndex));

        auto& embs = project.embroidery_objects;
        for (std::size_t i = 0; i < embs.size();) {
            if (embs[i].source_vector == id_) {
                removedEmbroideries_.emplace_back(i, embs[i]);
                embs.erase(embs.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
        applied_ = true;
    }
    void revert(document::Project& project) override {
        if (!applied_) {
            return;
        }
        const std::size_t vecPos = std::min(vectorIndex_, project.vector_objects.size());
        project.vector_objects.insert(
            project.vector_objects.begin() + static_cast<std::ptrdiff_t>(vecPos), removedVector_);
        // Ordre croissant d'index d'origine : réinsérer dans cet ordre
        // reproduit exactement la disposition initiale.
        for (const auto& [index, emb] : removedEmbroideries_) {
            const std::size_t pos = std::min(index, project.embroidery_objects.size());
            project.embroidery_objects.insert(
                project.embroidery_objects.begin() + static_cast<std::ptrdiff_t>(pos), emb);
        }
        applied_ = false;
    }
    [[nodiscard]] std::string name() const override { return "Supprimer l'objet vectoriel"; }

private:
    ObjectId id_;
    document::VectorObject removedVector_{};
    std::size_t vectorIndex_{};
    std::vector<std::pair<std::size_t, document::EmbroideryObject>> removedEmbroideries_;
    bool applied_{false};
};

// Déplace un objet vectoriel ENTIER (tous les morceaux, tous les trous)
// d'un même delta — glisser la forme au lieu de déplacer chaque nœud un par
// un (défaut remonté en usage réel : aucune commande n'existait pour ça,
// seule l'édition nœud par nœud était possible).
class TranslateVectorObjectCommand final : public ICommand {
public:
    TranslateVectorObjectCommand(ObjectId object, Vec2um delta) : object_(object), delta_(delta) {}

    void apply(document::Project& project) override { shift(project, delta_); }
    void revert(document::Project& project) override {
        shift(project, Vec2um{-delta_.x, -delta_.y});
    }
    [[nodiscard]] std::string name() const override { return "Déplacement de forme"; }

private:
    void shift(document::Project& project, Vec2um delta) const {
        auto* object = project.findObject(object_);
        if (object == nullptr) {
            return;
        }
        for (auto& set : object->paths) {
            for (auto& node : set.outer.nodes) {
                node.pos = node.pos + delta;
            }
            for (auto& hole : set.holes) {
                for (auto& node : hole.nodes) {
                    node.pos = node.pos + delta;
                }
            }
        }
    }

    ObjectId object_;
    Vec2um delta_;
};

// Redimensionne un objet vectoriel ENTIER autour d'un point d'ancrage FIXE
// (le coin opposé à la poignée glissée côté apps/desktop), indépendamment
// sur chaque axe. Les tangentes Bézier (relatives au nœud) sont mises à
// l'échelle avec lui pour conserver la forme des courbes. Undo EXACT par
// instantané (pas une inversion de facteur d'échelle, qui dériverait de
// quelques µm par l'arrondi entier) — même exigence de précision que le
// reste du projet (déterminisme, DST bit-à-bit).
class ScaleVectorObjectCommand final : public ICommand {
public:
    ScaleVectorObjectCommand(ObjectId object, Vec2um anchor, double scaleX, double scaleY)
        : object_(object), anchor_(anchor), scaleX_(scaleX), scaleY_(scaleY) {}

    void apply(document::Project& project) override {
        auto* object = project.findObject(object_);
        if (object == nullptr) {
            return;
        }
        before_ = object->paths;
        for (auto& set : object->paths) {
            for (auto& node : set.outer.nodes) {
                scaleNode(node);
            }
            for (auto& hole : set.holes) {
                for (auto& node : hole.nodes) {
                    scaleNode(node);
                }
            }
        }
    }
    void revert(document::Project& project) override {
        if (auto* object = project.findObject(object_)) {
            object->paths = before_;
        }
    }
    [[nodiscard]] std::string name() const override { return "Redimensionnement de forme"; }

private:
    [[nodiscard]] Vec2um scalePoint(Vec2um p) const {
        const double dx = static_cast<double>((p.x - anchor_.x).value);
        const double dy = static_cast<double>((p.y - anchor_.y).value);
        return Vec2um{
            Micrometers{anchor_.x.value + static_cast<std::int32_t>(std::lround(dx * scaleX_))},
            Micrometers{anchor_.y.value + static_cast<std::int32_t>(std::lround(dy * scaleY_))}};
    }
    [[nodiscard]] std::optional<Vec2um> scaleTangent(std::optional<Vec2um> t) const {
        if (!t) {
            return std::nullopt;
        }
        return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(t->x.value * scaleX_))},
                     Micrometers{static_cast<std::int32_t>(std::lround(t->y.value * scaleY_))}};
    }
    void scaleNode(geometry::PathNode& node) const {
        node.pos = scalePoint(node.pos);
        node.tan_in = scaleTangent(node.tan_in);
        node.tan_out = scaleTangent(node.tan_out);
    }

    ObjectId object_;
    Vec2um anchor_;
    double scaleX_;
    double scaleY_;
    std::vector<geometry::PathSet> before_;
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

// Déplace une poignée Bézier (tan_in ou tan_out, relative au nœud) d'un objet
// vectoriel. `std::nullopt` efface la poignée (le segment concerné redevient
// droit) — miroir de MoveSatinRailHandleCommand (rails satin), adressé par
// NodeRef comme MoveNodeCommand ci-dessus.
class SetNodeHandleCommand final : public ICommand {
public:
    SetNodeHandleCommand(ObjectId object, document::NodeRef ref, bool isOut,
                         std::optional<Vec2um> oldHandle, std::optional<Vec2um> newHandle)
        : object_(object), ref_(ref), isOut_(isOut), oldHandle_(oldHandle), newHandle_(newHandle) {}

    void apply(document::Project& project) override { setHandle(project, newHandle_); }
    void revert(document::Project& project) override { setHandle(project, oldHandle_); }
    [[nodiscard]] std::string name() const override { return "Poignée Bézier"; }

private:
    void setHandle(document::Project& project, std::optional<Vec2um> handle) {
        if (auto* object = project.findObject(object_)) {
            if (auto* path = document::path_in(*object, ref_.set, ref_.path);
                path != nullptr && ref_.node < path->nodes.size()) {
                (isOut_ ? path->nodes[ref_.node].tan_out : path->nodes[ref_.node].tan_in) = handle;
            }
        }
    }

    ObjectId object_;
    document::NodeRef ref_;
    bool isOut_;
    std::optional<Vec2um> oldHandle_;
    std::optional<Vec2um> newHandle_;
};

// Bascule le type (Coin/Lisse) d'un nœud d'objet vectoriel.
class SetNodeTypeCommand final : public ICommand {
public:
    SetNodeTypeCommand(ObjectId object, document::NodeRef ref, geometry::NodeType type)
        : object_(object), ref_(ref), type_(type) {}

    void apply(document::Project& project) override {
        if (auto* object = project.findObject(object_)) {
            if (auto* path = document::path_in(*object, ref_.set, ref_.path);
                path != nullptr && ref_.node < path->nodes.size()) {
                previous_ = path->nodes[ref_.node].type;
                path->nodes[ref_.node].type = type_;
            }
        }
    }
    void revert(document::Project& project) override {
        if (auto* object = project.findObject(object_)) {
            if (auto* path = document::path_in(*object, ref_.set, ref_.path);
                path != nullptr && ref_.node < path->nodes.size()) {
                path->nodes[ref_.node].type = previous_;
            }
        }
    }
    [[nodiscard]] std::string name() const override { return "Type de nœud"; }

private:
    ObjectId object_;
    document::NodeRef ref_;
    geometry::NodeType type_;
    geometry::NodeType previous_{geometry::NodeType::Corner};
};

// Supprime un nœud d'un objet vectoriel — simplification manuelle d'une
// forme (typiquement après vectorisation d'une région segmentée, quand le
// contour est trop détaillé pour être exploitable tel quel). Refuse de
// vider un chemin sous 3 nœuds (un polygone a besoin d'au moins un
// triangle) : no-op silencieux si la précondition n'est pas respectée,
// l'appelant est censé l'avoir déjà vérifiée avant de pousser la commande.
class RemoveNodeCommand final : public ICommand {
public:
    RemoveNodeCommand(ObjectId object, document::NodeRef ref) : object_(object), ref_(ref) {}

    void apply(document::Project& project) override {
        if (auto* object = project.findObject(object_)) {
            if (auto* path = document::path_in(*object, ref_.set, ref_.path);
                path != nullptr && ref_.node < path->nodes.size() && path->nodes.size() > 3) {
                removed_ = path->nodes[ref_.node];
                path->nodes.erase(path->nodes.begin() + static_cast<std::ptrdiff_t>(ref_.node));
                removedApplied_ = true;
            }
        }
    }
    void revert(document::Project& project) override {
        if (!removedApplied_) {
            return;
        }
        if (auto* object = project.findObject(object_)) {
            if (auto* path = document::path_in(*object, ref_.set, ref_.path);
                path != nullptr && ref_.node <= path->nodes.size()) {
                path->nodes.insert(path->nodes.begin() + static_cast<std::ptrdiff_t>(ref_.node), removed_);
            }
        }
        removedApplied_ = false;
    }
    [[nodiscard]] std::string name() const override { return "Suppression de nœud"; }

private:
    ObjectId object_;
    document::NodeRef ref_;
    geometry::PathNode removed_{};
    bool removedApplied_{false};
};

// Ajoute en une seule opération plusieurs objets vectoriels et de broderie
// (résultat de l'autonumérisation). L'annulation retire exactement ce lot.
class AddObjectBatchCommand final : public ICommand {
public:
    AddObjectBatchCommand(std::vector<document::VectorObject> vectors,
                          std::vector<document::EmbroideryObject> embroideries,
                          std::string label = "Numérisation automatique")
        : vectors_(std::move(vectors)),
          embroideries_(std::move(embroideries)),
          label_(std::move(label)) {}

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
    [[nodiscard]] std::string name() const override { return label_; }

private:
    std::vector<document::VectorObject> vectors_;
    std::vector<document::EmbroideryObject> embroideries_;
    std::string label_;
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

// Retouches manuelles de points generes (Lot 8.1, cf. docs/lot8-manual-editing-design.md
// SS3). `raw_fingerprint`/`raw_point_count` sont le fingerprint/la taille de
// `stitch_generation::raw_slice(objet)` sur la sequence ACTUELLE, calcules par
// l'appelant AVANT de construire la commande (meme convention que
// `SetSegmentationCommand` : un calcul faillible/couteux a lieu hors de la
// commande). Ce choix evite une dependance circulaire libs/commands ->
// libs/stitch_generation (qui depend deja de libs/document, jamais l'inverse) :
// la commande ne connait que document::Project, jamais generate_sequence.
namespace detail {

// Etat memorise par une commande d'edition de point pour un revert exact.
struct StitchEditContext {
    bool valid{false};          // apply() a reellement modifie le document
    bool createdEntry{false};   // l'entree base_index n'existait pas avant
    bool wasClean{false};       // overrides etait vide avant (transition Clean->ManuallyEdited)
    document::StitchOverride previousEntry{};
    std::uint64_t previousFingerprint{0};
    std::uint32_t previousPointCount{0};
};

// Localise (en la creant si besoin) l'entree `overrides[base_index]` de
// l'objet `id`, prete a recevoir la mutation du champ specifique de
// l'appelant (moved_to/forced_type/trim_after). Refuse (retourne nullptr,
// aucune mutation) si l'objet est introuvable, si `base_index` deborde la vue
// brute actuelle (`raw_point_count`), ou si l'objet est deja `Dirty` --
// aucune commande ne doit faire revivre un objet Dirty sans passage explicite
// par `DiscardOverridesCommand` (cadrage SS1, "pas de transition Dirty ->
// ManuallyEdited").
[[nodiscard]] inline document::StitchOverride* begin_stitch_edit(
    document::Project& project, ObjectId id, std::size_t base_index,
    std::uint64_t raw_fingerprint, std::uint32_t raw_point_count, StitchEditContext& ctx) {
    ctx = StitchEditContext{};

    auto* obj = project.findEmbroidery(id);
    if (obj == nullptr || base_index >= raw_point_count) {
        return nullptr;
    }
    const bool wasEmpty = obj->overrides.empty();
    const bool dirty = !wasEmpty && (obj->edited_point_count != raw_point_count ||
                                     obj->edited_fingerprint != raw_fingerprint);
    if (dirty) {
        return nullptr;
    }

    ctx.wasClean = wasEmpty;
    ctx.previousFingerprint = obj->edited_fingerprint;
    ctx.previousPointCount = obj->edited_point_count;
    if (wasEmpty) {
        obj->edited_fingerprint = raw_fingerprint;
        obj->edited_point_count = raw_point_count;
    }

    auto it = std::find_if(
        obj->overrides.begin(), obj->overrides.end(),
        [base_index](const document::StitchOverride& o) { return o.base_index == base_index; });
    ctx.valid = true;
    if (it != obj->overrides.end()) {
        ctx.createdEntry = false;
        ctx.previousEntry = *it;
        return &*it;
    }
    ctx.createdEntry = true;
    document::StitchOverride fresh;
    fresh.base_index = base_index;
    obj->overrides.push_back(fresh);
    return &obj->overrides.back();
}

// Une entree sans aucune modification effective (ni position, ni type force,
// ni coupe demandee) : jamais un etat valide a persister (cf. validation
// stricte a la lecture, `json_serialize.cpp::overrides_from_json`, revue
// corrective 8.1 point 4). `SetStitchTrimCommand` est la seule commande qui
// peut y ramener une entree existante (poser trim_after=false efface son
// seul champ effectif) -- Move/SetType posent toujours un champ a valeur non
// vide, jamais de retour a l'etat vide par ce chemin.
[[nodiscard]] inline bool is_override_empty(const document::StitchOverride& o) {
    return !o.moved_to.has_value() && !o.forced_type.has_value() && !o.trim_after;
}

// Annule exactement l'effet de `begin_stitch_edit` (entree restauree ou
// retiree, `edited_fingerprint`/`edited_point_count` restaures si la
// transition Clean->ManuallyEdited avait eu lieu). No-op si `apply()` n'avait
// rien modifie (`ctx.valid == false`). Gere aussi le cas ou `apply()` a videe
// puis retiree une entree PREEXISTANTE (`SetStitchTrimCommand`, cf.
// `is_override_empty`) : l'entree n'est alors plus trouvable par
// `base_index`, il faut la reinserer depuis `ctx.previousEntry` plutot que de
// ne rien faire.
inline void end_stitch_edit(document::Project& project, ObjectId id, std::size_t base_index,
                            const StitchEditContext& ctx) {
    if (!ctx.valid) {
        return;
    }
    auto* obj = project.findEmbroidery(id);
    if (obj == nullptr) {
        return;
    }
    auto it = std::find_if(
        obj->overrides.begin(), obj->overrides.end(),
        [base_index](const document::StitchOverride& o) { return o.base_index == base_index; });
    if (ctx.createdEntry) {
        if (it != obj->overrides.end()) {
            obj->overrides.erase(it);
        }
    } else if (it != obj->overrides.end()) {
        *it = ctx.previousEntry;
    } else {
        obj->overrides.push_back(ctx.previousEntry);
    }
    if (ctx.wasClean) {
        obj->edited_fingerprint = ctx.previousFingerprint;
        obj->edited_point_count = ctx.previousPointCount;
    }
}

}  // namespace detail

// Deplace un point genere (`StitchOverride::moved_to`). Seule la position est
// posee ; l'eligibilite de la cible (Stitch en passe TopStitch) est validee
// plus tard par `stitch_generation::apply_manual_overrides`, pas ici (le
// coeur pur reste l'unique source de verite sur l'eligibilite, cf. cadrage
// SS1).
class MoveStitchPointCommand final : public ICommand {
public:
    MoveStitchPointCommand(ObjectId id, std::size_t base_index, Vec2um new_pos,
                           std::uint64_t raw_fingerprint, std::uint32_t raw_point_count)
        : id_(id),
          base_index_(base_index),
          new_pos_(new_pos),
          raw_fingerprint_(raw_fingerprint),
          raw_point_count_(raw_point_count) {}

    void apply(document::Project& project) override {
        if (auto* entry = detail::begin_stitch_edit(project, id_, base_index_, raw_fingerprint_,
                                                    raw_point_count_, ctx_)) {
            entry->moved_to = new_pos_;
        }
    }
    void revert(document::Project& project) override {
        detail::end_stitch_edit(project, id_, base_index_, ctx_);
    }
    [[nodiscard]] std::string name() const override { return "Déplacement de point"; }

private:
    ObjectId id_;
    std::size_t base_index_;
    Vec2um new_pos_;
    std::uint64_t raw_fingerprint_;
    std::uint32_t raw_point_count_;
    detail::StitchEditContext ctx_;
};

// Force le type d'un point genere (`StitchOverride::forced_type`) : seule
// transition permise en MVP, Stitch<->Jump (cadrage SS2.2).
class SetStitchPointTypeCommand final : public ICommand {
public:
    SetStitchPointTypeCommand(ObjectId id, std::size_t base_index,
                              document::StitchPointType forced_type,
                              std::uint64_t raw_fingerprint, std::uint32_t raw_point_count)
        : id_(id),
          base_index_(base_index),
          forced_type_(forced_type),
          raw_fingerprint_(raw_fingerprint),
          raw_point_count_(raw_point_count) {}

    void apply(document::Project& project) override {
        if (auto* entry = detail::begin_stitch_edit(project, id_, base_index_, raw_fingerprint_,
                                                    raw_point_count_, ctx_)) {
            entry->forced_type = forced_type_;
        }
    }
    void revert(document::Project& project) override {
        detail::end_stitch_edit(project, id_, base_index_, ctx_);
    }
    [[nodiscard]] std::string name() const override { return "Type de point"; }

private:
    ObjectId id_;
    std::size_t base_index_;
    document::StitchPointType forced_type_;
    std::uint64_t raw_fingerprint_;
    std::uint32_t raw_point_count_;
    detail::StitchEditContext ctx_;
};

// Ajoute/retire un `Trim` juste apres un point genere
// (`StitchOverride::trim_after`, cadrage SS2.3). N'ajoute ni ne supprime de
// point de couture : une commande machine supplementaire seulement.
class SetStitchTrimCommand final : public ICommand {
public:
    SetStitchTrimCommand(ObjectId id, std::size_t base_index, bool trim_after,
                         std::uint64_t raw_fingerprint, std::uint32_t raw_point_count)
        : id_(id),
          base_index_(base_index),
          trim_after_(trim_after),
          raw_fingerprint_(raw_fingerprint),
          raw_point_count_(raw_point_count) {}

    void apply(document::Project& project) override {
        if (!trim_after_) {
            // Retirer un Trim qui n'existe deja pas est un no-op : ne jamais
            // creer d'entree vide via begin_stitch_edit, ni faire passer
            // l'objet de Clean a ManuallyEdited pour rien (revue corrective
            // 8.1, point 5). Ne s'applique qu'a trim_after_ == false : poser
            // trim_after_ == true est toujours une modification effective,
            // meme sur un index sans entree prealable.
            auto* obj = project.findEmbroidery(id_);
            const bool hasEntry =
                obj != nullptr &&
                std::any_of(obj->overrides.begin(), obj->overrides.end(),
                           [this](const document::StitchOverride& o) {
                               return o.base_index == base_index_;
                           });
            if (!hasEntry) {
                return;
            }
        }
        if (auto* entry = detail::begin_stitch_edit(project, id_, base_index_, raw_fingerprint_,
                                                    raw_point_count_, ctx_)) {
            entry->trim_after = trim_after_;
            if (detail::is_override_empty(*entry)) {
                // trim_after etait le seul champ effectif de cette entree
                // PREEXISTANTE (sinon `ctx_.createdEntry` serait vrai et le
                // garde-fou ci-dessus l'aurait empechee d'exister) : la
                // retirer plutot que de laisser une entree vide, invalide a
                // la relecture (`json_serialize.cpp`). `end_stitch_edit` sait
                // la reinserer depuis `ctx_.previousEntry` si `revert()` est
                // appele ensuite.
                if (auto* obj = project.findEmbroidery(id_)) {
                    auto it = std::find_if(obj->overrides.begin(), obj->overrides.end(),
                                           [this](const document::StitchOverride& o) {
                                               return o.base_index == base_index_;
                                           });
                    if (it != obj->overrides.end()) {
                        obj->overrides.erase(it);
                    }
                }
            }
        }
    }
    void revert(document::Project& project) override {
        detail::end_stitch_edit(project, id_, base_index_, ctx_);
    }
    [[nodiscard]] std::string name() const override { return "Coupe de fil"; }

private:
    ObjectId id_;
    std::size_t base_index_;
    bool trim_after_;
    std::uint64_t raw_fingerprint_;
    std::uint32_t raw_point_count_;
    detail::StitchEditContext ctx_;
};

// Abandonne les retouches manuelles d'un objet (seule sortie de l'etat Dirty
// en MVP, cadrage SS1/SS3) : vide `overrides` et remet le
// fingerprint/compteur a zero. Toujours annulable, meme depuis Dirty (aucune
// perte : l'ancien contenu est memorise pour le revert).
class DiscardOverridesCommand final : public ICommand {
public:
    explicit DiscardOverridesCommand(ObjectId id) : id_(id) {}

    void apply(document::Project& project) override {
        if (auto* obj = project.findEmbroidery(id_)) {
            previousOverrides_ = obj->overrides;
            previousFingerprint_ = obj->edited_fingerprint;
            previousPointCount_ = obj->edited_point_count;
            obj->overrides.clear();
            obj->edited_fingerprint = 0;
            obj->edited_point_count = 0;
        }
    }
    void revert(document::Project& project) override {
        if (auto* obj = project.findEmbroidery(id_)) {
            obj->overrides = previousOverrides_;
            obj->edited_fingerprint = previousFingerprint_;
            obj->edited_point_count = previousPointCount_;
        }
    }
    [[nodiscard]] std::string name() const override { return "Abandonner les retouches"; }

private:
    ObjectId id_;
    std::vector<document::StitchOverride> previousOverrides_;
    std::uint64_t previousFingerprint_{0};
    std::uint32_t previousPointCount_{0};
};

// Édition atomique des guides transversaux d'une colonne satin. La validation
// géométrique (projection sur les deux rails et monotonie) est effectuée avant
// construction par l'outil interactif ; ces commandes protègent néanmoins le
// document contre un identifiant/type/index devenu obsolète entre le geste et
// son commit (changement de sélection ou de projet).
class AddSatinGuideCommand final : public ICommand {
public:
    AddSatinGuideCommand(ObjectId id, document::SatinRung guide, std::size_t index)
        : id_(id), guide_(guide), index_(index) {}

    void apply(document::Project& project) override {
        applied_ = false;
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* satin = std::get_if<document::SatinParams>(&obj->params);
                satin != nullptr && index_ <= satin->rungs.size()) {
                satin->rungs.insert(satin->rungs.begin() + static_cast<std::ptrdiff_t>(index_), guide_);
                applied_ = true;
            }
        }
    }
    void revert(document::Project& project) override {
        if (!applied_) return;
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* satin = std::get_if<document::SatinParams>(&obj->params);
                satin != nullptr && index_ < satin->rungs.size()) {
                satin->rungs.erase(satin->rungs.begin() + static_cast<std::ptrdiff_t>(index_));
            }
        }
    }
    [[nodiscard]] std::string name() const override { return "Ajouter un guide satin"; }

private:
    ObjectId id_;
    document::SatinRung guide_;
    std::size_t index_{};
    bool applied_{false};
};

struct SatinGuideAddition {
    ObjectId id{};
    document::SatinRung guide{};
    std::size_t index{};
};

// Ajoute exactement un guide interne par section comme une transaction unique.
// Toutes les sections sont validées avant la première insertion ; les objets
// dupliqués sont refusés car une section de réseau correspond à un objet satin.
class AddSatinGuidesCommand final : public ICommand {
public:
    explicit AddSatinGuidesCommand(std::vector<SatinGuideAddition> additions)
        : additions_(std::move(additions)) {}

    void apply(document::Project& project) override {
        applied_ = false;
        if (!targetsAreValid(project)) {
            return;
        }
        for (const auto& addition : additions_) {
            auto* object = project.findEmbroidery(addition.id);
            auto& rungs = std::get<document::SatinParams>(object->params).rungs;
            rungs.insert(rungs.begin() + static_cast<std::ptrdiff_t>(addition.index),
                         addition.guide);
        }
        applied_ = true;
    }

    void revert(document::Project& project) override {
        if (!applied_ || !insertionsAreIntact(project)) {
            return;
        }
        for (const auto& addition : additions_) {
            auto* object = project.findEmbroidery(addition.id);
            auto& rungs = std::get<document::SatinParams>(object->params).rungs;
            rungs.erase(rungs.begin() + static_cast<std::ptrdiff_t>(addition.index));
        }
        applied_ = false;
    }

    [[nodiscard]] std::string name() const override {
        return "Ajouter des guides satin coordonnés";
    }

private:
    [[nodiscard]] bool targetsAreValid(document::Project& project) const {
        if (additions_.empty()) {
            return false;
        }
        std::vector<std::uint64_t> ids;
        ids.reserve(additions_.size());
        for (const auto& addition : additions_) {
            auto* object = project.findEmbroidery(addition.id);
            const auto* satin =
                object != nullptr ? std::get_if<document::SatinParams>(&object->params) : nullptr;
            if (satin == nullptr || addition.index > satin->rungs.size()) {
                return false;
            }
            ids.push_back(addition.id.value);
        }
        std::sort(ids.begin(), ids.end());
        return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
    }

    [[nodiscard]] bool insertionsAreIntact(document::Project& project) const {
        for (const auto& addition : additions_) {
            auto* object = project.findEmbroidery(addition.id);
            const auto* satin =
                object != nullptr ? std::get_if<document::SatinParams>(&object->params) : nullptr;
            if (satin == nullptr || addition.index >= satin->rungs.size() ||
                satin->rungs[addition.index] != addition.guide) {
                return false;
            }
        }
        return true;
    }

    std::vector<SatinGuideAddition> additions_;
    bool applied_{false};
};

class MoveSatinGuideCommand final : public ICommand {
public:
    MoveSatinGuideCommand(ObjectId id, std::size_t index, document::SatinRung guide)
        : id_(id), index_(index), guide_(guide) {}

    void apply(document::Project& project) override {
        applied_ = false;
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* satin = std::get_if<document::SatinParams>(&obj->params);
                satin != nullptr && index_ < satin->rungs.size()) {
                previous_ = satin->rungs[index_];
                satin->rungs[index_] = guide_;
                applied_ = true;
            }
        }
    }
    void revert(document::Project& project) override {
        if (!applied_) return;
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* satin = std::get_if<document::SatinParams>(&obj->params);
                satin != nullptr && index_ < satin->rungs.size()) {
                satin->rungs[index_] = previous_;
            }
        }
    }
    [[nodiscard]] std::string name() const override { return "Déplacer un guide satin"; }

private:
    ObjectId id_;
    std::size_t index_{};
    document::SatinRung guide_;
    document::SatinRung previous_{};
    bool applied_{false};
};

struct SatinGuideEdit {
    ObjectId id{};
    std::size_t index{};
    document::SatinRung guide{};
};

// Applique plusieurs guides comme une seule transaction undo/redo. Les cibles
// sont toutes validées avant la première mutation : une section supprimée,
// devenue non-satin ou un doublon rend l'ensemble neutre, sans état partiel.
// La projection et la monotonie restent validées par stitch_generation avant
// la construction de la commande, comme pour MoveSatinGuideCommand.
class MoveSatinGuidesCommand final : public ICommand {
public:
    explicit MoveSatinGuidesCommand(std::vector<SatinGuideEdit> edits)
        : edits_(std::move(edits)) {}

    void apply(document::Project& project) override {
        applied_ = false;
        previous_.clear();
        if (!targetsAreValid(project)) {
            return;
        }
        previous_.reserve(edits_.size());
        for (const auto& edit : edits_) {
            auto* object = project.findEmbroidery(edit.id);
            auto& satin = std::get<document::SatinParams>(object->params);
            previous_.push_back(satin.rungs[edit.index]);
        }
        for (const auto& edit : edits_) {
            auto* object = project.findEmbroidery(edit.id);
            std::get<document::SatinParams>(object->params).rungs[edit.index] = edit.guide;
        }
        applied_ = true;
    }

    void revert(document::Project& project) override {
        if (!applied_ || previous_.size() != edits_.size() || !targetsAreValid(project)) {
            return;
        }
        for (std::size_t i = 0; i < edits_.size(); ++i) {
            auto* object = project.findEmbroidery(edits_[i].id);
            std::get<document::SatinParams>(object->params).rungs[edits_[i].index] = previous_[i];
        }
        applied_ = false;
    }

    [[nodiscard]] std::string name() const override {
        return "Déplacer des guides satin coordonnés";
    }

private:
    [[nodiscard]] bool targetsAreValid(document::Project& project) const {
        if (edits_.empty()) {
            return false;
        }
        std::vector<std::pair<std::uint64_t, std::size_t>> keys;
        keys.reserve(edits_.size());
        for (const auto& edit : edits_) {
            auto* object = project.findEmbroidery(edit.id);
            const auto* satin =
                object != nullptr ? std::get_if<document::SatinParams>(&object->params) : nullptr;
            if (satin == nullptr || edit.index >= satin->rungs.size()) {
                return false;
            }
            keys.emplace_back(edit.id.value, edit.index);
        }
        std::sort(keys.begin(), keys.end());
        return std::adjacent_find(keys.begin(), keys.end()) == keys.end();
    }

    std::vector<SatinGuideEdit> edits_;
    std::vector<document::SatinRung> previous_;
    bool applied_{false};
};

class RemoveSatinGuideCommand final : public ICommand {
public:
    RemoveSatinGuideCommand(ObjectId id, std::size_t index) : id_(id), index_(index) {}

    void apply(document::Project& project) override {
        applied_ = false;
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* satin = std::get_if<document::SatinParams>(&obj->params);
                satin != nullptr && index_ < satin->rungs.size()) {
                removed_ = satin->rungs[index_];
                satin->rungs.erase(satin->rungs.begin() + static_cast<std::ptrdiff_t>(index_));
                applied_ = true;
            }
        }
    }
    void revert(document::Project& project) override {
        if (!applied_) return;
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* satin = std::get_if<document::SatinParams>(&obj->params);
                satin != nullptr && index_ <= satin->rungs.size()) {
                satin->rungs.insert(satin->rungs.begin() + static_cast<std::ptrdiff_t>(index_), removed_);
            }
        }
    }
    [[nodiscard]] std::string name() const override { return "Supprimer un guide satin"; }

private:
    ObjectId id_;
    std::size_t index_{};
    document::SatinRung removed_{};
    bool applied_{false};
};

struct SatinGuideRemoval {
    ObjectId id{};
    std::size_t index{};
};

// Supprime plusieurs guides comme une seule transaction undo/redo (groupe lié
// par link_id, cf. RemoveSatinGuidesCommand::apply). Toutes les cibles sont
// validées avant la première suppression, même logique tout-ou-rien que
// MoveSatinGuidesCommand/AddSatinGuidesCommand.
class RemoveSatinGuidesCommand final : public ICommand {
public:
    explicit RemoveSatinGuidesCommand(std::vector<SatinGuideRemoval> removals)
        : removals_(std::move(removals)) {}

    void apply(document::Project& project) override {
        applied_ = false;
        removed_.clear();
        if (!targetsAreValid(project)) {
            return;
        }
        // Retire chaque objet de l'index le plus haut vers le plus bas afin
        // qu'une suppression n'invalide pas l'index d'une autre cible du même
        // objet (rare — un groupe lié compte en pratique un guide par
        // section/objet, mais rien ne l'impose structurellement).
        auto ordered = removals_;
        std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.id != rhs.id)
                return lhs.id.value < rhs.id.value;
            return lhs.index > rhs.index;
        });
        removed_.reserve(ordered.size());
        for (const auto& target : ordered) {
            auto* object = project.findEmbroidery(target.id);
            auto& rungs = std::get<document::SatinParams>(object->params).rungs;
            removed_.push_back({target.id, target.index, rungs[target.index]});
            rungs.erase(rungs.begin() + static_cast<std::ptrdiff_t>(target.index));
        }
        applied_ = true;
    }

    void revert(document::Project& project) override {
        if (!applied_ || !revertTargetsAreValid(project)) {
            return; // état divergent depuis apply() : reste "appliqué", jamais de restauration
                    // partielle
        }
        // removed_ est trié index décroissant par objet (ordre de suppression) ;
        // le rejouer en sens inverse réinsère chaque objet index croissant afin
        // que les positions retrouvent exactement leur état d'origine.
        for (auto it = removed_.rbegin(); it != removed_.rend(); ++it) {
            auto* object = project.findEmbroidery(it->id);
            auto* satin = std::get_if<document::SatinParams>(&object->params);
            satin->rungs.insert(satin->rungs.begin() + static_cast<std::ptrdiff_t>(it->index),
                                it->guide);
        }
        applied_ = false;
        removed_.clear();
    }

    [[nodiscard]] std::string name() const override {
        return "Supprimer des guides satin coordonnés";
    }

private:
    // Simule la séquence d'insertion (même ordre que revert()) sur la taille
    // courante de chaque objet, sans muter le projet : garantit que la
    // restauration sera tout-ou-rien même si le document a changé entre
    // apply() et revert() (objet supprimé, redevenu non-satin, etc.).
    [[nodiscard]] bool revertTargetsAreValid(document::Project& project) const {
        std::unordered_map<std::uint64_t, std::size_t> sizes;
        for (auto it = removed_.rbegin(); it != removed_.rend(); ++it) {
            auto* object = project.findEmbroidery(it->id);
            const auto* satin =
                object != nullptr ? std::get_if<document::SatinParams>(&object->params) : nullptr;
            if (satin == nullptr) {
                return false;
            }
            auto [entry, inserted] = sizes.try_emplace(it->id.value, satin->rungs.size());
            if (it->index > entry->second) {
                return false;
            }
            ++entry->second;
        }
        return true;
    }

    [[nodiscard]] bool targetsAreValid(document::Project& project) const {
        if (removals_.empty()) {
            return false;
        }
        std::vector<std::pair<std::uint64_t, std::size_t>> keys;
        std::unordered_map<std::uint64_t, std::size_t> removalsPerObject;
        keys.reserve(removals_.size());
        for (const auto& target : removals_) {
            auto* object = project.findEmbroidery(target.id);
            const auto* satin =
                object != nullptr ? std::get_if<document::SatinParams>(&object->params) : nullptr;
            if (satin == nullptr || target.index >= satin->rungs.size()) {
                return false;
            }
            keys.emplace_back(target.id.value, target.index);
            ++removalsPerObject[target.id.value];
        }
        std::sort(keys.begin(), keys.end());
        if (std::adjacent_find(keys.begin(), keys.end()) != keys.end()) {
            return false;
        }
        for (const auto& [id, count] : removalsPerObject) {
            const auto* object = project.findEmbroidery(ObjectId{id});
            const auto* satin =
                object != nullptr ? std::get_if<document::SatinParams>(&object->params) : nullptr;
            if (satin == nullptr || count > satin->rungs.size() ||
                satin->rungs.size() - count < 2) {
                return false;
            }
        }
        return true;
    }

    struct Removed {
        ObjectId id{};
        std::size_t index{};
        document::SatinRung guide{};
    };

    std::vector<SatinGuideRemoval> removals_;
    std::vector<Removed> removed_;
    bool applied_{false};
};

// Côté d'un rail satin (A ou B), adressé indépendamment des barreaux/guides
// ci-dessus : les rails ne sont pas des `VectorObject::paths` (`NodeRef` ne
// s'y applique pas), ils portent leur propre géométrie éditable dans
// `SatinParams` (§ colonne satin manuelle).
enum class SatinRailSide { RailA, RailB };

namespace detail {
[[nodiscard]] inline geometry::Path* satin_rail(document::EmbroideryObject& obj,
                                                 SatinRailSide side) {
    auto* satin = std::get_if<document::SatinParams>(&obj.params);
    if (satin == nullptr) {
        return nullptr;
    }
    return side == SatinRailSide::RailA ? &satin->rail_a : &satin->rail_b;
}
}  // namespace detail

// Déplace un nœud d'un rail satin (mode remodelage). Miroir de MoveNodeCommand
// pour les objets vectoriels.
class MoveSatinRailNodeCommand final : public ICommand {
public:
    MoveSatinRailNodeCommand(ObjectId id, SatinRailSide side, std::size_t node, Vec2um oldPos,
                             Vec2um newPos)
        : id_(id), side_(side), node_(node), oldPos_(oldPos), newPos_(newPos) {}

    void apply(document::Project& project) override { setPos(project, newPos_); }
    void revert(document::Project& project) override { setPos(project, oldPos_); }
    [[nodiscard]] std::string name() const override { return "Déplacement de nœud de rail satin"; }

private:
    void setPos(document::Project& project, Vec2um pos) {
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* rail = detail::satin_rail(*obj, side_); rail != nullptr && node_ < rail->nodes.size()) {
                rail->nodes[node_].pos = pos;
            }
        }
    }

    ObjectId id_;
    SatinRailSide side_;
    std::size_t node_{};
    Vec2um oldPos_;
    Vec2um newPos_;
};

// Déplace une poignée Bézier (tan_in ou tan_out, relative au nœud) d'un rail
// satin. `std::nullopt` efface la poignée (le segment concerné redevient droit).
class MoveSatinRailHandleCommand final : public ICommand {
public:
    MoveSatinRailHandleCommand(ObjectId id, SatinRailSide side, std::size_t node, bool isOut,
                               std::optional<Vec2um> oldHandle, std::optional<Vec2um> newHandle)
        : id_(id),
          side_(side),
          node_(node),
          isOut_(isOut),
          oldHandle_(oldHandle),
          newHandle_(newHandle) {}

    void apply(document::Project& project) override { setHandle(project, newHandle_); }
    void revert(document::Project& project) override { setHandle(project, oldHandle_); }
    [[nodiscard]] std::string name() const override { return "Poignée de rail satin"; }

private:
    void setHandle(document::Project& project, std::optional<Vec2um> handle) {
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* rail = detail::satin_rail(*obj, side_); rail != nullptr && node_ < rail->nodes.size()) {
                (isOut_ ? rail->nodes[node_].tan_out : rail->nodes[node_].tan_in) = handle;
            }
        }
    }

    ObjectId id_;
    SatinRailSide side_;
    std::size_t node_{};
    bool isOut_;
    std::optional<Vec2um> oldHandle_;
    std::optional<Vec2um> newHandle_;
};

// Bascule le type (Coin/Lisse) d'un nœud de rail satin.
class SetSatinRailNodeTypeCommand final : public ICommand {
public:
    SetSatinRailNodeTypeCommand(ObjectId id, SatinRailSide side, std::size_t node,
                                geometry::NodeType type)
        : id_(id), side_(side), node_(node), type_(type) {}

    void apply(document::Project& project) override {
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* rail = detail::satin_rail(*obj, side_); rail != nullptr && node_ < rail->nodes.size()) {
                previous_ = rail->nodes[node_].type;
                rail->nodes[node_].type = type_;
            }
        }
    }
    void revert(document::Project& project) override {
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* rail = detail::satin_rail(*obj, side_); rail != nullptr && node_ < rail->nodes.size()) {
                rail->nodes[node_].type = previous_;
            }
        }
    }
    [[nodiscard]] std::string name() const override { return "Type de nœud de rail satin"; }

private:
    ObjectId id_;
    SatinRailSide side_;
    std::size_t node_{};
    geometry::NodeType type_;
    geometry::NodeType previous_{geometry::NodeType::Corner};
};

// Insère un nœud sur un segment de rail satin par subdivision De Casteljau
// EXACTE (cf. geometry::insert_node_on_segment) : la forme du rail ne change
// pas, seul un nœud supplémentaire apparaît.
class InsertSatinRailNodeCommand final : public ICommand {
public:
    InsertSatinRailNodeCommand(ObjectId id, SatinRailSide side, std::size_t segmentIndex,
                               double t)
        : id_(id), side_(side), segmentIndex_(segmentIndex), t_(t) {}

    void apply(document::Project& project) override {
        applied_ = false;
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* rail = detail::satin_rail(*obj, side_)) {
                before_ = *rail;
                *rail = geometry::insert_node_on_segment(*rail, segmentIndex_, t_);
                applied_ = rail->nodes.size() == before_.nodes.size() + 1;
            }
        }
    }
    void revert(document::Project& project) override {
        if (!applied_) {
            return;
        }
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* rail = detail::satin_rail(*obj, side_)) {
                *rail = before_;
            }
        }
    }
    [[nodiscard]] std::string name() const override { return "Ajouter un nœud de rail satin"; }

private:
    ObjectId id_;
    SatinRailSide side_;
    std::size_t segmentIndex_{};
    double t_{};
    geometry::Path before_;
    bool applied_{false};
};

// Supprime un nœud d'un rail satin. Refuse silencieusement (no-op, comme les
// autres commandes satin ci-dessus) si le rail ne compte plus que deux nœuds :
// un rail a besoin d'au moins deux nœuds pour rester une géométrie valide.
class RemoveSatinRailNodeCommand final : public ICommand {
public:
    RemoveSatinRailNodeCommand(ObjectId id, SatinRailSide side, std::size_t node)
        : id_(id), side_(side), node_(node) {}

    void apply(document::Project& project) override {
        applied_ = false;
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* rail = detail::satin_rail(*obj, side_);
                rail != nullptr && node_ < rail->nodes.size() && rail->nodes.size() > 2) {
                removed_ = rail->nodes[node_];
                rail->nodes.erase(rail->nodes.begin() + static_cast<std::ptrdiff_t>(node_));
                applied_ = true;
            }
        }
    }
    void revert(document::Project& project) override {
        if (!applied_) {
            return;
        }
        if (auto* obj = project.findEmbroidery(id_)) {
            if (auto* rail = detail::satin_rail(*obj, side_); rail != nullptr && node_ <= rail->nodes.size()) {
                rail->nodes.insert(rail->nodes.begin() + static_cast<std::ptrdiff_t>(node_), removed_);
            }
        }
    }
    [[nodiscard]] std::string name() const override { return "Supprimer un nœud de rail satin"; }

private:
    ObjectId id_;
    SatinRailSide side_;
    std::size_t node_{};
    geometry::PathNode removed_{};
    bool applied_{false};
};

}  // namespace openstitch::commands
