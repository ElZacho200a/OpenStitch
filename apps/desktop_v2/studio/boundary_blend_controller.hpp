// SPDX-License-Identifier: Apache-2.0
#pragma once
// Adaptateur Qt Quick du FONDU À LA FRONTIÈRE entre deux zones (docs/studio-fondu-frontiere-spec.md
// §8). Aucune logique métier : tout passe par `project_ops` (refus, commande, description) et par
// la session (une commande, un pas d'annulation). Coordonnées en mm « modèle » (Y vers le haut).
#include <QObject>
#include <QString>
#include <QVariantList>

#include <cstdint>

#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class BoundaryBlendController : public QObject {
    Q_OBJECT
    // Deux zones sélectionnées qui peuvent recevoir un fondu (ou en ont déjà un ensemble).
    Q_PROPERTY(bool canCreate READ canCreate NOTIFY changed)
    // Raison affichable quand deux zones sont sélectionnées mais refusées (vide sinon).
    Q_PROPERTY(QString refusal READ refusal NOTIFY changed)
    // La zone principale de la sélection porte un fondu à la frontière.
    Q_PROPERTY(bool hasBlend READ hasBlend NOTIFY changed)
    // Le fondu est actif (lien valide et frontière trouvée) ; faux : la zone est cousue en aplat.
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString partnerName READ partnerName NOTIFY changed)
    Q_PROPERTY(double widthMm READ widthMm NOTIFY changed)
    Q_PROPERTY(double partnerWidthMm READ partnerWidthMm NOTIFY changed)
    Q_PROPERTY(int curve READ curve NOTIFY changed)
    Q_PROPERTY(double frontierMm READ frontierMm NOTIFY changed)
    // Frontière courante pour le canevas : liste de polylignes, chacune une liste de {x, y} en mm.
    Q_PROPERTY(QVariantList frontier READ frontier NOTIFY changed)
    // La frontière du fondu de la zone principale est tracée à la main (lot L4).
    Q_PROPERTY(bool manualFrontier READ manualFrontier NOTIFY changed)
    // Sens des rangées dans la bande : "per_zone" | "shared" | "rotating" | "interlaced". Sans
    // fondu, c'est le choix en attente, appliqué à la prochaine création.
    Q_PROPERTY(QString rows READ rows NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY messageChanged)

public:
    explicit BoundaryBlendController(StudioController& studio, QObject* parent = nullptr);

    [[nodiscard]] bool canCreate() const { return canCreate_; }
    [[nodiscard]] QString refusal() const { return refusal_; }
    [[nodiscard]] bool hasBlend() const { return hasBlend_; }
    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] QString partnerName() const { return partnerName_; }
    [[nodiscard]] double widthMm() const { return widthMm_; }
    [[nodiscard]] double partnerWidthMm() const { return partnerWidthMm_; }
    [[nodiscard]] int curve() const { return curve_; }
    [[nodiscard]] double frontierMm() const { return frontierMm_; }
    [[nodiscard]] QVariantList frontier() const { return frontier_; }
    [[nodiscard]] bool manualFrontier() const { return !manual_.empty(); }
    [[nodiscard]] QString rows() const;
    [[nodiscard]] QString summary() const;
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool lastOk() const { return ok_; }

    // Pose (ou réédite) le fondu entre les deux zones sélectionnées. Largeurs en mm, vues depuis
    // la zone principale (dernière sélectionnée) puis l'autre. Un pas d'annulation.
    Q_INVOKABLE bool create(double widthMm, double partnerWidthMm, int curve);
    // Réédite le fondu de la zone principale (largeurs vues depuis elle). Un pas d'annulation.
    Q_INVOKABLE bool edit(double widthMm, double partnerWidthMm, int curve);
    // Retire le fondu de la zone principale (et le lien de sa voisine). Un pas d'annulation.
    Q_INVOKABLE bool remove();
    // Sens des rangées ("per_zone" | "shared" | "rotating" | "interlaced"). Avec un fondu : le
    // réédite en UN pas d'annulation ; sans fondu : mémorise le choix pour la création. Valeur
    // inconnue : refus.
    Q_INVOKABLE bool setRows(const QString& rows);
    // Frontière tracée à la main (lot L4) : liste de {x, y} en mm modèle, au moins deux points.
    // Sur deux zones sélectionnées sans fondu, crée le fondu avec cette frontière (les zones
    // peuvent ne pas se toucher) ; sur une zone qui porte un fondu, remplace sa frontière en
    // gardant largeurs et transition. Un pas d'annulation.
    Q_INVOKABLE bool setFrontier(const QVariantList& pointsMm);
    // Revient à la frontière calculée là où les zones se touchent. Un pas d'annulation.
    Q_INVOKABLE bool clearFrontier();

signals:
    void changed();
    void messageChanged();

private:
    void refresh();
    void say(bool ok, const QString& text);
    bool apply(std::uint64_t a, std::uint64_t b, double widthA, double widthB, int curve,
               const std::vector<Vec2um>& frontier);
    [[nodiscard]] std::uint64_t secondSelected() const;

    StudioController& studio_;
    std::uint64_t seenDoc_{0};
    std::uint64_t seenSel_{0};
    bool refreshed_{false};
    std::uint64_t primary_{0};
    std::uint64_t other_{0}; // seconde zone sélectionnée, ou partenaire du fondu
    bool canCreate_{false};
    QString refusal_;
    bool hasBlend_{false};
    bool active_{false};
    QString partnerName_;
    double widthMm_{2.5};
    double partnerWidthMm_{2.5};
    int curve_{0};
    double frontierMm_{0.0};
    QVariantList frontier_;
    std::vector<Vec2um> manual_;
    document::BoundaryBlendRows rows_{document::BoundaryBlendRows::PerZone};
    QString message_;
    bool ok_{true};
};

} // namespace openstitch::desktop_v2::studio
