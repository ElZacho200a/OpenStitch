// SPDX-License-Identifier: Apache-2.0
#include "edit_controller.hpp"

#include <QColor>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>

#include "arrange_batch.hpp"
#include "document_services.hpp"
#include "edit_batch.hpp"
#include "inspector_model.hpp"
#include "object_labels.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "transform_batch.hpp"

namespace openstitch::desktop_v2::studio {
namespace {

using stitch_render::RectMm;

std::optional<std::uint64_t> parseId(const QString& text) {
    bool ok = false;
    const auto value = text.toULongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return value;
}

SnapBox toModelBox(const RectMm& scene) {
    return {scene.x0, -scene.y1, scene.x1, -scene.y0};
}

bool touches(const RectMm& a, const RectMm& b, double tolerance) {
    return a.x0 <= b.x1 + tolerance && b.x0 <= a.x1 + tolerance && a.y0 <= b.y1 + tolerance &&
           b.y0 <= a.y1 + tolerance;
}

} // namespace

EditController::EditController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    connect(&studio_, &StudioController::changed, this, &EditController::treeChanged);
    connect(&studio_, &StudioController::sceneChanged, this, &EditController::treeChanged);
    connect(&studio_, &StudioController::documentReplaced, this, [this] {
        saved_.clear(); // les identifiants d'un autre document n'ont aucun sens ici
        move_.reset();
        emit savedSelectionsChanged();
    });
}

EditController::~EditController() = default;

void EditController::setSnapEnabled(bool value) {
    if (snap_ != value) {
        snap_ = value;
        emit snapChanged();
    }
}

void EditController::setEyedropper(bool value) {
    if (eyedropper_ != value) {
        eyedropper_ = value;
        say(value ? QStringLiteral("Pipette : cliquez sur un objet pour copier sa couleur")
                  : QString{});
        emit eyedropperChanged();
    }
}

void EditController::say(const QString& text) {
    message_ = text;
    emit messageChanged();
}

bool EditController::refuse(const QString& why) {
    say(why);
    return false;
}

std::vector<std::uint64_t> EditController::selectedIds() const {
    std::vector<std::uint64_t> ids;
    const auto& project = studio_.session().project();
    for (const auto& target : studio_.session().selection().targets()) {
        if (target.kind != TargetKind::EmbroideryObject) {
            continue;
        }
        // Un objet dont le calque est verrouillé reste intouchable au canevas.
        const auto* object = project.findEmbroidery(ObjectId{target.id});
        if (object && !project.shapeLockedByCalque(object->source_vector)) {
            ids.push_back(target.id);
        }
    }
    return ids;
}

int EditController::selectedCount() const {
    int count = 0;
    for (const auto& target : studio_.session().selection().targets()) {
        count += target.kind == TargetKind::EmbroideryObject ? 1 : 0;
    }
    return count;
}

bool EditController::visibleObject(std::uint64_t id) const {
    const auto& project = studio_.session().project();
    const auto* object = project.findEmbroidery(ObjectId{id});
    return object && object->visible && project.calqueVisible(*object);
}

bool EditController::selectIds(const std::vector<std::uint64_t>& ids, bool additive) {
    std::vector<Target> targets;
    if (additive) {
        targets = studio_.session().selection().targets();
    }
    for (const auto id : ids) {
        const Target target{TargetKind::EmbroideryObject, id};
        if (std::ranges::find(targets, target) == targets.end()) {
            targets.push_back(target);
        }
    }
    studio_.session().selection().replace(std::move(targets));
    return !ids.empty();
}

