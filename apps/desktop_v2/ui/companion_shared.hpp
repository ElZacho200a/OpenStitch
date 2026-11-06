// SPDX-License-Identifier: Apache-2.0
#pragma once

// Le compagnon IA (fournisseur Claude, réglages, boîte de dialogue) vit dans `apps/companion`,
// partagé avec le bureau V1, sous l'espace de noms historique `openstitch::desktop`. Ce fichier
// le rend disponible sous `openstitch::desktop_v2` : un seul endroit à changer si l'espace de noms
// de la bibliothèque évolue quand la V1 sera retirée.

#include "claude_backend.hpp"
#include "companion_backend.hpp"
#include "companion_settings_dialog.hpp"

namespace openstitch::desktop_v2 {

using desktop::ClaudeCompanionBackend;
using desktop::CompanionBackend;
using desktop::CompanionProposal;
using desktop::CompanionSettingsDialog;
using desktop::CompanionToolCall;
using desktop::makeSystemSecretStore;
namespace companion_settings = desktop::companion_settings;

} // namespace openstitch::desktop_v2
