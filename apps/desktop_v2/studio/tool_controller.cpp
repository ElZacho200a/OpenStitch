// SPDX-License-Identifier: Apache-2.0
#include "tool_controller.hpp"

#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <numbers>

#include "detail_controller.hpp"
#include "edit_batch.hpp"
#include "pen_shape.hpp"
#include "shape_batch.hpp"

namespace openstitch::desktop_v2::studio {

namespace {

struct ToolSpec {
    const char* id;
    const char* glyph;
    const char* group;
};

// Outils opérationnels dans Studio ; le libellé, le raccourci et le geste viennent du registre.
constexpr ToolSpec kSpecs[] = {
    {"draw.rectangle", "▭", "Formes"}, {"draw.ellipse", "◯", "Formes"},
    {"draw.polygon", "⬠", "Formes"},   {"draw.polygon-regular", "⬡", "Formes"},
    {"draw.bezier", "✒", "Tracés"},    {"draw.freeform", "✎", "Tracés"},
    {"node.edit", "◇", "Édition"},     {"knife", "✂", "Édition"},
    {"detail-brush", "◉", "Image"},    {"detail-erase", "◌", "Image"},
    {"click-to-stitch", "⊕", "Image"},
};

bool detailTool(const QString& tool) {
    return tool == QLatin1String("detail-brush") || tool == QLatin1String("detail-erase") ||
           tool == QLatin1String("click-to-stitch");
}

constexpr double kTwoPi = 2.0 * std::numbers::pi;

double dist(PointMm a, PointMm b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

std::vector<PointMm> sampleBezier(PointMm p0, PointMm c1, PointMm c2, PointMm p1) {
    std::vector<PointMm> out;
    constexpr int kSteps = 16;
    for (int i = 0; i <= kSteps; ++i) {
        const double t = static_cast<double>(i) / kSteps;
        const double u = 1.0 - t;
        out.push_back(
            {u * u * u * p0.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * p1.x,
             u * u * u * p0.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * p1.y});
    }
    return out;
}

} // namespace

ToolController::ToolController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio), registry_(ToolRegistry::builtin()) {
    connect(&studio_, &StudioController::selectionChanged, this, [this] {
        if (tool_ == QStringLiteral("node.edit")) {
            refreshNodeView();
            rebuild();
        }
    });
    connect(&studio_, &StudioController::sceneChanged, this, [this] {
        if (tool_ == QStringLiteral("node.edit") && drag_ == Drag::None) {
            refreshNodeView();
            rebuild();
        }
    });
    setHint();
}

QVariantList ToolController::tools() const {
    QVariantList out;
    for (const auto& spec : kSpecs) {
        if (!details_ && detailTool(QString::fromLatin1(spec.id))) {
            continue;
        }
        const auto* d = registry_.find(spec.id);
        if (!d) {
            continue;
        }
        out.push_back(QVariantMap{{QStringLiteral("id"), QString::fromUtf8(spec.id)},
                                  {QStringLiteral("label"), QString::fromStdString(d->label)},
                                  {QStringLiteral("shortcut"), QString::fromStdString(d->shortcut)},
                                  {QStringLiteral("glyph"), QString::fromUtf8(spec.glyph)},
                                  {QStringLiteral("group"), QString::fromUtf8(spec.group)}});
    }
    return out;
}

bool ToolController::capturesMouse() const {
    return tool_ != QStringLiteral("select") && tool_ != QStringLiteral("pan");
}

bool ToolController::busy() const {
    return (details_ &&
            (details_->gestureActive() || details_->busy() || details_->confirmationPending())) ||
           drag_ != Drag::None || !polyPoints_.empty() || !penNodes_.empty();
}

void ToolController::setDetailController(DetailController* controller) {
    details_ = controller;
    if (details_) {
        connect(details_, &DetailController::changed, this, &ToolController::overlayChanged);
        connect(details_, &QObject::destroyed, this, [this] { details_ = nullptr; });
    }
}

void ToolController::setActiveTool(const QString& id) {
    if (id == tool_) {
        return;
    }
    const bool known =
        id == QStringLiteral("select") || id == QStringLiteral("pan") ||
        std::ranges::any_of(kSpecs, [&](const ToolSpec& s) { return id == QLatin1String(s.id); });
    if (!known) {
        refuse(QStringLiteral("Outil inconnu ou pas encore disponible dans Studio : %1").arg(id));
        return;
    }
    if (detailTool(id) && !details_) {
        refuse(QStringLiteral("Outils image indisponibles."));
        return;
    }
    reset();
    tool_ = id;
    nodeSel_.clear();
    refreshNodeView();
    setHint();
    rebuild();
    emit activeToolChanged();
}

void ToolController::setHint() {
    QString text;
    if (detailTool(tool_)) {
        text = QStringLiteral("Image source : peindre / gommer les détails ou cliquer une zone à "
                              "broder. Échap annule le geste ; options dans le panneau Détail.");
    } else if (tool_ == QStringLiteral("draw.rectangle") ||
               tool_ == QStringLiteral("draw.ellipse")) {
        text = QStringLiteral(
            "Glisser pour tracer. Maj : carré / cercle. Alt : depuis le centre. Échap : annuler.");
    } else if (tool_ == QStringLiteral("draw.polygon")) {
        text = QStringLiteral("Cliquer les sommets ; Entrée ou double-clic pour fermer, Retour "
                              "arrière : dernier sommet, Échap : annuler.");
    } else if (tool_ == QStringLiteral("draw.polygon-regular")) {
        text = QStringLiteral(
            "Glisser depuis le centre ; le côté du glissement fixe rayon et rotation.");
    } else if (tool_ == QStringLiteral("draw.bezier")) {
        text = QStringLiteral("Clic : coin ; clic-glisser : nœud lisse. Entrée ou double-clic : "
                              "terminer ; clic sur le 1er nœud : fermer.");
    } else if (tool_ == QStringLiteral("draw.freeform")) {
        text =
            QStringLiteral("Dessiner le contour à main levée ; relâcher pour ajuster les courbes.");
    } else if (tool_ == QStringLiteral("knife")) {
        text = QStringLiteral(
            "Glisser une ligne à travers la ou les formes sélectionnées pour les découper.");
    } else if (tool_ == QStringLiteral("node.edit")) {
        text = QStringLiteral("Sélectionner une forme, puis : clic nœud, Maj : ajouter, glisser ; "
                              "glisser un segment : le courber ; double-clic segment : insérer un "
                              "nœud ; Suppr : supprimer ; C / S : angle / lisse.");
    }
    if (text != hint_) {
        hint_ = text;
        emit hintChanged();
    }
}

void ToolController::inform(const QString& text) {
    message_ = text;
    emit messageChanged();
}

void ToolController::refuse(const QString& text) {
    inform(text);
}

void ToolController::setSides(int value) {
    value = std::clamp(value, 3, 64);
    if (value != sides_) {
        sides_ = value;
        emit optionsChanged();
    }
}
void ToolController::setStar(bool value) {
    if (value != star_) {
        star_ = value;
        emit optionsChanged();
    }
}
void ToolController::setStarRatio(double value) {
    value = std::clamp(value, 0.1, 0.9);
    if (value != starRatio_) {
        starRatio_ = value;
        emit optionsChanged();
    }
}
void ToolController::setSnapGrid(bool value) {
    if (value != snapGrid_) {
        snapGrid_ = value;
        emit optionsChanged();
    }
}
void ToolController::setGridMm(double value) {
    value = std::clamp(value, 0.1, 100.0);
    if (value != gridMm_) {
        gridMm_ = value;
        emit optionsChanged();
    }
}
void ToolController::setSnapVertices(bool value) {
    if (value != snapVertices_) {
        snapVertices_ = value;
        emit optionsChanged();
    }
}
void ToolController::setSnapRadiusPx(int value) {
    value = std::clamp(value, 2, 40);
    if (value != snapRadiusPx_) {
        snapRadiusPx_ = value;
        emit optionsChanged();
    }
}

std::vector<std::uint64_t> ToolController::selectedIds() const {
    std::vector<std::uint64_t> ids;
    for (const auto& t : studio_.session().selection().targets()) {
        if (t.kind == TargetKind::EmbroideryObject) {
            ids.push_back(t.id);
        }
    }
    std::ranges::sort(ids);
    return ids;
}

PointMm ToolController::snapped(PointMm p, bool allowGrid) const {
    if (snapVertices_) {
        const double radius = snapRadiusPx_ / std::max(scale_, 1e-6);
        double best = radius;
        std::optional<PointMm> found;
        for (const auto& vector : studio_.session().project().vector_objects) {
            for (const auto& set : vector.paths) {
                for (const auto& node : set.outer.nodes) {
                    const PointMm q{node.pos.x.value / 1000.0, node.pos.y.value / 1000.0};
                    const double d = dist(p, q);
                    if (d < best) {
                        best = d;
                        found = q;
                    }
                }
            }
        }
        if (found) {
            return *found;
        }
    }
    if (allowGrid && snapGrid_ && gridMm_ > 0.0) {
        return {std::round(p.x / gridMm_) * gridMm_, std::round(p.y / gridMm_) * gridMm_};
    }
    return p;
}

void ToolController::reset() {
    if (details_) {
        details_->cancel();
    }
    drag_ = Drag::None;
    stroke_.clear();
    penNodes_.clear();
    polyPoints_.clear();
    penDragging_ = false;
    hover_ = false;
    segment_.reset();
    info_.clear();
}

bool ToolController::cancel() {
    const bool had = busy();
    reset();
    if (had) {
        inform(QStringLiteral("Geste annulé"));
    }
    rebuild();
    return had;
}

void ToolController::refreshNodeView() {
    view_.reset();
    const auto ids = selectedIds();
    if (tool_ != QStringLiteral("node.edit") || ids.size() != 1) {
        nodeObject_ = 0;
        nodeSel_.clear();
        return;
    }
    if (ids.front() != nodeObject_) {
        nodeSel_.clear();
    }
    nodeObject_ = ids.front();
    if (auto v = nodeView(studio_.session().project(), nodeObject_)) {
        view_ = std::move(*v);
        std::erase_if(nodeSel_, [&](const NodeRef& r) {
            return std::ranges::none_of(view_->nodes,
                                        [&](const NodeHandle& h) { return h.ref == r; });
        });
    } else {
        nodeSel_.clear();
    }
}

void ToolController::createdFrom(std::uint64_t id, const QString& label) {
    studio_.session().selection().replace({Target{TargetKind::EmbroideryObject, id}});
    inform(label);
    emit objectCreated(QString::number(id));
}

std::vector<PointMm> ToolController::boxPolygon(PointMm a, PointMm b,
                                                Qt::KeyboardModifiers mods) const {
    std::vector<PointMm> out;
    if (tool_ == QStringLiteral("draw.polygon-regular")) {
        const double r = dist(a, b);
        const double rot = std::atan2(b.y - a.y, b.x - a.x);
        const int n = star_ ? sides_ * 2 : sides_;
        for (int i = 0; i < n; ++i) {
            const double radius = star_ && (i % 2 == 1) ? r * starRatio_ : r;
            const double ang = rot + kTwoPi * i / n;
            out.push_back({a.x + radius * std::cos(ang), a.y + radius * std::sin(ang)});
        }
        return out;
    }
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    if (mods.testFlag(Qt::ShiftModifier)) {
        const double side = std::max(std::abs(dx), std::abs(dy));
        dx = dx < 0 ? -side : side;
        dy = dy < 0 ? -side : side;
    }
    PointMm lo{a.x, a.y};
    PointMm hi{a.x + dx, a.y + dy};
    if (mods.testFlag(Qt::AltModifier)) {
        lo = {a.x - dx, a.y - dy};
    }
    const PointMm min{std::min(lo.x, hi.x), std::min(lo.y, hi.y)};
    const PointMm max{std::max(lo.x, hi.x), std::max(lo.y, hi.y)};
    if (tool_ == QStringLiteral("draw.ellipse")) {
        const PointMm c{(min.x + max.x) / 2, (min.y + max.y) / 2};
        for (int i = 0; i < 48; ++i) {
            const double ang = kTwoPi * i / 48;
            out.push_back({c.x + (max.x - min.x) / 2 * std::cos(ang),
                           c.y + (max.y - min.y) / 2 * std::sin(ang)});
        }
        return out;
    }
    return {{min.x, min.y}, {max.x, min.y}, {max.x, max.y}, {min.x, max.y}};
}

bool ToolController::commitBox(PointMm a, PointMm b, Qt::KeyboardModifiers mods) {
    const auto poly = boxPolygon(a, b, mods);
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (const auto& p : poly) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    const double w = maxX - minX;
    const double h = maxY - minY;
    if (poly.empty() || w < 0.5 || h < 0.5) {
        refuse(QStringLiteral("Forme trop petite (moins de 0,5 mm) : rien créé"));
        return false;
    }
    std::string batch;
    QString label;
    if (tool_ == QStringLiteral("draw.rectangle")) {
        batch = batchAddRect({(minX + maxX) / 2, (minY + maxY) / 2}, w, h);
        label = QStringLiteral("Rectangle");
    } else if (tool_ == QStringLiteral("draw.ellipse")) {
        batch = batchAddEllipse({(minX + maxX) / 2, (minY + maxY) / 2}, w, h);
        label = QStringLiteral("Ellipse");
    } else {
        batch = batchAddPolygon(poly);
        label = star_ ? QStringLiteral("Étoile") : QStringLiteral("Polygone régulier");
    }
    if (batch.empty()) {
        refuse(label + QStringLiteral(" : valeurs hors limites"));
        return false;
    }
    const auto result = studio_.session().applyBatch(batch, label.toStdString());
    const auto id = createdObjectId(result.report);
    if (!result.committed || !id) {
        refuse(label + QStringLiteral(" refusé(e)"));
        return false;
    }
    createdFrom(*id, label + QStringLiteral(" créé(e)"));
    return true;
}

bool ToolController::press(double x, double y, double pxPerMm, Qt::KeyboardModifiers mods) {
    scale_ = pxPerMm;
    if (details_ && detailTool(tool_)) {
        return details_->press(tool_, x, y);
    }
    if (!capturesMouse()) {
        return false;
    }
    mods_ = mods;
    const PointMm raw = toModel(x, y);
    const bool nodeTool = tool_ == QStringLiteral("node.edit");
    const PointMm p = nodeTool ? raw : snapped(raw);
    current_ = p;
    anchor_ = p;
    if (tool_ == QStringLiteral("draw.rectangle") || tool_ == QStringLiteral("draw.ellipse") ||
        tool_ == QStringLiteral("draw.polygon-regular")) {
        drag_ = Drag::Box;
    } else if (tool_ == QStringLiteral("draw.polygon")) {
        if (polyPoints_.empty() || dist(polyPoints_.back(), p) > 2.0 / scale_) {
            polyPoints_.push_back(p);
        }
        hover_ = true;
    } else if (tool_ == QStringLiteral("draw.bezier")) {
        if (penNodes_.size() >= 3 && dist(penNodes_.front().pos, p) < 8.0 / scale_) {
            finish();
            return true;
        }
        penNodes_.push_back({p, std::nullopt});
        penDragging_ = true;
        drag_ = Drag::PenNode;
        hover_ = true;
    } else if (tool_ == QStringLiteral("draw.freeform")) {
        drag_ = Drag::Freehand;
        stroke_ = {p};
    } else if (tool_ == QStringLiteral("knife")) {
        drag_ = Drag::Line;
    } else if (nodeTool) {
        const double hit = 8.0 / scale_;
        if (view_) {
            // 1. poignées de tangente des nœuds sélectionnés
            for (const auto& t : view_->tangents) {
                if (std::ranges::find(nodeSel_, t.node) != nodeSel_.end() &&
                    dist(t.pos, raw) < hit) {
                    drag_ = Drag::Tangent;
                    activeNode_ = t.node;
                    tangentOut_ = t.isOut;
                    rebuild();
                    return true;
                }
            }
            // 2. nœuds
            const NodeHandle* best = nullptr;
            double bestD = hit;
            for (const auto& n : view_->nodes) {
                const double d = dist(n.pos, raw);
                if (d < bestD) {
                    bestD = d;
                    best = &n;
                }
            }
            if (best) {
                const auto it = std::ranges::find(nodeSel_, best->ref);
                if (mods.testFlag(Qt::ShiftModifier)) {
                    if (it != nodeSel_.end()) {
                        nodeSel_.erase(it);
                    } else {
                        nodeSel_.push_back(best->ref);
                    }
                } else if (it == nodeSel_.end()) {
                    nodeSel_ = {best->ref};
                }
                drag_ = nodeSel_.empty() ? Drag::None : Drag::Nodes;
                anchor_ = raw;
                emit overlayChanged();
                rebuild();
                return true;
            }
            // 3. segment : glisser = courber, double-clic = insérer
            if (const auto seg = nearestSegment(*view_, raw, hit)) {
                segment_ = *seg;
                drag_ = Drag::Segment;
                anchor_ = raw;
                rebuild();
                return true;
            }
        }
        // 4. autre objet sous le curseur : il devient l'objet édité
        const auto& scene = studio_.scene();
        if (scene) {
            if (const auto id = scene->pick(x, y, 5.0 / scale_)) {
                studio_.selectObject(QString::number(*id), false);
                return true;
            }
        }
        nodeSel_.clear();
        rebuild();
        return true;
    }
    rebuild();
    return true;
}

void ToolController::move(double x, double y, Qt::KeyboardModifiers mods) {
    if (details_ && detailTool(tool_)) {
        details_->move(x, y);
        return;
    }
    if (!capturesMouse()) {
        return;
    }
    const bool nodeTool = tool_ == QStringLiteral("node.edit");
    const PointMm raw = toModel(x, y);
    current_ = nodeTool ? raw : snapped(raw);
    mods_ = mods;
    if (drag_ == Drag::Freehand) {
        if (stroke_.empty() || dist(stroke_.back(), current_) > 1.5 / scale_) {
            stroke_.push_back(current_);
        }
    } else if (drag_ == Drag::PenNode && !penNodes_.empty()) {
        if (dist(penNodes_.back().pos, current_) > 3.0 / scale_) {
            penNodes_.back().handleOut = current_;
        }
    } else if (drag_ == Drag::Nodes || drag_ == Drag::Tangent || drag_ == Drag::Segment) {
        current_ = raw;
    }
    if (drag_ == Drag::Box) {
        const auto poly = boxPolygon(anchor_, current_, mods);
        double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        for (const auto& p : poly) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
        info_ =
            QStringLiteral("%1 × %2 mm").arg(maxX - minX, 0, 'f', 1).arg(maxY - minY, 0, 'f', 1);
    } else if (drag_ == Drag::Line) {
        info_ = QStringLiteral("%1 mm").arg(dist(anchor_, current_), 0, 'f', 1);
    } else if (drag_ == Drag::Nodes) {
        info_ = QStringLiteral("Δ %1 ; %2 mm")
                    .arg(current_.x - anchor_.x, 0, 'f', 1)
                    .arg(current_.y - anchor_.y, 0, 'f', 1);
    }
    rebuild();
}

void ToolController::release(double x, double y, Qt::KeyboardModifiers mods) {
    if (details_ && detailTool(tool_)) {
        details_->release(x, y);
        return;
    }
    if (!capturesMouse() || drag_ == Drag::None) {
        if (drag_ == Drag::None && penDragging_) {
            penDragging_ = false;
        }
        return;
    }
    const bool nodeTool = tool_ == QStringLiteral("node.edit");
    const PointMm raw = toModel(x, y);
    const PointMm p = nodeTool ? raw : snapped(raw);
    current_ = p;
    const Drag drag = drag_;
    const double moved = dist(anchor_, p) * scale_; // px
    drag_ = Drag::None;
    info_.clear();
    auto& session = studio_.session();
    switch (drag) {
    case Drag::Box:
        if (moved >= 3.0) {
            (void)commitBox(anchor_, p, mods);
        }
        break;
    case Drag::Freehand: {
        auto stroke = std::move(stroke_);
        stroke_.clear();
        if (stroke.size() >= 2) {
            const auto made = createFreehandShape(session, stroke);
            if (made.ok) {
                createdFrom(made.objectId, QStringLiteral("Forme à main levée créée"));
            } else {
                refuse(QString::fromStdString(made.message));
            }
        }
        break;
    }
    case Drag::PenNode:
        penDragging_ = false;
        break;
    case Drag::Line: {
        if (moved < 3.0) {
            break;
        }
        const auto ids = selectedIds();
        if (ids.empty()) {
            refuse(QStringLiteral("Couteau : sélectionnez d'abord la ou les formes à découper"));
            break;
        }
        const auto batch = batchSplit(ids, anchor_, p);
        if (batch.empty()) {
            refuse(QStringLiteral("Couteau : ligne invalide"));
            break;
        }
        const auto result = session.applyBatch(batch, "Découper");
        if (!result.committed) {
            refuse(
                QStringLiteral("Découpe refusée : la ligne ne traverse aucune forme sélectionnée"));
            break;
        }
        std::vector<Target> pieces;
        for (const auto id : createdObjectIds(result.report)) {
            pieces.push_back({TargetKind::EmbroideryObject, id});
        }
        if (!pieces.empty()) {
            session.selection().replace(std::move(pieces));
        }
        inform(QStringLiteral("Forme découpée en %1 morceaux")
                   .arg(createdObjectIds(result.report).size()));
        break;
    }
    case Drag::Nodes:
    case Drag::Tangent:
    case Drag::Segment: {
        if (moved >= 3.0 && nodeObject_ != 0) {
            const PointMm delta{raw.x - anchor_.x, raw.y - anchor_.y};
            ServiceResult r;
            if (drag == Drag::Nodes) {
                r = moveNodes(session, nodeObject_, nodeSel_, delta,
                              mods.testFlag(Qt::AltModifier));
            } else if (drag == Drag::Tangent) {
                r = moveTangent(session, nodeObject_, activeNode_, tangentOut_, raw);
            } else if (segment_) {
                r = reshapeSegment(session, nodeObject_, *segment_, delta);
            }
            if (!r.ok) {
                refuse(QString::fromStdString(r.message));
            } else {
                inform(QStringLiteral("Contour modifié"));
            }
        }
        segment_.reset();
        break;
    }
    case Drag::None:
        break;
    }
    refreshNodeView();
    rebuild();
}

void ToolController::doubleClick(double x, double y, double pxPerMm) {
    scale_ = pxPerMm;
    if (tool_ == QStringLiteral("draw.polygon") || tool_ == QStringLiteral("draw.bezier")) {
        // Le premier clic du double-clic a déjà posé le sommet.
        (void)finish();
        return;
    }
    if (tool_ == QStringLiteral("node.edit") && view_) {
        drag_ = Drag::None;
        segment_.reset();
        const PointMm raw = toModel(x, y);
        if (const auto seg = nearestSegment(*view_, raw, 8.0 / scale_)) {
            const auto r = insertNode(studio_.session(), nodeObject_, *seg);
            nodeSel_.clear();
            inform(r.ok ? QStringLiteral("Nœud inséré") : QString::fromStdString(r.message));
            refreshNodeView();
            rebuild();
        }
    }
}

bool ToolController::finish() {
    auto& session = studio_.session();
    if (tool_ == QStringLiteral("draw.polygon")) {
        auto points = polyPoints_;
        while (points.size() > 1 && dist(points.back(), points[points.size() - 2]) < 2.0 / scale_) {
            points.pop_back();
        }
        if (points.size() < 3) {
            refuse(QStringLiteral("Polygone : au moins 3 sommets"));
            return false;
        }
        reset();
        const auto batch = batchAddPolygon(points);
        const auto result = session.applyBatch(batch, "Polygone");
        const auto id = createdObjectId(result.report);
        if (!result.committed || !id) {
            refuse(QStringLiteral("Polygone refusé (sommets hors limites ou contour dégénéré)"));
            rebuild();
            return false;
        }
        createdFrom(*id, QStringLiteral("Polygone créé"));
        rebuild();
        return true;
    }
    if (tool_ == QStringLiteral("draw.bezier")) {
        auto nodes = penNodes_;
        while (nodes.size() > 1 &&
               dist(nodes.back().pos, nodes[nodes.size() - 2].pos) < 2.0 / scale_) {
            nodes.pop_back();
        }
        if (nodes.size() < 3) {
            refuse(QStringLiteral("Plume : au moins 3 nœuds"));
            return false;
        }
        reset();
        const auto made = createPenShape(session, nodes);
        if (!made.ok) {
            refuse(QString::fromStdString(made.message));
            rebuild();
            return false;
        }
        createdFrom(made.objectId,
                    made.curved ? QStringLiteral("Courbe créée") : QStringLiteral("Forme créée"));
        rebuild();
        return true;
    }
    return false;
}

bool ToolController::keyPress(int key) {
    if (!capturesMouse()) {
        return false;
    }
    if (key == Qt::Key_Escape) {
        return cancel();
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        return finish();
    }
    if (key == Qt::Key_Backspace) {
        if (!polyPoints_.empty()) {
            polyPoints_.pop_back();
            rebuild();
            return true;
        }
        if (!penNodes_.empty()) {
            penNodes_.pop_back();
            rebuild();
            return true;
        }
    }
    if (tool_ == QStringLiteral("node.edit")) {
        if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
            return deleteSelectedNodes();
        }
        if (key == Qt::Key_C) {
            return setSelectedNodeKind(false);
        }
        if (key == Qt::Key_S) {
            return setSelectedNodeKind(true);
        }
    }
    return false;
}