std::optional<RectMm> EditController::selectionBounds() const {
    const auto& scene = studio_.scene();
    if (!scene) {
        return std::nullopt;
    }
    std::optional<RectMm> box;
    for (const auto& target : studio_.session().selection().targets()) {
        const auto found = scene->objectBounds.find(target.id);
        if (target.kind != TargetKind::EmbroideryObject || found == scene->objectBounds.end()) {
            continue;
        }
        const auto& r = found->second;
        if (!box) {
            box = r;
        } else {
            box->x0 = std::min(box->x0, r.x0);
            box->y0 = std::min(box->y0, r.y0);
            box->x1 = std::max(box->x1, r.x1);
            box->y1 = std::max(box->y1, r.y1);
        }
    }
    return box;
}

QVariantMap EditController::selectionBox() const {
    const auto box = selectionBounds();
    if (!box) {
        return {};
    }
    return {{QStringLiteral("x"), box->x0},
            {QStringLiteral("y"), box->y0},
            {QStringLiteral("width"), box->width()},
            {QStringLiteral("height"), box->height()},
            {QStringLiteral("cx"), (box->x0 + box->x1) / 2.0},
            {QStringLiteral("cy"), (box->y0 + box->y1) / 2.0}};
}

PointMm EditController::selectionCentreModel() const {
    const auto box = selectionBounds();
    if (!box) {
        return {};
    }
    return {(box->x0 + box->x1) / 2.0, -(box->y0 + box->y1) / 2.0};
}

// --- Sélection ----------------------------------------------------------------------------------

int EditController::selectAll() {
    std::vector<std::uint64_t> ids;
    for (const auto& object : studio_.session().project().embroidery_objects) {
        if (visibleObject(object.id.value)) {
            ids.push_back(object.id.value);
        }
    }
    if (ids.empty()) {
        return refuse(QStringLiteral("Aucun objet à sélectionner")), 0;
    }
    (void)selectIds(ids);
    say(QStringLiteral("%1 objet(s) sélectionné(s)").arg(ids.size()));
    return static_cast<int>(ids.size());
}

void EditController::selectNone() {
    studio_.clearSelection();
}

int EditController::invertSelection() {
    std::vector<std::uint64_t> ids;
    for (const auto& object : studio_.session().project().embroidery_objects) {
        const Target target{TargetKind::EmbroideryObject, object.id.value};
        if (visibleObject(object.id.value) && !studio_.session().selection().contains(target)) {
            ids.push_back(object.id.value);
        }
    }
    (void)selectIds(ids);
    say(QStringLiteral("Sélection inversée : %1 objet(s)").arg(ids.size()));
    return static_cast<int>(ids.size());
}

int EditController::selectSameColor() {
    const auto& project = studio_.session().project();
    std::set<std::array<std::uint8_t, 3>> colours;
    for (const auto& target : studio_.session().selection().targets()) {
        if (const auto* object = project.findEmbroidery(ObjectId{target.id})) {
            colours.insert(object->rgb);
        }
    }
    if (colours.empty()) {
        return refuse(QStringLiteral("Sélectionnez d'abord un objet")), 0;
    }
    std::vector<std::uint64_t> ids;
    for (const auto& object : project.embroidery_objects) {
        if (colours.contains(object.rgb) && visibleObject(object.id.value)) {
            ids.push_back(object.id.value);
        }
    }
    (void)selectIds(ids);
    say(QStringLiteral("%1 objet(s) de même couleur").arg(ids.size()));
    return static_cast<int>(ids.size());
}

int EditController::selectNeighbors() {
    const auto& scene = studio_.scene();
    if (!scene || studio_.session().selection().empty()) {
        return refuse(QStringLiteral("Sélectionnez d'abord un objet")), 0;
    }
    std::vector<std::uint64_t> ids;
    for (const auto& [id, box] : scene->objectBounds) {
        if (!visibleObject(id)) {
            continue;
        }
        const Target self{TargetKind::EmbroideryObject, id};
        bool next = studio_.session().selection().contains(self);
        for (const auto& target : studio_.session().selection().targets()) {
            const auto found = scene->objectBounds.find(target.id);
            if (!next && found != scene->objectBounds.end() && touches(box, found->second, 0.5)) {
                next = true;
            }
        }
        if (next) {
            ids.push_back(id);
        }
    }
    (void)selectIds(ids);
    say(QStringLiteral("%1 objet(s) avec les voisines").arg(ids.size()));
    return static_cast<int>(ids.size());
}

