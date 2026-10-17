// SPDX-License-Identifier: Apache-2.0
#include "app_settings.hpp"

#include <QSettings>

#include <algorithm>
#include <cmath>

namespace openstitch::desktop {

namespace {

constexpr auto kUnionGap = "edit/unionGapMm";
constexpr auto kKnifeWidth = "edit/knifeWidthMm";
constexpr auto kKnifeMinLength = "edit/knifeMinLengthMm";
constexpr auto kSnapNodes = "edit/snapNodesOnDrag";
constexpr auto kRegionOpacity = "ui/regionMapOpacity";
constexpr auto kLongPress = "navigation/longPressMs";

// Même couple organisation/application que app_theme.cpp et interaction_map.cpp.
const QString kOrganization = QStringLiteral("OpenStitch");
const QString kApplication = QStringLiteral("OpenStitch Studio");

// Une valeur illisible (texte, NaN) retombe sur le défaut au lieu de fausser le réglage.
double readDouble(const QSettings& s, const char* key, double fallback) {
    bool ok = false;
    const double v = s.value(QLatin1String(key)).toDouble(&ok);
    return ok && std::isfinite(v) ? v : fallback;
}

QSettings userSettings() {
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope, kOrganization, kApplication);
}

} // namespace

EditSettings EditSettings::clamped() const {
    EditSettings out = *this;
    out.unionGapMm = std::clamp(unionGapMm, 0.0, 0.5);
    out.knifeWidthMm = std::clamp(knifeWidthMm, 0.005, 0.2);
    out.knifeMinLengthMm = std::clamp(knifeMinLengthMm, 0.1, 10.0);
    out.regionMapOpacity = std::clamp(regionMapOpacity, 0.2, 1.0);
    out.longPressMs = std::clamp(longPressMs, 300, 1000);
    return out;
}

EditSettings loadEditSettings() {
    const QSettings s = userSettings();
    const EditSettings d = EditSettings::defaults();
    EditSettings out;
    out.unionGapMm = readDouble(s, kUnionGap, d.unionGapMm);
    out.knifeWidthMm = readDouble(s, kKnifeWidth, d.knifeWidthMm);
    out.knifeMinLengthMm = readDouble(s, kKnifeMinLength, d.knifeMinLengthMm);
    out.snapNodesOnDrag = s.value(QLatin1String(kSnapNodes), d.snapNodesOnDrag).toBool();
    out.regionMapOpacity = readDouble(s, kRegionOpacity, d.regionMapOpacity);
    out.longPressMs = s.value(QLatin1String(kLongPress), d.longPressMs).toInt();
    return out.clamped();
}

void saveEditSettings(const EditSettings& settings) {
    const EditSettings v = settings.clamped();
    QSettings s = userSettings();
    s.setValue(QLatin1String(kUnionGap), v.unionGapMm);
    s.setValue(QLatin1String(kKnifeWidth), v.knifeWidthMm);
    s.setValue(QLatin1String(kKnifeMinLength), v.knifeMinLengthMm);
    s.setValue(QLatin1String(kSnapNodes), v.snapNodesOnDrag);
    s.setValue(QLatin1String(kRegionOpacity), v.regionMapOpacity);
    s.setValue(QLatin1String(kLongPress), v.longPressMs);
    s.sync();
}

int migrateSettings(QSettings& from, QSettings& to) {
    if (!to.allKeys().isEmpty()) {
        return 0;
    }
    const QStringList keys = from.allKeys();
    for (const QString& key : keys) {
        to.setValue(key, from.value(key));
    }
    to.sync();
    return static_cast<int>(keys.size());
}

void configureSettingsStorage() {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings ini = userSettings();
    QSettings native(QSettings::NativeFormat, QSettings::UserScope, kOrganization, kApplication);
    migrateSettings(native, ini);
}

QString settingsFilePath() {
    return userSettings().fileName();
}

} // namespace openstitch::desktop
