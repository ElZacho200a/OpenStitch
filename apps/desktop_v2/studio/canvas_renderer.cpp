// SPDX-License-Identifier: Apache-2.0
#include "canvas_renderer.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGGeometryNode>
#include <QSGOpacityNode>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>
#include <algorithm>
#include <cmath>
#include <limits>

namespace openstitch::desktop_v2::studio {
QRectF comparisonSourceClip(const ComparisonLayers& layers, const ComparisonDisplay& display) {
    const auto& image = layers.source;
    return {image.x0, image.y0,
            image.widthMm * (display.curtain ? std::clamp(display.position, 0.0, 1.0) : 1.0),
            image.heightMm};
}
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

// Premultiplied translucent quad for grid/guide lines (vertex colour material is premultiplied).
void translucentRectangle(Vertices& out, double x0, double y0, double x1, double y1, QColor color,
                          int alpha) {
    const auto mul = [alpha](int c) { return static_cast<std::uint8_t>(c * alpha / 255); };
    const auto vertex = [&](double x, double y) {
        return StudioVertex{static_cast<float>(x), static_cast<float>(y),
                            mul(color.red()),      mul(color.green()),
                            mul(color.blue()),     static_cast<std::uint8_t>(alpha)};
    };
    out.insert(out.end(), {vertex(x0, y0), vertex(x1, y0), vertex(x0, y1), vertex(x0, y1),
                           vertex(x1, y0), vertex(x1, y1)});
}

// Grid step shown on screen: the requested step, widened by powers of five while lines would
// be closer than 7 px (never thousands of quads when zoomed out).
double effectiveGridStep(double stepMm, double scale) {
    double step = std::max(0.1, stepMm);
    while (step * scale < 7.0 && step < 1e6) {
        step *= 5.0;
    }
    return step;
}

void appendScreenOverlays(Vertices& out, const CanvasOverlays& overlays,
                          const CanvasRenderState& state) {
    const auto& camera = state.camera;
    const double w = state.viewport.width();
    const double h = state.viewport.height();
    if (overlays.grid) {
        const double step = effectiveGridStep(overlays.gridStepMm, camera.scale());
        const auto lo = camera.screenToWorld({0, 0});
        const auto hi = camera.screenToWorld({w, h});
        const QColor line(130, 136, 150);
        for (double x = std::ceil(lo.x / step) * step; x <= hi.x; x += step) {
            const double sx = camera.worldToScreen({x, 0}).x;
            const bool major = std::fmod(std::abs(std::round(x / step)), 5.0) == 0.0;
            translucentRectangle(out, sx - 0.5, 0, sx + 0.5, h, line, major ? 90 : 45);
        }
        for (double y = std::ceil(lo.y / step) * step; y <= hi.y; y += step) {
            const double sy = camera.worldToScreen({0, y}).y;
            const bool major = std::fmod(std::abs(std::round(y / step)), 5.0) == 0.0;
            translucentRectangle(out, 0, sy - 0.5, w, sy + 0.5, line, major ? 90 : 45);
        }
    }
    for (const double gx : overlays.guidesX) {
        const double sx = camera.worldToScreen({gx, 0}).x;
        translucentRectangle(out, sx - 0.5, 0, sx + 0.5, h, overlays.guideColor, 220);
    }
    for (const double gy : overlays.guidesY) {
        const double sy = camera.worldToScreen({0, gy}).y;
        translucentRectangle(out, 0, sy - 0.5, w, sy + 0.5, overlays.guideColor, 220);
    }
    for (const auto& jump : overlays.jumps) {
        const auto a = camera.worldToScreen({jump.ax, jump.ay});
        const auto b = camera.worldToScreen({jump.bx, jump.by});
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double len = std::hypot(dx, dy);
        if (len < 0.5) {
            continue;
        }
        const double nx = -dy / len * 0.5;
        const double ny = dx / len * 0.5;
        const auto v = [&](double x, double y) {
            return StudioVertex{static_cast<float>(x), static_cast<float>(y), 188, 125, 31, 200};
        };
        out.insert(out.end(),
                   {v(a.x + nx, a.y + ny), v(a.x - nx, a.y - ny), v(b.x + nx, b.y + ny),
                    v(b.x + nx, b.y + ny), v(a.x - nx, a.y - ny), v(b.x - nx, b.y - ny)});
    }
    for (const auto& marker : overlays.markers) {
        const auto p = camera.worldToScreen({marker.x, marker.y});
        border(out, {p.x - 9, p.y - 9, p.x + 9, p.y + 9}, 2.0, marker.color);
        rectangle(out, p.x - 1, p.y - 1, p.x + 1, p.y + 1, marker.color);
    }
}

constexpr std::size_t kBatchVertices = 60'000; // multiple of six: batches end on whole segments

// Stitch vertices in bounded batches, each wrapped so the simulation cursor can hide whole
// batches (opacity 0) and draw only the cursor batch's prefix as a small "tail" node.
struct StitchBatches {
    QSGNode* group = new QSGNode;
    QSGOpacityNode* all = new QSGOpacityNode;
    QSGNode* tail = new QSGNode;
    std::vector<QSGOpacityNode*> batches;
    StitchBatches() {
        all->appendChildNode(group);
        all->appendChildNode(tail);
    }
};

// Retained CX12 resources: settings and camera only change node state, never textures/vertices.
struct CompareNodes {
    QSGTransformNode* world = new QSGTransformNode;
    QSGOpacityNode* source = new QSGOpacityNode;
    QSGClipNode* clip = new QSGClipNode;
    QSGOpacityNode* segmentation = new QSGOpacityNode;
    QSGOpacityNode* vectors = new QSGOpacityNode;
    QSGOpacityNode* order = new QSGOpacityNode;
    std::shared_ptr<const ComparisonLayers> data;
    CompareNodes() {
        source->appendChildNode(clip);
        clip->setIsRectangular(true);
        world->appendChildNode(source);
        world->appendChildNode(segmentation);
        world->appendChildNode(vectors);
        world->appendChildNode(order);
    }
};
void appendCompareLines(QSGNode& parent, const std::vector<OverlayJump>& lines, QColor color) {
    Vertices vertices;
    for (const auto& line : lines) {
        const double dx = line.bx - line.ax, dy = line.by - line.ay;
        const double length = std::hypot(dx, dy);
        if (length <= 0)
            continue;
        const double nx = -dy / length * 0.08, ny = dx / length * 0.08;
        const auto v = [&](double x, double y) {
            return StudioVertex{static_cast<float>(x),
                                static_cast<float>(y),
                                static_cast<std::uint8_t>(color.red()),
                                static_cast<std::uint8_t>(color.green()),
                                static_cast<std::uint8_t>(color.blue()),
                                255};
        };
        vertices.insert(vertices.end(),
                        {v(line.ax + nx, line.ay + ny), v(line.ax - nx, line.ay - ny),
                         v(line.bx + nx, line.by + ny), v(line.bx + nx, line.by + ny),
                         v(line.ax - nx, line.ay - ny), v(line.bx - nx, line.by - ny)});
    }
    appendGeometry(parent, vertices);
}
void synchronizeComparison(CompareNodes& nodes, const CanvasOverlays& overlays,
                           QQuickWindow& window) {
    if (nodes.data != overlays.comparison) {
        nodes.data = overlays.comparison;
        clearChildren(*nodes.clip);
        clearChildren(*nodes.segmentation);
        clearChildren(*nodes.vectors);
        clearChildren(*nodes.order);
        if (nodes.data) {
            const auto texture = [&window](QSGNode& parent, const OverlayImage& image) {
                if (image.image.isNull())
                    return;
                auto* node = new QSGSimpleTextureNode;
                node->setOwnsTexture(true);
                node->setTexture(window.createTextureFromImage(image.image));
                node->setRect(QRectF(image.x0, image.y0, image.widthMm, image.heightMm));
                parent.appendChildNode(node);
            };
            texture(*nodes.clip, nodes.data->source);
            texture(*nodes.segmentation, nodes.data->segmentation);
            appendCompareLines(*nodes.vectors, nodes.data->vectors, QColor(0, 170, 230));
            appendCompareLines(*nodes.order, nodes.data->order, QColor(240, 170, 30));
        }
    }
    const auto& display = overlays.comparisonDisplay;
    nodes.source->setOpacity(display.source ? display.opacity : 0);
    nodes.segmentation->setOpacity(display.segmentation ? display.opacity : 0);
    nodes.vectors->setOpacity(display.vectors ? display.opacity : 0);
    nodes.order->setOpacity(display.order ? display.opacity : 0);
    if (nodes.data)
        nodes.clip->setClipRect(comparisonSourceClip(*nodes.data, display));
}

struct GeometryRoot : QSGNode {
    StudioResourceState resources;
    QSGTransformNode* world = new QSGTransformNode;
    QSGOpacityNode* frame = new QSGOpacityNode;
    StitchBatches stitches;
    QSGTransformNode* layers = new QSGTransformNode;
    QSGNode* overlay = new QSGNode;
    CompareNodes comparison;
    std::uint64_t overlayRevision{std::numeric_limits<std::uint64_t>::max()};
    GeometryRoot() {
        world->appendChildNode(frame);
        world->appendChildNode(stitches.all);
        appendChildNode(world);
        appendChildNode(layers);
        appendChildNode(comparison.world);
        appendChildNode(overlay);
    }
};

void applySimulation(GeometryRoot& root, const Vertices& vertices, std::size_t visibleSegments) {
    auto& st = root.stitches;
    clearChildren(*st.tail);
    const std::size_t shown = visibleSegments == CanvasOverlays::all
                                  ? vertices.size()
                                  : std::min(vertices.size(), visibleSegments * 6);
    for (std::size_t i = 0; i < st.batches.size(); ++i) {
        const std::size_t begin = i * kBatchVertices;
        const bool full = begin + kBatchVertices <= shown;
        st.batches[i]->setOpacity(full ? 1.0 : 0.0);
        if (!full && begin < shown) {
            const Vertices prefix(vertices.begin() + static_cast<std::ptrdiff_t>(begin),
                                  vertices.begin() + static_cast<std::ptrdiff_t>(shown));
            appendGeometry(*st.tail, prefix);
        }
    }
}

class GeometryBackend final : public CanvasRenderBackend {
public:
    CanvasRenderResult synchronize(QSGNode* previous, const CanvasRenderState& state,
                                   QQuickWindow& window) const override {
        auto* root = previous ? static_cast<GeometryRoot*>(previous) : new GeometryRoot;
        const auto dirty =
            root->resources.observe(state.scene, state.selectionRevision, state.camera.revision());
        const bool overlayDirty = root->overlayRevision != state.overlayRevision;
        const CanvasOverlays defaults;
        const CanvasOverlays& overlays = state.overlays ? *state.overlays : defaults;
        synchronizeComparison(root->comparison, overlays, window);
        double elapsed = 0;
        if (dirty.geometry) {
            QElapsedTimer timer;
            timer.start();
            clearChildren(*root->frame);
            clearChildren(*root->stitches.group);
            root->stitches.batches.clear();
            if (state.scene) {
                Vertices frame;
                border(frame, state.scene->hoop, 0.25, QColor(92, 96, 106));
                appendGeometry(*root->frame, frame);
                const auto& vertices = state.scene->vertices;
                for (std::size_t first = 0; first < vertices.size(); first += kBatchVertices) {
                    const auto count = std::min(kBatchVertices, vertices.size() - first);
                    auto* wrapper = new QSGOpacityNode;
                    const Vertices part(vertices.begin() + static_cast<std::ptrdiff_t>(first),
                                        vertices.begin() +
                                            static_cast<std::ptrdiff_t>(first + count));
                    appendGeometry(*wrapper, part);
                    root->stitches.group->appendChildNode(wrapper);
                    root->stitches.batches.push_back(wrapper);
                }
            }
            elapsed = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        }
        if (dirty.geometry || overlayDirty) {
            root->frame->setOpacity(overlays.showHoop ? 1.0 : 0.0);
            root->stitches.all->setOpacity(overlays.showStitches ? 1.0 : 0.0);
            if (state.scene) {
                applySimulation(*root, state.scene->vertices, overlays.visibleSegments);
            }
            clearChildren(*root->layers);
            for (const auto& layer : overlays.images) {
                if (layer.image.isNull() || layer.widthMm <= 0 || layer.heightMm <= 0) {
                    continue;
                }
                auto* node = new QSGSimpleTextureNode;
                node->setOwnsTexture(true);
                node->setTexture(window.createTextureFromImage(layer.image));
                node->setRect(QRectF(layer.x0, layer.y0, layer.widthMm, layer.heightMm));
                root->layers->appendChildNode(node);
            }
        }
        if (dirty.camera || dirty.geometry) {
            QMatrix4x4 matrix;
            const auto offset = state.camera.offset();
            matrix.translate(static_cast<float>(offset.x), static_cast<float>(offset.y));
            matrix.scale(static_cast<float>(state.camera.scale()));
            root->world->setMatrix(matrix);
            root->layers->setMatrix(matrix);
            root->comparison.world->setMatrix(matrix);
        }
        if (dirty.selection || dirty.camera || dirty.geometry || overlayDirty) {
            clearChildren(*root->overlay);
            Vertices screen;
            appendScreenOverlays(screen, overlays, state);
            if (state.scene && !state.selection.empty()) {
                for (const auto& target : state.selection) {
                    if (target.kind != TargetKind::EmbroideryObject) {
                        continue;
                    }
                    if (const auto bounds = state.scene->boundsOf(target.id)) {
                        const auto a = state.camera.worldToScreen({bounds->x0, bounds->y0});
                        const auto b = state.camera.worldToScreen({bounds->x1, bounds->y1});
                        border(screen, {a.x - 4, a.y - 4, b.x + 4, b.y + 4}, 1.5,
                               QColor(229, 99, 137));
                    }
                }
            }
            appendGeometry(*root->overlay, screen);
        }
        root->overlayRevision = state.overlayRevision;
        return {root, dirty.geometry && state.scene != nullptr, elapsed};
    }
};

struct SoftwareRoot : QSGSimpleTextureNode {
    StudioResourceState resources;
    QSizeF viewport;
    double dpr{0};
    std::uint64_t overlayRevision{std::numeric_limits<std::uint64_t>::max()};
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
            root->overlayRevision == state.overlayRevision && root->viewport == state.viewport &&
            root->dpr == state.devicePixelRatio) {
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
        const CanvasOverlays defaults;
        const CanvasOverlays& overlays = state.overlays ? *state.overlays : defaults;
        if (state.scene) {
            const auto& h = state.scene->hoop;
            if (overlays.showHoop) {
                painter.setPen(QPen(QColor(92, 96, 106), 0.25));
                painter.drawRect(QRectF(h.x0, h.y0, h.width(), h.height()));
            }
            QPen pen;
            pen.setWidthF(state.scene->threadWidthMm);
            pen.setCapStyle(Qt::FlatCap);
            const auto shown = overlays.showStitches ? std::min(state.scene->segments.size(),
                                                                overlays.visibleSegments)
                                                     : std::size_t{0};
            for (std::size_t i = 0; i < shown; ++i) {
                const auto& s = state.scene->segments[i].thread;
                pen.setColor(QColor(s.rgb[0], s.rgb[1], s.rgb[2]));
                painter.setPen(pen);
                painter.drawLine(QPointF(s.ax, s.ay), QPointF(s.bx, s.by));
            }
            for (const auto& layer : overlays.images) {
                if (!layer.image.isNull()) {
                    painter.drawImage(QRectF(layer.x0, layer.y0, layer.widthMm, layer.heightMm),
                                      layer.image);
                }
            }
            if (overlays.comparison) {
                const auto& layers = *overlays.comparison;
                const auto& display = overlays.comparisonDisplay;
                painter.save();
                painter.setOpacity(display.opacity);
                if (display.source && !layers.source.image.isNull()) {
                    painter.save();
                    painter.setClipRect(comparisonSourceClip(layers, display));
                    const auto& layer = layers.source;
                    painter.drawImage(QRectF(layer.x0, layer.y0, layer.widthMm, layer.heightMm),
                                      layer.image);
                    painter.restore();
                }
                if (display.segmentation && !layers.segmentation.image.isNull()) {
                    const auto& layer = layers.segmentation;
                    painter.drawImage(QRectF(layer.x0, layer.y0, layer.widthMm, layer.heightMm),
                                      layer.image);
                }
                const auto lines = [&painter](const std::vector<OverlayJump>& data, QColor color) {
                    painter.setPen(QPen(color, 0.16));
                    for (const auto& line : data)
                        painter.drawLine(QPointF(line.ax, line.ay), QPointF(line.bx, line.by));
                };
                if (display.vectors)
                    lines(layers.vectors, QColor(0, 170, 230));
                if (display.order)
                    lines(layers.order, QColor(240, 170, 30));
                painter.restore();
            }
            painter.resetTransform();
            const double vw = size.width();
            const double vh = size.height();
            if (overlays.grid) {
                const double step = effectiveGridStep(overlays.gridStepMm, state.camera.scale());
                const auto lo = state.camera.screenToWorld({0, 0});
                const auto hi = state.camera.screenToWorld({vw, vh});
                painter.setPen(QPen(QColor(130, 136, 150, 70), 1.0));
                for (double x = std::ceil(lo.x / step) * step; x <= hi.x; x += step) {
                    const double sx = state.camera.worldToScreen({x, 0}).x;
                    painter.drawLine(QPointF(sx, 0), QPointF(sx, vh));
                }
                for (double y = std::ceil(lo.y / step) * step; y <= hi.y; y += step) {
                    const double sy = state.camera.worldToScreen({0, y}).y;
                    painter.drawLine(QPointF(0, sy), QPointF(vw, sy));
                }
            }
            painter.setPen(QPen(overlays.guideColor, 1.0));
            for (const double gx : overlays.guidesX) {
                const double sx = state.camera.worldToScreen({gx, 0}).x;
                painter.drawLine(QPointF(sx, 0), QPointF(sx, vh));
            }
            for (const double gy : overlays.guidesY) {
                const double sy = state.camera.worldToScreen({0, gy}).y;
                painter.drawLine(QPointF(0, sy), QPointF(vw, sy));
            }
            painter.setPen(QPen(QColor(188, 125, 31, 200), 1.0));
            for (const auto& jump : overlays.jumps) {
                const auto a = state.camera.worldToScreen({jump.ax, jump.ay});
                const auto b = state.camera.worldToScreen({jump.bx, jump.by});
                painter.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
            }
            for (const auto& marker : overlays.markers) {
                const auto p = state.camera.worldToScreen({marker.x, marker.y});
                painter.setPen(QPen(marker.color, 2.0));
                painter.drawRect(QRectF(p.x - 9, p.y - 9, 18, 18));
            }
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
        root->overlayRevision = state.overlayRevision;
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
