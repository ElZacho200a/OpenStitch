// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>

#include "openstitch/document/finishing.hpp"

class QWidget;

namespace openstitch::desktop {

// Dialogue « Options de génération » : finitions de la séquence du projet
// (coupes automatiques, points d'arrêt, points courts -- Lots E/F). Ne
// modifie rien lui-même : renvoie les nouvelles valeurs (à appliquer par
// `commands::SetFinishingCommand`, annulable), ou nullopt si annulé.
[[nodiscard]] std::optional<document::SequenceFinishing>
editSequenceFinishing(QWidget* parent, const document::SequenceFinishing& current);

} // namespace openstitch::desktop
