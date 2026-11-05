// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- outil Plume : une forme fermée dessinée au clic (coin) ou au clic-glisser (nœud
// lisse dont la poignée suit la souris, tangente symétrique). Crée l'objet vectoriel ET l'objet de
// broderie en UNE commande annulable (mêmes réglages par défaut que l'opération `add_shape`).
// Aucun point cousu n'est stocké. Sans Qt.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "document_services.hpp"
#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

struct PenNode {
    PointMm pos;                      // position du nœud, mm « modèle » (Y vers le haut)
    std::optional<PointMm> handleOut; // poignée sortante (position absolue) ; absente = coin
};

struct PenResult {
    bool ok{false};
    std::string message;       // refus en clair
    std::uint64_t objectId{0}; // objet de broderie créé
    bool curved{false};        // au moins un nœud porte une poignée
};

// Crée une forme fermée avec au moins 3 nœuds. Refus clairs : trop peu de nœuds, nœuds confondus,
// coordonnées hors zone de travail, contour qui se croise, aire trop petite.
PenResult createPenShape(DocumentSession& session, const std::vector<PenNode>& nodes);

} // namespace openstitch::desktop_v2
