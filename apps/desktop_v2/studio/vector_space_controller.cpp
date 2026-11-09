// SPDX-License-Identifier: Apache-2.0
#include "vector_space_controller.hpp"

#include <algorithm>

#include "gpu_canvas.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/vectorization/vectorize.hpp"
#include "stage_status.hpp"

namespace openstitch::desktop_v2::studio {

namespace {

const document::EmbroideryObject* embroideryOf(const document::Project& project,
                                               std::uint64_t vectorId) {
    for (const auto& object : project.embroidery_objects) {
        if (object.source_vector.value == vectorId) {
            return &object;
        }
    }
    return nullptr;
}

// Résultat d'une revectorisation des régions de la segmentation courante.
struct Fresh {
    std::vector<std::pair<ObjectId, std::vector<geometry::PathSet>>> replace;
    std::vector<document::VectorObject> add; // id à réserver à l'exécution
    int keep{0};
    int orphan{0};
};

Fresh revectorize(const document::Project& project) {
    Fresh out;
    if (!project.segmentation) {
        return out;
    }
    std::vector<RegionId> ids;
    for (const auto& slot : project.segmentation->region_slots) {
        if (slot) {
            ids.push_back(slot->id);
        }
    }
    const vectorization::VectorizeOptions options{.mm_per_px = project.mm_per_px,
                                                  .detail_mask = &project.detail_mask};
    const auto results = vectorization::vectorize_regions(*project.segmentation, ids, options);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (!results[i]) {
            continue;
        }
        const document::VectorObject* existing = nullptr;
        for (const auto& shape : project.vector_objects) {
            if (shape.source_region && *shape.source_region == ids[i]) {
                existing = &shape;
                break;
            }
        }
        if (existing != nullptr) {
            if (existing->paths == *results[i]) {
                ++out.keep;
            } else {
                out.replace.emplace_back(existing->id, *results[i]);
            }
        } else if (!results[i]->empty()) {
            document::VectorObject shape;
            shape.name = "Région " + std::to_string(ids[i].value);
            shape.source_region = ids[i];
            shape.rgb = project.segmentation->find(ids[i])->rgb;
            shape.paths = *results[i];
            out.add.push_back(std::move(shape));
        }
    }
    for (const auto& shape : project.vector_objects) {
        if (shape.source_region && project.segmentation->find(*shape.source_region) == nullptr) {
            ++out.orphan;
        }
    }
    return out;
}

} // namespace

VectorSpaceController::VectorSpaceController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    // StudioController::selectionChanged émet aussi changed : un seul refresh par clic.
    connect(&studio_, &StudioController::changed, this, &VectorSpaceController::refresh);
    refresh();
}

int VectorSpaceController::shapeCount() const {
    return static_cast<int>(studio_.session().project().vector_objects.size());
}

void VectorSpaceController::say(const QString& text) {
    message_ = text;
    emit messageChanged();
}

std::vector<std::uint64_t> VectorSpaceController::selectedShapeIds() const {
    const auto& project = studio_.session().project();
    std::vector<std::uint64_t> out;
    const auto add = [&out](std::uint64_t id) {
        if (id != 0 && std::ranges::find(out, id) == out.end()) {
            out.push_back(id);
        }
    };
    for (const auto& target : studio_.session().selection().targets()) {
        switch (target.kind) {
        case TargetKind::VectorObject:
            add(target.id);
            break;
        case TargetKind::EmbroideryObject:
            for (const auto& object : project.embroidery_objects) {
                if (object.id.value == target.id) {
                    add(object.source_vector.value);
                }
            }
            break;
        case TargetKind::Region:
            for (const auto& shape : project.vector_objects) {
                if (shape.source_region && shape.source_region->value == target.id) {
                    add(shape.id.value);
                }
            }
            break;
        }
    }
    return out;
}