int EditController::selectByType(const QString& typeId) {
    std::vector<std::uint64_t> ids;
    for (const auto& object : studio_.session().project().embroidery_objects) {
        const auto type = stitchTypeId(object);
        if (QString::fromUtf8(type.data(), static_cast<qsizetype>(type.size())) == typeId &&
            visibleObject(object.id.value)) {
            ids.push_back(object.id.value);
        }
    }
    if (ids.empty()) {
        return refuse(QStringLiteral("Aucun objet de ce type")), 0;
    }
    (void)selectIds(ids);
    say(QStringLiteral("%1 objet(s) de type %2").arg(ids.size()).arg(typeId));
    return static_cast<int>(ids.size());
}

int EditController::selectByLayer(const QString& calqueId) {
    const auto wanted = parseId(calqueId);
    const auto& project = studio_.session().project();
    if (!wanted) {
        return 0;
    }
    std::vector<std::uint64_t> ids;
    for (const auto& object : project.embroidery_objects) {
        const auto* calque = project.calqueOfShape(object.source_vector);
        if (calque && calque->id.value == *wanted && object.visible) {
            ids.push_back(object.id.value);
        }
    }
    (void)selectIds(ids);
    say(QStringLiteral("%1 objet(s) dans le calque").arg(ids.size()));
    return static_cast<int>(ids.size());
}

int EditController::selectGroup(const QString& groupId) {
    const auto wanted = parseId(groupId);
    const auto& project = studio_.session().project();
    if (!wanted) {
        return 0;
    }
    std::vector<std::uint64_t> ids;
    for (const auto& object : project.embroidery_objects) {
        const auto* shape = project.findObject(object.source_vector);
        if (shape && project.groupIsWithin(shape->group, ObjectId{*wanted})) {
            ids.push_back(object.id.value);
        }
    }
    (void)selectIds(ids);
    return static_cast<int>(ids.size());
}

int EditController::selectRect(double x0, double y0, double x1, double y1, bool crossing,
                               bool additive) {
    const auto& scene = studio_.scene();
    if (!scene) {
        return 0;
    }
    const RectMm rect{std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)};
    std::vector<std::uint64_t> ids;
    for (const auto& [id, box] : scene->objectBounds) { // std::map : ordre croissant, déterministe
        if (!visibleObject(id)) {
            continue;
        }
        const bool inside =
            box.x0 >= rect.x0 && box.x1 <= rect.x1 && box.y0 >= rect.y0 && box.y1 <= rect.y1;
        if (crossing ? touches(box, rect, 0.0) : inside) {
            ids.push_back(id);
        }
    }
    if (ids.empty() && !additive) {
        studio_.clearSelection();
        return 0;
    }
    (void)selectIds(ids, additive);
    return static_cast<int>(ids.size());
}

bool EditController::saveSelection(const QString& name) {
    const auto ids = selectedIds();
    const QString trimmed = name.trimmed();
    if (ids.empty() || trimmed.isEmpty()) {
        return refuse(QStringLiteral("Sélectionnez des objets et donnez un nom"));
    }
    for (auto& entry : saved_) {
        if (entry.first == trimmed) {
            entry.second = ids;
            say(QStringLiteral("Sélection « %1 » mise à jour").arg(trimmed));
            emit savedSelectionsChanged();
            return true;
        }
    }
    saved_.emplace_back(trimmed, ids);
    say(QStringLiteral("Sélection « %1 » mémorisée").arg(trimmed));
    emit savedSelectionsChanged();
    return true;
}

