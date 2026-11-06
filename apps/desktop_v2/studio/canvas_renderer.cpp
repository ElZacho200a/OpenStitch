// SPDX-License-Identifier: Apache-2.0
#include "canvas_renderer.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>
#include <algorithm>
#include <cmath>

namespace openstitch::desktop_v2::studio {
namespace {
using Vertices = std::vector<StudioVertex>;

void rectangle(Vertices& out, double x0, double y0, double x1, double y1, QColor color) {
    const auto vertex = [color](double x, double y) {
        return StudioVertex{static_cast<float>(x),
                            static_cast<float>(y),
                            static_cast<std::uint8_t>(color.red()),
                            static_cast<std::uint8_t>(color.green()),
                            static_cast<std::uint8_t>(color.blue()),
                            255};
    };
    out.insert(out.end(), {vertex(x0, y0), vertex(x1, y0), vertex(x0, y1), vertex(x0, y1),
                           vertex(x1, y0), vertex(x1, y1)});
}

void border(Vertices& out, const stitch_render::RectMm& r, double width, QColor color) {
    rectangle(out, r.x0, r.y0, r.x1, r.y0 + width, color);
    rectangle(out, r.x0, r.y1 - width, r.x1, r.y1, color);
    rectangle(out, r.x0, r.y0, r.x0 + width, r.y1, color);
    rectangle(out, r.x1 - width, r.y0, r.x1, r.y1, color);
}

void appendGeometry(QSGNode& parent, const Vertices& vertices) {
    // Bounded non-indexed batches; no assumptions about 16-bit index support.
    constexpr std::size_t batchSize = 60'000;
    for (std::size_t first = 0; first < vertices.size(); first += batchSize) {
        const auto count = std::min(batchSize, vertices.size() - first);
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(),
                                         static_cast<int>(count));
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::StaticPattern);
        auto* data = geometry->vertexDataAsColoredPoint2D();
        for (std::size_t i = 0; i < count; ++i) {
            const auto& v = vertices[first + i];
            data[i].set(v.x, v.y, v.r, v.g, v.b, v.a);
        }
        auto* node = new QSGGeometryNode;
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(new QSGVertexColorMaterial);
        node->setFlag(QSGNode::OwnsMaterial);
        parent.appendChildNode(node);
    }
}

void clearChildren(QSGNode& node) {
    while (auto* child = node.firstChild()) {
        node.removeChildNode(child);
        delete child;
    }
}

struct GeometryRoot : QSGNode {
    StudioResourceState resources;
    QSGTransformNode* world = new QSGTransformNode;
    QSGNode* overlay = new QSGNode;
    GeometryRoot() {
        appendChildNode(world);
        appendChildNode(overlay);
    }
};

class GeometryBackend final : public CanvasRenderBackend {
public:
    CanvasRenderResult synchronize(QSGNode* previous, const CanvasRenderState& state,
                                   QQuickWindow&) const override {
        auto* root = previous ? static_cast<GeometryRoot*>(previous) : new GeometryRoot;
        const auto dirty =
            root->resources.observe(state.scene, state.selectionRevision, state.camera.revision());
        double elapsed = 0;
        if (dirty.geometry) {
            QElapsedTimer timer;
            timer.start();
            clearChildren(*root->world);
            if (state.scene) {
                Vertices frame;
                border(frame, state.scene->hoop, 0.25, QColor(92, 96, 106));
                appendGeometry(*root->world, frame);
                appendGeometry(*root->world, state.scene->vertices);
            }
            elapsed = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        }
        if (dirty.camera || dirty.geometry) {
            QMatrix4x4 matrix;
            const auto offset = state.camera.offset();
            matrix.translate(static_cast<float>(offset.x), static_cast<float>(offset.y));
            matrix.scale(static_cast<float>(state.camera.scale()));
            root->world->setMatrix(matrix);
        }
        if (dirty.selection || dirty.camera || dirty.geometry) {
            clearChildren(*root->overlay);
            if (state.scene && !state.selection.empty()) {
                Vertices boxes;
                boxes.reserve(state.selection.size() * 24);
                for (const auto& target : state.selection) {
                    if (target.kind != TargetKind::EmbroideryObject) {
                        continue;
                    }
                    if (const auto bounds = state.scene->boundsOf(target.id)) {
                        const auto a = state.camera.worldToScreen({bounds->x0, bounds->y0});
                        const auto b = state.camera.worldToScreen({bounds->x1, bounds->y1});
                        border(boxes, {a.x - 4, a.y - 4, b.x + 4, b.y + 4}, 1.5,
                               QColor(229, 99, 137));
                    }
                }
                appendGeometry(*root->overlay, boxes);
            }
        }
        return {root, dirty.geometry && state.scene != nullptr, elapsed};
    }
};

