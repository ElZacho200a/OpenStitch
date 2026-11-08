// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QImage>
#include <QRectF>
#include <cstdint>
#include <memory>
#include <vector>

class QPainter;
class QQuickWindow;
class QSGNode;
class QSGSimpleTextureNode;

namespace openstitch::desktop_v2::studio {

// Couche « sourceImage » du canevas : l'image de travail (et l'originale pour l'avant/après),
// positionnée en mm de SCÈNE (Y vers le bas, centrée sur l'origine, `mmPerPx` mm par pixel).
// Données IMMUABLES : un changement d'image publie une nouvelle instance et incrémente
// `dataRevision` (une seule reconstruction de texture par révision) ; le mode et le rideau ne
// touchent que des rectangles (`viewRevision`), jamais les textures. Aucun QObject ici : la
// structure est copiée pendant la phase de synchronisation du rendu.
struct SourceImageData {
    QImage after;  // image de travail (pile d'opérations rejouée)
    QImage before; // image d'origine, intacte
    double mmPerPx{0.0};
};

enum class SourceCompare : int { After = 0, Before = 1, Curtain = 2 };

struct SourceImageView {
    std::shared_ptr<const SourceImageData> data;
    std::uint64_t dataRevision{0};
    std::uint64_t viewRevision{0};
    SourceCompare mode{SourceCompare::After};
    double curtain{0.5}; // 0..1 de la largeur de l'image de travail : « avant » à gauche
    bool operator==(const SourceImageView&) const = default;
};

// Un morceau dessiné : quelle image, où (mm de scène) et quelle part de ses pixels.
struct SourcePiece {
    bool before{false};
    QRectF destMm;
    QRectF srcPx;
};

// Géométrie pure et déterministe, partagée par les deux backends (et testable sans fenêtre).
[[nodiscard]] QRectF sourceImageRectMm(const QImage& image, double mmPerPx);
[[nodiscard]] std::vector<SourcePiece> sourceImagePieces(const SourceImageView& view);

// Chemin logiciel (le backend logiciel de Qt Quick ne sait pas dessiner de géométrie
// personnalisée : on peint l'image avec QPainter).
void paintSourceImage(QPainter& painter, const SourceImageView& view);

// Chemin RHI : nœuds texture standard (mipmaps, filtrage linéaire). `world` est le nœud dont la
// matrice est la caméra ; la couche s'y insère EN PREMIER (sous les points).
class SourceImageNodes {
public:
    // Reconstruit les textures seulement si `dataRevision` change ; sinon ajuste les rectangles.
    void synchronize(QSGNode& world, const SourceImageView& view, QQuickWindow& window);
    [[nodiscard]] int textureBuilds() const { return builds_; }

private:
    QSGNode* group_{nullptr};
    QSGSimpleTextureNode* after_{nullptr};
    QSGSimpleTextureNode* before_{nullptr};
    std::uint64_t dataRevision_{~std::uint64_t{0}};
    std::uint64_t viewRevision_{~std::uint64_t{0}};
    int builds_{0};
};

} // namespace openstitch::desktop_v2::studio
