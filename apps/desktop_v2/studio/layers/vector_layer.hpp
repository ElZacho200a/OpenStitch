// SPDX-License-Identifier: Apache-2.0
#pragma once

// Studio -- couche « vectors » du canevas (espace Vectorisation). Construite par révision
// géométrique, indépendamment de la sélection et de la caméra : les formes vectorielles
// (`Project::vector_objects`, mm scène, Y vers le bas) sont rastérisées dans une image partagée
// immuable, publiée comme calque d'image du canevas (nœud texture standard : fonctionne aussi
// avec le backend logiciel de Qt Quick). Aucun objet QML par forme ni par nœud.

#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QRectF>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "canvas_renderer.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop_v2::studio {

class GpuCanvas;

struct VectorShape {
    std::uint64_t id{0};
    std::uint64_t sourceRegion{0}; // 0 : forme sans région (dessinée à la main)
    QColor color;
    QPainterPath path; // mm scène, trous inclus (remplissage pair-impair)
    std::vector<QPointF> nodes;
    QRectF bounds;
    bool locked{false};
};

class VectorLayer {
public:
    [[nodiscard]] static std::shared_ptr<const VectorLayer> build(const document::Project& project);

    [[nodiscard]] const std::vector<VectorShape>& shapes() const { return shapes_; }
    [[nodiscard]] const OverlayImage& image() const { return image_; }
    // Forme la plus haute sous le point (mm scène), tolérance en mm ; ignore les formes
    // verrouillées.
    [[nodiscard]] std::optional<std::uint64_t> pick(double x, double y, double toleranceMm) const;

private:
    std::vector<VectorShape> shapes_;
    OverlayImage image_;
};

// Surcouche du seul canevas Vecteurs. Réutilise les chemins et nœuds du calque immuable.
class VectorSelectionOverlay final : public QQuickPaintedItem {
public:
    explicit VectorSelectionOverlay(GpuCanvas& canvas);
    void setSelection(std::shared_ptr<const VectorLayer> layer,
                      const std::vector<std::uint64_t>& ids);
    void paint(QPainter* painter) override;

private:
    void updateViewport();

    GpuCanvas& canvas_;
    std::shared_ptr<const VectorLayer> layer_;
    std::vector<const VectorShape*> selected_;
    QRectF bounds_;
    QPointF origin_;
    double scale_{1};
};

} // namespace openstitch::desktop_v2::studio
