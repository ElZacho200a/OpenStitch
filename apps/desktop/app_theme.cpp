// SPDX-License-Identifier: Apache-2.0
#include "app_theme.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QPointer>
#include <QSettings>
#include <QStyle>
#include <QStyleHints>
#include <QWidget>

#include "app_style.hpp"
#include "style_assets.hpp"

#include <spdlog/spdlog.h>

#include <utility>
#include <vector>

namespace openstitch::desktop {

namespace {

QSettings settings() {
    return QSettings(QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"));
}

QString choice_to_string(ThemeChoice c) {
    switch (c) {
    case ThemeChoice::System:
        return QStringLiteral("system");
    case ThemeChoice::Dark:
        return QStringLiteral("dark");
    case ThemeChoice::Light:
        break;
    }
    return QStringLiteral("light");
}

ThemeChoice choice_from_string(const QString& s) {
    if (s == QStringLiteral("system")) {
        return ThemeChoice::System;
    }
    if (s == QStringLiteral("dark")) {
        return ThemeChoice::Dark;
    }
    return ThemeChoice::Light;
}

} // namespace

AppTheme& AppTheme::instance() {
    static AppTheme theme;
    return theme;
}

// Style de base neutre forcé (identité propre d'OpenStitch). setStyle() réinitialise
// la palette : TOUJOURS avant setPalette. Idempotent. Avec une feuille de style
// d'application, QApplication::style() est le proxy QStyleSheetStyle dont name() est
// vide : on se fie alors au drapeau posé lors du dernier forçage.
void AppTheme::ensureFusion(QApplication& app) {
    const QStyle* current = app.style();
    const QString name = current != nullptr ? current->name() : QString();
    if (name.compare(QLatin1String("fusion"), Qt::CaseInsensitive) == 0) {
        fusionForced_ = true;
        return;
    }
    if (name.isEmpty() && fusionForced_) {
        return;
    }
    if (QApplication::setStyle(QStringLiteral("Fusion")) == nullptr) {
        spdlog::warn("AppTheme: style Fusion indisponible, rendu de base conserve");
        return;
    }
    fusionForced_ = true;
}

// Thème système au démarrage, lu AVANT tout setStyle/setPalette (chemin Qt 6.4).
void AppTheme::probeStartupScheme() {
    if (startupProbed_ || QGuiApplication::instance() == nullptr) {
        return;
    }
    startupProbed_ = true;
    startupPrefersDark_ = QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

ThemeMode AppTheme::resolve() const {
    switch (choice_) {
    case ThemeChoice::Light:
        return ThemeMode::Light;
    case ThemeChoice::Dark:
        return ThemeMode::Dark;
    case ThemeChoice::System:
        break;
    }
    if (systemOverride_.has_value()) {
        return *systemOverride_ ? ThemeMode::Dark : ThemeMode::Light;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QGuiApplication::instance() != nullptr) {
        const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
        if (scheme != Qt::ColorScheme::Unknown) {
            return scheme == Qt::ColorScheme::Dark ? ThemeMode::Dark : ThemeMode::Light;
        }
    }
#endif
    return startupPrefersDark_ ? ThemeMode::Dark : ThemeMode::Light;
}

void AppTheme::applyToApp(QApplication& app) {
    // Lecture du thème système AVANT tout setStyle/setPalette (ensureFusion réinitialise
    // la palette) : c'est la valeur de repli de Qt 6.4.
    probeStartupScheme();
    load();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (!schemeSignalConnected_) {
        schemeSignalConnected_ = true;
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
            if (choice_ == ThemeChoice::System && !applying_) {
                reapply();
            }
        });
    }
#endif
    // `reapply` fait ensureFusion -> police -> palette -> QSS -> changed().
    reapply();
    (void)app;
}

void AppTheme::reapply() {
    if (applying_) {
        return; // récursion via colorSchemeChanged déclenchée par setColorScheme
    }
    applying_ = true;
    auto* app = qobject_cast<QApplication*>(QApplication::instance());
    if (app != nullptr) {
        probeStartupScheme(); // no-op si applyToApp l'a déjà fait ; avant ensureFusion
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        // Chrome natif (barre de titre, dialogues natifs) : suit le choix de
        // l'utilisateur ; Unknown rend la main à l'OS. Posé AVANT resolve() : pour
        // System, colorScheme() doit refléter l'OS et non le choix précédent.
        QGuiApplication::styleHints()->setColorScheme(
            choice_ == ThemeChoice::System ? Qt::ColorScheme::Unknown
            : choice_ == ThemeChoice::Dark ? Qt::ColorScheme::Dark
                                           : Qt::ColorScheme::Light);
#endif
    }
    mode_ = resolve();
    tokens_ = tokens_for(mode_, density_);

    if (app != nullptr) {
        ensureFusion(*app);

        // Mises à jour suspendues pendant l'application ; état antérieur restitué
        // (une fenêtre déjà suspendue le reste), fenêtres détruites ignorées.
        std::vector<std::pair<QPointer<QWidget>, bool>> windows;
        for (QWidget* w : QApplication::topLevelWidgets()) {
            windows.emplace_back(w, w->updatesEnabled());
            w->setUpdatesEnabled(false);
        }

        // Ordre : style (déjà fait) -> police -> palette (APRÈS setColorScheme) -> QSS.
        app->setFont(app_font(tokens_));
        app->setPalette(build_palette(tokens_));
        const QString root = style_assets::install(tokens_);
        app->setStyleSheet(build_stylesheet(tokens_, root));
        // L'ancienne racine n'est libérée qu'une fois la nouvelle feuille appliquée.
        style_assets::releaseRetired();

        for (const auto& [w, wasEnabled] : windows) {
            if (w != nullptr) {
                w->setUpdatesEnabled(wasEnabled);
            }
        }
    }
    save();
    applying_ = false;
    emit changed();
}

void AppTheme::setMode(ThemeMode mode) {
    setThemeChoice(mode == ThemeMode::Dark ? ThemeChoice::Dark : ThemeChoice::Light);
}

void AppTheme::setThemeChoice(ThemeChoice choice) {
    if (choice_ == choice) {
        return;
    }
    choice_ = choice;
    reapply();
}

void AppTheme::setDensity(Density density) {
    if (density_ == density) {
        return;
    }
    density_ = density;
    reapply();
}

void AppTheme::resetStartupProbeForTesting() {
    startupProbed_ = false;
}

void AppTheme::setSystemPreferenceForTesting(std::optional<bool> prefersDark) {
    systemOverride_ = prefersDark;
}

void AppTheme::load() {
    auto s = settings();
    choice_ =
        choice_from_string(s.value(QStringLiteral("ui/theme"), QStringLiteral("light")).toString());
    density_ = s.value(QStringLiteral("ui/density"), QStringLiteral("comfortable")).toString() ==
                       QStringLiteral("compact")
                   ? Density::Compact
                   : Density::Comfortable;
}

void AppTheme::save() const {
    auto s = settings();
    s.setValue(QStringLiteral("ui/theme"), choice_to_string(choice_));
    s.setValue(QStringLiteral("ui/density"), density_ == Density::Compact
                                                 ? QStringLiteral("compact")
                                                 : QStringLiteral("comfortable"));
}

} // namespace openstitch::desktop
