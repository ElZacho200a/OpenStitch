// SPDX-License-Identifier: Apache-2.0
#include "layers/region_layer.hpp"

#include <QPainter>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <algorithm>
#include <cmath>

namespace openstitch::desktop_v2::studio {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void mix(std::uint64_t& h, std::uint64_t v) {
    h ^= v;
    h *= kFnvPrime;
}

QRgb premul(int r, int g, int b, int a) {
    return qPremultiply(qRgba(r, g, b, a));
}

void clearChildren(QSGNode& node) {
    while (auto* child = node.firstChild()) {
        node.removeChildNode(child);
        delete child;
    }
}

// Texture prête pour le nœud ; le nœud en devient propriétaire.
QSGSimpleTextureNode* textureNode(QQuickWindow& window, const QImage& image, const QRectF& rect) {
    auto* node = new QSGSimpleTextureNode;
    node->setOwnsTexture(true);
    node->setTexture(window.createTextureFromImage(image));
    node->setRect(rect);
    return node;
}

} // namespace

std::vector<RegionPixelBounds> buildRegionBounds(const document::Project& project) {
    if (!project.segmentation) {
        return {};
    }
    const auto& seg = *project.segmentation;
    const auto w = static_cast<std::size_t>(std::max(0, seg.width));
    const auto h = static_cast<std::size_t>(std::max(0, seg.height));
    std::vector<RegionPixelBounds> bounds(seg.region_slots.size() + 1);
    if (w == 0 || h == 0 || seg.labels.size() < w * h) {
        return bounds;
    }
    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t x = 0; x < w; ++x) {
            const auto label = seg.labels[y * w + x];
            if (label == 0 || label >= bounds.size() || !seg.region_slots[label - 1]) {
                continue;
            }
            auto& box = bounds[label];
            box.left = std::min(box.left, static_cast<int>(x));
            box.right = std::max(box.right, static_cast<int>(x));
            box.top = std::min(box.top, static_cast<int>(y));
            box.bottom = std::max(box.bottom, static_cast<int>(y));
        }
    }
    return bounds;
}

std::uint64_t segmentationFingerprint(const document::Project& project) {
    if (!project.segmentation) {
        return 0;
    }
    const auto& seg = *project.segmentation;
    std::uint64_t h = kFnvOffset;
    mix(h, static_cast<std::uint64_t>(seg.width));
    mix(h, static_cast<std::uint64_t>(seg.height));
    for (const auto label : seg.labels) {
        mix(h, label);
    }
    for (const auto& slot : seg.region_slots) {
        mix(h, slot ? 1U : 0U);
        if (slot) {
            mix(h, (static_cast<std::uint64_t>(slot->rgb[0]) << 16) |
                       (static_cast<std::uint64_t>(slot->rgb[1]) << 8) | slot->rgb[2]);
        }
    }
    return h == 0 ? 1 : h;
}

QRectF regionLayerRect(const document::Project& project) {
    if (!project.segmentation) {
        return {};
    }
    const double mm = project.mm_per_px.value;
    const double w = project.segmentation->width * mm;
    const double h = project.segmentation->height * mm;
    return {-w / 2.0, -h / 2.0, w, h};
}

bool scenePointToPixel(const document::Project& project, double xMm, double yMm, int& px, int& py) {
    if (!project.segmentation || project.mm_per_px.value <= 0.0) {
        return false;
    }
    const auto& seg = *project.segmentation;
    const double mm = project.mm_per_px.value;
    const double fx = xMm / mm + seg.width / 2.0;
    const double fy = yMm / mm + seg.height / 2.0;
    if (fx < 0.0 || fy < 0.0 || fx >= seg.width || fy >= seg.height) {
        return false;
    }
    px = static_cast<int>(std::floor(fx));
    py = static_cast<int>(std::floor(fy));
    return true;
}

