// SPDX-License-Identifier: Apache-2.0
#include "vector_space_controller.hpp"

#include <algorithm>
#include <cmath>

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

Fresh revectorize(const document::Project& project, double simplifyMm, bool curves,
                  double curveTolerancePx) {
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
    const vectorization::VectorizeOptions options{
        .mm_per_px = project.mm_per_px,
        .simplify_tolerance = to_micrometers(Millimeters{simplifyMm}),
        .curve_tolerance = curves ? Micrometers{static_cast<std::int32_t>(std::lround(
                                        curveTolerancePx * project.mm_per_px.value * 1000.0))}
                                  : Micrometers{0},
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

// One key per vector in painter order. semantic_geometry_fingerprint is translation
// invariant, so include the absolute first node as well. The UI-only signature also
// captures every field consumed by VectorLayer::build (not stitch parameters).
std::vector<std::uint64_t> vectorVisualKeys(const document::Project& project) {
    std::vector<std::uint64_t> keys;
    keys.reserve(project.vector_objects.size());
    for (const auto& object : project.vector_objects) {
        std::uint64_t key = 14695981039346656037ULL;
        const auto mix = [&](std::uint64_t value) {
            for (unsigned byte = 0; byte < 8; ++byte) {
                key ^= (value >> (byte * 8U)) & 0xffU;
                key *= 1099511628211ULL;
            }
        };
        mix(object.id.value);
        mix(object.visible ? 1 : 0);
        mix(object.locked ? 1 : 0);
        mix(object.source_region ? object.source_region->value : 0);
        for (const auto color : object.rgb) {
            mix(color);
        }
        mix(document::semantic_geometry_fingerprint(object));
        if (!object.paths.empty() && !object.paths.front().outer.nodes.empty()) {
            const auto origin = object.paths.front().outer.nodes.front().pos;
            mix(static_cast<std::uint64_t>(origin.x.value));
            mix(static_cast<std::uint64_t>(origin.y.value));
        }
        keys.push_back(key);
    }
    return keys;
}

} // namespace

VectorSpaceController::VectorSpaceController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    // La sélection actualise l'inspecteur, sans rasteriser un calque de poignées
    // redondant avec l'overlay interactif de ToolController.
    connect(&studio_, &StudioController::selectionChanged, this,
            &VectorSpaceController::layerChanged);
    connect(&studio_, &StudioController::changed, this, &VectorSpaceController::refresh);
    pump_.setInterval(8);
    connect(&pump_, &QTimer::timeout, this, [this] { tasks_.pump(); });
    pump_.start();
    refresh();
}

