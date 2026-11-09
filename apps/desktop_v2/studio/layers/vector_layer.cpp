// SPDX-License-Identifier: Apache-2.0
#include "vector_layer.hpp"

#include <QImage>
#include <QPainter>
#include <QPen>
#include <algorithm>
#include <cmath>

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
        layer->shapes_.push_back(std::move(shape));
    }
    if (layer->shapes_.empty() || all.width() <= 0 || all.height() <= 0) {
        return layer;
    }
    const double margin = 2.0; // mm : le trait et les nœuds débordent un peu
    const QRectF area = all.adjusted(-margin, -margin, margin, margin);
    // Résolution bornée : une seule image, quelle que soit la taille du motif.
    const double pxPerMm = std::clamp(4096.0 / std::max(area.width(), area.height()), 1.0, 12.0);
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