void VectorSpaceController::refresh() {
    const auto& session = studio_.session();
    const auto selected = selectedShapeIds();
    bool changed = false;
    // Une retouche de segmentation ne modifie PAS les vecteurs déjà produits.
    // On garde le layer et son image raster, même si la révision du document avance.
    const bool newRevision = builtRevision_ != session.revision();
    const bool geometryDirty = newRevision && session.lastChangeAffectsEmbroidery();
    const bool rebuildBase = !layer_ || geometryDirty;
    if (rebuildBase) {
        layer_ = VectorLayer::build(session.project());
        ++builds_;
        changed = true;
        if (canvas_) {
            canvas_->setVectorLayer(std::shared_ptr<const OverlayImage>(layer_, &layer_->image()));
        }
    }
    if (rebuildBase || builtSelected_ != selected) {
        // Un clic ne doit pas redessiner le fond, seulement le ROI des formes retenues.
        selectionLayer_ = layer_->selectionOverlay(selected);
        builtSelected_ = selected;
        ++selectionBuilds_;
        changed = true;
        if (canvas_) {
            canvas_->setVectorSelectionLayer(selectionLayer_);
        }
    }
    builtRevision_ = session.revision();
    const auto statuses =
        computeStageStatus(session.project(), {.segmentationDirty = dirty_,
                                               .dirtyReason = dirtyReason_.toStdString()});
    const auto& vec = statuses[2];
    const QString state = QString::fromUtf8(stageStateName(vec.state));
    QString detail = QString::fromStdString(vec.detail);
    if (vec.state == StageState::UpToDate) {
        detail = tr("%1 forme(s) vectorielle(s)").arg(shapeCount());
    }
    if (state != stageState_ || detail != stageDetail_) {
        stageState_ = state;
        stageDetail_ = detail;
        changed = true;
    }
    if (changed) {
        emit layerChanged();
    }
}

void VectorSpaceController::attach(QObject* canvas) {
    if (canvas_ && canvas_ == qobject_cast<GpuCanvas*>(canvas)) {
        refresh();
        return; // le même Loader ne doit pas détruire et renvoyer sa texture
    }
    detach();
    // Actualiser d'abord hors canevas : si le calque doit être reconstruit, il ne sera
    // publié qu'une fois, une fois le nouveau canevas branché.
    refresh();
    canvas_ = qobject_cast<GpuCanvas*>(canvas);
    if (!canvas_) {
        return;
    }
    canvas_->setVectorPicker([this](double x, double y, double tolerance, bool toggle) {
        pickAt(x, y, tolerance, toggle);
    });
    if (layer_) {
        canvas_->setVectorLayer(std::shared_ptr<const OverlayImage>(layer_, &layer_->image()));
    }
    canvas_->setVectorSelectionLayer(selectionLayer_);
}

void VectorSpaceController::detach() {
    if (canvas_) {
        canvas_->setVectorPicker({});
        canvas_->setVectorLayer(nullptr);
        canvas_->setVectorSelectionLayer(nullptr);
    }
    canvas_.clear();
}

void VectorSpaceController::setSegmentationDirty(bool dirty, const QString& reason) {
    if (dirty_ == dirty && dirtyReason_ == reason) {
        return;
    }
    dirty_ = dirty;
    dirtyReason_ = reason;
    refresh();
}

QString VectorSpaceController::pickAt(double x, double y, double toleranceMm, bool toggle) {
    if (!layer_) {
        return {};
    }
    const auto hit = layer_->pick(x, y, toleranceMm);
    if (!hit) {
        if (!toggle) {
            studio_.session().selection().clear();
        }
        return {};
    }
    const auto id = QString::number(*hit);
    selectShape(id, toggle);
    return id;
}

void VectorSpaceController::selectShape(const QString& id, bool toggle) {
    const auto vectorId = id.toULongLong();
    auto& session = studio_.session();
    if (session.project().findObject(ObjectId{vectorId}) == nullptr) {
        return;
    }
    // Une forme déjà numérisée est sélectionnée par son objet de broderie : les outils de dessin,
    // d'édition et de nœuds (qui opèrent sur ces objets) agissent alors dessus.
    const auto* object = embroideryOf(session.project(), vectorId);
    const Target target = object ? Target{TargetKind::EmbroideryObject, object->id.value}
                                 : Target{TargetKind::VectorObject, vectorId};
    if (toggle) {
        session.selection().toggle(target);
    } else {
        session.selection().replace({target});
    }
}

