// SPDX-License-Identifier: Apache-2.0
#pragma once

// UX-TEXT-01 lot B -- outil Texte natif : un clic au canevas ouvre une saisie sur place (création),
// un clic sur un texte existant rouvre sa saisie (édition). Adaptateur Qt SANS logique métier : le
// texte est créé / modifié par les actions du catalogue (`text.add`, `text.edit`) donc UNE étape
// d'annulation par validation, et la sélection est celle de ces actions. Ce contrôleur ne porte que
// l'état du brouillon (position, texte saisi, hauteur), jamais un second document.

#include <QObject>
#include <QString>
#include <cstdint>
#include <optional>

#include "shape_batch.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class TextTool : public QObject {
    Q_OBJECT
    // Une saisie est ouverte (création ou édition) : le canevas affiche l'éditeur.
    Q_PROPERTY(bool editing READ editing NOTIFY changed)
    // Vrai : nouveau texte ; faux : édition d'un texte existant (`textId`).
    Q_PROPERTY(bool creating READ creating NOTIFY changed)
    // Point d'ancrage de l'éditeur, mm modèle (Y vers le haut) : le clic (création) ou le coin
    // bas-gauche du texte (édition).
    Q_PROPERTY(double anchorX READ anchorX NOTIFY changed)
    Q_PROPERTY(double anchorY READ anchorY NOTIFY changed)
    // Place libre sous l'ancre, mm : en création le texte sera centré sur le clic (une demi-hauteur
    // de capitale en dessous) ; en édition l'ancre est déjà sous le texte. L'éditeur s'y pose pour
    // ne jamais masquer ce qu'il crée ou corrige.
    Q_PROPERTY(double clearanceMm READ clearanceMm NOTIFY changed)
    // Texte initial de la saisie (vide en création).
    Q_PROPERTY(QString draft READ draft NOTIFY changed)
    // Hauteur de capitale d'un nouveau texte, mm ; mémorisée d'une création à l'autre sans jamais
    // modifier les textes existants.
    Q_PROPERTY(double capHeightMm READ capHeightMm WRITE setCapHeightMm NOTIFY capHeightChanged)
    // Dernier refus de validation, en clair ; vide sinon.
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit TextTool(StudioController& studio, QObject* parent = nullptr);

    [[nodiscard]] bool editing() const { return editing_; }
    [[nodiscard]] bool creating() const { return textId_ == 0; }
    [[nodiscard]] double anchorX() const { return anchor_.x; }
    [[nodiscard]] double anchorY() const { return anchor_.y; }
    [[nodiscard]] double clearanceMm() const { return textId_ == 0 ? capHeightMm_ / 2.0 : 0.0; }
    [[nodiscard]] QString draft() const { return draft_; }
    [[nodiscard]] double capHeightMm() const { return capHeightMm_; }
    void setCapHeightMm(double value);
    [[nodiscard]] QString error() const { return error_; }

    // Clic de l'outil : (sceneX, sceneY) en mm scène (Y vers le bas), `model` le même point en mm
    // modèle. Sur un texte existant : rouvre sa saisie et sélectionne ses lettres ; ailleurs :
    // ouvre une saisie vide à cet endroit. Vrai si une saisie est ouverte.
    bool begin(double sceneX, double sceneY, PointMm model);
    // Abandonne la saisie sans rien muter ; faux s'il n'y en avait pas.
    Q_INVOKABLE bool cancel();
    // Valide la saisie par l'action du catalogue (`actions` = ActionController). Retourne le refus
    // en clair, ou une chaîne vide si c'est fait (ou s'il n'y a rien à faire : création vide
    // abandonnée, édition inchangée). En cas de refus la saisie reste ouverte.
    Q_INVOKABLE QString commit(const QString& text, QObject* actions);

    // Identifiant du texte dont l'emprise contient ce point scène ; nullopt sinon. Public pour les
    // tests et pour un futur double-clic en sélection.
    [[nodiscard]] std::optional<std::uint64_t> textAt(double sceneX, double sceneY) const;

signals:
    void changed();
    void capHeightChanged();
    void message(const QString& text);

private:
    void close();

    StudioController& studio_;
    bool editing_{false};
    std::uint64_t textId_{0}; // 0 : création
    PointMm anchor_{};
    QString draft_;
    double capHeightMm_{12.0};
    QString error_;
};

} // namespace openstitch::desktop_v2::studio
