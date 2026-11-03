// SPDX-License-Identifier: Apache-2.0
#pragma once

// HP-UX-008 : unité d'affichage des longueurs (millimètres ou pouces). Le document et le moteur
// restent TOUJOURS en micromètres entiers ; la conversion n'existe qu'aux frontières d'affichage
// et de saisie (règles, coordonnées du curseur, dimensions, pas de grille), via les fonctions
// ci-dessous -- jamais dans le cœur. Réglage mémorisé via QSettings (`ui_memory`).

#include <QString>

#include <cmath>

#include "openstitch/core/units.hpp"
#include "ui_memory.hpp"

namespace openstitch::desktop::length_units {

enum class Unit { Millimeters = 0, Inches = 1 };

inline constexpr double kMmPerInch = 25.4;

[[nodiscard]] inline Unit current() {
    return ui_memory::intValue(QStringLiteral("units/length"), 0, 0, 1) == 1 ? Unit::Inches
                                                                             : Unit::Millimeters;
}
inline void setCurrent(Unit unit) {
    ui_memory::setIntValue(QStringLiteral("units/length"), unit == Unit::Inches ? 1 : 0);
}

// Millimètres par unité d'affichage.
[[nodiscard]] constexpr double mmPerUnit(Unit unit) {
    return unit == Unit::Inches ? kMmPerInch : 1.0;
}
[[nodiscard]] constexpr double fromMm(double mm, Unit unit) {
    return mm / mmPerUnit(unit);
}
[[nodiscard]] constexpr double toMm(double value, Unit unit) {
    return value * mmPerUnit(unit);
}
// Valeur d'affichage d'une longueur du modèle (frontière µm -> affichage).
[[nodiscard]] constexpr double fromMicrometers(Micrometers um, Unit unit) {
    return static_cast<double>(um.value) / (1000.0 * mmPerUnit(unit));
}
// Saisie -> modèle (frontière affichage -> µm, arrondi au µm).
[[nodiscard]] inline Micrometers toMicrometers(double value, Unit unit) {
    return Micrometers{static_cast<std::int32_t>(std::lround(value * mmPerUnit(unit) * 1000.0))};
}

// « mm » / « in » (neutre en traduction : symboles d'unité).
[[nodiscard]] inline QString suffix(Unit unit) {
    return unit == Unit::Inches ? QStringLiteral("in") : QStringLiteral("mm");
}
// Décimales d'affichage usuelles : 1 en mm, 3 en pouces.
[[nodiscard]] constexpr int decimals(Unit unit) {
    return unit == Unit::Inches ? 3 : 1;
}

// Formatage commun d'une longueur donnée en mm : « 12.5 mm » / « 0.492 in ». `withUnit` = faux
// pour un nombre seul (étiquette de règle).
[[nodiscard]] inline QString formatMm(double mm, Unit unit, bool withUnit = true) {
    const QString number = QString::number(fromMm(mm, unit), 'f', decimals(unit));
    return withUnit ? number + QLatin1Char(' ') + suffix(unit) : number;
}
[[nodiscard]] inline QString formatMm(double mm, bool withUnit = true) {
    return formatMm(mm, current(), withUnit);
}
[[nodiscard]] inline QString formatMicrometers(Micrometers um, Unit unit, bool withUnit = true) {
    return formatMm(static_cast<double>(um.value) / 1000.0, unit, withUnit);
}
[[nodiscard]] inline QString formatMicrometers(Micrometers um, bool withUnit = true) {
    return formatMicrometers(um, current(), withUnit);
}

// Coordonnées « x : … y : … » de la barre d'état.
[[nodiscard]] inline QString formatPoint(double xMm, double yMm, Unit unit) {
    return QStringLiteral("x : %1   y : %2").arg(formatMm(xMm, unit)).arg(formatMm(yMm, unit));
}

} // namespace openstitch::desktop::length_units
