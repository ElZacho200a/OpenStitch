// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <mutex>
#include <vector>

#include "image_prep.hpp"
#include "openstitch/image/ops.hpp"
#include "studio_controller.hpp"
#include "task_manager.hpp"

namespace openstitch::desktop_v2::studio {

// Images d'aperçu partagées entre les contrôleurs de « Préparer » et le fournisseur d'images QML
// (`image://prepare/<clé>`). Clés : « before » (image d'origine), « after » (image de travail),
// « preview » (aperçu non appliqué), « segmentation » (aperçu de segmentation), « regions »
// (carte des régions du projet). Accès protégé : le fournisseur lit depuis le fil de rendu.
class PrepareImages {
public:
    void set(const QString& key, const QImage& image);
    [[nodiscard]] QImage get(const QString& key) const;

private:
    mutable std::mutex mutex_;
    std::vector<std::pair<QString, QImage>> images_;
};

class PrepareImageProvider : public QQuickImageProvider {
public:
    explicit PrepareImageProvider(std::shared_ptr<PrepareImages> images)
        : QQuickImageProvider(QQuickImageProvider::Image), images_(std::move(images)) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    std::shared_ptr<PrepareImages> images_;
};

// Famille E « Images, prétraitement, segmentation » : adaptateur Qt au-dessus de `image_prep`,
// `segmentation_preview`, `source_image_tools` et des opérations `project_ops`. Aucune logique
// métier : toute mutation passe par DocumentSession (une étape d'annulation), les calculs longs
// par son TaskManager (annulables, hors fil d'interface).
class ImageController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY changed)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY changed)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY changed)
    Q_PROPERTY(QStringList ops READ ops NOTIFY changed)
    Q_PROPERTY(int imageRevision READ imageRevision NOTIFY imagesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyLabel READ busyLabel NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)
    Q_PROPERTY(bool prepPreviewActive READ prepPreviewActive NOTIFY previewChanged)
    Q_PROPERTY(bool segPreviewActive READ segPreviewActive NOTIFY previewChanged)
    Q_PROPERTY(QString segPreviewInfo READ segPreviewInfo NOTIFY previewChanged)
    Q_PROPERTY(bool hasSegmentation READ hasSegmentation NOTIFY changed)
    Q_PROPERTY(int regionCount READ regionCount NOTIFY changed)
    // Nombre de couleurs DISTINCTES de la segmentation du projet (<= « couleurs maximum »).
    Q_PROPERTY(int colorCount READ colorCount NOTIFY changed)
    Q_PROPERTY(QVariantList prepCatalog READ prepCatalog CONSTANT)
    Q_PROPERTY(QVariantMap segmentationDefaults READ segmentationDefaults CONSTANT)
    Q_PROPERTY(QVariantList aiBackends READ aiBackends NOTIFY aiChanged)
    Q_PROPERTY(QString aiSummary READ aiSummary NOTIFY aiChanged)
    Q_PROPERTY(bool aiBusy READ aiBusy NOTIFY aiChanged)
public:
    explicit ImageController(StudioController& studio, QObject* parent = nullptr);
    ~ImageController() override;

    [[nodiscard]] std::shared_ptr<PrepareImages> images() const { return images_; }
    // À enregistrer par l'hôte : `engine.addImageProvider("prepare", controller.createProvider())`.
    [[nodiscard]] PrepareImageProvider* createProvider() const {
        return new PrepareImageProvider(images_);
    }

    [[nodiscard]] bool hasImage() const;
    [[nodiscard]] int imageWidth() const { return width_; }
    [[nodiscard]] int imageHeight() const { return height_; }
    [[nodiscard]] QStringList ops() const { return ops_; }
    [[nodiscard]] int imageRevision() const { return imageRevision_; }
    [[nodiscard]] bool busy() const { return busy_; }
    [[nodiscard]] QString busyLabel() const { return busyLabel_; }
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool messageIsError() const { return messageIsError_; }
    [[nodiscard]] bool prepPreviewActive() const { return prepPreview_; }
    [[nodiscard]] bool segPreviewActive() const { return segPreview_; }
    [[nodiscard]] QString segPreviewInfo() const { return segInfo_; }
    [[nodiscard]] bool hasSegmentation() const;
    [[nodiscard]] int regionCount() const;
    [[nodiscard]] int colorCount() const;
    [[nodiscard]] QVariantList prepCatalog() const;
    [[nodiscard]] QVariantMap segmentationDefaults() const;
    [[nodiscard]] QVariantList aiBackends() const { return ai_; }
    [[nodiscard]] QString aiSummary() const { return aiSummary_; }
    [[nodiscard]] bool aiBusy() const { return aiBusy_; }

    // --- Prétraitement (non destructif : pile d'opérations rejouée depuis l'original) --------
    // kind : brightness_contrast, grayscale, sharpen, clahe, median, bilateral, flatten, quantize,
    // flip_h, flip_v, rotate_right, rotate_left, rotate_180, crop. "" = appliqué, sinon le refus.
    Q_INVOKABLE QString applyPrep(const QString& kind, const QVariantMap& params);
    // Aperçu AVANT/APRÈS sans toucher au document (calcul annulable) : image « preview ».
    Q_INVOKABLE void previewPrep(const QString& kind, const QVariantMap& params);
    Q_INVOKABLE void clearPrepPreview();

    // --- Segmentation couleur ----------------------------------------------------------------
    // options : max_colors, min_region_px, smoothing_radius_px. Aperçu annulable, document intact.
    Q_INVOKABLE void previewSegmentation(const QVariantMap& options);
    Q_INVOKABLE void cancelSegmentationPreview();
    // Segmente l'image de travail et installe la segmentation (UNE étape d'annulation). Les objets
    // existants ne sont pas régénérés : `segmentationApplied()` le signale à « Régions ».
    Q_INVOKABLE void applySegmentation(const QVariantMap& options);
    Q_INVOKABLE void cancelTask();

    // --- Segmentation IA (backends déclarés dans les réglages IA) ---------------------------
    Q_INVOKABLE void refreshAiBackends();

signals:
    void changed();
    void imagesChanged();
    void busyChanged();
    void messageChanged();
    void previewChanged();
    void aiChanged();
    // Une opération d'image ou une segmentation a changé le document : les objets sont obsolètes.
    void prepApplied(const QString& label);
    void segmentationApplied(int regions);

private:
    void setMessage(const QString& text, bool error = false);
    void setBusy(bool busy, const QString& label = {});
    void scheduleRefresh();
    void refreshNow();
    void trackOps();
    [[nodiscard]] segmentation::SegmentationOptions optionsFrom(const QVariantMap& map) const;

    StudioController& studio_;
    std::shared_ptr<PrepareImages> images_{std::make_shared<PrepareImages>()};
    DocumentSession::Token token_{};
    QTimer pump_;
    QTimer refreshTimer_;
    int width_{0};
    int height_{0};
    QStringList ops_;
    std::vector<image::ImageOp> lastOps_;
    std::uint64_t lastOriginalKey_{0};
    int imageRevision_{0};
    bool busy_{false};
    QString busyLabel_;
    QString message_;
    bool messageIsError_{false};
    bool prepPreview_{false};
    bool segPreview_{false};
    QString segInfo_;
    QVariantList ai_;
    QString aiSummary_;
    bool aiBusy_{false};
    TaskId refreshTask_{0};
    TaskId prepTask_{0};
    TaskId segTask_{0};
    TaskId applyTask_{0};
    TaskId aiTask_{0};
    // Détruit avant les membres ci-dessus : ses rappels les utilisent.
    TaskManager tasks_;
};

} // namespace openstitch::desktop_v2::studio
