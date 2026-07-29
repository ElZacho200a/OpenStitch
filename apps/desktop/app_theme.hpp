// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>

#include "design_tokens.hpp"

class QApplication;

namespace openstitch::desktop {

// Applique un thème (palette Qt + feuille de style CIBLÉE générée depuis les
// tokens) à toute l'application. Point d'accès unique aux tokens courants pour le
// dessin du canevas. Persiste le choix (mode + densité) via QSettings.
//
// N'est PAS une seconde vérité métier : ne concerne que l'apparence.
class AppTheme : public QObject {
    Q_OBJECT

public:
    static AppTheme& instance();

    // Charge le choix persistant puis applique le thème à l'application.
    void applyToApp(QApplication& app);

    [[nodiscard]] const Tokens& tokens() const { return tokens_; }
    [[nodiscard]] ThemeMode mode() const { return mode_; }
    [[nodiscard]] Density density() const { return density_; }

    void setMode(ThemeMode mode);        // ré-applique + persiste
    void setDensity(Density density);    // ré-applique + persiste

signals:
    // Émis après un changement de thème/densité : les vues à dessin personnalisé
    // (canevas) doivent se redessiner.
    void changed();

private:
    AppTheme() = default;
    void reapply();
    void load();
    void save() const;

    ThemeMode mode_{ThemeMode::Light};
    Density density_{Density::Comfortable};
    Tokens tokens_{light_tokens()};
};

}  // namespace openstitch::desktop
