// SPDX-License-Identifier: Apache-2.0
#pragma once

// Couche « regions » du canevas Studio (espace Segmentation) : les régions de
// `Project::segmentation` posées en mm de scène (Y vers le bas, image centrée sur l'origine,
// `mm_per_px` mm par pixel). Données IMMUABLES publiées par révision : `fill` (régions + contours)
// ne se reconstruit que si la segmentation change ; `highlight` (sélection) que si la sélection
// change ; l'opacité est un simple réglage du nœud, jamais une reconstruction. Le pan/zoom ne
// touche pas aux textures (seule la matrice du nœud bouge). Aucun nœud ni objet QML par région.

#include <QImage>
#include <QRectF>
#include <QSGNode>
#include <cstdint>
#include <memory>
#include <vector>

#include "document_services.hpp"

class QPainter;
class QQuickWindow;

namespace openstitch::desktop_v2::studio {

struct RegionLayerData {
    QImage fill; // ARGB32 prémultiplié : régions colorées, contours assombris (ligne 0 = haut)
    QImage highlight; // sélection : voile + contour ; nul sans sélection
    QRectF rectMm;    // position en mm de scène
    double opacity{0.85};
    std::uint64_t fillRevision{0};
    std::uint64_t highlightRevision{0};
};

// Empreinte déterministe de la carte d'étiquettes et des couleurs (clé de reconstruction).
[[nodiscard]] std::uint64_t segmentationFingerprint(const document::Project& project);
// Régions colorées + contours ; image nulle sans segmentation.
[[nodiscard]] QImage buildRegionFill(const document::Project& project, bool contours);
// Voile + contour des régions `selected` ; image nulle si rien n'est sélectionné.
[[nodiscard]] QImage buildRegionHighlight(const document::Project& project,
                                          const std::vector<std::uint64_t>& selected);
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
    bool hasFill_{false};
    bool hasHighlight_{false};
};

// Repli logiciel : même rendu, au pinceau (le backend logiciel ne dessine pas de géométrie).
void paintRegionLayer(QPainter& painter, const RegionLayerData* data);

} // namespace openstitch::desktop_v2::studio
