// SPDX-License-Identifier: Apache-2.0
#pragma once

// Studio : adaptateur Qt des formes vectorielles et de leur sélection.
// Le fond raster suit la géométrie ; les poignées restent dans l'overlay des outils.
// L'espace assure le picking, les liens aux régions et les conversions.
// Toute mutation passe par DocumentSession (une seule étape d'annulation).

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <cstdint>
#include <memory>
#include <vector>

#include "layers/vector_layer.hpp"
#include "studio_controller.hpp"
#include "task_manager.hpp"

namespace openstitch::desktop_v2::studio {

class GpuCanvas;

class VectorSpaceController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int shapeCount READ shapeCount NOTIFY layerChanged)
    // Précision de la vectorisation (V1) : tolérance de simplification en mm (polygones), ou
    // courbes de Bézier avec un écart maximal en pixels. Appliquée à updateFromSegmentation().
    Q_PROPERTY(double precisionMm READ precisionMm WRITE setPrecisionMm NOTIFY optionsChanged)
    Q_PROPERTY(bool curves READ curves WRITE setCurves NOTIFY optionsChanged)
    Q_PROPERTY(double curveTolerancePx READ curveTolerancePx WRITE setCurveTolerancePx NOTIFY
                   optionsChanged)
    Q_PROPERTY(int layerBuildCount READ layerBuildCount NOTIFY layerChanged)
    // Publications servies depuis le cache de calques déjà rasterisés (aucun calcul).
    Q_PROPERTY(int layerCacheHits READ layerCacheHits NOTIFY layerChanged)
    // Vrai pendant qu'un calque est rasterisé sur un worker : l'ancien reste affiché.
    Q_PROPERTY(bool layerPending READ layerPending NOTIFY layerChanged)
    Q_PROPERTY(QVariantMap selectedShape READ selectedShape NOTIFY layerChanged)
    Q_PROPERTY(QString stageState READ stageState NOTIFY layerChanged)
    Q_PROPERTY(QString stageDetail READ stageDetail NOTIFY layerChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
public:
    explicit VectorSpaceController(StudioController& studio, QObject* parent = nullptr);
    ~VectorSpaceController() override;

    // Au-delà de ce nombre de formes visibles, la rasterisation (~50 µs par forme, mesurée dans
    // docs/desktop-v2/18) quitte le fil d'interface ; en deçà elle reste synchrone (≈ 8 ms).
    static constexpr std::size_t kSyncShapeLimit = 150;

    [[nodiscard]] int shapeCount() const;
    [[nodiscard]] int layerBuildCount() const { return builds_; }
    [[nodiscard]] int layerCacheHits() const { return cacheHits_; }
    [[nodiscard]] bool layerPending() const { return pending_; }
    // Formes du calque réellement publié (peut retarder sur le document pendant `layerPending`).
    [[nodiscard]] int publishedShapeCount() const {
        return layer_ ? static_cast<int>(layer_->shapes().size()) : 0;
    }
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

public:
    [[nodiscard]] double precisionMm() const { return precisionMm_; }
    [[nodiscard]] bool curves() const { return curves_; }
    [[nodiscard]] double curveTolerancePx() const { return curveTolerancePx_; }
    void setPrecisionMm(double mm);
    void setCurves(bool on);
    void setCurveTolerancePx(double px);

signals:
    void optionsChanged();
    void layerChanged();
    void messageChanged();

private:
    void refresh();
    // Publie un calque terminé (fil d'interface seulement).
    void publishLayer(std::shared_ptr<const VectorLayer> layer, std::vector<std::uint64_t> keys);
    void startAsyncBuild(std::vector<std::uint64_t> keys);
    [[nodiscard]] std::vector<std::uint64_t> selectedShapeIds() const;
    void say(const QString& text);
    double precisionMm_{0.2};
    bool curves_{false};
    double curveTolerancePx_{1.5};

    StudioController& studio_;
    QPointer<GpuCanvas> canvas_;
    std::shared_ptr<const VectorLayer> layer_;
    std::uint64_t builtRevision_{0};
    std::uint64_t statusRevision_{0};
    // Visual-only fingerprints: a stitch parameter change must not repaint vector shapes.
    std::vector<std::uint64_t> renderedVisualKeys_;
    // Calques déjà rasterisés, du plus récent au plus ancien (clés visuelles exactes) : revenir à
    // un état déjà vu (masquer puis ré-afficher, annuler) republie l'image sans la recalculer.
    struct CachedLayer {
        std::vector<std::uint64_t> keys;
        std::shared_ptr<const VectorLayer> layer;
    };
    std::vector<CachedLayer> layerCache_;
    int cacheHits_{0};
    [[nodiscard]] std::shared_ptr<const VectorLayer>
    cachedLayer(const std::vector<std::uint64_t>& keys);
    void rememberLayer(const std::vector<std::uint64_t>& keys,
                       const std::shared_ptr<const VectorLayer>& layer);
    int builds_{0};
    // Reconstruction asynchrone : une seule à la fois, les résultats périmés sont ignorés.
    TaskManager tasks_;
    QTimer pump_;
    std::uint64_t buildRequest_{0};
    bool pending_{false};
    std::vector<std::uint64_t> pendingVisualKeys_;
    bool dirty_{false};
    QString dirtyReason_;
    QString stageState_{QStringLiteral("empty")};
    QString stageDetail_;
    QString message_;
};

} // namespace openstitch::desktop_v2::studio
