// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- fils, coloris et ordre de couture. Tout passe par des `ICommand` (une étape
// d'annulation par action). Vue « plan de fils » : les objets regroupés par couleur de fil, dans
// l'ordre de couture. Sans Qt.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "document_services.hpp"

namespace openstitch::desktop_v2 {

struct ThreadEntry {
    std::array<std::uint8_t, 3> rgb{};
    std::string label;                  // référence du nuancier si assignée, sinon « #rrggbb »
    std::vector<std::uint64_t> objects; // dans l'ordre de couture
};

// Fils distincts (clé de nuancier si présente, sinon RGB), dans l'ordre de première couture.
[[nodiscard]] std::vector<ThreadEntry> threadEntries(const document::Project& project);

// Donne aux objets de l'entrée `from` la couleur et le fil de l'entrée `into` (fusion de fils).
ServiceResult mergeThreads(DocumentSession& session, std::size_t from, std::size_t into);
// Recolore tous les objets d'une entrée (couleur libre : l'assignation de nuancier est retirée).
ServiceResult recolorThread(DocumentSession& session, std::size_t entry,
                            std::array<std::uint8_t, 3> rgb);
// Masque ou affiche tous les objets d'un fil.
ServiceResult setThreadVisible(DocumentSession& session, std::size_t entry, bool visible);

// Déplace un groupe de fil vers son indice final, en conservant l'ordre interne des groupes.
ServiceResult moveThread(DocumentSession& session, std::size_t from, std::size_t to);

struct OrderRow {
    std::uint64_t id{0};
    std::string name;
    std::array<std::uint8_t, 3> rgb{};
};
[[nodiscard]] std::vector<OrderRow> orderRows(const document::Project& project);
// Déplace un objet à la position `toPosition` (0 = cousu en premier). Refuse un déplacement qui
// casserait l'ordre des calques.
ServiceResult moveInOrder(DocumentSession& session, std::uint64_t id, std::size_t toPosition);
// Déplace un objet vers une position absolue (supporte drag-and-drop style, saute les objets masqués)
ServiceResult moveToPositionAbsolute(DocumentSession& session, std::uint64_t id, int toPosition);

// --- Coloris -------------------------------------------------------------------------------
struct ColorwayRow {
    std::string name;
    bool active{false};
};
[[nodiscard]] std::vector<ColorwayRow> colorwayRows(const document::Project& project);
ServiceResult addColorway(DocumentSession& session, const std::string& name);
ServiceResult activateColorway(DocumentSession& session, int index);
ServiceResult removeColorway(DocumentSession& session, int index);
ServiceResult renameColorway(DocumentSession& session, int index, const std::string& name);

} // namespace openstitch::desktop_v2