bool EditController::recallSelection(const QString& name) {
    for (const auto& entry : saved_) {
        if (entry.first != name) {
            continue;
        }
        std::vector<std::uint64_t> alive;
        for (const auto id : entry.second) {
            if (studio_.session().exists({TargetKind::EmbroideryObject, id})) {
                alive.push_back(id);
            }
        }
        if (alive.empty()) {
            return refuse(QStringLiteral("Les objets de cette sélection n'existent plus"));
        }
        (void)selectIds(alive);
        say(QStringLiteral("Sélection « %1 » rappelée").arg(name));
        return true;
    }
    return refuse(QStringLiteral("Sélection inconnue"));
}

bool EditController::deleteSavedSelection(const QString& name) {
    const auto removed = std::erase_if(saved_, [&](const auto& e) { return e.first == name; });
    if (removed != 0) {
        emit savedSelectionsChanged();
    }
    return removed != 0;
}

QStringList EditController::savedSelections() const {
    QStringList names;
    for (const auto& entry : saved_) {
        names.push_back(entry.first);
    }
    return names;
}

QStringList EditController::stitchTypes() const {
    std::set<std::string> types;
    for (const auto& object : studio_.session().project().embroidery_objects) {
        types.emplace(stitchTypeId(object));
    }
    QStringList list;
    for (const auto& type : types) {
        list.push_back(QString::fromStdString(type));
    }
    return list;
}

// --- Transformations ----------------------------------------------------------------------------

bool EditController::applyBatch(const std::string& batch, const QString& label,
                                const QString& done) {
    if (batch.empty()) {
        return refuse(
            QStringLiteral("%1 : rien à faire (sélection ou valeur insuffisante)").arg(label));
    }
    const auto result = studio_.session().applyBatch(batch, label.toStdString());
    if (!result.committed) {
        return refuse(QStringLiteral("%1 refusé").arg(label));
    }
    say(done.isEmpty() ? label : done);
    return true;
}

std::string EditController::mergeBatches(const std::vector<std::string>& batches) {
    std::string ops;
    for (const auto& batch : batches) {
        const auto open = batch.find("\"operations\":[");
        const auto close = batch.rfind(']');
        if (open == std::string::npos || close == std::string::npos) {
            continue;
        }
        const auto begin = open + std::string("\"operations\":[").size();
        if (close > begin) {
            ops += (ops.empty() ? "" : ",") + batch.substr(begin, close - begin);
        }
    }
    return ops.empty() ? std::string{}
                       : R"({"schema":"openstitch.batch/1","operations":[)" + ops + "]}";
}

bool EditController::rotate(double degrees) {
    return applyBatch(batchRotate(selectedIds(), degrees, selectionCentreModel()),
                      QStringLiteral("Pivoter"), {});
}

bool EditController::rotate90(bool clockwise) {
    return applyBatch(batchRotate(selectedIds(), clockwise ? -90.0 : 90.0, selectionCentreModel()),
                      QStringLiteral("Pivoter de 90°"), {});
}

bool EditController::mirror(bool horizontal) {
    return applyBatch(
        batchMirror(selectedIds(), horizontal ? MirrorAxis::Horizontal : MirrorAxis::Vertical,
                    selectionCentreModel()),
        horizontal ? QStringLiteral("Miroir horizontal") : QStringLiteral("Miroir vertical"), {});
}

bool EditController::scale(double factor, double anchorXmm, double anchorYmm) {
    return applyBatch(batchScale(selectedIds(), factor, {anchorXmm, -anchorYmm}),
                      QStringLiteral("Redimensionner"), {});
}

bool EditController::duplicate(double offsetMm) {
    const auto ids = selectedIds();
    const auto batch = batchDuplicate(ids, {offsetMm, -offsetMm});
    if (batch.empty()) {
        return refuse(QStringLiteral("Dupliquer : sélection vide"));
    }
    const auto result = studio_.session().applyBatch(batch, "Dupliquer");
    if (!result.committed) {
        return refuse(QStringLiteral("Duplication refusée"));
    }
    (void)selectIds(createdObjectIds(result.report)); // les copies deviennent la sélection
    say(QStringLiteral("%1 objet(s) dupliqué(s)").arg(ids.size()));
    return true;
}

