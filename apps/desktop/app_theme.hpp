// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>

#include <optional>

#include "design_tokens.hpp"

class QApplication;

namespace openstitch::desktop {

// Choix de thème de l'utilisateur ; `System` suit le thème de l'OS (Qt >= 6.5 :
// en direct ; Qt 6.4 : valeur lue au démarrage).
enum class ThemeChoice { System, Light, Dark };

// Applique un thème (style de base Fusion forcé + police + palette COMPLÈTE +
// feuille de style générée depuis les tokens) à toute l'application. Point d'accès
// unique aux tokens courants pour le dessin du canevas. Persiste le choix
// (thème + densité) via QSettings.
//
// N'est PAS une seconde vérité métier : ne concerne que l'apparence.
class AppTheme : public QObject {
    Q_OBJECT

public:
    static AppTheme& instance();

    // Charge le choix persistant puis applique le thème à l'application.
    void applyToApp(QApplication& app);

    [[nodiscard]] const Tokens& tokens() const { return tokens_; }
    // Mode EFFECTIF (Light/Dark) : pour le choix System, le mode résolu.
    [[nodiscard]] ThemeMode mode() const { return mode_; }
    [[nodiscard]] ThemeMode resolvedMode() const { return mode_; }
    [[nodiscard]] Density density() const { return density_; }
    [[nodiscard]] ThemeChoice themeChoice() const { return choice_; }

    void setMode(ThemeMode mode);            // = setThemeChoice(Light|Dark)
    void setThemeChoice(ThemeChoice choice); // ré-applique + persiste
    void setDensity(Density density);        // ré-applique + persiste

    // Crochet de test : force la préférence « système » (true = sombre) pour les
    // deux chemins (Qt 6.4 et >= 6.5). std::nullopt = détection réelle.
    void setSystemPreferenceForTesting(std::optional<bool> prefersDark);

signals:
    // Émis après un changement de thème/densité : les vues à dessin personnalisé
    // (canevas) doivent se redessiner.
    void changed();

private:
    AppTheme() = default;
    void reapply();
    void load();
    void save() const;
    void ensureFusion(QApplication& app);
    void probeStartupScheme();
    [[nodiscard]] ThemeMode resolve() const;

    ThemeChoice choice_{ThemeChoice::Light};
    ThemeMode mode_{ThemeMode::Light};
    Density density_{Density::Comfortable};
    Tokens tokens_{light_tokens()};
    bool applying_{false}; // garde anti-récursion (colorSchemeChanged)
    bool startupProbed_{false};
    bool startupPrefersDark_{false}; // thème système lu AVANT tout setStyle/setPalette
    bool schemeSignalConnected_{false};
    bool fusionForced_{false};
    std::optional<bool> systemOverride_;
};

} // namespace openstitch::desktop
