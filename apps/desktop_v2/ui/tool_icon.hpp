// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QIcon>
#include <string_view>

namespace openstitch::desktop_v2 {

// Icônes vectorielles générées localement : utilisables hors ligne, sans thème d'icônes OS.
[[nodiscard]] QIcon toolIcon(std::string_view id, const QColor& foreground);

} // namespace openstitch::desktop_v2
