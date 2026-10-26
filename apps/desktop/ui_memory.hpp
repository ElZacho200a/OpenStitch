// SPDX-License-Identifier: Apache-2.0
#pragma once

// Petites préférences d'usage mémorisées d'une session à l'autre (dernier réglage d'un dialogue,
// dernier dossier d'import) : de simples valeurs d'interface, jamais des données du document.
// Toute lecture est bornée : une valeur corrompue ou hors plage retombe sur le défaut.

#include <QSettings>
#include <QString>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <type_traits>
#include <variant>

#include "openstitch/document/embroidery_object.hpp"

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

// HP-UX-002 : derniers réglages de couture, repris par la création directe d'un objet (un clic,
// sans dialogue). Préférence d'interface uniquement : rien n'est écrit dans le document ni dans
// le fichier .osp. Une clé absente laisse la valeur par défaut du type inchangée.
namespace creation {

inline const QString kTatamiSpacing = QStringLiteral("create/tatami/rowSpacingUm");
inline const QString kTatamiLength = QStringLiteral("create/tatami/stitchLengthUm");
inline const QString kTatamiAngle = QStringLiteral("create/tatami/angleDeci");
inline const QString kRunningLength = QStringLiteral("create/running/stitchLengthUm");
inline const QString kRunningRepeats = QStringLiteral("create/running/repeats");
inline const QString kDirectionalSpacing = QStringLiteral("create/directional/rowSpacingUm");
inline const QString kSatinSpacing = QStringLiteral("create/autoSatin/spacingUm");

// Mémorise les réglages de couture d'un objet qui vient d'être édité (une valeur par type).
inline void remember(const document::StitchParams& params) {
    std::visit(
        [](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, document::TatamiParams>) {
                setIntValue(kTatamiSpacing, p.row_spacing.value);
                setIntValue(kTatamiLength, p.stitch_length.value);
                const double deg = std::fmod(
                    std::fmod(p.angle.radians * 180.0 / std::numbers::pi, 180.0) + 180.0, 180.0);
                setIntValue(kTatamiAngle, static_cast<int>(std::lround(deg * 10.0)) % 1800);
            } else if constexpr (std::is_same_v<T, document::RunningStitchParams>) {
                setIntValue(kRunningLength, p.stitch_length.value);
                setIntValue(kRunningRepeats, p.repeats);
            } else if constexpr (std::is_same_v<T, document::DirectionalFillParams>) {
                setIntValue(kDirectionalSpacing, p.row_spacing.value);
            } else if constexpr (std::is_same_v<T, document::AutoSatinParams>) {
                setIntValue(kSatinSpacing, p.spacing.value);
            }
        },
        params);
}

// Applique les derniers réglages mémorisés à des paramètres neufs (objet qui vient d'être créé).
inline void apply(document::TatamiParams& p) {
    p.row_spacing.value = intValue(kTatamiSpacing, p.row_spacing.value, 100, 5'000);
    p.stitch_length.value = intValue(kTatamiLength, p.stitch_length.value, 1'000, 10'000);
    const int deci = intValue(
        kTatamiAngle, static_cast<int>(std::lround(p.angle.radians * 1800.0 / std::numbers::pi)), 0,
        1'799);
    p.angle = Angle{deci / 10.0 * std::numbers::pi / 180.0};
    // HP-ENG-002 : un objet créé à la main reçoit la sous-couche automatique (les projets
    // existants gardent leur mode : le fichier ne l'écrit que s'il n'est pas Manual).
    p.underlay_mode = document::UnderlayMode::Auto;
    p.pull_mode = document::PullMode::Auto; // HP-ENG-001.b
}
inline void apply(document::RunningStitchParams& p) {
    p.stitch_length.value = intValue(kRunningLength, p.stitch_length.value, 500, 20'000);
    p.repeats = intValue(kRunningRepeats, p.repeats, 1, 3);
}
inline void apply(document::DirectionalFillParams& p) {
    p.row_spacing.value = intValue(kDirectionalSpacing, p.row_spacing.value, 100, 5'000);
    p.underlay_mode = document::UnderlayMode::Auto; // HP-ENG-002
    p.pull_mode = document::PullMode::Auto;         // HP-ENG-001.b
}
inline void apply(document::AutoSatinParams& p) {
    p.spacing.value = intValue(kSatinSpacing, p.spacing.value, 100, 2'000);
    p.underlay_mode = document::UnderlayMode::Auto; // HP-ENG-002
    p.pull_mode = document::PullMode::Auto;         // HP-ENG-001.b
}
inline void apply(document::StitchParams& params) {
    std::visit(
        [](auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, document::TatamiParams> ||
                          std::is_same_v<T, document::RunningStitchParams> ||
                          std::is_same_v<T, document::DirectionalFillParams> ||
                          std::is_same_v<T, document::AutoSatinParams>) {
                apply(p);
            }
        },
        params);
}

} // namespace creation

} // namespace openstitch::desktop::ui_memory