bool EditController::deleteSelection() {
    const auto ids = selectedIds();
    return applyBatch(batchRemove(ids), QStringLiteral("Supprimer"),
                      QStringLiteral("%1 objet(s) supprimé(s)").arg(ids.size()));
}

bool EditController::align(const QString& mode) {
    static const std::pair<const char*, AlignMode> modes[] = {
        {"left", AlignMode::Left}, {"centerH", AlignMode::CenterH}, {"right", AlignMode::Right},
        {"top", AlignMode::Top},   {"centerV", AlignMode::CenterV}, {"bottom", AlignMode::Bottom}};
    for (const auto& [name, value] : modes) {
        if (mode == QLatin1String(name)) {
            return applyBatch(batchAlign(studio_.session().project(), selectedIds(), value),
                              QStringLiteral("Aligner"), {});
        }
    }
    return refuse(QStringLiteral("Alignement inconnu"));
}

bool EditController::distribute(bool horizontal) {
    return applyBatch(
        batchDistribute(studio_.session().project(), selectedIds(),
                        horizontal ? DistributeAxis::Horizontal : DistributeAxis::Vertical),
        QStringLiteral("Répartir"), {});
}

bool EditController::placeSelection(double xMm, double yMm, double widthMm, double heightMm,
                                    double rotateDeg) {
    const auto ids = selectedIds();
    if (ids.empty()) {
        return refuse(QStringLiteral("Placer : sélection vide"));
    }
    const auto opt = [](double v) {
        return std::isfinite(v) ? std::optional<double>(v) : std::nullopt;
    };
    if (ids.size() == 1) {
        PlaceRequest request{opt(xMm), opt(yMm), opt(widthMm), opt(heightMm),
                             std::isfinite(rotateDeg) ? rotateDeg : 0.0};
        return applyBatch(batchPlace(ids, request), QStringLiteral("Placer"), {});
    }
    const auto box = selectionBounds();
    if (!box) {
        return refuse(QStringLiteral("Placer : emprise inconnue"));
    }
    const PointMm centre{(box->x0 + box->x1) / 2.0, -(box->y0 + box->y1) / 2.0};
    std::vector<std::string> parts;
    if (std::isfinite(rotateDeg) && std::abs(rotateDeg) > 1e-6) {
        parts.push_back(batchRotate(ids, rotateDeg, centre));
    }
    double factor = 1.0;
    if (std::isfinite(widthMm) && box->width() > 0) {
        factor = widthMm / box->width();
    } else if (std::isfinite(heightMm) && box->height() > 0) {
        factor = heightMm / box->height();
    }
    if (std::abs(factor - 1.0) > 1e-4) {
        parts.push_back(batchScale(ids, factor, centre));
    }
    const double dx = std::isfinite(xMm) ? xMm - centre.x : 0.0;
    const double dy = std::isfinite(yMm) ? yMm - centre.y : 0.0;
    if (std::abs(dx) > 1e-4 || std::abs(dy) > 1e-4) {
        parts.push_back(batchTranslate(ids, dx, dy));
    }
    return applyBatch(mergeBatches(parts), QStringLiteral("Placer"), {});
}

bool EditController::stack(const QString& move) {
    return applyBatch(batchStack(selectedIds(), move.toStdString()),
                      QStringLiteral("Ordre de couture"), {});
}

bool EditController::group(const QString& name) {
    return applyBatch(
        batchGroup(selectedIds(), name.isEmpty() ? std::string("Groupe") : name.toStdString()),
        QStringLiteral("Grouper"), {});
}

