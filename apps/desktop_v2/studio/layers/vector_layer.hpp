// SPDX-License-Identifier: Apache-2.0
#pragma once

// Studio -- couche « vectors » du canevas (espace Vectorisation). Construite UNIQUEMENT
// lorsque les formes changent, jamais lors de la sélection ni au pan/zoom : les formes vectorielles
// (`Project::vector_objects`, mm scène, Y vers le bas) sont rastérisées dans une image partagée
// immuable, publiée comme calque d'image du canevas (nœud texture standard : fonctionne aussi
// avec le backend logiciel de Qt Quick). Aucun objet QML par forme ni par nœud.

#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "canvas_renderer.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop_v2::studio {

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
    // Rasterise uniquement les formes sélectionnées dans leur rectangle local.
    // Ne touche jamais au fond ni aux QPainterPath déjà construits.
    [[nodiscard]] std::shared_ptr<const OverlayImage>
    selectionOverlay(const std::vector<std::uint64_t>& selectedShapes) const;

    [[nodiscard]] const std::vector<VectorShape>& shapes() const { return shapes_; }
    [[nodiscard]] const OverlayImage& image() const { return image_; }
    // Forme la plus haute sous le point (mm scène), tolérance en mm ; ignore les formes
    // verrouillées.
    [[nodiscard]] std::optional<std::uint64_t> pick(double x, double y, double toleranceMm) const;

private:
    std::vector<VectorShape> shapes_;
    // Selection hot path: resolve only selected objects, not every visible shape.
    std::unordered_map<std::uint64_t, std::size_t> shapeIndex_;
    OverlayImage image_;
    double pxPerMm_{1.0};
};

} // namespace openstitch::desktop_v2::studio
