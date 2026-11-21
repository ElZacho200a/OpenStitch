// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/core/ids.hpp"

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <cstdint>
#include <limits>
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
    Q_PROPERTY(double imageWidthMm READ imageWidthMm NOTIFY layerChanged)
    Q_PROPERTY(double imageHeightMm READ imageHeightMm NOTIFY layerChanged)
    Q_PROPERTY(bool canResize READ canResize NOTIFY changed)
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
    // Outil actif au canevas : "" (sélection), "crop", "pick" (fond transparent), "measure".
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(int pickTolerance READ pickTolerance WRITE setPickTolerance NOTIFY toolChanged)
    Q_PROPERTY(bool pickContiguous READ pickContiguous WRITE setPickContiguous NOTIFY toolChanged)
    // Nombre de calques image du document (> 1 : document multi-calques, `.ora` par exemple).
    Q_PROPERTY(int imageLayerCount READ imageLayerCount NOTIFY changed)
    // Calque de la source image ACTIVE verrouillé : tous les outils de mutation de cet espace
    // (recadrage, pipette, échelle, pile de traitements) se refusent tant qu'il l'est.
    Q_PROPERTY(bool activeLayerLocked READ activeLayerLocked NOTIFY changed)
    // Visibilité / verrou de chaque entrée de `imageTools.imageLayers`, DANS LE MÊME ORDRE : une
    // liste de {visible, locked}, pour les icônes du sélecteur de calque image.
    Q_PROPERTY(QVariantList imageLayerStates READ imageLayerStates NOTIFY changed)
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
    [[nodiscard]] double imageWidthMm() const;
    [[nodiscard]] double imageHeightMm() const;
    // Vrai tant qu'aucun objet de broderie n'existe : l'échelle de l'image peut alors changer
    // sans désaccord avec des objets déjà numérisés (ceux-ci se redimensionnent par la sélection).
    [[nodiscard]] bool canResize() const;
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
    [[nodiscard]] QString tool() const;
    void setTool(const QString& tool);
    [[nodiscard]] int pickTolerance() const { return pickTolerance_; }
    void setPickTolerance(int tolerance);
    [[nodiscard]] bool pickContiguous() const { return pickContiguous_; }
    void setPickContiguous(bool contiguous);
    [[nodiscard]] int imageLayerCount() const;
    [[nodiscard]] bool activeLayerLocked() const;
    [[nodiscard]] QVariantList imageLayerStates() const;
    // Bascule la visibilité / le verrou du calque de l'entrée `index` de `imageTools.imageLayers`
    // (sans effet hors bornes ou sur un document à un seul calque implicite). Une étape
    // d'annulation.
    Q_INVOKABLE void toggleLayerVisible(int index);
    Q_INVOKABLE void toggleLayerLocked(int index);

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
    // Redimensionne l'image source (ratio conservé : les pixels ne changent pas, seule leur taille
    // physique). Tous les calques image changent du même facteur, pour rester alignés. Une étape
    // d'annulation. Renvoie un message d'erreur, vide en cas de succès.
    Q_INVOKABLE QString setImageWidthMm(double widthMm);
    Q_INVOKABLE QString setImageHeightMm(double heightMm);
    Q_INVOKABLE QString scaleImage(double percent);
    Q_INVOKABLE void beginCrop();
    Q_INVOKABLE void cancelCrop();
    Q_INVOKABLE void setCrop(double x, double y, double w, double h);
    // Multi-calques : le cadre s'applique à tous les calques image de même taille de travail que
    // le calque actif (la toile commune d'un `.ora`), en UNE étape ; les autres ne bougent pas.
    Q_INVOKABLE QString applyCrop();
    // Pipette « fond transparent » : la couleur du pixel d'écran (sx, sy) devient transparente
    // dans le calque actif (zone contiguë ou toute la couleur selon `pickContiguous`).
    Q_INVOKABLE QString makeTransparentAt(double sx, double sy);
    // Couleur (#rrggbb) du pixel d'écran dans l'image de travail, vide hors de l'image.
    Q_INVOKABLE QString colorAt(double sx, double sy) const;
    // Règle : une longueur mesurée de `lengthPx` pixels de l'image mesure `realMm` en vrai. Change
    // l'échelle de TOUS les calques image d'un même facteur (leur alignement est conservé).
    Q_INVOKABLE QString scaleFromMeasure(double lengthPx, double realMm);
    // Repères : pixel de l'image de travail <-> pixel d'écran du canevas.
    Q_INVOKABLE QPointF imageToScreen(double px, double py) const;
    Q_INVOKABLE QPointF screenToImage(double sx, double sy) const;

signals:
    void changed();
    void viewChanged();
    void layerChanged();
    void cropChanged();
    void messageChanged();
    void toolChanged();

private:
    void refresh();
    void publishLayer();
    void trackSegmentation();
    void setMessage(const QString& text);
    // Message de refus commun aux outils de mutation d'image quand le calque de la source image
    // active est verrouillé ; vide (et aucun message publié) si l'opération peut continuer.
    QString lockedLayerRefusal();

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
    ObjectId observedImageSource_;
    std::uint64_t observedRevision_{std::numeric_limits<std::uint64_t>::max()};
    // Identité des images SOURCE, avant la réduction potentiellement coûteuse à 4096 px.
    bool imageKeysSet_{false};
    qint64 beforeKey_{0};
    qint64 afterKey_{0};
    double imageMmPerPx_{0};
    QString message_;
    QString tool_;
    int pickTolerance_{24};
    bool pickContiguous_{true};
};

} // namespace openstitch::desktop_v2::studio
