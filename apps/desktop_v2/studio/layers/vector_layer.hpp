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
    // Même construction à partir des seules formes : permet de la lancer sur un worker avec une
    // copie immuable de `Project::vector_objects`, sans copier tout le projet. Résultat identique à
    // `build(project)` pixel pour pixel (c'est le même code).
    [[nodiscard]] static std::shared_ptr<const VectorLayer>
    build(const std::vector<document::VectorObject>& shapes);
    [[nodiscard]] const std::vector<VectorShape>& shapes() const { return shapes_; }
    [[nodiscard]] const OverlayImage& image() const { return image_; }
    // Forme la plus haute sous le point (mm scène), tolérance en mm ; ignore les formes
    // verrouillées.
    [[nodiscard]] std::optional<std::uint64_t> pick(double x, double y, double toleranceMm) const;

private:
    std::vector<VectorShape> shapes_;
    OverlayImage image_;
};

} // namespace openstitch::desktop_v2::studio
