// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "canvas_renderer.hpp"
#include "display_filters.hpp"
#include "gpu_canvas.hpp"
#include "simulation_nav.hpp"
#include "studio_controller.hpp"
#include "task_manager.hpp"

namespace openstitch::desktop_v2::studio {

// Qt adapter for the display state of the canvas: visibility filters (document mutations through
// display_filters, one undo step), grid / guides / rulers, display layers, zoom shortcuts and the
// sewing simulation. It owns no document and no stitch data; the canvas only receives immutable
// CanvasOverlays, so pan/zoom/cursor changes never rebuild the stitch geometry.
class ViewController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY changed)
    Q_PROPERTY(bool gridVisible READ gridVisible WRITE setGridVisible NOTIFY displayChanged)
    Q_PROPERTY(double gridStepMm READ gridStepMm WRITE setGridStepMm NOTIFY displayChanged)
    Q_PROPERTY(bool guidesVisible READ guidesVisible WRITE setGuidesVisible NOTIFY displayChanged)
    Q_PROPERTY(bool rulersVisible READ rulersVisible WRITE setRulersVisible NOTIFY displayChanged)
    Q_PROPERTY(QVariantList guides READ guides NOTIFY displayChanged)
    Q_PROPERTY(bool layerStitches READ layerStitches WRITE setLayerStitches NOTIFY displayChanged)
    Q_PROPERTY(bool layerJumps READ layerJumps WRITE setLayerJumps NOTIFY displayChanged)
    Q_PROPERTY(bool layerHoop READ layerHoop WRITE setLayerHoop NOTIFY displayChanged)
    Q_PROPERTY(bool layerDensity READ layerDensity WRITE setLayerDensity NOTIFY displayChanged)
    Q_PROPERTY(bool layerRisk READ layerRisk WRITE setLayerRisk NOTIFY displayChanged)
    // Famille K : rendu réaliste (vue seule). GPU quand le backend est RHI, sinon repli sur le
    // raster CPU de référence (AnalysisController::computeRealistic).
    Q_PROPERTY(bool realistic READ realistic WRITE setRealistic NOTIFY realisticChanged)
    Q_PROPERTY(bool realisticOnGpu READ realisticOnGpu NOTIFY realisticModeChanged)
    Q_PROPERTY(QString realisticMode READ realisticMode NOTIFY realisticModeChanged)
    Q_PROPERTY(double pixelsPerMmAt100READ pixelsPerMmAt100 NOTIFY displayChanged)
    Q_PROPERTY(bool simActive READ simActive NOTIFY simChanged)
    Q_PROPERTY(bool simReady READ simReady NOTIFY simChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY simChanged)
    Q_PROPERTY(int simPosition READ simPosition WRITE setSimPosition NOTIFY simChanged)
    Q_PROPERTY(int simTotal READ simTotal NOTIFY simChanged)
    Q_PROPERTY(double simSpeed READ simSpeed WRITE setSimSpeed NOTIFY simChanged)
    Q_PROPERTY(int machineSpm READ machineSpm WRITE setMachineSpm NOTIFY simChanged)
    Q_PROPERTY(QString simTimeText READ simTimeText NOTIFY simChanged)
    Q_PROPERTY(QString simEstimate READ simEstimate NOTIFY simChanged)
