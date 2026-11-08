// SPDX-License-Identifier: Apache-2.0
#include "canvas_renderer.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGOpacityNode>
#include <QSGOpaqueTextureMaterial>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QSGTextureMaterial>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <span>

#include "layers/region_layer.hpp"
#include "thread_textures.hpp"

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

struct RealisticLayer;

struct GeometryRoot : QSGNode {
    StudioResourceState resources;
    // Couche « regions » (sous les points) : sa matrice suit la caméra, ses textures la révision.
    RegionLayerNode* regions = new RegionLayerNode;
    std::uint64_t regionsCameraRevision{std::numeric_limits<std::uint64_t>::max()};
    RealisticLayer* realistic = nullptr; // créé à la première activation, enfant de `world`
    QSGTransformNode* world = new QSGTransformNode;
    QSGOpacityNode* frame = new QSGOpacityNode;
    StitchBatches stitches;
    SourceImageNodes source; // couche « sourceImage » (layers/source_image_layer.*)
    QSGTransformNode* layers = new QSGTransformNode;
    QSGNode* overlay = new QSGNode;
    std::uint64_t overlayRevision{std::numeric_limits<std::uint64_t>::max()};
    // Cached scene-graph layers are independent of the display-only overlay revision.
    // A visibility change must redraw even if the document, camera and overlays did not move.
    CanvasLayers displayedLayers;
    std::shared_ptr<const OverlayImage> displayedVector;
    GeometryRoot() {
        appendChildNode(regions);
        world->appendChildNode(frame);
        world->appendChildNode(stitches.all);
        appendChildNode(world);
        appendChildNode(layers);
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

// --- Rendu réaliste GPU --------------------------------------------------------------------
// Couture pour brancher plus tard des shaders (module qtshadertools) sans toucher au reste :
// aujourd'hui, uniquement les matériaux de texture standard de Qt Quick.
class ThreadMaterialProvider {
public:
    virtual ~ThreadMaterialProvider() = default;
    [[nodiscard]] virtual QSGMaterial* threadMaterial(QSGTexture& sprite) const = 0;
    [[nodiscard]] virtual QSGMaterial* shadowMaterial(QSGTexture& shadow) const = 0;
    [[nodiscard]] virtual QSGMaterial* fabricMaterial(QSGTexture& tile) const = 0;
};

class StandardTextureMaterials final : public ThreadMaterialProvider {
public:
    QSGMaterial* threadMaterial(QSGTexture& sprite) const override { return blended(sprite); }
    QSGMaterial* shadowMaterial(QSGTexture& shadow) const override { return blended(shadow); }
    QSGMaterial* fabricMaterial(QSGTexture& tile) const override {
        tile.setFiltering(QSGTexture::Linear);
        tile.setMipmapFiltering(QSGTexture::Linear);
        tile.setHorizontalWrapMode(QSGTexture::Repeat);
        tile.setVerticalWrapMode(QSGTexture::Repeat);
        auto* material = new QSGOpaqueTextureMaterial;
        material->setTexture(&tile);
        material->setFiltering(QSGTexture::Linear);
        material->setMipmapFiltering(QSGTexture::Linear);
        material->setHorizontalWrapMode(QSGTexture::Repeat);
        material->setVerticalWrapMode(QSGTexture::Repeat);
        return material;
    }

private:
    static QSGMaterial* blended(QSGTexture& texture) {
        texture.setFiltering(QSGTexture::Linear);
        texture.setMipmapFiltering(QSGTexture::Linear);
        texture.setHorizontalWrapMode(QSGTexture::ClampToEdge);
        texture.setVerticalWrapMode(QSGTexture::ClampToEdge);
        auto* material = new QSGTextureMaterial;
        material->setTexture(&texture);
        material->setFiltering(QSGTexture::Linear);
        material->setMipmapFiltering(QSGTexture::Linear);
        return material;
    }
};

QImage toImage(const PixelImage& pixels) {
    return QImage(pixels.rgba.data(), pixels.width, pixels.height, pixels.width * 4,
                  QImage::Format_RGBA8888_Premultiplied)
        .copy(); // détache des octets de la source
}

QSGTexture* makeTexture(QQuickWindow& window, const PixelImage& pixels) {
    QQuickWindow::CreateTextureOptions options;
    options.setFlag(QQuickWindow::TextureHasAlphaChannel);
    options.setFlag(QQuickWindow::TextureHasMipmaps);
    return window.createTextureFromImage(toImage(pixels), options);
}

QSGGeometryNode* texturedNode(const RealisticVertex* src, std::size_t count,
                              QSGMaterial* material) {
    auto* geometry =
        new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), static_cast<int>(count));
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    geometry->setVertexDataPattern(QSGGeometry::StaticPattern);
    static_assert(sizeof(QSGGeometry::TexturedPoint2D) == sizeof(RealisticVertex));
    std::copy_n(src, count, static_cast<RealisticVertex*>(geometry->vertexData()));
    auto* node = new QSGGeometryNode;
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

// Calque réaliste : tissu, puis tranches de runs dans l'ordre de couture (ombre portée puis
// fil), puis une « queue » pour le curseur de simulation. Toutes les ressources GPU (textures)
// sont détenues ici, donc détruites avec le scene graph.
struct RealisticLayer : QSGOpacityNode {
    QSGNode* fabric = new QSGNode;
    QSGNode* group = new QSGNode;
    QSGNode* tail = new QSGNode;
    std::vector<QSGOpacityNode*> chunkNodes;
    std::vector<RealisticRun> runs;
    std::vector<RealisticChunk> chunks;
    ThreadSpriteCache cache;
    std::map<std::uint32_t, std::unique_ptr<QSGTexture>> spriteTextures; // clé = couleur
    std::unique_ptr<QSGTexture> shadowTexture;
    std::unique_ptr<QSGTexture> fabricTexture;
    std::shared_ptr<const StudioSceneSnapshot> builtScene;
    RealisticStyle builtStyle;
    std::size_t shownSegments{std::numeric_limits<std::size_t>::max()};
    bool built{false};
    RealisticLayer() {
        appendChildNode(fabric);
        appendChildNode(group);
        appendChildNode(tail);
    }
    QSGTexture& spriteTexture(QQuickWindow& window, std::array<std::uint8_t, 3> rgb) {
        const std::uint32_t key = (static_cast<std::uint32_t>(rgb[0]) << 16) |
                                  (static_cast<std::uint32_t>(rgb[1]) << 8) | rgb[2];
        auto it = spriteTextures.find(key);
        if (it == spriteTextures.end()) {
            it = spriteTextures
                     .emplace(key,
                              std::unique_ptr<QSGTexture>(makeTexture(window, cache.sprite(rgb))))
                     .first;
        }
        return *it->second;
    }
};

// (Re)construit tout le calque : tissu + tranches. Appelé seulement quand la scène ou le style
// change, jamais au pan/zoom.
void buildRealistic(RealisticLayer& layer,
                    const std::shared_ptr<const StudioSceneSnapshot>& scenePtr,
                    const RealisticStyle& style, QQuickWindow& window,
                    const ThreadMaterialProvider& materials) {
    const auto& scene = *scenePtr;
    clearChildren(*layer.fabric);
    clearChildren(*layer.group);
    clearChildren(*layer.tail);
    layer.chunkNodes.clear();
    layer.spriteTextures.clear();
    layer.cache.setStyle(style);
    layer.shadowTexture.reset(makeTexture(window, generateShadowSprite(style)));
    layer.fabricTexture.reset(makeTexture(window, generateFabricTile(style)));

    const auto& b = scene.bounds;
    const auto x0 = static_cast<float>(b.x0);
    const auto y0 = static_cast<float>(b.y0);
    const auto x1 = static_cast<float>(b.x1);
    const auto y1 = static_cast<float>(b.y1);
    const auto tx = static_cast<float>(b.width() / kFabricTileMm);
    const auto ty = static_cast<float>(b.height() / kFabricTileMm);
    const RealisticVertex quad[6] = {{x0, y0, 0, 0},  {x1, y0, tx, 0}, {x0, y1, 0, ty},
                                     {x0, y1, 0, ty}, {x1, y0, tx, 0}, {x1, y1, tx, ty}};
    layer.fabric->appendChildNode(
        texturedNode(quad, 6, materials.fabricMaterial(*layer.fabricTexture)));

    layer.runs = buildRealisticRuns(scene.segments);
    layer.chunks = buildRealisticChunks(layer.runs);
    std::vector<RealisticVertex> scratch;
    for (const auto& chunk : layer.chunks) {
        const std::span<const StudioSegment> part(scene.segments.data() + chunk.first, chunk.count);
        scratch.resize(chunk.count * kVerticesPerSegment);
        auto* wrapper = new QSGOpacityNode;
        if (style.shadow > 0.0) {
            writeShadowQuads(part, style, scratch.data());
            wrapper->appendChildNode(texturedNode(scratch.data(), scratch.size(),
                                                  materials.shadowMaterial(*layer.shadowTexture)));
        }
        writeThreadQuads(part, style, scratch.data());
        auto& sprite = layer.spriteTexture(window, layer.runs[chunk.run].rgb);
        wrapper->appendChildNode(
            texturedNode(scratch.data(), scratch.size(), materials.threadMaterial(sprite)));
        layer.group->appendChildNode(wrapper);
        layer.chunkNodes.push_back(wrapper);
    }
    layer.builtScene = scenePtr;
    layer.builtStyle = style;
    layer.shownSegments = std::numeric_limits<std::size_t>::max() - 1; // force la mise à jour
    layer.built = true;
}

// Simulation : tranches entières visibles ou non ; la tranche du curseur est redessinée en
// préfixe dans la queue.
void applyRealisticSimulation(RealisticLayer& layer, const StudioSceneSnapshot& scene,
                              const RealisticStyle& style, std::size_t visibleSegments,
                              QQuickWindow& window, const ThreadMaterialProvider& materials) {
    const std::size_t total = scene.segments.size();
    const std::size_t shown =
        visibleSegments == CanvasOverlays::all ? total : std::min(total, visibleSegments);
    if (layer.shownSegments == shown) {
        return;
    }
    layer.shownSegments = shown;
    clearChildren(*layer.tail);
    for (std::size_t i = 0; i < layer.chunks.size(); ++i) {
        const auto& chunk = layer.chunks[i];
        const bool full = chunk.first + chunk.count <= shown;
        layer.chunkNodes[i]->setOpacity(full ? 1.0 : 0.0);
        if (!full && chunk.first < shown) {
            const std::size_t count = shown - chunk.first;
            const std::span<const StudioSegment> part(scene.segments.data() + chunk.first, count);
            std::vector<RealisticVertex> scratch(count * kVerticesPerSegment);
            if (style.shadow > 0.0) {
                writeShadowQuads(part, style, scratch.data());
                layer.tail->appendChildNode(
                    texturedNode(scratch.data(), scratch.size(),
                                 materials.shadowMaterial(*layer.shadowTexture)));
            }
            writeThreadQuads(part, style, scratch.data());
            auto& sprite = layer.spriteTexture(window, layer.runs[chunk.run].rgb);
            layer.tail->appendChildNode(
                texturedNode(scratch.data(), scratch.size(), materials.threadMaterial(sprite)));
        }
    }
}

const ThreadMaterialProvider& materialProvider() {
    static const StandardTextureMaterials provider;
    return provider;
}

// Crochet de la couche « regions » : une mise à jour par synchronisation, textures recréées
// seulement quand la révision publiée change, matrice recalculée quand la caméra bouge.
void syncRegionLayer(GeometryRoot& root, const CanvasRenderState& state, QQuickWindow& window) {
    const RegionLayerData* data = state.layers.regions ? state.regions.get() : nullptr;
    root.regions->update(data, window);
    if (root.regionsCameraRevision != state.camera.revision()) {
        QMatrix4x4 matrix;
        const auto offset = state.camera.offset();
        matrix.translate(static_cast<float>(offset.x), static_cast<float>(offset.y));
        matrix.scale(static_cast<float>(state.camera.scale()));
        root.regions->setMatrix(matrix);
        root.regionsCameraRevision = state.camera.revision();
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
        const bool layersChanged = root->displayedLayers != state.layers;
        const bool vectorChanged = root->displayedVector != state.vectorImage;
        const bool layerDirty = layersChanged || vectorChanged;
        const CanvasOverlays defaults;
        const CanvasOverlays& overlays = state.overlays ? *state.overlays : defaults;
        syncRegionLayer(*root, state, window);
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
        if (dirty.geometry || overlayDirty || layerDirty) {
            root->frame->setOpacity(overlays.showHoop ? 1.0 : 0.0);
            root->stitches.all->setOpacity(overlays.showStitches ? 1.0 : 0.0);
            if (state.scene) {
                applySimulation(*root, state.scene->vertices, overlays.visibleSegments);
            }
            clearChildren(*root->layers);
            if (state.vectorImage && !state.vectorImage->image.isNull()) {
                const auto& v = *state.vectorImage;
                auto* node = new QSGSimpleTextureNode;
                node->setOwnsTexture(true);
                node->setTexture(window.createTextureFromImage(v.image));
                node->setRect(QRectF(v.x0, v.y0, v.widthMm, v.heightMm));
                root->layers->appendChildNode(node);
            }
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
        }
        if (dirty.selection || dirty.camera || dirty.geometry || overlayDirty || layerDirty) {
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
        synchronizeSourceImage(*root, state, window);
        root->overlayRevision = state.overlayRevision;
        root->displayedLayers = state.layers;
        root->displayedVector = state.vectorImage;
        CanvasRenderResult result{root, dirty.geometry && state.scene != nullptr, elapsed};
        synchronizeRealistic(*root, state, overlays, window, dirty, overlayDirty, result);
        return result;
    }

private:
    // Couche « sourceImage » : textures reconstruites par révision d'image, jamais au pan/zoom.
    static void synchronizeSourceImage(GeometryRoot& root, const CanvasRenderState& state,
                                       QQuickWindow& window) {
        if (state.sourceImage.data || root.source.textureBuilds() > 0) {
            root.source.synchronize(*root.world, state.sourceImage, window);
        }
    }

    // Calque réaliste : reconstruit seulement si la scène ou le style change (jamais au
    // pan/zoom), masqué sans rien détruire quand la bascule est coupée.
    static void synchronizeRealistic(GeometryRoot& root, const CanvasRenderState& state,
                                     const CanvasOverlays& overlays, QQuickWindow& window,
                                     const StudioInvalidation& dirty, bool overlayDirty,
                                     CanvasRenderResult& result) {
        const bool want = overlays.realistic && state.scene != nullptr;
        if (!want && root.realistic == nullptr) {
            return;
        }
        if (want && root.realistic == nullptr) {
            root.realistic = new RealisticLayer;
            root.realistic->setOpacity(0.0);
            root.world->insertChildNodeAfter(root.realistic, root.frame);
        }
        auto& layer = *root.realistic;
        if (want && (!layer.built || layer.builtScene != state.scene ||
                     !(layer.builtStyle == overlays.realisticStyle))) {
            QElapsedTimer timer;
            timer.start();
            buildRealistic(layer, state.scene, overlays.realisticStyle, window, materialProvider());
            result.realisticBuilt = true;
            result.realisticMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        }
        if (want && (result.realisticBuilt || dirty.geometry || overlayDirty)) {
            applyRealisticSimulation(layer, *state.scene, overlays.realisticStyle,
                                     overlays.visibleSegments, window, materialProvider());
        }
        layer.setOpacity(want && overlays.showStitches ? 1.0 : 0.0);
        if (want) {
            root.stitches.all->setOpacity(0.0); // le calque réaliste remplace les points plats
        }
        result.realisticActive = want && overlays.showStitches;
    }
};

struct SoftwareRoot : QSGSimpleTextureNode {
    StudioResourceState resources;
    QSizeF viewport;
    double dpr{0};
    std::shared_ptr<const RegionLayerData> regions; // couche « regions » déjà rastérisée
    CanvasLayers displayedLayers;
    std::shared_ptr<const OverlayImage> displayedVector;
    std::uint64_t overlayRevision{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t sourceData{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t sourceView{std::numeric_limits<std::uint64_t>::max()};
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
            root->regions == (state.layers.regions ? state.regions : nullptr) &&
            root->displayedLayers == state.layers && root->displayedVector == state.vectorImage &&
            root->overlayRevision == state.overlayRevision && root->viewport == state.viewport &&
            root->sourceData == state.sourceImage.dataRevision &&
            root->sourceView == state.sourceImage.viewRevision &&
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
        paintSourceImage(painter, state.sourceImage); // sous les régions et les formes
        paintRegionLayer(painter, state.layers.regions ? state.regions.get() : nullptr);
        root->regions = state.layers.regions ? state.regions : nullptr;
        const CanvasOverlays defaults;
        const CanvasOverlays& overlays = state.overlays ? *state.overlays : defaults;
        if (state.vectorImage && !state.vectorImage->image.isNull()) {
            const auto& v = *state.vectorImage;
            painter.drawImage(QRectF(v.x0, v.y0, v.widthMm, v.heightMm), v.image);
        }
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
        root->displayedLayers = state.layers;
        root->displayedVector = state.vectorImage;
        root->sourceData = state.sourceImage.dataRevision;
        root->sourceView = state.sourceImage.viewRevision;
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
