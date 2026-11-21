// SPDX-License-Identifier: Apache-2.0
#include "vector_space_controller.hpp"

#include "task_pump.hpp"

#include "perf_log.hpp"

#include <QElapsedTimer>
#include <algorithm>
#include <cmath>

#include <algorithm>

#include "gpu_canvas.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/autodigitize/vector_convert.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/project_ops/revectorize.hpp"
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

// Revectorisation partagée avec l'opération `vectorize_segmentation` (project_ops).
project_ops::RevectorizePlan revectorize(const document::Project& project, double simplifyMm,
                                         bool curves, double curveTolerancePx) {
    return project_ops::revectorize(
        project,
        {.precision_mm = simplifyMm, .curves = curves, .curve_tolerance_px = curveTolerancePx});
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
    attachTaskPump(tasks_, this);
    attachTaskPump(convertTasks_, this);
    refresh();
}

VectorSpaceController::~VectorSpaceController() {
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
                if (project.fromActiveImage(shape) && shape.source_region->value == target.id) {
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
        bytes += entry.layer->bytes();
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
    if (perfLogEnabled()) {
        perfLog(QStringLiteral("vector.layer"),
                {{QStringLiteral("shapes"),
                  static_cast<qulonglong>(studio_.session().project().vector_objects.size())},
                 {QStringLiteral("builds"), builds_ + 1},
                 {QStringLiteral("cacheHits"), cacheHits_}});
    }
    layer_ = std::move(layer);
    renderedVisualKeys_ = std::move(keys);
    ++builds_;
    if (canvas_) {
        canvas_->setVectorLayer(layer_);
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
    // Le GPU dessine les triangles ; seul le backend logiciel (ou un canevas pas encore affiché)
    // a besoin de l'image.
    const bool software = !canvas_ || !canvas_->rhiBackend();
    tasks_.submit<std::shared_ptr<const VectorLayer>>(
        "studio.vector-layer",
        [shapes, software, workerKeys = pendingVisualKeys_,
         previous = layer_](const TaskContext& task) -> Result<std::shared_ptr<const VectorLayer>> {
            if (task.cancelled()) {
                return cancelled_error();
            }
            // Incrémental : seules les formes modifiées sont retriangulées (un déplacement ne
            // retriangule plus les centaines de formes du motif).
            auto layer = VectorLayer::build(*shapes, workerKeys, previous.get());
            if (software) {
                (void)layer->image(); // backend logiciel : rastériser ici, pas pendant la synchro
            }
            return layer;
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
    canvas_->setVectorPicker(
        [this](double x, double y, double tolerance, bool toggle, bool keepSelected) {
            return pickAt(x, y, tolerance, toggle, keepSelected);
        });
    if (layer_) {
        canvas_->setVectorLayer(layer_);
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

QString VectorSpaceController::pickAt(double x, double y, double toleranceMm, bool toggle,
                                      bool keepSelected) {
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
    if (keepSelected) {
        const auto* object = embroideryOf(studio_.session().project(), *hit);
        const Target target = object ? Target{TargetKind::EmbroideryObject, object->id.value}
                                     : Target{TargetKind::VectorObject, *hit};
        if (studio_.session().selection().contains(target)) {
            return id; // menu contextuel sur la sélection en cours
        }
    }
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
    const auto* source = project.findImageSource(shape->source_image);
    const bool regionExists = hasSource && source != nullptr && source->segmentation &&
                              source->segmentation->find(*shape->source_region) != nullptr;
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
    const auto* source = project.findImageSource(shape->source_image);
    if (source == nullptr || !source->segmentation ||
        !source->segmentation->find(*shape->source_region)) {
        say(tr("La région d'origine n'existe plus (fusionnée ou supprimée)."));
        return false;
    }
    studio_.session().selectImageSource(shape->source_image);
    studio_.session().selection().replace(
        {Target{TargetKind::Region, shape->source_region->value}});
    return true;
}

void VectorSpaceController::setPrecisionMm(double mm) {
    const double clamped =
        std::clamp(mm, project_ops::kPrecisionMinMm, project_ops::kPrecisionMaxMm);
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
    const double clamped =
        std::clamp(px, project_ops::kCurveToleranceMinPx, project_ops::kCurveToleranceMaxPx);
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
    if (!session.project().imageSource().segmentation) {
        say(tr("Aucune segmentation : rien à vectoriser."));
        return false;
    }
    auto fresh = revectorize(session.project(), precisionMm_, curves_, curveTolerancePx_);
    if (fresh.replace.empty() && fresh.add.empty() && fresh.absorbed.empty()) {
        say(tr("Les formes suivent déjà la segmentation."));
        return false;
    }
    auto group = std::make_unique<commands::CompositeCommand>(
        "Mettre à jour les formes depuis la segmentation");
    for (auto& [id, paths] : fresh.replace) {
        group->add(std::make_unique<commands::SetVectorPathsCommand>(id, std::move(paths),
                                                                     "Mettre à jour une forme"));
    }
    project_ops::append_absorbed_removals(session.project(), fresh, *group);
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

namespace {
std::vector<ObjectId> segmentedSources(const document::Project& project) {
    std::vector<ObjectId> out;
    if (!project.original.empty() && project.segmentation) {
        out.push_back(ObjectId{}); // image d'origine
    }
    for (const auto& entry : project.image_sources) {
        if (entry.source.segmentation) {
            out.push_back(entry.calque);
        }
    }
    return out;
}
} // namespace

int VectorSpaceController::segmentedLayerCount() const {
    return static_cast<int>(segmentedSources(studio_.session().project()).size());
}

bool VectorSpaceController::updateAllLayersFromSegmentation() {
    auto& session = studio_.session();
    const auto sources = segmentedSources(session.project());
    if (sources.empty()) {
        say(tr("Aucun calque segmenté : rien à vectoriser."));
        return false;
    }
    auto group = std::make_unique<commands::CompositeCommand>(
        "Mettre à jour les formes de tous les calques");
    std::size_t replaced = 0;
    std::size_t added = 0;
    for (const auto source : sources) {
        auto fresh =
            project_ops::revectorize(session.project(), {.precision_mm = precisionMm_,
                                                         .curves = curves_,
                                                         .curve_tolerance_px = curveTolerancePx_,
                                                         .image_source = source});
        replaced += fresh.replace.size() + fresh.absorbed.size();
        added += fresh.add.size();
        project_ops::append_absorbed_removals(session.project(), fresh, *group);
        for (auto& [id, paths] : fresh.replace) {
            group->add(std::make_unique<commands::SetVectorPathsCommand>(
                id, std::move(paths), "Mettre à jour une forme"));
        }
        for (auto& shape : fresh.add) {
            shape.id = session.allocateObjectId();
            group->add(std::make_unique<commands::AddVectorObjectCommand>(std::move(shape)));
        }
    }
    if (replaced + added == 0) {
        say(tr("Les formes de tous les calques suivent déjà la segmentation."));
        return false;
    }
    session.execute(std::move(group));
    say(tr("%1 calque(s) : %2 forme(s) remplacée(s), %3 ajoutée(s).")
            .arg(sources.size())
            .arg(replaced)
            .arg(added));
    return true;
}

bool VectorSpaceController::convertToEmbroidery(const QVariantMap& conversion) {
    if (converting_) {
        return false;
    }
    const bool autoPull = conversion.value("pull", "auto").toString() != "none";
    const double overlapMm = std::clamp(conversion.value("overlapMm", 0.5).toDouble(), 0.0, 2.0);
    const double insetMm = std::clamp(conversion.value("insetMm", 0.2).toDouble(), 0.0, 2.0);
    const bool satin = conversion.value("satin", false).toBool();
    // 0 : on garde l'espacement par défaut des paramètres créés.
    const double spacingMm = std::clamp(conversion.value("spacingMm", 0.0).toDouble(), 0.0, 2.0);
    // La classification de chaque forme (tatami, contour…) est longue sur un gros motif : elle
    // tourne sur un instantané, hors du fil d'interface ; seul l'ajout du lot (une commande
    // annulable) se fait ici, et seulement si le document n'a pas changé entre-temps.
    auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    converting_ = true;
    emit convertingChanged();
    say(tr("Conversion en broderie…"));
    convertTasks_.submit<autodigitize::VectorConversion>(
        "studio.convert-vectors",
        [snapshot, autoPull, insetMm,
         satin](const TaskContext& task) -> Result<autodigitize::VectorConversion> {
            if (task.cancelled()) {
                return cancelled_error();
            }
            autodigitize::AutoOptions options;
            options.mm_per_px = snapshot->imageSource().mm_per_px;
            options.auto_pull_compensation = autoPull;
            options.fill_inset = to_micrometers(Millimeters{insetMm});
            options.satin_bands = satin;
            auto ids = snapshot->object_ids; // copie : les vrais identifiants sont pris au retour
            return autodigitize::convert_vectors_to_embroidery(*snapshot, ids, options);
        },
        [this, revision, overlapMm, spacingMm](Result<autodigitize::VectorConversion>&& result) {
            converting_ = false;
            emit convertingChanged();
            if (!result) {
                say(tr("Conversion refusée : %1")
                        .arg(QString::fromStdString(result.error().message)));
                return;
            }
            auto& session = studio_.session();
            if (session.revision() != revision) {
                say(tr("Le document a changé pendant la conversion : relancez-la."));
                return;
            }
            if (result->embroideries.empty()) {
                say(tr("Conversion refusée : aucun objet de broderie produit"));
                return;
            }
            const auto spacing = to_micrometers(Millimeters{spacingMm});
            for (auto& embroidery : result->embroideries) {
                embroidery.id = session.allocateObjectId();
                if (spacingMm > 0.0) {
                    if (auto* tatami = std::get_if<document::TatamiParams>(&embroidery.params)) {
                        tatami->row_spacing = spacing;
                    } else if (auto* satin =
                                   std::get_if<document::AutoSatinParams>(&embroidery.params)) {
                        satin->spacing = spacing;
                    }
                }
            }
            const auto count = result->embroideries.size();
            // Un seul pas d'annulation pour tout le lot : l'ajout des broderies et le débord sous
            // les voisins (ci-dessous) sont simulés sur une copie avant d'être rejoués une seule
            // fois pour de vrai, dans le même `CompositeCommand`.
            auto batch =
                std::make_unique<commands::CompositeCommand>("Numériser les formes vectorielles");
            auto addCmd = std::make_unique<commands::AddObjectBatchCommand>(
                std::vector<document::VectorObject>{}, std::move(result->embroideries),
                "Numériser les formes vectorielles");
            // Sans débord demandé, inutile de copier le projet ni de planifier (travail sur le
            // fil d'interface) : on ajoute simplement le lot.
            if (overlapMm <= 0.0) {
                batch->add(std::move(addCmd));
                session.execute(std::move(batch));
                say(tr("%1 forme(s) converties en objets de broderie.").arg(count));
                return;
            }
            document::Project scratch = session.project();
            addCmd->apply(scratch);
            batch->add(std::move(addCmd));
            // Sans débord, contours tracés par les centres des pixels + retrait du tatami
            // laissent un liseré de tissu nu à chaque frontière (même règle que l'op
            // `overlap_fills` : op_convert.cpp, project_ops).
            // `overlapMm` = 0 : aucune modification des contours (plan vide).
            auto overlapChanges =
                autodigitize::plan_fill_overlap(scratch, to_micrometers(Millimeters{overlapMm}));
            for (auto& change : overlapChanges) {
                const auto* emb = scratch.findEmbroidery(change.embroidery);
                auto params = emb->params;
                std::get<document::TatamiParams>(params).inset = Micrometers{0};
                batch->add(std::make_unique<commands::SetVectorPathsCommand>(
                    change.vector, std::move(change.paths), "Élargir sous les voisins"));
                batch->add(std::make_unique<commands::SetStitchParamsCommand>(
                    change.embroidery, std::move(params), "Retrait"));
            }
            session.execute(std::move(batch));
            say(tr("%1 forme(s) converties en objets de broderie.").arg(count));
        });
    return true;
}

} // namespace openstitch::desktop_v2::studio