bool ToolController::deleteSelectedNodes() {
    if (nodeObject_ == 0 || nodeSel_.empty()) {
        return false;
    }
    const auto r = deleteNodes(studio_.session(), nodeObject_, nodeSel_);
    inform(r.ok ? QStringLiteral("Nœud(s) supprimé(s)") : QString::fromStdString(r.message));
    if (r.ok) {
        nodeSel_.clear();
    }
    refreshNodeView();
    rebuild();
    return r.ok;
}

bool ToolController::setSelectedNodeKind(bool smooth) {
    if (nodeObject_ == 0 || nodeSel_.empty()) {
        return false;
    }
    const auto r = setNodeKind(studio_.session(), nodeObject_, nodeSel_,
                               smooth ? NodeKind::Smooth : NodeKind::Corner);
    inform(r.ok ? (smooth ? QStringLiteral("Nœud(s) lisse(s)") : QStringLiteral("Nœud(s) anguleux"))
                : QString::fromStdString(r.message));
    refreshNodeView();
    rebuild();
    return r.ok;
}

bool ToolController::removeHole() {
    if (nodeObject_ == 0 || nodeSel_.empty()) {
        return false;
    }
    const auto r = removeHoleOfNode(studio_.session(), nodeObject_, nodeSel_.front());
    inform(r.ok ? QStringLiteral("Trou supprimé") : QString::fromStdString(r.message));
    if (r.ok) {
        nodeSel_.clear();
    }
    refreshNodeView();
    rebuild();
    return r.ok;
}

