// SPDX-License-Identifier: Apache-2.0
#include "node_edit_controller.hpp"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineF>
#include <QTransform>

#include <algorithm>

#include "preview_canvas.hpp"

namespace openstitch::desktop_v2 {

namespace {
QString fromUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
} // namespace

NodeEditController::NodeEditController(Host host, QObject* parent)
    : QObject(parent), host_(std::move(host)) {}

void NodeEditController::connectCanvas() {
    connect(&host_.canvas, &PreviewCanvas::nodePressed, this, &NodeEditController::onNodePressed);
    connect(&host_.canvas, &PreviewCanvas::nodeDragMovedPx, this,
            &NodeEditController::onNodeDragMoved);
    connect(&host_.canvas, &PreviewCanvas::nodeDragFinishedPx, this,
            &NodeEditController::onNodeDragFinished);
    host_.canvas.setSegmentHitTest([this](const QPointF& scenePx) {
        if (host_.activeTool() != QStringLiteral("node.edit") || nodeObject_ == 0 ||
            host_.frame.px_per_mm <= 0.0 || fadeView_) {
            return false;
        }
        const PointMm at =
            screenToModel(host_.frame.pxToMmX(scenePx.x()), host_.frame.pxToMmY(scenePx.y()));
        const double zoom = std::max(0.01, host_.canvas.transform().m11());
        return curveSegmentAt(host_.session.project(), nodeObject_, at,
                              8.0 / (zoom * host_.frame.px_per_mm))
            .has_value();
    });
    connect(&host_.canvas, &PreviewCanvas::segmentPressed, this, [this](QPointF scenePx) {
        const PointMm at =
            screenToModel(host_.frame.pxToMmX(scenePx.x()), host_.frame.pxToMmY(scenePx.y()));
        const double zoom = std::max(0.01, host_.canvas.transform().m11());
        segmentDrag_ = curveSegmentAt(host_.session.project(), nodeObject_, at,
                                      8.0 / (zoom * host_.frame.px_per_mm));
        nodeSel_.clear();
        host_.notice(tr("Segment saisi : tirez pour courber le contour sans déplacer les nœuds."),
                     false);
    });
    connect(&host_.canvas, &PreviewCanvas::nodeDragCancelled, this, [this] {
        segmentDrag_.reset();
        host_.canvas.setNodeOutline({}, true);
        host_.notice(tr("Déplacement de nœuds annulé."), false);
    });
    connect(&host_.canvas, &PreviewCanvas::nodeInsertRequestedPx, this,
            &NodeEditController::onNodeInsert);
}

void NodeEditController::clearSelection() {
    if (!nodeSel_.empty()) {
        nodeSel_.clear();
        refresh();
    }
}

namespace {
PointMm fadeHandleMm(const FadeAxisView& v, int index) {
    switch (index) {
    case 0:
        return v.start;
    case 1:
        return v.end;
    case 2:
        return v.centre;
    default:
        return v.rotate;
    }
}
} // namespace

int NodeEditController::handleCount() const {
    return fadeView_ ? 4 : static_cast<int>(nodeView_.nodes.size());
}

QPointF NodeEditController::handlePx(int index) const {
    if (fadeView_) {
        if (index < 0 || index > 3) {
            return {};
        }
        const auto p = fadeHandleMm(*fadeView_, index);
        return {host_.frame.mmToPxX(p.x), host_.frame.mmToPxY(-p.y)};
    }
    if (index < 0 || index >= static_cast<int>(nodeView_.nodes.size())) {
        return {};
    }
    const auto& p = nodeView_.nodes[static_cast<std::size_t>(index)].pos;
    return {host_.frame.mmToPxX(p.x), host_.frame.mmToPxY(-p.y)};
}

void NodeEditController::refresh() {
    fadeView_.reset();
    if (host_.activeTool() == QStringLiteral("fade.axis") && host_.documentOpen() &&
        host_.frame.px_per_mm > 0.0) {
        nodeView_ = {};
        nodeSel_.clear();
        host_.canvas.setNodeOutline({}, true);
        const auto target = host_.singleTarget();
        if (target) {
            fadeView_ = fadeAxisView(host_.session.project(), *target);
        }
        if (!fadeView_) {
            host_.canvas.setNodeHandles({}, {});
            host_.canvas.setNodeGuides({});
            return;
        }
        fadeObject_ = *target;
        QVector<QPointF> pts;
        for (int i = 0; i < 4; ++i) {
            pts.push_back(handlePx(i));
        }
        host_.canvas.setNodeHandles(pts, {});
        const QPointF a = pts[0];
        const QPointF b = pts[1];
        host_.canvas.setNodeGuides({QLineF(a, b), QLineF(pts[2], pts[3])});
        return;
    }
    const auto target = host_.activeTool() == QStringLiteral("node.edit") && host_.documentOpen()
                            ? host_.singleTarget()
                            : std::nullopt;
    host_.canvas.setNodeOutline({}, true);
    if (!target || host_.frame.px_per_mm <= 0.0) {
        nodeView_ = {};
        nodeSel_.clear();
        nodeObject_ = 0;
        host_.canvas.setNodeHandles({}, {});
        host_.canvas.setNodeGuides({});
        return;
    }
    auto view = nodeView(host_.session.project(), *target);
    if (!view) {
        nodeView_ = {};
        nodeSel_.clear();
        nodeObject_ = 0;
        host_.canvas.setNodeHandles({}, {});
        host_.notice(fromUtf8(view.error().message), true);
        return;
    }
    if (nodeObject_ != *target || view->nodes.size() != nodeView_.nodes.size()) {
        nodeSel_.clear(); // autre objet ou contour modifié : l'ancienne sélection n'a plus de sens
    }
    nodeObject_ = *target;
    nodeView_ = std::move(*view);
    QVector<QPointF> pts;
    pts.reserve(static_cast<qsizetype>(nodeView_.nodes.size()));
    for (std::size_t i = 0; i < nodeView_.nodes.size(); ++i) {
        pts.push_back(handlePx(static_cast<int>(i)));
    }
    host_.canvas.setNodeHandles(pts, QVector<int>(nodeSel_.begin(), nodeSel_.end()));
    QVector<QLineF> rungs;
    for (const auto& [a, b] : nodeView_.rungs) {
        rungs.push_back(QLineF(host_.frame.mmToPxX(a.x), host_.frame.mmToPxY(-a.y),
                               host_.frame.mmToPxX(b.x), host_.frame.mmToPxY(-b.y)));
    }
    host_.canvas.setNodeGuides(rungs);
}

void NodeEditController::onNodePressed(int index, Qt::KeyboardModifiers modifiers) {
    segmentDrag_.reset();
    if (fadeView_) {
        fadeHandle_ = index;
        static const char* const names[] = {"début de la transition", "fin de la transition",
                                            "déplacement de l'axe", "rotation de l'axe"};
        if (index >= 0 && index <= 3) {
            host_.notice(
                tr("Fondu : %1. Maj = extrémités symétriques. %2")
                    .arg(QString::fromUtf8(names[index]), fromUtf8(fadeSummary(*fadeView_))),
                false);
        }
        return;
    }
    if (index < 0 || index >= static_cast<int>(nodeView_.nodes.size())) {
        return;
    }
    const bool additive = modifiers & (Qt::ShiftModifier | Qt::ControlModifier);
    const auto inSel = std::find(nodeSel_.begin(), nodeSel_.end(), index);
    const auto sameChain = [&](int other) {
        const auto& a = nodeView_.nodes[static_cast<std::size_t>(other)].ref;
        const auto& b = nodeView_.nodes[static_cast<std::size_t>(index)].ref;
        return a.set == b.set && a.path == b.path;
    };
    if (additive) {
        if (inSel != nodeSel_.end()) {
            nodeSel_.erase(inSel); // Maj/Ctrl + clic sur un nœud déjà choisi le retire
        } else {
            if (!nodeSel_.empty() && !sameChain(nodeSel_.front())) {
                nodeSel_.clear(); // un déplacement groupé reste dans un seul contour
            }
            nodeSel_.push_back(index);
        }
    } else if (inSel == nodeSel_.end()) {
        nodeSel_ = {index}; // sur un nœud déjà choisi, on garde le groupe pour le glisser
    }
    QVector<QPointF> pts;
    for (std::size_t i = 0; i < nodeView_.nodes.size(); ++i) {
        pts.push_back(handlePx(static_cast<int>(i)));
    }
    host_.canvas.setNodeHandles(pts, QVector<int>(nodeSel_.begin(), nodeSel_.end()));
    QString message = tr("%n nœud(s) sélectionné(s). Glissez pour déplacer, double-cliquez un "
                         "segment pour insérer un nœud.",
                         "", static_cast<int>(nodeSel_.size()));
    if (const auto width = satinWidthSummary(nodeView_); !width.empty()) {
        message += QStringLiteral(" ") + fromUtf8(width) + QLatin1Char('.');
    }
    host_.notice(message, false);
}

void NodeEditController::onNodeDragMoved(QPointF deltaPx) {
    if (segmentDrag_ && host_.frame.px_per_mm > 0.0) {
        const PointMm delta{deltaPx.x() / host_.frame.px_per_mm,
                            -deltaPx.y() / host_.frame.px_per_mm};
        const auto preview =
            previewSegmentReshape(host_.session.project(), nodeObject_, *segmentDrag_, delta);
        const auto& outlines = preview ? *preview : nodeView_.outlines;
        QVector<QVector<QPointF>> polylines;
        for (const auto& line : outlines) {
            QVector<QPointF> px;
            px.reserve(static_cast<qsizetype>(line.size()));
            for (const auto& p : line) {
                px.push_back({host_.frame.mmToPxX(p.x), host_.frame.mmToPxY(-p.y)});
            }
            polylines.push_back(std::move(px));
        }
        host_.canvas.setNodeOutline(polylines, preview.has_value());
        host_.notice(
            preview ? tr("Courbure : %1 mm, %2 mm").arg(delta.x, 0, 'f', 1).arg(delta.y, 0, 'f', 1)
                    : tr("Remodelage impossible : le contour se croiserait."),
            !preview);
        return;
    }
    if (fadeView_) {
        if (fadeHandle_ < 0 || host_.frame.px_per_mm <= 0.0) {
            return;
        }
        const QPointF at = handlePx(fadeHandle_) + deltaPx;
        const PointMm pointer =
            screenToModel(host_.frame.pxToMmX(at.x()), host_.frame.pxToMmY(at.y()));
        const bool symmetric = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
        const auto preview =
            previewFadeDrag(host_.session.project(), fadeObject_,
                            static_cast<FadeHandle>(fadeHandle_), pointer, symmetric);
        QVector<QVector<QPointF>> lines;
        const auto px = [&](PointMm p) {
            return QPointF(host_.frame.mmToPxX(p.x), host_.frame.mmToPxY(-p.y));
        };
        const FadeAxisView& shown = preview ? *preview : *fadeView_;
        lines.push_back({px(shown.start), px(shown.end)});
        lines.push_back({px(shown.centre), px(shown.rotate)});
        host_.canvas.setNodeOutline(lines, preview.has_value());
        host_.notice(preview ? fromUtf8(fadeSummary(shown))
                             : tr("Déplacement impossible : l'axe deviendrait inutilisable."),
                     !preview);
        return;
    }
    if (nodeSel_.empty() || host_.frame.px_per_mm <= 0.0) {
        return;
    }
    std::vector<NodeRef> refs;
    for (const int i : nodeSel_) {
        refs.push_back(nodeView_.nodes[static_cast<std::size_t>(i)].ref);
    }
    const PointMm delta{deltaPx.x() / host_.frame.px_per_mm, -deltaPx.y() / host_.frame.px_per_mm};
    const auto preview = previewNodeMove(host_.session.project(), nodeObject_, refs, delta, false);
    const auto& outlines = preview ? *preview : nodeView_.outlines;
    QVector<QVector<QPointF>> polylines;
    for (const auto& line : outlines) {
        QVector<QPointF> px;
        px.reserve(static_cast<qsizetype>(line.size()));
        for (const auto& p : line) {
            px.push_back({host_.frame.mmToPxX(p.x), host_.frame.mmToPxY(-p.y)});
        }
        polylines.push_back(std::move(px));
    }
    host_.canvas.setNodeOutline(polylines, preview.has_value());
    host_.notice(
        preview ? tr("Déplacement : %1 mm, %2 mm").arg(delta.x, 0, 'f', 1).arg(delta.y, 0, 'f', 1)
                : tr("Déplacement impossible : le contour se croiserait."),
        !preview);
}

void NodeEditController::onNodeDragFinished(QPointF deltaPx) {
    if (segmentDrag_) {
        const auto hit = *segmentDrag_;
        segmentDrag_.reset();
        host_.canvas.setNodeOutline({}, true);
        if (host_.frame.px_per_mm <= 0.0) {
            return;
        }
        const PointMm delta{deltaPx.x() / host_.frame.px_per_mm,
                            -deltaPx.y() / host_.frame.px_per_mm};
        const auto r = reshapeSegment(host_.session, nodeObject_, hit, delta);
        host_.notice(r.ok ? tr("Segment courbé.") : tr("Nœuds : %1").arg(fromUtf8(r.message)),
                     !r.ok);
        if (!r.ok) {
            refresh();
        }
        return;
    }
    if (fadeView_) {
        host_.canvas.setNodeOutline({}, true);
        if (fadeHandle_ < 0 || host_.frame.px_per_mm <= 0.0) {
            return;
        }
        const QPointF at = handlePx(fadeHandle_) + deltaPx;
        const PointMm pointer =
            screenToModel(host_.frame.pxToMmX(at.x()), host_.frame.pxToMmY(at.y()));
        const bool symmetric = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
        const auto r = dragFadeHandle(host_.session, fadeObject_,
                                      static_cast<FadeHandle>(fadeHandle_), pointer, symmetric);
        fadeHandle_ = -1;
        if (!r.ok) {
            host_.notice(tr("Fondu : %1").arg(fromUtf8(r.message)), true);
            refresh(); // les poignées reviennent à leur place
            return;
        }
        if (const auto view = fadeAxisView(host_.session.project(), fadeObject_)) {
            host_.notice(fromUtf8(fadeSummary(*view)), false);
        }
        return;
    }
    host_.canvas.setNodeOutline({}, true);
    if (nodeSel_.empty() || host_.frame.px_per_mm <= 0.0) {
        return;
    }
    std::vector<NodeRef> refs;
    for (const int i : nodeSel_) {
        refs.push_back(nodeView_.nodes[static_cast<std::size_t>(i)].ref);
    }
    const PointMm delta{deltaPx.x() / host_.frame.px_per_mm, -deltaPx.y() / host_.frame.px_per_mm};
    const auto r = moveNodes(host_.session, nodeObject_, refs, delta, false);
    if (!r.ok) {
        host_.notice(tr("Nœuds : %1").arg(fromUtf8(r.message)), true);
        refresh(); // les poignées reviennent à leur place
        return;
    }
    host_.notice(tr("%n nœud(s) déplacé(s).", "", static_cast<int>(refs.size())), false);
}

void NodeEditController::onNodeInsert(QPointF scenePx) {
    if (nodeView_.nodes.empty() || host_.frame.px_per_mm <= 0.0) {
        return;
    }
    const PointMm at =
        screenToModel(host_.frame.pxToMmX(scenePx.x()), host_.frame.pxToMmY(scenePx.y()));
    const double zoom = std::max(0.01, host_.canvas.transform().m11());
    const auto hit = nearestSegment(nodeView_, at, 8.0 / (zoom * host_.frame.px_per_mm));
    if (!hit) {
        host_.notice(tr("Double-cliquez sur un segment du contour pour insérer un nœud."), false);
        return;
    }
    const auto r = insertNode(host_.session, nodeObject_, *hit);
    host_.notice(r.ok ? tr("Nœud inséré.") : tr("Nœuds : %1").arg(fromUtf8(r.message)), !r.ok);
}

} // namespace openstitch::desktop_v2
