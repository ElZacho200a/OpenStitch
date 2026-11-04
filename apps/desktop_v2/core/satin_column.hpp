// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- colonnes de satin manuelles : deux rails reliés par des barreaux (une paire de
// points par barreau), puis remodelage des rails (déplacer / insérer un nœud). Chaque action est
// UNE `ICommand` annulable. Sans Qt.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "document_services.hpp"
#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

struct SatinCreateResult {
    bool ok{false};
    std::string message;
    std::uint64_t objectId{0};
    bool railsCross{false}; // créée quand même, mais la couture sera probablement incohérente
};

enum class RailSide { A, B };

// `rungs` : (point du rail A, point du rail B) en mm modèle. Au moins 2 barreaux, A != B pour
// chacun.
SatinCreateResult createSatinColumn(DocumentSession& session,
                                    const std::vector<std::pair<PointMm, PointMm>>& rungs);

// Nombre de nœuds du rail, ou -1 si l'objet n'est pas une colonne satin à rails.
[[nodiscard]] int railNodeCount(const document::Project& project, std::uint64_t objectId,
                                RailSide side);

ServiceResult moveSatinRailNode(DocumentSession& session, std::uint64_t objectId, RailSide side,
                                std::size_t node, PointMm to);
// Insère un nœud au paramètre t (0..1, exclu) du segment `segment`.
ServiceResult insertSatinRailNode(DocumentSession& session, std::uint64_t objectId, RailSide side,
                                  std::size_t segment, double t);

} // namespace openstitch::desktop_v2
