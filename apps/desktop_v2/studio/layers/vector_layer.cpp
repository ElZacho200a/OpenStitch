// SPDX-License-Identifier: Apache-2.0
#include "vector_layer.hpp"

#include <QImage>
#include <QPainter>
#include <QPen>
#include <QTransform>
#include <QtGui/private/qtriangulator_p.h>
#include <algorithm>
#include <array>
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

// Apparence d'une forme, commune au raster logiciel et aux triangles GPU.
constexpr double kOutlineMm = 0.18;
int fillAlpha(const VectorShape& shape) {
    return shape.sourceRegion != 0 ? 235 : 200;
}
bool dashedOutline(const VectorShape& shape) {
    return shape.sourceRegion == 0; // forme dessinée à la main : contour pointillé distinct
}

StudioVertex premultiplied(QPointF p, const QColor& color, int alpha) {
    const auto mul = [alpha](int c) { return static_cast<std::uint8_t>(c * alpha / 255); };
    return {static_cast<float>(p.x()), static_cast<float>(p.y()), mul(color.red()),
            mul(color.green()),        mul(color.blue()),         static_cast<std::uint8_t>(alpha)};
}

// Unité de travail des aplatissements : 10 µm. La triangulation de Qt arrondit à 1/32 d'unité et
// découpe les courbes à environ une unité près, soit bien sous le pixel au zoom maximal.
constexpr double kUnitsPerMm = 100.0;

// Remplissage pair-impair (trous compris) en triangles.
void appendFill(std::vector<StudioVertex>& out, const VectorShape& shape) {
    const QTriangleSet set =
        qTriangulate(shape.path, QTransform::fromScale(kUnitsPerMm, kUnitsPerMm));
    const auto& v = set.vertices;
    const int count = set.indices.size();
    const bool wide = set.indices.type() == QVertexIndexVector::UnsignedInt;
    const auto* idx32 = static_cast<const quint32*>(set.indices.data());
    const auto* idx16 = static_cast<const quint16*>(set.indices.data());
    const int alpha = fillAlpha(shape);
    out.reserve(out.size() + static_cast<std::size_t>(count));
    for (int i = 0; i + 2 < count; i += 3) {
        for (int k = 0; k < 3; ++k) {
            const auto at = static_cast<qsizetype>(wide ? idx32[i + k] : idx16[i + k]);
            const QPointF p(v[2 * at] / kUnitsPerMm, v[2 * at + 1] / kUnitsPerMm);
            out.push_back(premultiplied(p, shape.color, alpha));
        }
    }
}

// Un trait [a, b] de largeur `width`, prolongé d'une demi-largeur à chaque bout (bouts carrés :
// les traits successifs se recouvrent aux sommets, sans trou aux angles).
void appendStroke(std::vector<StudioVertex>& out, QPointF a, QPointF b, double width,
                  const QColor& color) {
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double len = std::hypot(dx, dy);
    if (len <= 0.0) {
        return;
    }
    const double h = width / 2.0;
    const QPointF along(dx / len * h, dy / len * h);
    const QPointF across(-along.y(), along.x());
    const QPointF s = a - along;
    const QPointF e = b + along;
    const std::array<QPointF, 6> quad{s + across, s - across, e + across,
                                      e + across, s - across, e - across};
    for (const QPointF& p : quad) {
        out.push_back(premultiplied(p, color, 255));
    }
}

// Contour aplati, plein ou pointillé comme `Qt::DashLine` (4 largeurs tracées, 2 laissées), le
// motif reprenant au début de chaque sous-chemin comme pour QPainter.
void appendOutline(std::vector<StudioVertex>& out, const VectorShape& shape) {
    const QColor color = shape.color.darker(160);
    const bool dashed = dashedOutline(shape);
    const double on = 4.0 * kOutlineMm;
    const double off = 2.0 * kOutlineMm;
    const QTransform up = QTransform::fromScale(kUnitsPerMm, kUnitsPerMm);
    for (const QPolygonF& poly : shape.path.toSubpathPolygons(up)) {
        double phase = 0.0; // distance parcourue dans le motif courant
        for (qsizetype i = 0; i + 1 < poly.size(); ++i) {
            const QPointF a = poly[i] / kUnitsPerMm;
            const QPointF b = poly[i + 1] / kUnitsPerMm;
            if (!dashed) {
                appendStroke(out, a, b, kOutlineMm, color);
                continue;
            }
            const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
            double t = 0.0;
            while (t < len) {
                const bool drawing = phase < on;
                const double left = drawing ? on - phase : on + off - phase;
                const double step = std::min(left, len - t);
                if (drawing) {
                    const QPointF p = a + (b - a) * (t / len);
                    const QPointF q = a + (b - a) * ((t + step) / len);
                    appendStroke(out, p, q, kOutlineMm, color);
                }
                t += step;
                phase += step;
                if (phase >= on + off) {
                    phase = 0.0;
                }
            }
        }
    }
}

} // namespace

std::shared_ptr<const VectorLayer> VectorLayer::build(const document::Project& project) {
    return build(project.vector_objects);
}

std::shared_ptr<const VectorLayer>
VectorLayer::build(const std::vector<document::VectorObject>& shapes) {
    auto layer = std::make_shared<VectorLayer>();
    QRectF all;
    for (const auto& object : shapes) {
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
    layer->area_ = all.adjusted(-margin, -margin, margin, margin);
    for (const auto& shape : layer->shapes_) {
        appendFill(layer->triangles_, shape);
        appendOutline(layer->triangles_, shape);
    }
    return layer;
}

const OverlayImage& VectorLayer::image() const {
    std::call_once(rasterOnce_, [this] { rasterize(); });
    return image_;
}

qint64 VectorLayer::bytes() const {
    const auto geometry = static_cast<qint64>(triangles_.size() * sizeof(StudioVertex));
    return geometry + (rasterized_.load() ? image_.image.sizeInBytes() : 0);
}

void VectorLayer::rasterize() const {
    if (shapes_.empty() || area_.width() <= 0 || area_.height() <= 0) {
        rasterized_ = true;
        return;
    }
    const QRectF& area = area_;
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
    for (const auto& shape : shapes_) {
        QColor fill = shape.color;
        fill.setAlpha(fillAlpha(shape));
        painter.setBrush(fill);
        QPen outline(shape.color.darker(160), kOutlineMm);
        outline.setStyle(dashedOutline(shape) ? Qt::DashLine : Qt::SolidLine);
        painter.setPen(outline);
        painter.drawPath(shape.path);
    }
    painter.end();
    image_.image = std::move(image);
    image_.x0 = area.x();
    image_.y0 = area.y();
    image_.widthMm = area.width();
    image_.heightMm = area.height();
    rasterized_ = true;
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