VectorSpaceController::~VectorSpaceController() {
    pump_.stop();
    tasks_.cancelAll();
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

namespace {
constexpr std::size_t kLayerCacheEntries = 4;
constexpr qint64 kLayerCacheBytes = 96LL * 1024 * 1024; // plafond de mémoire des images conservées
} // namespace

std::shared_ptr<const VectorLayer>
VectorSpaceController::cachedLayer(const std::vector<std::uint64_t>& keys) {
    const auto it =
        std::ranges::find_if(layerCache_, [&](const CachedLayer& e) { return e.keys == keys; });
    if (it == layerCache_.end()) {
        return nullptr;
    }
    std::rotate(layerCache_.begin(), it, it + 1); // le plus récemment utilisé en tête
    return layerCache_.front().layer;
}

void VectorSpaceController::rememberLayer(const std::vector<std::uint64_t>& keys,
                                          const std::shared_ptr<const VectorLayer>& layer) {
    if (!layer) {
        return;
    }
    std::erase_if(layerCache_, [&](const CachedLayer& e) { return e.keys == keys; });
    layerCache_.insert(layerCache_.begin(), CachedLayer{keys, layer});
    qint64 bytes = 0;
    std::size_t kept = 0;
    for (const auto& entry : layerCache_) {
        bytes += entry.layer->image().image.sizeInBytes();
        if (kept >= kLayerCacheEntries || (kept > 0 && bytes > kLayerCacheBytes)) {
            break;
        }
        ++kept;
    }
    layerCache_.resize(std::max<std::size_t>(kept, 1));
}

void VectorSpaceController::publishLayer(std::shared_ptr<const VectorLayer> layer,
                                         std::vector<std::uint64_t> keys) {
    rememberLayer(keys, layer);
    layer_ = std::move(layer);
    renderedVisualKeys_ = std::move(keys);
    ++builds_;
    if (canvas_) {
        canvas_->setVectorLayer(std::shared_ptr<const OverlayImage>(layer_, &layer_->image()));
    }
}

void VectorSpaceController::startAsyncBuild(std::vector<std::uint64_t> keys) {
    tasks_.cancelAll();
    const auto request = ++buildRequest_;
    pending_ = true;
    pendingVisualKeys_ = keys;
    // Copie immuable des seules formes : le worker ne touche jamais au document vivant.
    auto shapes = std::make_shared<const std::vector<document::VectorObject>>(
        studio_.session().project().vector_objects);
    tasks_.submit<std::shared_ptr<const VectorLayer>>(
        "studio.vector-layer",
        [shapes](const TaskContext& task) -> Result<std::shared_ptr<const VectorLayer>> {
            if (task.cancelled()) {
                return cancelled_error();
            }
            return VectorLayer::build(*shapes);
        },
        [this, request,
         keys = std::move(keys)](Result<std::shared_ptr<const VectorLayer>>&& result) mutable {
            if (request != buildRequest_) {
                return; // un état plus récent a été demandé, ou l'état affiché est redevenu courant
            }
            pending_ = false;
            if (!result) {
                emit layerChanged(); // annulée : l'état affiché reste le dernier publié
                return;
            }
            publishLayer(std::move(*result), std::move(keys));
            emit layerChanged();
        });
}

void VectorSpaceController::refresh() {
    const auto& session = studio_.session();
    bool changed = false;
    // Une modification des points, du tissu ou de la segmentation peut avancer la
    // révision sans changer les formes. Comparer seulement leurs propriétés visuelles,
    // avant d'allouer une grande QImage ou de republier la texture GPU.
    const bool newRevision = builtRevision_ != session.revision();
    // Segmentation/image-only commands promise not to change any vector: avoid even
    // scanning their nodes during a rapid succession of brush edits.
    const bool mayChangeVectors =
        session.lastChangeAffectsEmbroidery() || !session.lastChangeAffectsSegmentation();
    std::vector<std::uint64_t> nextVisualKeys;
    const bool haveBase = layer_ != nullptr || pending_;
    const bool keysNeeded = !haveBase || (newRevision && mayChangeVectors);
    if (keysNeeded) {
        nextVisualKeys = vectorVisualKeys(session.project());
        const bool differsFromRendered = !layer_ || nextVisualKeys != renderedVisualKeys_;
        const bool alreadyBuilding = pending_ && nextVisualKeys == pendingVisualKeys_;
        if (alreadyBuilding) {
            // Ce même état est déjà en cours de rasterisation : ne pas relancer.
        } else if (differsFromRendered) {
            if (const auto known = cachedLayer(nextVisualKeys)) {
                // État déjà rasterisé (masquer puis ré-afficher, annuler) : republié tel quel.
                tasks_.cancelAll();
                ++buildRequest_;
                pending_ = false;
                ++cacheHits_;
                publishLayer(known, std::move(nextVisualKeys));
            } else if (session.project().vector_objects.size() <= kSyncShapeLimit) {
                // Petite scène : rasterisation synchrone (quelques ms), comme avant. Toute
                // rasterisation asynchrone devenue inutile est abandonnée.
                tasks_.cancelAll();
                ++buildRequest_;
                pending_ = false;
                publishLayer(VectorLayer::build(session.project()), std::move(nextVisualKeys));
            } else {
                startAsyncBuild(std::move(nextVisualKeys));
            }
            changed = true;
        } else if (pending_) {
            // Le document est revenu à l'état déjà affiché (annulation) : la rasterisation en
            // cours produirait un calque périmé, on l'abandonne.
            tasks_.cancelAll();
            ++buildRequest_;
            pending_ = false;
            changed = true;
        }
    }
    builtRevision_ = session.revision();
    // Le statut du workflow dépend du document et de l'état de segmentation,
    // jamais d'un clic de sélection ni de la fin d'un calcul de scène.
    if (statusRevision_ != session.revision()) {
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
        statusRevision_ = session.revision();
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
}

void VectorSpaceController::detach() {
    if (canvas_) {
        canvas_->setVectorPicker({});
        canvas_->setVectorLayer(nullptr);
    }
    canvas_.clear();
}

void VectorSpaceController::setSegmentationDirty(bool dirty, const QString& reason) {
    if (dirty_ == dirty && dirtyReason_ == reason) {
        return;
    }
    dirty_ = dirty;
    dirtyReason_ = reason;
    statusRevision_ = 0; // l'état de segmentation n'incrémente pas toujours la révision
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

void VectorSpaceController::setPrecisionMm(double mm) {
    const double clamped = std::clamp(mm, 0.02, 2.0);
    if (clamped != precisionMm_) {
        precisionMm_ = clamped;
        emit optionsChanged();
    }
}

void VectorSpaceController::setCurves(bool on) {
    if (on != curves_) {
        curves_ = on;
        emit optionsChanged();
    }
}

void VectorSpaceController::setCurveTolerancePx(double px) {
    const double clamped = std::clamp(px, 0.5, 10.0);
    if (clamped != curveTolerancePx_) {
        curveTolerancePx_ = clamped;
        emit optionsChanged();
    }
}

QVariantMap VectorSpaceController::updatePlan() const {
    const auto fresh =
        revectorize(studio_.session().project(), precisionMm_, curves_, curveTolerancePx_);
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
    auto fresh = revectorize(session.project(), precisionMm_, curves_, curveTolerancePx_);
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