bool EditController::ungroup() {
    return applyBatch(batchUngroup(studio_.session().project(), selectedIds()),
                      QStringLiteral("Dégrouper"), {});
}

bool EditController::renameObject(const QString& id, const QString& name) {
    const auto value = parseId(id);
    if (!value || name.trimmed().isEmpty() ||
        !studio_.session().exists({TargetKind::EmbroideryObject, *value})) {
        return refuse(QStringLiteral("Renommer : nom ou objet invalide"));
    }
    studio_.session().execute(std::make_unique<commands::RenameEmbroideryCommand>(
        ObjectId{*value}, name.trimmed().toStdString()));
    say(QStringLiteral("Objet renommé"));
    return true;
}

bool EditController::setObjectVisible(const QString& id, bool visible) {
    const auto value = parseId(id);
    return value &&
           applyBatch(batchSetVisible({*value}, visible),
                      visible ? QStringLiteral("Afficher") : QStringLiteral("Masquer"), {});
}

bool EditController::setObjectLocked(const QString& id, bool locked) {
    const auto value = parseId(id);
    return value &&
           applyBatch(batchSetLocked({*value}, locked),
                      locked ? QStringLiteral("Verrouiller") : QStringLiteral("Déverrouiller"), {});
}

bool EditController::applyColorFrom(const QString& sourceId) {
    const auto value = parseId(sourceId);
    const auto* source =
        value ? studio_.session().project().findEmbroidery(ObjectId{*value}) : nullptr;
    if (!source) {
        return refuse(QStringLiteral("Pipette : objet inconnu"));
    }
    return applyBatch(
        batchSetColor(selectedIds(), source->rgb), QStringLiteral("Pipette"),
        QStringLiteral("Couleur copiée depuis %1").arg(QString::fromStdString(source->name)));
}

// --- Déplacement souris -------------------------------------------------------------------------

bool EditController::beginMove() {
    const auto ids = selectedIds();
    const auto box = selectionBounds();
    const auto& scene = studio_.scene();
    if (ids.empty() || !box || !scene) {
        move_.reset();
        return false;
    }
    MoveState state;
    state.ids = ids;
    state.moving = toModelBox(*box);
    for (const auto& [id, other] : scene->objectBounds) {
        if (std::ranges::find(ids, id) == ids.end() && visibleObject(id)) {
            state.targets.push_back(toModelBox(other));
        }
    }
    move_ = std::move(state);
    return true;
}

QVariantMap EditController::previewMove(double dxMm, double dyMm, double thresholdMm, bool snap) {
    if (!move_) {
        return {};
    }
    // Repère modèle : Y inversé par rapport à la scène.
    const auto result = snapMove(move_->moving, {dxMm, -dyMm}, move_->targets,
                                 snap && snap_ ? thresholdMm : 0.0, true);
    QVariantList guides;
    for (const auto& g : result.guides) {
        // Un repère vertical s'étend sur y modèle [from, to] = y scène [-to, -from].
        guides.push_back(
            QVariantMap{{QStringLiteral("vertical"), g.vertical},
                        {QStringLiteral("position"), g.vertical ? g.positionMm : -g.positionMm},
                        {QStringLiteral("from"), g.vertical ? -g.toMm : g.fromMm},
                        {QStringLiteral("to"), g.vertical ? -g.fromMm : g.toMm}});
    }
    return {{QStringLiteral("dx"), result.delta.x},
            {QStringLiteral("dy"), -result.delta.y},
            {QStringLiteral("snappedX"), result.snappedX},
            {QStringLiteral("snappedY"), result.snappedY},
            {QStringLiteral("guides"), guides}};
}

bool EditController::commitMove(double dxMm, double dyMm) {
    if (!move_) {
        return false;
    }
    const auto ids = move_->ids;
    move_.reset();
    return applyBatch(batchTranslate(ids, dxMm, -dyMm), QStringLiteral("Déplacer"), {});
}

void EditController::cancelMove() {
    move_.reset();
}