public:
    // `configDir` holds the small view-settings file; an empty path disables persistence.
    explicit ViewController(StudioController& studio, std::filesystem::path configDir = {},
                            QObject* parent = nullptr);
    ~ViewController() override;

    Q_INVOKABLE void attachCanvas(QObject* canvas);

    // --- Visibility filters (document mutations; one undo step each) -------------------------
    // `hex` is "#rrggbb". Return true when the document changed; message() says why otherwise.
    Q_INVOKABLE bool showOnlyColour(const QString& hex);
    Q_INVOKABLE bool showOnlyType(const QString& typeId);
    Q_INVOKABLE bool showAll();

    // --- Grid, guides, rulers, layers (display state, persisted) -----------------------------
    Q_INVOKABLE bool addGuide(bool vertical, double positionMm); // model mm, Y up
    Q_INVOKABLE void clearGuides();
    Q_INVOKABLE bool removeGuide(int index);
    Q_INVOKABLE void setGuideColor(const QColor& color);

    // --- Zoom --------------------------------------------------------------------------------
    Q_INVOKABLE void zoomRectangle(); // arms the rubber-band tool for one drag
    Q_INVOKABLE bool zoomSelection();
    Q_INVOKABLE void zoomActualSize();
    Q_INVOKABLE bool zoomPreviousView();
    // The on-screen length of `referenceMm` (model mm) measured with a physical ruler, in real
    // mm; stores the screen's px/mm for "100 %". Refuses non-positive or absurd values.
    Q_INVOKABLE bool calibrateZoom100(double measuredMm, double referenceMm = 100.0);
    // Centre the view on a model point (mm, Y up) without changing the scale.
    Q_INVOKABLE void centerOnModel(double xMm, double yMm);

    // --- Simulation --------------------------------------------------------------------------
    Q_INVOKABLE void startSimulation();
    Q_INVOKABLE void stopSimulation();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void step(int delta);
    Q_INVOKABLE void nextObject();
    Q_INVOKABLE void previousObject();
    Q_INVOKABLE void nextColour();
    Q_INVOKABLE void previousColour();
    Q_INVOKABLE void toEnd();
    Q_INVOKABLE void toStart();
    // Advances the playback by `milliseconds` of wall time (the timer calls it; tests drive it).
    Q_INVOKABLE void advance(int milliseconds);

    // --- Realistic view (famille K) -----------------------------------------------------------
    Q_INVOKABLE void toggleRealistic() { setRealistic(!realistic_); }
    [[nodiscard]] bool realistic() const { return realistic_; }
    void setRealistic(bool value);
    // Chemin effectif : vrai si le canevas tourne sur RHI (le rendu réaliste est alors du GPU).
    [[nodiscard]] bool realisticOnGpu() const { return canvas_ && canvas_->rhiBackend(); }
    // "" (désactivé), "Réaliste · GPU" ou "Réaliste · CPU (repli)".
    [[nodiscard]] QString realisticMode() const;
    // Réglages de qualité (venus d'AnalysisController) : republie, ne reconstruit la géométrie
    // réaliste que si le style change réellement.
    void setRealisticStyle(const RenderSettings& settings);
    [[nodiscard]] const RealisticStyle& realisticStyle() const { return realisticStyle_; }

    // --- Hooks for the analysis adapter -------------------------------------------------------
    void setAnalysisImage(const QString& key, std::optional<OverlayImage> image);
    void setMarkers(std::vector<OverlayMarker> markers);
    [[nodiscard]] StudioController& studio() const { return studio_; }
    [[nodiscard]] GpuCanvas* canvas() const { return canvas_; }

    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] int hiddenCount() const;
    [[nodiscard]] bool gridVisible() const { return grid_; }
    void setGridVisible(bool value);
    [[nodiscard]] double gridStepMm() const { return gridStep_; }
    void setGridStepMm(double value);
    [[nodiscard]] bool guidesVisible() const { return guidesVisible_; }
    void setGuidesVisible(bool value);
    [[nodiscard]] bool rulersVisible() const { return rulers_; }
    void setRulersVisible(bool value);
    [[nodiscard]] QVariantList guides() const;
    [[nodiscard]] bool layerStitches() const { return layerStitches_; }
    void setLayerStitches(bool value);
    [[nodiscard]] bool layerJumps() const { return layerJumps_; }
    void setLayerJumps(bool value);
    [[nodiscard]] bool layerHoop() const { return layerHoop_; }
    void setLayerHoop(bool value);
    [[nodiscard]] bool layerDensity() const { return layerDensity_; }
    void setLayerDensity(bool value);
    [[nodiscard]] bool layerRisk() const { return layerRisk_; }
    void setLayerRisk(bool value);
    [[nodiscard]] double pixelsPerMmAt100() const { return pxPerMm100_; }
    [[nodiscard]] bool simActive() const { return simActive_; }
    [[nodiscard]] bool simReady() const { return sim_ != nullptr; }
    [[nodiscard]] bool playing() const { return playing_; }
    [[nodiscard]] int simPosition() const { return static_cast<int>(position_); }
    void setSimPosition(int value);
    [[nodiscard]] int simTotal() const;
    [[nodiscard]] double simSpeed() const { return speed_; }
    void setSimSpeed(double value);
    [[nodiscard]] int machineSpm() const { return spm_; }
    void setMachineSpm(int value);
    [[nodiscard]] QString simTimeText() const;
    [[nodiscard]] QString simEstimate() const;
signals:
    void changed();
    void displayChanged();
    void simChanged();
    void messageChanged();
    void realisticChanged();     // bascule demandée par l'utilisateur
    void realisticModeChanged(); // chemin effectif (GPU / CPU) connu ou modifié
    // Repli CPU actif et motif modifié : l'image de référence est à recalculer.
    void realisticRefreshRequested();

private:
    struct SimData; // effective sequence prefix data, built on a worker per scene
    void say(const QString& text);
    void onSceneChanged();
    void rebuildSimData();
    void goTo(std::size_t position);
    void publish();
    [[nodiscard]] std::size_t segmentsAt(std::size_t position) const;
    void load();
    void save() const;

    StudioController& studio_;
    std::filesystem::path configFile_;
    QPointer<GpuCanvas> canvas_;
    QString message_;
    bool grid_{false};
    double gridStep_{10.0};
    bool guidesVisible_{true};
    bool rulers_{false};
    std::vector<double> guidesX_, guidesY_; // model mm (X right, Y up)
    QColor guideColor_{0, 170, 220};
    bool layerStitches_{true}, layerJumps_{false}, layerHoop_{true}, layerDensity_{true},
        layerRisk_{true};
    bool realistic_{false};
    bool lastRealisticGpu_{false};
    RealisticStyle realisticStyle_;
    double pxPerMm100_{96.0 / 25.4};
    std::map<QString, OverlayImage> images_;
    std::vector<OverlayMarker> markers_;
    // Simulation
    std::shared_ptr<const SimData> sim_;
    std::uint64_t simRevision_{0};
    bool simActive_{false};
    bool playing_{false};
    std::size_t position_{0};
    double speed_{20.0};
    int spm_{800};
    double carry_{0.0};
    QTimer playTimer_;
    QTimer pump_;
    TaskManager tasks_;
};

} // namespace openstitch::desktop_v2::studio
