// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <cstdint>
#include <memory>
#include <vector>

#include "image_controller.hpp"
#include "layers/region_layer.hpp"
#include "region_controller.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class GpuCanvas;
struct RegionLayerData;

// Contrôleur de contexte de l'espace « Segmentation » : adaptateur Qt SANS logique métier. Il
// publie au canevas les données immuables de la couche « regions » (une reconstruction par
// révision de segmentation, une pour la sélection, aucune au pan/zoom), traduit le clic canevas
// en pixel de l'image de travail (RegionController porte la sélection, partagée avec la liste) et
// calcule l'état de l'étape que l'espace publie au WorkspaceController.
class SegmentationSpaceController : public QObject {
    Q_OBJECT
    Q_PROPERTY(double opacity READ opacity WRITE setOpacity NOTIFY displayChanged)
    Q_PROPERTY(bool contours READ contours WRITE setContours NOTIFY displayChanged)
    Q_PROPERTY(bool hasSegmentation READ hasSegmentation NOTIFY stageChanged)
    Q_PROPERTY(int regionCount READ regionCount NOTIFY stageChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY stageChanged)
    // "" = ne rien publier (pas d'image : le calcul de base de l'hôte reprend), sinon
    // "empty" | "upToDate" | "stale" | "blocked".
    Q_PROPERTY(QString stageState READ stageState NOTIFY stageChanged)
    Q_PROPERTY(QString stageDetail READ stageDetail NOTIFY stageChanged)
    Q_PROPERTY(bool canSegment READ canSegment NOTIFY stageChanged)
    Q_PROPERTY(bool canUpdateShapes READ canUpdateShapes NOTIFY stageChanged)
    Q_PROPERTY(QString confirmText READ confirmText NOTIFY confirmChanged)
    // Compteurs de reconstruction (diagnostic et tests : le pan/zoom n'en provoque aucune).
    Q_PROPERTY(int fillRebuilds READ fillRebuilds NOTIFY layerChanged)
    Q_PROPERTY(int highlightRebuilds READ highlightRebuilds NOTIFY layerChanged)
public:
    SegmentationSpaceController(StudioController& studio, ImageController& images,
                                RegionController& regions, QObject* parent = nullptr);
    ~SegmentationSpaceController() override;

    [[nodiscard]] double opacity() const { return opacity_; }
    void setOpacity(double value);
    [[nodiscard]] bool contours() const { return contours_; }
    void setContours(bool value);
    [[nodiscard]] bool hasSegmentation() const;
    [[nodiscard]] int regionCount() const;
    [[nodiscard]] int selectedCount() const { return regions_.selectedCount(); }
    [[nodiscard]] QString stageState() const { return stageState_; }
    [[nodiscard]] QString stageDetail() const { return stageDetail_; }
    [[nodiscard]] bool canSegment() const;
    [[nodiscard]] bool canUpdateShapes() const;
    [[nodiscard]] QString confirmText() const { return confirmText_; }
    [[nodiscard]] int fillRebuilds() const { return fillRebuilds_; }
    [[nodiscard]] int highlightRebuilds() const { return highlightRebuilds_; }

    // Le GpuCanvas de l'espace : reçoit les données de la couche à chaque révision.
    Q_INVOKABLE void attach(QObject* canvas);
    Q_INVOKABLE void detach();
    // Clic canevas (mm de scène) : sélection de la région sous le point ; faux hors image.
    Q_INVOKABLE bool pickAt(double xMm, double yMm, bool additive);
    // Pixel de l'image de travail sous un point de scène : {valid, x, y}.
    Q_INVOKABLE [[nodiscard]] QVariantMap pixelAt(double xMm, double yMm) const;
    // Segmente l'image (options vides = réglages par défaut) : une étape d'annulation.
    Q_INVOKABLE void segmentImage(const QVariantMap& options = {});
    // Régénère les formes depuis les régions ; confirmation si des retouches seraient écrasées.
    Q_INVOKABLE void updateShapes(bool confirmed = false);
    Q_INVOKABLE void cancelConfirm();

signals:
    void displayChanged();
    void stageChanged();
    void confirmChanged();
    void layerChanged();

private:
    void scheduleRefresh();
    void refresh();
    void publish();
    void computeStage();

    StudioController& studio_;
    ImageController& images_;
    RegionController& regions_;
    QPointer<QObject> canvas_;
    QTimer refreshTimer_;
    double opacity_{0.85};
    bool contours_{true};
    QString stageState_;
    QString stageDetail_;
    QString confirmText_;
    bool hadSegmentation_{false};
    std::uint64_t seenRevision_{~0ULL};
    std::uint64_t fingerprint_{0};
    bool fillContours_{true};
    std::uint64_t fillRevision_{0};
    std::uint64_t highlightRevision_{0};
    std::vector<std::uint64_t> shownSelection_;
    QImage fill_;
    QImage highlight_;
    QRect highlightBoundsPx_;
    std::vector<RegionPixelBounds> bounds_;
    std::shared_ptr<const RegionLayerData> layer_;
    int fillRebuilds_{0};
    int highlightRebuilds_{0};
};

} // namespace openstitch::desktop_v2::studio