QImage buildRegionFill(const document::Project& project, bool contours) {
    if (!project.segmentation) {
        return {};
    }
    const auto& seg = *project.segmentation;
    const auto w = static_cast<std::size_t>(std::max(0, seg.width));
    const auto h = static_cast<std::size_t>(std::max(0, seg.height));
    if (w == 0 || h == 0 || seg.labels.size() < w * h) {
        return {};
    }
    QImage image(seg.width, seg.height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    const auto colour = [&](std::uint32_t label, bool edge) -> QRgb {
        if (label == 0 || label > seg.region_slots.size() || !seg.region_slots[label - 1]) {
            return 0;
        }
        const auto& rgb = seg.region_slots[label - 1]->rgb;
        const int k = edge ? 55 : 100; // contour : couleur assombrie
        return premul(rgb[0] * k / 100, rgb[1] * k / 100, rgb[2] * k / 100, 255);
    };
    for (std::size_t y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<QRgb*>(image.scanLine(static_cast<int>(y)));
        for (std::size_t x = 0; x < w; ++x) {
            const auto label = seg.labels[y * w + x];
            bool edge = false;
            if (contours && label != 0) {
                edge = (x + 1 < w && seg.labels[y * w + x + 1] != label) ||
                       (y + 1 < h && seg.labels[(y + 1) * w + x] != label);
            }
            row[x] = colour(label, edge);
        }
    }
    return image;
}

RegionHighlight buildRegionHighlight(const document::Project& project,
                                     const std::vector<std::uint64_t>& selected,
                                     const std::vector<RegionPixelBounds>& bounds) {
    if (!project.segmentation || selected.empty()) {
        return {};
    }
    const auto& seg = *project.segmentation;
    const auto w = static_cast<std::size_t>(std::max(0, seg.width));
    const auto h = static_cast<std::size_t>(std::max(0, seg.height));
    if (w == 0 || h == 0 || seg.labels.size() < w * h) {
        return {};
    }

    QRect roi;
    for (const auto id : selected) {
        if (id == 0 || id >= bounds.size() || !bounds[id].valid()) {
            continue;
        }
        const auto& box = bounds[id];
        const QRect region(QPoint(box.left, box.top), QPoint(box.right, box.bottom));
        roi = roi.isValid() ? roi.united(region) : region;
    }
    if (!roi.isValid()) {
        return {};
    }

    std::vector<std::uint64_t> ids = selected;
    std::sort(ids.begin(), ids.end());
    const auto isSelected = [&](std::uint32_t label) {
        return label != 0 &&
               std::binary_search(ids.begin(), ids.end(), static_cast<std::uint64_t>(label));
    };
    // Le voile est limité au rectangle des régions retenues. Les contours
    // utilisent quand même les voisins globaux, pour être identiques au rendu plein cadre.
    QImage image(roi.width(), roi.height(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    const QRgb veil = premul(255, 255, 255, 80);
    const QRgb line = premul(255, 196, 0, 255);
    for (int y = roi.top(); y <= roi.bottom(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(image.scanLine(y - roi.top()));
        for (int x = roi.left(); x <= roi.right(); ++x) {
            const auto i = static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x);
            if (!isSelected(seg.labels[i])) {
                continue;
            }
            const bool edge = x == 0 || y == 0 || x + 1 == seg.width || y + 1 == seg.height ||
                              !isSelected(seg.labels[i - 1]) || !isSelected(seg.labels[i + 1]) ||
                              !isSelected(seg.labels[i - w]) || !isSelected(seg.labels[i + w]);
            row[x - roi.left()] = edge ? line : veil;
        }
    }
    return {std::move(image), roi};
}

RegionLayerNode::RegionLayerNode()
    : fillOpacity_(new QSGOpacityNode), highlightOpacity_(new QSGOpacityNode) {
    appendChildNode(fillOpacity_);
    appendChildNode(highlightOpacity_);
}

void RegionLayerNode::update(const RegionLayerData* data, QQuickWindow& window) {
    const bool wantFill = data && !data->fill.isNull() && data->rectMm.width() > 0;
    const bool wantHigh = wantFill && !data->highlight.isNull();
    if (!wantFill) {
        if (hasFill_ || hasHighlight_) {
            clearChildren(*fillOpacity_);
            clearChildren(*highlightOpacity_);
        }
        hasFill_ = hasHighlight_ = false;
        fillRevision_ = highlightRevision_ = 0;
        displayedRect_ = {};
        displayedHighlightRect_ = {};
        return;
    }
    const bool rectChanged = displayedRect_ != data->rectMm;
    const bool highlightRectChanged = displayedHighlightRect_ != data->highlightRectMm;
    if (!hasFill_ || fillRevision_ != data->fillRevision) {
        clearChildren(*fillOpacity_);
        fillOpacity_->appendChildNode(textureNode(window, data->fill, data->rectMm));
        fillRevision_ = data->fillRevision;
        hasFill_ = true;
    } else if (rectChanged) {
        // A scale or document-size change moves the same segmentation texture in scene mm.
        // Its fingerprint may be unchanged: update geometry without reallocating the texture.
        static_cast<QSGSimpleTextureNode*>(fillOpacity_->firstChild())->setRect(data->rectMm);
    }
    if (!wantHigh) {
        if (hasHighlight_) {
            clearChildren(*highlightOpacity_);
        }
        hasHighlight_ = false;
    } else if (!hasHighlight_ || highlightRevision_ != data->highlightRevision) {
        clearChildren(*highlightOpacity_);
        highlightOpacity_->appendChildNode(textureNode(window, data->highlight, data->highlightRectMm));
        highlightRevision_ = data->highlightRevision;
        hasHighlight_ = true;
    } else if (highlightRectChanged) {
        static_cast<QSGSimpleTextureNode*>(highlightOpacity_->firstChild())
            ->setRect(data->highlightRectMm);
    }
    displayedRect_ = data->rectMm;
    displayedHighlightRect_ = data->highlightRectMm;
    fillOpacity_->setOpacity(std::clamp(data->opacity, 0.0, 1.0));
    highlightOpacity_->setOpacity(1.0);
}

void paintRegionLayer(QPainter& painter, const RegionLayerData* data) {
    if (!data || data->fill.isNull() || data->rectMm.width() <= 0) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setOpacity(std::clamp(data->opacity, 0.0, 1.0));
    painter.drawImage(data->rectMm, data->fill);
    if (!data->highlight.isNull()) {
        painter.setOpacity(1.0);
        painter.drawImage(data->highlightRectMm, data->highlight);
    }
    painter.restore();
}

} // namespace openstitch::desktop_v2::studio
