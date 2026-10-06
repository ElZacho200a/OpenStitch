// SPDX-License-Identifier: Apache-2.0
#pragma once

// Support des variantes « thémées » des suites desktop existantes (plan L2 §5.7).
// Quand OPENSTITCH_TEST_APPLY_THEME=<light|dark>-<comfortable|compact> est défini,
// la suite tourne avec le style Fusion + QSS + palette du design system v2 (comme
// l'application réelle) ; absent, rien ne change (rendu par défaut de la plateforme).
// À appeler en FIN d'initTestCase, une fois les QSettings isolés.

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QString>
#include <QtGlobal>

#include "app_theme.hpp"

namespace openstitch::desktop::test {

// Retourne true si un thème a été appliqué. Refuse (false + avertissement) de
// toucher à un QSettings qui ne serait pas dans le répertoire temporaire.
inline bool applyThemeFromEnv() {
    const QString spec = qEnvironmentVariable("OPENSTITCH_TEST_APPLY_THEME");
    if (spec.isEmpty()) {
        return false;
    }
    const QStringList parts = spec.split(QLatin1Char('-'));
    const bool okSpec =
        parts.size() == 2 &&
        (parts[0] == QLatin1String("light") || parts[0] == QLatin1String("dark")) &&
        (parts[1] == QLatin1String("comfortable") || parts[1] == QLatin1String("compact"));
    if (!okSpec) {
        qWarning("OPENSTITCH_TEST_APPLY_THEME invalide: %s", qPrintable(spec));
        return false;
    }
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (app == nullptr) {
        return false;
    }
    QSettings store(QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"));
    if (!store.fileName().startsWith(QDir::tempPath())) {
        qWarning("applyThemeFromEnv: QSettings non isole (%s), theme ignore",
                 qPrintable(store.fileName()));
        return false;
    }
    store.setValue(QStringLiteral("ui/theme"), parts[0]);
    store.setValue(QStringLiteral("ui/density"), parts[1]);
    store.sync();
    AppTheme::instance().applyToApp(*app);
    return true;
}

} // namespace openstitch::desktop::test
