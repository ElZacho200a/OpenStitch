// SPDX-License-Identifier: Apache-2.0
#pragma once

// Studio -- couche « vectors » du canevas (espace Vectorisation). Construite UNIQUEMENT
// lorsque les formes changent, jamais lors de la sélection ni au pan/zoom : les formes vectorielles
// (`Project::vector_objects`, mm scène, Y vers le bas) sont triangulées pour le backend GPU (RHI),
// qui les dessine nettes à tout zoom, et rastérisées à la demande dans une image partagée immuable
// pour le backend logiciel de Qt Quick. Aucun objet QML par forme ni par nœud.

#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
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
    // `build(project)` (c'est le même code).
    [[nodiscard]] static std::shared_ptr<const VectorLayer>
    build(const std::vector<document::VectorObject>& shapes);
    [[nodiscard]] const std::vector<VectorShape>& shapes() const { return shapes_; }
    // Triangles des formes (mm scène, couleurs prémultipliées) dans l'ordre de peinture :
    // remplissage puis contour de chaque forme. Dessinés tels quels par le backend GPU.
    [[nodiscard]] const std::vector<StudioVertex>& triangles() const { return triangles_; }
    // Image des formes pour le backend logiciel, rastérisée au premier appel seulement : le
    // chemin GPU ne la demande jamais. Sûr depuis plusieurs fils.
    [[nodiscard]] const OverlayImage& image() const;
    // Mémoire occupée : triangles, plus l'image si elle a été rastérisée.
    [[nodiscard]] qint64 bytes() const;
    // Forme la plus haute sous le point (mm scène), tolérance en mm ; ignore les formes
    // verrouillées.
    [[nodiscard]] std::optional<std::uint64_t> pick(double x, double y, double toleranceMm) const;

private:
    void rasterize() const;

    std::vector<VectorShape> shapes_;
    std::vector<StudioVertex> triangles_;
    QRectF area_; // mm scène, marge comprise ; vide sans forme
    mutable std::once_flag rasterOnce_;
    mutable OverlayImage image_;
    mutable std::atomic_bool rasterized_{false}; // lu par bytes() depuis un autre fil
};

} // namespace openstitch::desktop_v2::studio
