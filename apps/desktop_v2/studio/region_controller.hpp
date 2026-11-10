// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>
#include <vector>

#include "image_controller.hpp"
#include "region_edit.hpp"
#include "studio_controller.hpp"
#include "task_manager.hpp"

namespace openstitch::desktop_v2::studio {

// Famille E « édition des régions » : adaptateur Qt au-dessus de `region_edit`. Chaque opération
// est UNE commande annulable de la session ; la régénération des objets (long) passe par le
// TaskManager, annulable, et avertit avant d'écraser des retouches de points.
class RegionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasSegmentation READ hasSegmentation NOTIFY regionsChanged)
    Q_PROPERTY(QVariantList regions READ regions NOTIFY regionsChanged)
    Q_PROPERTY(int mapRevision READ mapRevision NOTIFY regionsChanged)
    Q_PROPERTY(int mapWidth READ mapWidth NOTIFY regionsChanged)
    Q_PROPERTY(int mapHeight READ mapHeight NOTIFY regionsChanged)
    Q_PROPERTY(QVariantList selected READ selected NOTIFY selectionChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(bool stale READ stale NOTIFY staleChanged)
    Q_PROPERTY(QString staleReason READ staleReason NOTIFY staleChanged)
    Q_PROPERTY(QVariantMap handEdits READ handEdits NOTIFY regionsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(double brushRadius READ brushRadius WRITE setBrushRadius NOTIFY toolChanged)
public:
    RegionController(StudioController& studio, ImageController& images, QObject* parent = nullptr);
    ~RegionController() override;

    [[nodiscard]] bool hasSegmentation() const;
    [[nodiscard]] QVariantList regions() const { return regions_; }
    [[nodiscard]] int mapRevision() const { return mapRevision_; }
    [[nodiscard]] int mapWidth() const;
    [[nodiscard]] int mapHeight() const;
    [[nodiscard]] QVariantList selected() const;
    [[nodiscard]] int selectedCount() const { return static_cast<int>(selected_.size()); }
    [[nodiscard]] bool stale() const { return stale_; }
    [[nodiscard]] QString staleReason() const { return staleReason_; }
    [[nodiscard]] QVariantMap handEdits() const;
    [[nodiscard]] bool busy() const { return busy_; }
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool messageIsError() const { return messageIsError_; }
    [[nodiscard]] QString tool() const { return tool_; }
    [[nodiscard]] double brushRadius() const { return brushRadius_; }
    void setTool(const QString& tool);
    void setBrushRadius(double radiusPx);

    // --- Sélection ---------------------------------------------------------------------------
    // mode : "replace" | "toggle" | "add".
    Q_INVOKABLE void selectRegion(double id, const QString& mode = QStringLiteral("replace"));
    // Pixel de l'image de travail (clic sur la carte des régions).
    Q_INVOKABLE void selectAtPixel(int x, int y, bool additive = false);
    Q_INVOKABLE void selectSameColor();
    Q_INVOKABLE void selectNeighbours();
    Q_INVOKABLE void invertSelection();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    // Objets de broderie issus des régions sélectionnées (identifiants).
    Q_INVOKABLE [[nodiscard]] QVariantList objectsOfSelection() const;

    // --- Retouches (chacune : UNE étape d'annulation ; "" = fait, sinon le refus) -------------
    Q_INVOKABLE QString recolorSelected(const QString& hexColor);
    Q_INVOKABLE QString mergeSelected();
    Q_INVOKABLE QString mergeWithNeighbour(double id);
    Q_INVOKABLE QString mergeColorInto(double source, double target);
    Q_INVOKABLE QString deleteSelected();
    Q_INVOKABLE QString cleanIslands(int minPixels);
    Q_INVOKABLE QString fillHoles(int maxPixels);
    // Aperçu de la réduction de palette : {before, after, recolors, merges}.
    Q_INVOKABLE [[nodiscard]] QVariantMap previewReduction(int maxColors, double maxDistance,
                                                           bool mergeAdjacent) const;
    Q_INVOKABLE QString reducePalette(int maxColors, double maxDistance, bool mergeAdjacent);
    // Pinceau / gomme sur la région sélectionnée : points {x, y} en pixels de l'image de travail.
    Q_INVOKABLE QString paintStroke(const QVariantList& pointsPx, bool erase);

    // --- Régénération des objets -------------------------------------------------------------
    // Sans `confirmed`, des retouches de points qui seraient remplacées déclenchent
    // `confirmRequested` : rien n'est modifié tant que l'utilisateur n'a pas confirmé.
    // `settings` : max_colors, min_region_px, smoothing_px (re-segmentation si `resegment`).
    Q_INVOKABLE void regenerate(bool resegment, bool confirmed = false,
                                const QVariantMap& settings = {});
    Q_INVOKABLE void cancelTask();

signals:
    void regionsChanged();
    void selectionChanged();
    void staleChanged();
    void busyChanged();
    void messageChanged();
    void toolChanged();
    void confirmRequested(const QString& question);
    void regenerated(int objectsBefore, int objectsAfter);

private:
    void setMessage(const QString& text, bool error = false);
    void setStale(bool stale, const QString& reason = {});
    void scheduleRefresh();
    void refresh();
    QString report(const ServiceResult& result, const QString& done);

    StudioController& studio_;
    ImageController& images_;
    DocumentSession::Token token_{};
    QTimer refreshTimer_;
    QVariantList regions_;
    int mapRevision_{0};
    std::uint64_t observedDocumentRevision_{0};
    std::vector<std::uint64_t> selected_;
    bool stale_{false};
    QString staleReason_;
    bool busy_{false};
    QString message_;
    bool messageIsError_{false};
    QString tool_{QStringLiteral("select")};
    double brushRadius_{4.0};
    std::uint64_t seenRevision_{0};
    TaskId task_{0};
    // Détruit avant les membres ci-dessus : ses rappels les utilisent.
    TaskManager tasks_;
};

} // namespace openstitch::desktop_v2::studio
