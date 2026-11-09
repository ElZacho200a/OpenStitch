// SPDX-License-Identifier: Apache-2.0
#include "vector_layer.hpp"

#include <QImage>
#include <QPainter>
#include <QPen>
#include <algorithm>
#include <cmath>
#include <utility>

namespace openstitch::desktop_v2::studio {

namespace {

QPointF toScene(const Vec2um& p) {
    return {p.x.value / 1000.0, -p.y.value / 1000.0};
}

// Contour (polyligne ou cubiques) ; tangentes relatives au nœud, en µm.
void appendPath(QPainterPath& out, const geometry::Path& path, std::vector<QPointF>& nodes) {
    const auto& n = path.nodes;
    if (n.size() < 2) {
        return;
    }
    for (const auto& node : n) {
        nodes.push_back(toScene(node.pos));
    }
    out.moveTo(toScene(n.front().pos));
    const std::size_t segments = path.closed ? n.size() : n.size() - 1;
    for (std::size_t i = 0; i < segments; ++i) {
        const auto& a = n[i];
        const auto& b = n[(i + 1) % n.size()];
        if (a.tan_out || b.tan_in) {
            const Vec2um c1{Micrometers{a.pos.x.value + (a.tan_out ? a.tan_out->x.value : 0)},
                            Micrometers{a.pos.y.value + (a.tan_out ? a.tan_out->y.value : 0)}};
            const Vec2um c2{Micrometers{b.pos.x.value + (b.tan_in ? b.tan_in->x.value : 0)},
                            Micrometers{b.pos.y.value + (b.tan_in ? b.tan_in->y.value : 0)}};
            out.cubicTo(toScene(c1), toScene(c2), toScene(b.pos));
        } else {
            out.lineTo(toScene(b.pos));
        }
    }
    if (path.closed) {
        out.closeSubpath();
    }
}

} // namespace

std::shared_ptr<const VectorLayer> VectorLayer::build(const document::Project& project) {
    auto layer = std::make_shared<VectorLayer>();
    QRectF all;
    for (const auto& object : project.vector_objects) {
        if (!object.visible) {
            continue;
        }
        VectorShape shape;
        shape.id = object.id.value;
        shape.sourceRegion = object.source_region ? object.source_region->value : 0;
        shape.color = QColor(object.rgb[0], object.rgb[1], object.rgb[2]);
        shape.locked = object.locked;
        shape.path.setFillRule(Qt::OddEvenFill);
        for (const auto& set : object.paths) {
            appendPath(shape.path, set.outer, shape.nodes);
            for (const auto& hole : set.holes) {
                appendPath(shape.path, hole, shape.nodes);
            }
        }
        if (shape.path.isEmpty()) {
            continue;
        }
        shape.bounds = shape.path.boundingRect();
        all = all.isNull() ? shape.bounds : all.united(shape.bounds);
        layer->shapeIndex_.emplace(shape.id, layer->shapes_.size());
        layer->shapes_.push_back(std::move(shape));
    }
    if (layer->shapes_.empty() || all.width() <= 0 || all.height() <= 0) {
        return layer;
    }
    const double margin = 2.0; // mm : le trait et les nœuds débordent un peu
    const QRectF area = all.adjusted(-margin, -margin, margin, margin);
    // Résolution bornée : une seule image, quelle que soit la taille du motif.
    layer->pxPerMm_ = std::clamp(4096.0 / std::max(area.width(), area.height()), 1.0, 12.0);
    const double pxPerMm = layer->pxPerMm_;
    QImage image(std::max(1, static_cast<int>(std::ceil(area.width() * pxPerMm))),
                 std::max(1, static_cast<int>(std::ceil(area.height() * pxPerMm))),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(pxPerMm, pxPerMm);
    painter.translate(-area.topLeft());
    for (const auto& shape : layer->shapes_) {
        QColor fill = shape.color;
        fill.setAlpha(shape.sourceRegion != 0 ? 235 : 200);
        painter.setBrush(fill);
        QPen outline(shape.color.darker(160), 0.18);
        // Forme sans région d'origine (dessinée à la main) : contour pointillé distinct.
        outline.setStyle(shape.sourceRegion != 0 ? Qt::SolidLine : Qt::DashLine);
        painter.setPen(outline);
        painter.drawPath(shape.path);
    }
    painter.end();
    layer->image_.image = std::move(image);
    layer->image_.x0 = area.x();
    layer->image_.y0 = area.y();
    layer->image_.widthMm = area.width();
    layer->image_.heightMm = area.height();
    return layer;
}

std::shared_ptr<const OverlayImage>
VectorLayer::selectionOverlay(const std::vector<std::uint64_t>& selectedShapes) const {
    if (selectedShapes.empty() || shapes_.empty() || image_.image.isNull()) {
        return {};
    }
    std::vector<std::size_t> selectedIndices;
    selectedIndices.reserve(selectedShapes.size());
    for (const auto id : selectedShapes) {
        if (const auto found = shapeIndex_.find(id); found != shapeIndex_.end()) {
            selectedIndices.push_back(found->second);
        }
    }
    if (selectedIndices.empty()) {
        return {};
    }
    // Preserve painter order for overlapping selected shapes, regardless of selection order.
    std::sort(selectedIndices.begin(), selectedIndices.end());
    selectedIndices.erase(std::unique(selectedIndices.begin(), selectedIndices.end()),
                          selectedIndices.end());
    QRectF bounds = shapes_[selectedIndices.front()].bounds;
    for (const auto index : selectedIndices) {
        bounds = bounds.united(shapes_[index].bounds);
    }

    // Ne rasteriser que le voisinage des contours et poignées effectivement sélectionnés.
    // Conserver l'échelle px/mm de l'image de fond pour un rendu superposable.
    constexpr double marginMm = 2.0;
    const QRectF full(image_.x0, image_.y0, image_.widthMm, image_.heightMm);
    const QRectF area = bounds.adjusted(-marginMm, -marginMm, marginMm, marginMm).intersected(full);
    if (area.isEmpty()) {
        return {};
    }
    QImage image(std::max(1, static_cast<int>(std::ceil(area.width() * pxPerMm_))),
                 std::max(1, static_cast<int>(std::ceil(area.height() * pxPerMm_))),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(pxPerMm_, pxPerMm_);
    painter.translate(-area.topLeft());
    const QColor accent(229, 99, 137);
    for (const auto index : selectedIndices) {
        const auto& shape = shapes_[index];
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(accent, 0.45));
        painter.drawPath(shape.path);
        painter.setBrush(QColor(255, 255, 255));
        painter.setPen(QPen(accent, 0.2));
        const double half =
            std::clamp(std::min(shape.bounds.width(), shape.bounds.height()) / 40.0, 0.25, 0.8);
        for (const auto& node : shape.nodes) {
            painter.drawRect(QRectF(node.x() - half, node.y() - half, 2 * half, 2 * half));
        }
    }
    painter.end();
    auto out = std::make_shared<OverlayImage>();
    out->image = std::move(image);
    out->x0 = area.x();
    out->y0 = area.y();
    out->widthMm = area.width();
    out->heightMm = area.height();
    return out;
}

std::optional<std::uint64_t> VectorLayer::pick(double x, double y, double toleranceMm) const {
    const QPointF p(x, y);
    for (auto it = shapes_.rbegin(); it != shapes_.rend(); ++it) {
        if (it->locked || !it->bounds.adjusted(-toleranceMm, -toleranceMm, toleranceMm, toleranceMm)
                               .contains(p)) {
            continue;
        }
        if (it->path.contains(p)) {
            return it->id;
        }
        for (const auto& node : it->nodes) {
            if (std::hypot(node.x() - x, node.y() - y) <= toleranceMm) {
                return it->id;
            }
        }
    }
    return std::nullopt;
}

} // namespace openstitch::desktop_v2::studio
