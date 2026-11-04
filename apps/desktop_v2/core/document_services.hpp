// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- services documentaires qui passent par des `ICommand` (pas par des lots JSON) :
// préréglages de points et calques. Chaque fonction exécute UNE commande sur la session (une étape
// d'annulation), valide ses entrées et dit pourquoi elle refuse. Sans Qt.

#include <cstdint>
#include <string>
#include <vector>

#include "document_session.hpp"

namespace openstitch::desktop_v2 {

struct ServiceResult {
    bool ok{false};
    std::string message; // refus en clair quand !ok
};

// --- Préréglages de points (réglages d'un objet réutilisables) -----------------------------
[[nodiscard]] std::vector<std::string> presetNames(const document::Project& project);
// Enregistre les réglages de couture de l'objet sous `name` (remplace un préréglage de même nom).
ServiceResult savePreset(DocumentSession& session, const std::string& name, std::uint64_t objectId);
// Applique le préréglage d'indice `index` à ces objets de broderie (même type ou non : le type de
// points suit le préréglage).
ServiceResult applyPreset(DocumentSession& session, int index,
                          const std::vector<std::uint64_t>& objectIds);
ServiceResult removePreset(DocumentSession& session, int index);

// --- Calques --------------------------------------------------------------------------------
struct CalqueInfo {
    std::uint64_t id{0};
    std::string name;
    bool visible{true};
    bool locked{false};
};
[[nodiscard]] std::vector<CalqueInfo>
calques(const document::Project& project); // du fond au dessus
ServiceResult addCalque(DocumentSession& session, const std::string& name);
ServiceResult renameCalque(DocumentSession& session, std::uint64_t id, const std::string& name);
ServiceResult removeCalque(DocumentSession& session, std::uint64_t id);
ServiceResult setCalqueVisible(DocumentSession& session, std::uint64_t id, bool visible);
ServiceResult setCalqueLocked(DocumentSession& session, std::uint64_t id, bool locked);
ServiceResult moveCalque(DocumentSession& session, std::uint64_t id, int direction); // +1 dessus
// Place les formes sources des objets de broderie donnés dans ce calque.
ServiceResult moveObjectsToCalque(DocumentSession& session,
                                  const std::vector<std::uint64_t>& objectIds,
                                  std::uint64_t calqueId);

} // namespace openstitch::desktop_v2
