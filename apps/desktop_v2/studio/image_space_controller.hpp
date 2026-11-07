// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <memory>

#include "layers/source_image_layer.hpp"

namespace openstitch::desktop_v2::studio {

class GpuCanvas;
class ImageController;
class StudioController;

// Contrôleur de contexte de l'espace « Image » (`imageSpace`) : adaptateur Qt SANS logique métier
// propre. Il relie l'image de travail de `ImageController` à la couche `sourceImage` du canevas,
// porte l'état d'affichage (avant / après / rideau), la pile d'opérations visible (supprimer et
// réordonner une opération = UNE étape d'annulation) et l'outil Recadrer. L'état d'étape publié
// (`stageState`, `stageDetail`) est transmis au bandeau par la QML via `workspace.setStageStatus`.
class ImageSpaceController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY changed)
    Q_PROPERTY(double mmPerPx READ mmPerPx NOTIFY changed)
    Q_PROPERTY(int imageWidthPx READ imageWidthPx NOTIFY layerChanged)
    Q_PROPERTY(int imageHeightPx READ imageHeightPx NOTIFY layerChanged)
    Q_PROPERTY(int compareMode READ compareMode WRITE setCompareMode NOTIFY viewChanged)
    Q_PROPERTY(double curtain READ curtain WRITE setCurtain NOTIFY viewChanged)
    Q_PROPERTY(int layerRevision READ layerRevision NOTIFY layerChanged)
    Q_PROPERTY(QVariantList opStack READ opStack NOTIFY changed)
    Q_PROPERTY(bool cropActive READ cropActive NOTIFY cropChanged)
    Q_PROPERTY(double cropX READ cropX NOTIFY cropChanged)
    Q_PROPERTY(double cropY READ cropY NOTIFY cropChanged)
    Q_PROPERTY(double cropW READ cropW NOTIFY cropChanged)
    Q_PROPERTY(double cropH READ cropH NOTIFY cropChanged)
    Q_PROPERTY(bool imageChanged READ imageChanged NOTIFY changed)
    Q_PROPERTY(QString stageState READ stageState NOTIFY changed)
    Q_PROPERTY(QString stageDetail READ stageDetail NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
public:
    ImageSpaceController(StudioController& studio, ImageController& images,
                         QObject* parent = nullptr);
    ~ImageSpaceController() override;

    [[nodiscard]] bool hasImage() const;
    [[nodiscard]] double mmPerPx() const;
    // Dimensions de l'image de travail (pixels), identiques à celles de ImageController.
    [[nodiscard]] int imageWidthPx() const;
    [[nodiscard]] int imageHeightPx() const;
    [[nodiscard]] int compareMode() const { return static_cast<int>(view_.mode); }
    void setCompareMode(int mode);
    [[nodiscard]] double curtain() const { return view_.curtain; }
    void setCurtain(double fraction);
    [[nodiscard]] int layerRevision() const { return layerRevision_; }
    [[nodiscard]] QVariantList opStack() const;
    [[nodiscard]] bool cropActive() const { return cropActive_; }
    [[nodiscard]] double cropX() const { return cx_; }
    [[nodiscard]] double cropY() const { return cy_; }
    [[nodiscard]] double cropW() const { return cw_; }
    [[nodiscard]] double cropH() const { return ch_; }
    // Vrai quand un traitement a invalidé une segmentation existante (à refaire).
    [[nodiscard]] bool imageChanged() const { return imageChanged_; }
    [[nodiscard]] QString stageState() const;
    [[nodiscard]] QString stageDetail() const;
    [[nodiscard]] QString message() const { return message_; }

    // Branche le GpuCanvas de l'espace (appelé par la QML) ; la couche est publiée aussitôt.
    Q_INVOKABLE void attachCanvas(QObject* canvas);
    Q_INVOKABLE void detachCanvas();
    // Cadre l'image de travail dans la fenêtre du canevas (mm de scène).
    Q_INVOKABLE void fit();
    // Pile d'opérations : « » = fait, sinon le refus lisible. Chaque appel = UNE étape
    // d'annulation.
    Q_INVOKABLE QString removeOp(int index);
    Q_INVOKABLE QString moveOp(int index, int delta);
    // Recadrage au canevas : le cadre est en pixels de l'image de travail.
    Q_INVOKABLE void beginCrop();
    Q_INVOKABLE void cancelCrop();
    Q_INVOKABLE void setCrop(double x, double y, double w, double h);
    Q_INVOKABLE QString applyCrop();
    // Repères : pixel de l'image de travail <-> pixel d'écran du canevas.
    Q_INVOKABLE QPointF imageToScreen(double px, double py) const;
    Q_INVOKABLE QPointF screenToImage(double sx, double sy) const;

signals:
    void changed();
    void viewChanged();
    void layerChanged();
    void cropChanged();
    void messageChanged();

private:
    void refresh();
    void publishLayer();
    void trackSegmentation();
    void setMessage(const QString& text);

    StudioController& studio_;
    ImageController& images_;
    QPointer<GpuCanvas> canvas_;
    SourceImageView view_;
    int layerRevision_{0};
    bool cropActive_{false};
    double cx_{0}, cy_{0}, cw_{0}, ch_{0};
    bool hadSegmentation_{false};
    bool imageChanged_{false};
    std::size_t lastOpCount_{0};
    QString message_;
};

} // namespace openstitch::desktop_v2::studio