QVariantMap VectorSpaceController::selectedShape() const {
    const auto ids = selectedShapeIds();
    if (ids.empty()) {
        return {};
    }
    const auto& project = studio_.session().project();
    const auto* shape = project.findObject(ObjectId{ids.back()});
    if (shape == nullptr) {
        return {};
    }
    std::size_t nodes = 0;
    for (const auto& set : shape->paths) {
        nodes += set.outer.nodes.size();
        for (const auto& hole : set.holes) {
            nodes += hole.nodes.size();
        }
    }
    const bool hasSource = shape->source_region.has_value();
    const auto regionId = hasSource ? shape->source_region->value : 0;
    const bool regionExists =
        hasSource && project.segmentation && project.segmentation->find(*shape->source_region);
    const auto* object = embroideryOf(project, ids.back());
    return {{QStringLiteral("id"), QString::number(shape->id.value)},
            {QStringLiteral("name"), QString::fromStdString(shape->name)},
            {QStringLiteral("hasSource"), hasSource},
            {QStringLiteral("regionId"), QString::number(regionId)},
            {QStringLiteral("regionExists"), regionExists},
            {QStringLiteral("nodes"), static_cast<int>(nodes)},
            {QStringLiteral("pieces"), static_cast<int>(shape->paths.size())},
            {QStringLiteral("embroidered"), object != nullptr},
            {QStringLiteral("count"), static_cast<int>(ids.size())}};
}

bool VectorSpaceController::selectOriginRegion() {
    const auto ids = selectedShapeIds();
    if (ids.empty()) {
        return false;
    }
    const auto* shape = studio_.session().project().findObject(ObjectId{ids.back()});
    if (shape == nullptr || !shape->source_region) {
        say(tr("Cette forme a été dessinée à la main : elle n'a pas de région d'origine."));
        return false;
    }
    const auto& project = studio_.session().project();
    if (!project.segmentation || project.segmentation->find(*shape->source_region) == nullptr) {
        say(tr("La région d'origine n'existe plus (fusionnée ou supprimée)."));
        return false;
    }
    studio_.session().selection().replace(
        {Target{TargetKind::Region, shape->source_region->value}});
    return true;
}

QVariantMap VectorSpaceController::updatePlan() const {
    const auto fresh = revectorize(studio_.session().project());
    return {{QStringLiteral("replace"), static_cast<int>(fresh.replace.size())},
            {QStringLiteral("add"), static_cast<int>(fresh.add.size())},
            {QStringLiteral("keep"), fresh.keep},
            {QStringLiteral("orphan"), fresh.orphan}};
}

bool VectorSpaceController::updateFromSegmentation() {
    auto& session = studio_.session();
    if (!session.project().segmentation) {
        say(tr("Aucune segmentation : rien à vectoriser."));
        return false;
    }
    auto fresh = revectorize(session.project());
    if (fresh.replace.empty() && fresh.add.empty()) {
        say(tr("Les formes suivent déjà la segmentation."));
        return false;
    }
    auto group = std::make_unique<commands::CompositeCommand>(
        "Mettre à jour les formes depuis la segmentation");
    for (auto& [id, paths] : fresh.replace) {
        group->add(std::make_unique<commands::SetVectorPathsCommand>(id, std::move(paths),
                                                                     "Mettre à jour une forme"));
    }
    for (auto& shape : fresh.add) {
        shape.id = session.allocateObjectId();
        group->add(std::make_unique<commands::AddVectorObjectCommand>(std::move(shape)));
    }
    const auto replaced = fresh.replace.size();
    const auto added = fresh.add.size();
    session.execute(std::move(group));
    say(tr("Formes mises à jour : %1 remplacée(s), %2 ajoutée(s).").arg(replaced).arg(added));
    return true;
}

bool VectorSpaceController::convertToEmbroidery() {
    const auto result = studio_.session().applyBatch(
        R"({"schema":"openstitch.batch/1","operations":[{"op":"convert_vectors"}]})",
        "Convertir les formes en broderie");
    if (!result.committed) {
        const auto& outcomes = result.report.outcomes;
        say(tr("Conversion refusée : %1")
                .arg(outcomes.empty() ? tr("aucun effet")
                                      : QString::fromStdString(outcomes.back().message)));
        return false;
    }
    say(tr("Formes converties en objets de broderie."));
    return true;
}

} // namespace openstitch::desktop_v2::studio