// --- Calques et arbre ---------------------------------------------------------------------------

namespace {
QString idText(std::uint64_t id) {
    return QString::number(id);
}
} // namespace

bool EditController::addLayer(const QString& name) {
    const auto r = openstitch::desktop_v2::addCalque(studio_.session(), name.toStdString());
    say(r.ok ? QStringLiteral("Calque créé") : QString::fromStdString(r.message));
    return r.ok;
}

bool EditController::renameLayer(const QString& id, const QString& name) {
    const auto value = parseId(id);
    const auto r =
        value ? openstitch::desktop_v2::renameCalque(studio_.session(), *value, name.toStdString())
              : ServiceResult{false, "Calque inconnu"};
    say(r.ok ? QStringLiteral("Calque renommé") : QString::fromStdString(r.message));
    return r.ok;
}

bool EditController::removeLayer(const QString& id) {
    const auto value = parseId(id);
    const auto r = value ? openstitch::desktop_v2::removeCalque(studio_.session(), *value)
                         : ServiceResult{false, "Calque inconnu"};
    say(r.ok ? QStringLiteral("Calque supprimé") : QString::fromStdString(r.message));
    return r.ok;
}

bool EditController::setLayerVisible(const QString& id, bool visible) {
    const auto value = parseId(id);
    const auto r =
        value ? openstitch::desktop_v2::setCalqueVisible(studio_.session(), *value, visible)
              : ServiceResult{false, "Calque inconnu"};
    say(r.ok ? QString{} : QString::fromStdString(r.message));
    return r.ok;
}

bool EditController::setLayerLocked(const QString& id, bool locked) {
    const auto value = parseId(id);
    const auto r = value
                       ? openstitch::desktop_v2::setCalqueLocked(studio_.session(), *value, locked)
                       : ServiceResult{false, "Calque inconnu"};
    say(r.ok ? QString{} : QString::fromStdString(r.message));
    return r.ok;
}

bool EditController::moveLayer(const QString& id, int direction) {
    const auto value = parseId(id);
    const auto r = value ? openstitch::desktop_v2::moveCalque(studio_.session(), *value, direction)
                         : ServiceResult{false, "Calque inconnu"};
    say(r.ok ? QStringLiteral("Calque déplacé") : QString::fromStdString(r.message));
    return r.ok;
}

bool EditController::moveSelectionToLayer(const QString& id) {
    const auto value = parseId(id);
    std::vector<std::uint64_t> ids;
    for (const auto& target : studio_.session().selection().targets()) {
        if (target.kind == TargetKind::EmbroideryObject) {
            ids.push_back(target.id);
        }
    }
    const auto r = value
                       ? openstitch::desktop_v2::moveObjectsToCalque(studio_.session(), ids, *value)
                       : ServiceResult{false, "Calque inconnu"};
    say(r.ok ? QStringLiteral("%1 objet(s) déplacé(s) vers le calque").arg(ids.size())
             : QString::fromStdString(r.message));
    return r.ok;
}

bool EditController::moveObjectToLayer(const QString& objectId, const QString& layerId) {
    const auto object = parseId(objectId);
    if (!object || !studio_.session().exists({TargetKind::EmbroideryObject, *object})) {
        return refuse(QStringLiteral("Objet inconnu"));
    }
    if (studio_.session().selection().contains({TargetKind::EmbroideryObject, *object})) {
        return moveSelectionToLayer(layerId);
    }
    const auto value = parseId(layerId);
    const auto r =
        value ? openstitch::desktop_v2::moveObjectsToCalque(studio_.session(), {*object}, *value)
              : ServiceResult{false, "Calque inconnu"};
    say(r.ok ? QStringLiteral("Objet déplacé vers le calque") : QString::fromStdString(r.message));
    return r.ok;
}

