// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>
#include <QStringList>

class QLocale;

namespace openstitch::desktop {

// Langue de l'interface (HP-I18N-001). Le français est la langue source : les chaînes de
// `tr()` sont écrites en français, donc « fr » n'a pas de fichier de traduction. Les autres
// langues sont des fichiers `.ts` compilés en `.qm` et embarqués (`:/i18n/openstitch_<code>.qm`).
// Décision documentée dans docs/source/i18n.md.

inline constexpr const char* kLanguageSystem = "system"; // suit la langue du système

// Codes de langue proposés dans les réglages, dans l'ordre d'affichage (hors « system »).
[[nodiscard]] const QStringList& supportedLanguages();

// Préférence enregistrée : « system » (défaut), « fr » ou « en ». Toute autre valeur lue du
// fichier de réglages est ramenée à « system » (jamais d'erreur).
[[nodiscard]] QString loadLanguagePreference();
void saveLanguagePreference(const QString& preference);

// Langue effective (« fr » ou « en ») : la préférence si elle est explicite, sinon la langue du
// système (français → « fr », toute autre langue → « en »).
[[nodiscard]] QString resolveLanguage(const QString& preference, const QLocale& system);

// Installe (en remplaçant les précédents) les traducteurs de Qt (boutons standard) et de
// l'application pour `language`. Renvoie vrai si un fichier de traduction de l'application a été
// chargé ; pour « fr » (langue source) renvoie faux sans que ce soit une erreur.
bool installTranslators(const QString& language);

} // namespace openstitch::desktop
