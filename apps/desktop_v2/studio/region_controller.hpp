// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
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
    // Question en attente avant d'écraser des retouches ("" = aucune). Une seule source pour tous
    // les panneaux : la demande mémorisée est celle que `confirmRegeneration()` exécutera.
    Q_PROPERTY(QString confirmText READ confirmText NOTIFY confirmChanged)
    // Mode « choisir la cible » : le prochain clic de région (liste, carte ou canevas) désigne la
    // région qui absorbe l'unique région sélectionnée.
    Q_PROPERTY(bool mergeTargetMode READ mergeTargetMode WRITE setMergeTargetMode NOTIFY
                   mergeTargetModeChanged)
    // Satin de la génération des objets : relais de `StudioController::satinOptions` (un seul
    // état pour toute l'interface). Un changement rend les objets de l'image périmés.
    Q_PROPERTY(
        QVariantMap satinOptions READ satinOptions WRITE setSatinOptions NOTIFY satinOptionsChanged)
    // Avertissements du moteur de la dernière régénération réussie, dans son ordre :
    // {text, region} (region = identifiant cité « région N », -1 sinon). État propre : vidé
    // seulement au lancement de la génération suivante, jamais par un autre message.
    Q_PROPERTY(QVariantList warnings READ warnings NOTIFY warningsChanged)
    // Résumé court (« 3 régions écartées », « 1 avertissement »…) ; "" sans avertissement.
    Q_PROPERTY(QString warningSummary READ warningSummary NOTIFY warningsChanged)
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
    [[nodiscard]] QString confirmText() const { return confirmText_; }
    [[nodiscard]] bool mergeTargetMode() const { return mergeTargetMode_; }
    void setMergeTargetMode(bool on);
    [[nodiscard]] QVariantMap satinOptions() const { return studio_.satinOptions(); }
    void setSatinOptions(const QVariantMap& options) { studio_.setSatinOptions(options); }
    [[nodiscard]] QVariantList warnings() const { return warnings_; }
    [[nodiscard]] QString warningSummary() const;
    // Réglages réellement transmis au moteur par la dernière régénération lancée.
    [[nodiscard]] const project_ops::DigitizeSettings& lastDigitizeSettings() const {
        return lastDigitizeSettings_;
    }
    // Fin d'une régénération (appelé sur le fil d'interface par la tâche) : applique le résultat
    // en une étape d'annulation, puis publie le résumé et les avertissements du moteur.
    void finishRegeneration(Result<Regeneration>&& result);

    // --- Sélection ---------------------------------------------------------------------------
    // mode : "replace" | "toggle" | "add".
    Q_INVOKABLE void selectRegion(double id, const QString& mode = QStringLiteral("replace"));
    // Pixel de l'image de travail (clic sur la carte des régions).
    Q_INVOKABLE void selectAtPixel(int x, int y, bool additive = false);
    // Cadre ou lasso : polygone de points {x, y} en pixels (fractionnaires) de l'image de travail.
    // `additive` ajoute à la sélection au lieu de la remplacer ; `touching` prend aussi les
    // régions seulement touchées (sinon : entièrement dans la zone). Les régions verrouillées sont
    // écartées. Renvoie le nombre de régions désignées par la zone.
    Q_INVOKABLE int selectInZone(const QVariantList& polygonPx, bool additive = false,
                                 bool touching = false);
    Q_INVOKABLE void selectSameColor();
    Q_INVOKABLE void selectNeighbours();
    Q_INVOKABLE void invertSelection();
    Q_INVOKABLE void selectAll();
    // Parcours séquentiel de la liste des régions : sélection unique, retour à la première
    // (ou à la dernière) en bout de liste. Faux si la segmentation n'a aucune région.
    Q_INVOKABLE bool selectNextRegion();
    Q_INVOKABLE bool selectPreviousRegion();
    // Verrouillage d'une région : état de SESSION (non enregistré dans le projet), pour protéger
    // une région déjà jugée correcte pendant qu'on retouche les autres. Désélectionne si besoin.
    Q_INVOKABLE void toggleLockRegion(double id);
    Q_INVOKABLE [[nodiscard]] bool isRegionLocked(double id) const;
    // Agrandit (pas > 0) ou réduit (pas < 0) le rayon du pinceau d'un pixel par pas, borné 1-200.
    Q_INVOKABLE void stepBrushRadius(int steps);
    Q_INVOKABLE void clearSelection();
    // Reprend les régions de la sélection partagée de la session (lien « Aller à la région » de
    // l'espace Vecteurs). Faux si elle n'en contient aucune qui existe encore, ou si cette demande
    // (révision de la sélection partagée) a déjà été reprise. Coupe le choix de cible de fusion.
    Q_INVOKABLE bool adoptSessionSelection();
    // Objets de broderie issus des régions sélectionnées (identifiants).
    Q_INVOKABLE [[nodiscard]] QVariantList objectsOfSelection() const;

    // --- Retouches (chacune : UNE étape d'annulation ; "" = fait, sinon le refus) -------------
    Q_INVOKABLE QString recolorSelected(const QString& hexColor);
    Q_INVOKABLE QString mergeSelected();
    // Merge the single selected (source) region into a user-chosen target.
    Q_INVOKABLE QString mergeSelectedInto(double target);
    Q_INVOKABLE QString mergeWithNeighbour(double id);
    Q_INVOKABLE QString mergeColorInto(double source, double target);
    // Sépare la région sélectionnée en ses composantes connexes (zones non reliées réunies par
    // une fusion) ; refuse si elle n'en a qu'une.
    Q_INVOKABLE QString splitSelected();
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
    // `settings` : max_colors, min_region_px, smoothing_radius_px (les clés de
    // `ImageController::segmentationDefaults`), utilisées pour la re-segmentation si `resegment`.
    Q_INVOKABLE void regenerate(bool resegment, bool confirmed = false,
                                const QVariantMap& settings = {});
    // Exécute la demande en attente de confirmation (la même, avec les mêmes réglages) ; sans effet
    // s'il n'y en a pas.
    Q_INVOKABLE void confirmRegeneration();
    Q_INVOKABLE void cancelConfirmation();
    Q_INVOKABLE void cancelTask();

