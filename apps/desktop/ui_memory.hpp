// SPDX-License-Identifier: Apache-2.0
#pragma once

// Petites préférences d'usage mémorisées d'une session à l'autre (dernier réglage d'un dialogue,
// dernier dossier d'import) : de simples valeurs d'interface, jamais des données du document.
// Toute lecture est bornée : une valeur corrompue ou hors plage retombe sur le défaut.

#include <QSettings>
#include <QString>

#include <algorithm>

namespace openstitch::desktop::ui_memory {

[[nodiscard]] inline int intValue(const QString& key, int fallback, int lo, int hi) {
    bool ok = false;
    const int value = QSettings().value(QStringLiteral("ui/memory/") + key, fallback).toInt(&ok);
    return ok ? std::clamp(value, lo, hi) : fallback;
}

inline void setIntValue(const QString& key, int value) {
    QSettings().setValue(QStringLiteral("ui/memory/") + key, value);
}

[[nodiscard]] inline bool boolValue(const QString& key, bool fallback) {
    return QSettings().value(QStringLiteral("ui/memory/") + key, fallback).toBool();
}

inline void setBoolValue(const QString& key, bool value) {
    QSettings().setValue(QStringLiteral("ui/memory/") + key, value);
}

} // namespace openstitch::desktop::ui_memory
