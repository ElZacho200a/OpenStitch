// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- magnétisme et repères intelligents pendant le déplacement d'objets : les bords et
// le centre de la sélection s'accrochent aux bords et centres des autres objets (et à l'axe du
// motif) à moins d'un seuil, et la fonction rend les repères à tracer. Calcul pur en mm « modèle »
// (Y vers le haut), sans Qt, déterministe ; il ne modifie JAMAIS le document : l'aperçu du glisser
// l'appelle, la validation applique le décalage obtenu en UNE commande. Maintenir Alt le désactive.

#include <vector>

#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

struct SnapBox {
    double x0{0.0};
    double y0{0.0};
    double x1{0.0};
    double y1{0.0};

    [[nodiscard]] double centreX() const { return (x0 + x1) / 2.0; }
    [[nodiscard]] double centreY() const { return (y0 + y1) / 2.0; }
};

// Repère d'alignement : une droite verticale (x fixe) ou horizontale (y fixe), bornée à l'étendue
// utile (de `from` à `to` sur l'autre axe).
struct SnapGuide {
    bool vertical{true};
    double positionMm{0.0};
    double fromMm{0.0};
    double toMm{0.0};
};

struct SnapResult {
    PointMm delta; // décalage à appliquer (le décalage demandé, éventuellement corrigé)
    bool snappedX{false};
    bool snappedY{false};
    std::vector<SnapGuide> guides;
};

// `moving` : boîte de la sélection avant le glisser ; `delta` : décalage demandé ; `targets` :
// boîtes des objets fixes ; `thresholdMm` : distance d'accrochage (> 0) ; `includeOrigin` : l'axe
// du motif (x = 0, y = 0) est aussi une cible. Seuil <= 0 : aucun accrochage.
[[nodiscard]] SnapResult snapMove(const SnapBox& moving, PointMm delta,
                                  const std::vector<SnapBox>& targets, double thresholdMm,
                                  bool includeOrigin);

} // namespace openstitch::desktop_v2
