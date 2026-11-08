// SPDX-License-Identifier: Apache-2.0
#pragma once

// Couche « regions » du canevas Studio (espace Segmentation) : les régions de
// `Project::segmentation` posées en mm de scène (Y vers le bas, image centrée sur l'origine,
// `mm_per_px` mm par pixel). Données IMMUABLES publiées par révision : `fill` (régions + contours)
// ne se reconstruit que si la segmentation change ; `highlight` (sélection) que si la sélection
// change ; l'opacité est un simple réglage du nœud, jamais une reconstruction. Le pan/zoom ne
// touche pas aux textures (seule la matrice du nœud bouge). Aucun nœud ni objet QML par région.

#include <QImage>
#include <QRect>
#include <QRectF>
#include <QSGNode>
#include <climits>
#include <cstdint>
#include <memory>
#include <vector>

#include "document_services.hpp"

class QPainter;
class QQuickWindow;

namespace openstitch::desktop_v2::studio {

struct RegionLayerData {
    // ARGB32 prémultiplié : régions colorées, contours assombris.
    QImage fill;
    // Voile + contour de sélection, texture recadrée à la zone retenue.
    QImage highlight;
    QRectF rectMm;
    QRectF highlightRectMm;
    double opacity{0.85};
    std::uint64_t fillRevision{0};
    std::uint64_t highlightRevision{0};
};

// Index spatial (1-based, comme les étiquettes) construit avec le fond, puis
// réutilisé à chaque sélection au lieu de balayer tous les pixels.
struct RegionPixelBounds {
    int left{INT_MAX};
    int top{INT_MAX};
    int right{-1};
    int bottom{-1};
    [[nodiscard]] bool valid() const { return right >= left && bottom >= top; }
};

[[nodiscard]] std::vector<RegionPixelBounds> buildRegionBounds(const document::Project& project);

struct RegionHighlight {
    QImage image;
    QRect boundsPx; // rectangle de la texture dans la segmentation complète
};

// Empreinte déterministe de la carte d'étiquettes et des couleurs (clé de reconstruction).
[[nodiscard]] std::uint64_t segmentationFingerprint(const document::Project& project);
// Régions colorées + contours ; image nulle sans segmentation.
[[nodiscard]] QImage buildRegionFill(const document::Project& project, bool contours);
// Voile + contour des régions `selected` ; image nulle si rien n'est sélectionné.
[[nodiscard]] RegionHighlight buildRegionHighlight(const document::Project& project,
                                                   const std::vector<std::uint64_t>& selected,
                                                   const std::vector<RegionPixelBounds>& bounds);
// Rectangle de scène de l'image de travail (mm).
[[nodiscard]] QRectF regionLayerRect(const document::Project& project);
// Pixel de l'image de travail sous un point de scène (mm) ; faux hors image.
[[nodiscard]] bool scenePointToPixel(const document::Project& project, double xMm, double yMm,
                                     int& px, int& py);

// Nœud du graphe de scène (fil de rendu). Texture recréée seulement quand la révision change.
class RegionLayerNode final : public QSGTransformNode {
public:
    RegionLayerNode();
    void update(const RegionLayerData* data, QQuickWindow& window);

private:
    QSGOpacityNode* fillOpacity_;
    QSGOpacityNode* highlightOpacity_;
    std::uint64_t fillRevision_{0};
    std::uint64_t highlightRevision_{0};
    QRectF displayedRect_;
    QRectF displayedHighlightRect_;
    bool hasFill_{false};
    bool hasHighlight_{false};
};

// Repli logiciel : même rendu, au pinceau (le backend logiciel ne dessine pas de géométrie).
void paintRegionLayer(QPainter& painter, const RegionLayerData* data);

} // namespace openstitch::desktop_v2::studio