struct SoftwareRoot : QSGSimpleTextureNode {
    StudioResourceState resources;
    QSizeF viewport;
    double dpr{0};
    SoftwareRoot() { setOwnsTexture(true); }
};

class RasterBackend final : public CanvasRenderBackend {
public:
    CanvasRenderResult synchronize(QSGNode* previous, const CanvasRenderState& state,
                                   QQuickWindow& window) const override {
        auto* root = previous ? static_cast<SoftwareRoot*>(previous) : new SoftwareRoot;
        const auto dirty =
            root->resources.observe(state.scene, state.selectionRevision, state.camera.revision());
        if (!dirty.geometry && !dirty.camera && !dirty.selection &&
            root->viewport == state.viewport && root->dpr == state.devicePixelRatio) {
            return {root, false, 0};
        }
        QElapsedTimer timer;
        timer.start();
        const auto size = state.viewport;
        // Explicit software fallback: rasterize display-only geometry, never regenerate stitches.
        QImage image(
            QSize(std::max(1, static_cast<int>(std::ceil(size.width() * state.devicePixelRatio))),
                  std::max(1, static_cast<int>(std::ceil(size.height() * state.devicePixelRatio)))),
            QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(state.devicePixelRatio);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto offset = state.camera.offset();
        painter.translate(offset.x, offset.y);
        painter.scale(state.camera.scale(), state.camera.scale());
        if (state.scene) {
            const auto& h = state.scene->hoop;
            painter.setPen(QPen(QColor(92, 96, 106), 0.25));
            painter.drawRect(QRectF(h.x0, h.y0, h.width(), h.height()));
            QPen pen;
            pen.setWidthF(state.scene->threadWidthMm);
            pen.setCapStyle(Qt::FlatCap);
            for (const auto& segment : state.scene->segments) {
                const auto& s = segment.thread;
                pen.setColor(QColor(s.rgb[0], s.rgb[1], s.rgb[2]));
                painter.setPen(pen);
                painter.drawLine(QPointF(s.ax, s.ay), QPointF(s.bx, s.by));
            }
            painter.resetTransform();
            painter.setPen(QPen(QColor(229, 99, 137), 1.5));
            for (const auto& target : state.selection) {
                if (target.kind != TargetKind::EmbroideryObject) {
                    continue;
                }
                if (const auto b = state.scene->boundsOf(target.id)) {
                    const auto a = state.camera.worldToScreen({b->x0, b->y0});
                    const auto z = state.camera.worldToScreen({b->x1, b->y1});
                    painter.drawRect(QRectF(QPointF(a.x - 4, a.y - 4), QPointF(z.x + 4, z.y + 4)));
                }
            }
        }
        painter.end();
        root->setTexture(window.createTextureFromImage(image));
        root->setRect(QRectF(QPointF(0, 0), size));
        root->viewport = size;
        root->dpr = state.devicePixelRatio;
        return {root, dirty.geometry && state.scene != nullptr,
                static_cast<double>(timer.nsecsElapsed()) / 1e6};
    }
};
} // namespace

const CanvasRenderBackend& sceneGraphBackend() {
    static const GeometryBackend renderer;
    return renderer;
}
const CanvasRenderBackend& softwareBackend() {
    static const RasterBackend renderer;
    return renderer;
}

} // namespace openstitch::desktop_v2::studio