QString ToolController::cursorName() const {
    return capturesMouse() ? QStringLiteral("cross") : QStringLiteral("arrow");
}

void ToolController::rebuild() {
    overlay_.clear();
    auto line = [&](ToolOverlayItem::Kind kind, const std::vector<PointMm>& pts) {
        ToolOverlayItem item;
        item.kind = kind;
        for (const auto& p : pts) {
            item.points.push_back(toScene(p));
        }
        if (!item.points.empty()) {
            overlay_.push_back(std::move(item));
        }
    };
    if (drag_ == Drag::Box) {
        line(ToolOverlayItem::Kind::Closed, boxPolygon(anchor_, current_, mods_));
    } else if (drag_ == Drag::Line) {
        line(ToolOverlayItem::Kind::Line, {anchor_, current_});
    } else if (drag_ == Drag::Freehand || !stroke_.empty()) {
        line(ToolOverlayItem::Kind::Line, stroke_);
    }
    if (!polyPoints_.empty()) {
        auto pts = polyPoints_;
        if (hover_) {
            pts.push_back(current_);
        }
        line(ToolOverlayItem::Kind::Line, pts);
        for (const auto& p : polyPoints_) {
            line(ToolOverlayItem::Kind::Point, {p});
        }
    }
    if (!penNodes_.empty()) {
        std::vector<PointMm> curve{penNodes_.front().pos};
        for (std::size_t i = 0; i + 1 < penNodes_.size(); ++i) {
            const auto& a = penNodes_[i];
            const auto& b = penNodes_[i + 1];
            const PointMm c1 = a.handleOut ? *a.handleOut : a.pos;
            const PointMm c2 =
                b.handleOut ? PointMm{2 * b.pos.x - b.handleOut->x, 2 * b.pos.y - b.handleOut->y}
                            : b.pos;
            const auto seg = sampleBezier(a.pos, c1, c2, b.pos);
            curve.insert(curve.end(), seg.begin() + 1, seg.end());
        }
        if (hover_ && !penDragging_) {
            curve.push_back(current_);
        }
        line(ToolOverlayItem::Kind::Line, curve);
        for (const auto& n : penNodes_) {
            line(ToolOverlayItem::Kind::Point, {n.pos});
            if (n.handleOut) {
                line(ToolOverlayItem::Kind::Tangent, {n.pos, *n.handleOut});
                line(ToolOverlayItem::Kind::Tangent,
                     {n.pos, {2 * n.pos.x - n.handleOut->x, 2 * n.pos.y - n.handleOut->y}});
            }
        }
    }
    if (tool_ == QStringLiteral("node.edit") && view_) {
        const bool dragging =
            drag_ == Drag::Nodes || drag_ == Drag::Tangent || drag_ == Drag::Segment;
        std::optional<std::vector<std::vector<PointMm>>> preview;
        const auto& project = studio_.session().project();
        const PointMm delta{current_.x - anchor_.x, current_.y - anchor_.y};
        if (dragging) {
            if (drag_ == Drag::Nodes) {
                preview = previewNodeMove(project, nodeObject_, nodeSel_, delta,
                                          mods_.testFlag(Qt::AltModifier));
            } else if (drag_ == Drag::Tangent) {
                preview =
                    previewTangentDrag(project, nodeObject_, activeNode_, tangentOut_, current_);
            } else if (segment_) {
                preview = previewSegmentReshape(project, nodeObject_, *segment_, delta);
            }
        }
        for (const auto& outline : preview ? *preview : view_->outlines) {
            line(preview ? ToolOverlayItem::Kind::Ghost : ToolOverlayItem::Kind::Closed, outline);
        }
        for (const auto& n : view_->nodes) {
            const bool selected = std::ranges::find(nodeSel_, n.ref) != nodeSel_.end();
            PointMm pos = n.pos;
            if (selected && drag_ == Drag::Nodes) {
                pos = {pos.x + delta.x, pos.y + delta.y};
            }
            line(selected ? ToolOverlayItem::Kind::Selected : ToolOverlayItem::Kind::Point, {pos});
        }
        for (const auto& t : view_->tangents) {
            if (std::ranges::find(nodeSel_, t.node) != nodeSel_.end()) {
                line(ToolOverlayItem::Kind::Tangent, {t.anchor, t.pos});
                line(ToolOverlayItem::Kind::Point, {t.pos});
            }
        }
    }
    emit overlayChanged();
}

} // namespace openstitch::desktop_v2::studio