signals:
    void regionsChanged();
    void selectionChanged();
    void staleChanged();
    void busyChanged();
    void messageChanged();
    void toolChanged();
    void confirmChanged();
    void mergeTargetModeChanged();
    void satinOptionsChanged();
    void warningsChanged();
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
    bool stepRegion(int direction);
    QVariantList regions_;
    int mapRevision_{0};
    std::uint64_t observedDocumentRevision_{0};
    ObjectId observedImageSource_;
    std::map<ObjectId, QString> staleSources_;
    std::vector<std::uint64_t> selected_;
    std::set<std::uint64_t> locked_;
    void applyLockFlags();
    bool stale_{false};
    QString staleReason_;
    bool busy_{false};
    QString message_;
    bool messageIsError_{false};
    QString tool_{QStringLiteral("select")};
    double brushRadius_{4.0};
    QString confirmText_;
    bool pendingResegment_{false};
    QVariantMap pendingSettings_;
    QVariantList warnings_;
    void setWarnings(const std::vector<std::string>& warnings);
    project_ops::DigitizeSettings lastDigitizeSettings_;
    bool mergeTargetMode_{false};
    // Révision de la sélection partagée déjà examinée : une demande « Aller à la région » n'est
    // reprise qu'une fois, mais chaque nouvelle demande l'est, même vers la même région.
    std::optional<std::uint64_t> adoptedSelectionRevision_;
    std::uint64_t seenRevision_{0};
    TaskId task_{0};
    // Détruit avant les membres ci-dessus : ses rappels les utilisent.
    TaskManager tasks_;
};

} // namespace openstitch::desktop_v2::studio
