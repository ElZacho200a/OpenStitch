// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <cstdint>
#include <vector>

#include "image_controller.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

// Zones de détail de l'espace « Segmentation » : adaptateur Qt au-dessus de `source_image_tools`
// (`paintDetailPixels`, `clearDetail`). L'utilisateur peint à l'endroit où il veut conserver les
// petites formes ; le masque (enregistré dans le projet) adoucit alors le lissage et le nettoyage
// de la segmentation, et affine la vectorisation de ces régions. Un trait = UNE commande annulable.
// Le masque se peint avant comme après la segmentation : il ne s'applique qu'à la suivante.
// Aucune logique métier ici : conversion mm -> pixel, incrustation, messages.
class DetailZoneController : public QObject {
    Q_OBJECT
    // "" (hors outil) | "paint" | "erase".
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(double radiusMm READ radiusMm WRITE setRadiusMm NOTIFY radiusChanged)
    Q_PROPERTY(bool hasDetail READ hasDetail NOTIFY maskChanged)
    // Part de l'image marquée « détail », en % (0 sans masque).
    Q_PROPERTY(double coveragePercent READ coveragePercent NOTIFY maskChanged)
    // Change quand l'incrustation `image://prepare/detail` doit être relue.
    Q_PROPERTY(int overlayRevision READ overlayRevision NOTIFY maskChanged)
    // Le masque a changé depuis la dernière segmentation : la relancer l'applique.
    Q_PROPERTY(bool needsSegmentation READ needsSegmentation NOTIFY maskChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)
public:
    DetailZoneController(StudioController& studio, ImageController& images,
                         QObject* parent = nullptr);
    ~DetailZoneController() override;

    [[nodiscard]] QString tool() const { return tool_; }
    void setTool(const QString& tool);
    [[nodiscard]] double radiusMm() const { return radiusMm_; }
    void setRadiusMm(double radiusMm);
    [[nodiscard]] bool hasDetail() const { return coverage_ > 0.0; }
    [[nodiscard]] double coveragePercent() const { return coverage_; }
    [[nodiscard]] int overlayRevision() const { return overlayRevision_; }
    [[nodiscard]] bool needsSegmentation() const { return dirty_ && hasDetail(); }
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool messageIsError() const { return messageIsError_; }

    // Agrandit / réduit le rayon d'un cran (~20 %), borné aux limites du service.
    Q_INVOKABLE void stepRadius(int steps);
    // Trait : points {x, y} en mm « scène » (Y vers le bas, origine au centre de l'image).
    // Retourne "" si fait, sinon le refus.
    Q_INVOKABLE QString paintStroke(const QVariantList& scenePointsMm);
    Q_INVOKABLE QString clearAll();

signals:
    void toolChanged();
    void radiusChanged();
    void maskChanged();
    void messageChanged();

private:
    void setMessage(const QString& text, bool error = false);
    void scheduleRefresh();
    void refresh();

    StudioController& studio_;
    ImageController& images_;
    DocumentSession::Token token_{};
    QTimer refreshTimer_;
    QString tool_;
    double radiusMm_{4.0};
    double coverage_{0.0};
    int overlayRevision_{0};
    // Masque observé à la dernière actualisation : pas de reconstruction d'incrustation si rien
    // n'a changé (comparaison exacte).
    std::vector<std::uint8_t> shownWeights_;
    int shownWidth_{0};
    int shownHeight_{0};
    bool dirty_{false};
    QString message_;
    bool messageIsError_{false};
};

} // namespace openstitch::desktop_v2::studio
