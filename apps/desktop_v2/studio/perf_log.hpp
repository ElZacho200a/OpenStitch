// SPDX-License-Identifier: Apache-2.0
#pragma once

// Journal de performance de Studio, ACTIVÉ SEULEMENT par la variable d'environnement
// OPENSTITCH_PERF_LOG=<fichier> : une ligne JSON par événement (reconstruction de scène, calque
// vectoriel...) avec sa durée réelle. Sans la variable, `perfLog` ne coûte qu'un test booléen.
// Sert à mesurer sur un VRAI projet où le temps part (aucune promesse de gain sans ces données).

#include <QString>
#include <QVariantMap>

namespace openstitch::desktop_v2::studio {

[[nodiscard]] bool perfLogEnabled();
void perfLog(const QString& event, const QVariantMap& fields);

} // namespace openstitch::desktop_v2::studio
