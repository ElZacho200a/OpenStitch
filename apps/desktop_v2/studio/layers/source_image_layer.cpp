// SPDX-License-Identifier: Apache-2.0
#include "layers/source_image_layer.hpp"

#include <QPainter>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <algorithm>
#include <cmath>

namespace openstitch::desktop_v2::studio {

QRectF sourceImageRectMm(const QImage& image, double mmPerPx) {
    if (image.isNull() || mmPerPx <= 0.0) {
        return {};
    }
    const double w = image.width() * mmPerPx;
    const double h = image.height() * mmPerPx;
    return {-w / 2.0, -h / 2.0, w, h};
}

namespace {

// Part de `rect` située à gauche (ou à droite) de la verticale `x`, avec la part de pixels.
bool clipped(const QImage& image, const QRectF& rect, double x, bool keepLeft, QRectF& dest,
             QRectF& src) {
    const double lo = keepLeft ? rect.left() : std::max(rect.left(), x);
    const double hi = keepLeft ? std::min(rect.right(), x) : rect.right();
    if (hi <= lo) {
        return false;
    }
    dest = QRectF(lo, rect.top(), hi - lo, rect.height());
    const double sx = image.width() / rect.width();
    src = QRectF((lo - rect.left()) * sx, 0.0, (hi - lo) * sx, image.height());
    return true;
}

} // namespace

std::vector<SourcePiece> sourceImagePieces(const SourceImageView& view) {
    std::vector<SourcePiece> pieces;
    if (!view.data || view.data->mmPerPx <= 0.0) {
        return pieces;
    }
    const auto& d = *view.data;
    const QRectF after = sourceImageRectMm(d.after, d.mmPerPx);
    const QRectF before = sourceImageRectMm(d.before, d.mmPerPx);
    const auto full = [](const QImage& image, const QRectF& rect, bool isBefore) {
        return SourcePiece{isBefore, rect, QRectF(0, 0, image.width(), image.height())};
    };
    switch (view.mode) {
    case SourceCompare::After:
        if (!after.isEmpty()) {
            pieces.push_back(full(d.after, after, false));
        }
        break;
    case SourceCompare::Before:
        if (!before.isEmpty()) {
            pieces.push_back(full(d.before, before, true));
        }
        break;
    case SourceCompare::Curtain: {
        const QRectF span = after.isEmpty() ? before : after;
        if (span.isEmpty()) {
            break;
        }
        const double x = span.left() + std::clamp(view.curtain, 0.0, 1.0) * span.width();
        SourcePiece piece;
        if (!d.before.isNull() && clipped(d.before, before, x, true, piece.destMm, piece.srcPx)) {
            piece.before = true;
            pieces.push_back(piece);
        }
        if (!d.after.isNull() && clipped(d.after, after, x, false, piece.destMm, piece.srcPx)) {
            piece.before = false;
            pieces.push_back(piece);
        }
        break;
    }
    }
    return pieces;
}

void paintSourceImage(QPainter& painter, const SourceImageView& view) {
    if (!view.data) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (const auto& piece : sourceImagePieces(view)) {
        painter.drawImage(piece.destMm, piece.before ? view.data->before : view.data->after,
                          piece.srcPx);
    }
    painter.restore();
}

namespace {

QSGSimpleTextureNode* makeNode(const QImage& image, QQuickWindow& window) {
    auto* node = new QSGSimpleTextureNode;
    node->setOwnsTexture(true);
    // Mipmaps : l'image est réduite à l'écran lors du dézoom ; la texture est immuable.
    auto* texture = window.createTextureFromImage(
        image, QQuickWindow::CreateTextureOptions(QQuickWindow::TextureHasMipmaps));
    node->setTexture(texture);
    node->setFiltering(QSGTexture::Linear);
    if (texture != nullptr) {
        texture->setMipmapFiltering(QSGTexture::Linear);
    }
    return node;
}

void place(QSGSimpleTextureNode& node, const SourcePiece* piece) {
    if (piece == nullptr) {
        node.setRect(QRectF());
        return;
    }
    node.setRect(piece->destMm);
    node.setSourceRect(piece->srcPx);
}

} // namespace

void SourceImageNodes::synchronize(QSGNode& world, const SourceImageView& view,
                                   QQuickWindow& window) {
    if (group_ == nullptr) {
        group_ = new QSGNode;
        world.prependChildNode(group_);
    }
    if (dataRevision_ != view.dataRevision) {
        while (group_->firstChild() != nullptr) {
            auto* child = group_->firstChild();
            group_->removeChildNode(child);
            delete child;
        }
        after_ = before_ = nullptr;
        if (view.data) {
            if (!view.data->before.isNull()) {
                before_ = makeNode(view.data->before, window);
                group_->appendChildNode(before_);
            }
            if (!view.data->after.isNull()) {
                after_ = makeNode(view.data->after, window);
                group_->appendChildNode(after_);
            }
            ++builds_;
        }
        dataRevision_ = view.dataRevision;
        viewRevision_ = ~std::uint64_t{0};
    }
    if (viewRevision_ != view.viewRevision) {
        const auto pieces = sourceImagePieces(view);
        const SourcePiece* pb = nullptr;
        const SourcePiece* pa = nullptr;
        for (const auto& piece : pieces) {
            (piece.before ? pb : pa) = &piece;
        }
        if (before_ != nullptr) {
            place(*before_, pb);
        }
        if (after_ != nullptr) {
            place(*after_, pa);
        }
        viewRevision_ = view.viewRevision;
    }
}

} // namespace openstitch::desktop_v2::studio