QVariantList EditController::layers() const {
    QVariantList list;
    const auto& project = studio_.session().project();
    const auto all = openstitch::desktop_v2::calques(project); // du fond au dessus
    for (auto it = all.rbegin(); it != all.rend(); ++it) {     // le dessus en premier, comme V1
        list.push_back(QVariantMap{{QStringLiteral("id"), idText(it->id)},
                                   {QStringLiteral("name"), QString::fromStdString(it->name)},
                                   {QStringLiteral("visible"), it->visible},
                                   {QStringLiteral("locked"), it->locked}});
    }
    return list;
}

QVariantList EditController::tree() const {
    QVariantList rows;
    const auto& project = studio_.session().project();
    const auto& selection = studio_.session().selection();
    const auto order = project.sewingOrder();
    const auto all = openstitch::desktop_v2::calques(project);
    const auto objectRow = [&](const document::EmbroideryObject& object, int depth) {
        const Target target{TargetKind::EmbroideryObject, object.id.value};
        const auto type = stitchTypeId(object);
        return QVariantMap{
            {QStringLiteral("kind"), QStringLiteral("object")},
            {QStringLiteral("id"), idText(object.id.value)},
            {QStringLiteral("label"),
             QString::fromStdString(object.name.empty() ? "Objet " + std::to_string(object.id.value)
                                                        : object.name)},
            {QStringLiteral("depth"), depth},
            {QStringLiteral("color"), QColor(object.rgb[0], object.rgb[1], object.rgb[2])},
            {QStringLiteral("selected"), selection.contains(target)},
            {QStringLiteral("visible"), object.visible},
            {QStringLiteral("locked"), object.locked},
            {QStringLiteral("type"),
             QString::fromUtf8(type.data(), static_cast<qsizetype>(type.size()))}};
    };
    // Groupes : un en-tête par groupe direct, ses objets dessous (ordre de couture conservé).
    const auto emitScope = [&](std::uint64_t calqueId, bool implicit, int depth) {
        std::set<std::uint64_t> groupsDone;
        for (const auto index : order) {
            const auto& object = project.embroidery_objects[index];
            const auto* calque = project.calqueOfShape(object.source_vector);
            if (!implicit && (!calque || calque->id.value != calqueId)) {
                continue;
            }
            const auto* shape = project.findObject(object.source_vector);
            const auto groupId = shape ? shape->group.value : 0;
            const auto* group = groupId ? project.findGroup(ObjectId{groupId}) : nullptr;
            if (!group) {
                rows.push_back(objectRow(object, depth));
                continue;
            }
            if (!groupsDone.insert(groupId).second) {
                continue;
            }
            rows.push_back(
                QVariantMap{{QStringLiteral("kind"), QStringLiteral("group")},
                            {QStringLiteral("id"), idText(groupId)},
                            {QStringLiteral("label"), QString::fromStdString(group->name)},
                            {QStringLiteral("depth"), depth}});
            for (const auto inner : order) {
                const auto& member = project.embroidery_objects[inner];
                const auto* memberShape = project.findObject(member.source_vector);
                const auto* memberCalque = project.calqueOfShape(member.source_vector);
                if (memberShape && memberShape->group.value == groupId &&
                    (implicit || (memberCalque && memberCalque->id.value == calqueId))) {
                    rows.push_back(objectRow(member, depth + 1));
                }
            }
        }
    };
    if (all.empty()) {
        emitScope(0, true, 0);
        return rows;
    }
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        rows.push_back(QVariantMap{{QStringLiteral("kind"), QStringLiteral("layer")},
                                   {QStringLiteral("id"), idText(it->id)},
                                   {QStringLiteral("label"), QString::fromStdString(it->name)},
                                   {QStringLiteral("depth"), 0},
                                   {QStringLiteral("visible"), it->visible},
                                   {QStringLiteral("locked"), it->locked}});
        emitScope(it->id, false, 1);
    }
    return rows;
}

} // namespace openstitch::desktop_v2::studio
