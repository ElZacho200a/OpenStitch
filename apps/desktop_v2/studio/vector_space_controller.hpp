// SPDX-License-Identifier: Apache-2.0
#pragma once

// Studio : adaptateur Qt des formes vectorielles et de leur sélection.
// Le fond raster suit la géométrie ; le calque léger suit uniquement la sélection.
// L'espace assure aussi le picking, les liens aux régions et les conversions.
// Toute mutation passe par DocumentSession (une seule étape d'annulation).

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <cstdint>
#include <memory>
#include <vector>

#include "layers/vector_layer.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class GpuCanvas;

class VectorSpaceController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int shapeCount READ shapeCount NOTIFY layerChanged)
    Q_PROPERTY(int layerBuildCount READ layerBuildCount NOTIFY layerChanged)
    Q_PROPERTY(int selectionBuildCount READ selectionBuildCount NOTIFY layerChanged)
    Q_PROPERTY(QVariantMap selectedShape READ selectedShape NOTIFY layerChanged)
    Q_PROPERTY(QString stageState READ stageState NOTIFY layerChanged)
    Q_PROPERTY(QString stageDetail READ stageDetail NOTIFY layerChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
public:
    explicit VectorSpaceController(StudioController& studio, QObject* parent = nullptr);

    [[nodiscard]] int shapeCount() const;
    [[nodiscard]] int layerBuildCount() const { return builds_; }
    [[nodiscard]] int selectionBuildCount() const { return selectionBuilds_; }
    [[nodiscard]] QVariantMap selectedShape() const;
    [[nodiscard]] QString stageState() const { return stageState_; }
    [[nodiscard]] QString stageDetail() const { return stageDetail_; }
    [[nodiscard]] QString message() const { return message_; }

    // Branche le canevas de l'espace (propriété `canvas` de VectorSpace) ; détache le précédent.
    Q_INVOKABLE void attach(QObject* canvas);
    Q_INVOKABLE void detach();
    // Corrections de régions non répercutées (reflet de `workspace.segmentationDirty`).
    Q_INVOKABLE void setSegmentationDirty(bool dirty, const QString& reason);
    // Clic au canevas (mm scène) ; renvoie l'identifiant de forme touchée (0 : aucune).
    Q_INVOKABLE QString pickAt(double x, double y, double toleranceMm, bool toggle);
    Q_INVOKABLE void selectShape(const QString& id, bool toggle);

    // Lien amont : sélectionne la région d'origine de la forme choisie (sélection partagée) ;
    // l'appelant bascule ensuite sur l'espace Segmentation.
    Q_INVOKABLE bool selectOriginRegion();

    // Mise à jour depuis la segmentation : `updatePlan()` = {replace, add, keep, orphan} sans rien
    // modifier ; `updateFromSegmentation()` exécute UNE étape d'annulation (remplace les formes
    // dont la région a changé, ajoute celles des régions sans forme, laisse les formes à main).
    Q_INVOKABLE QVariantMap updatePlan() const;
    Q_INVOKABLE bool updateFromSegmentation();
    // Numérise en objets de broderie les formes sans objet (UNE étape d'annulation).
    Q_INVOKABLE bool convertToEmbroidery();

signals:
    void layerChanged();
    void messageChanged();

private:
    void refresh();
    [[nodiscard]] std::vector<std::uint64_t> selectedShapeIds() const;
    void say(const QString& text);

    StudioController& studio_;
    QPointer<GpuCanvas> canvas_;
    std::shared_ptr<const VectorLayer> layer_;
    std::shared_ptr<const OverlayImage> selectionLayer_;
    std::uint64_t builtRevision_{0};
    std::uint64_t statusRevision_{0};
    // Visual-only fingerprints: a stitch parameter change must not repaint vector shapes.
    std::vector<std::uint64_t> renderedVisualKeys_;
    std::vector<std::uint64_t> builtSelected_;
    int builds_{0};
    int selectionBuilds_{0};
    bool dirty_{false};
    QString dirtyReason_;
    QString stageState_{QStringLiteral("empty")};
    QString stageDetail_;
    QString message_;
};

} // namespace openstitch::desktop_v2::studio
