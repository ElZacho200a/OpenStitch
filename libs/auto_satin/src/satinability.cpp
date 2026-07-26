// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/satinability.hpp"

#include <algorithm>
#include <cmath>

#include "openstitch/geometry/simplify.hpp"

namespace openstitch::auto_satin {

const char* to_string(SatinabilityStatus s) {
    switch (s) {
    case SatinabilityStatus::Suitable: return "Suitable";
    case SatinabilityStatus::SuitableWithWarnings: return "SuitableWithWarnings";
    case SatinabilityStatus::RequiresDecomposition: return "RequiresDecomposition";
    case SatinabilityStatus::Ambiguous: return "Ambiguous";
    case SatinabilityStatus::Unsuitable: return "Unsuitable";
    }
    return "?";
}

SatinabilityReport evaluate_satinability(const geometry::PathSet& region,
                                         const SkeletonGraph& graph, const DistanceField& distance,
                                         const SatinabilityThresholds& th) {
    SatinabilityReport r;

    const double areaUm2 = std::abs(geometry::signed_area_um2(region.outer));
    double holesUm2 = 0.0;
    for (const auto& hole : region.holes) {
        holesUm2 += std::abs(geometry::signed_area_um2(hole));
    }
    const double netAreaUm2 = std::max(0.0, areaUm2 - holesUm2);

    double perimUm = 0.0;
    for (std::size_t i = 0; i < region.outer.nodes.size(); ++i) {
        perimUm += length_um(region.outer.nodes[(i + 1) % region.outer.nodes.size()].pos -
                             region.outer.nodes[i].pos);
    }

    r.area_mm2 = netAreaUm2 / 1e6;
    r.perimeter_mm = perimUm / 1000.0;
    r.hole_count = static_cast<int>(region.holes.size());
    r.branch_count = static_cast<int>(graph.edges.size());
    r.endpoint_count = static_cast<int>(graph.endpoint_count());
    r.junction_count = static_cast<int>(graph.junction_count());

    const double meanWidthUm = perimUm > 0.0 ? 2.0 * netAreaUm2 / perimUm : 0.0;
    const double maxWidthUm = 2.0 * static_cast<double>(distance.max_value());
    // Largeur minimale : rayon minimal le long des arêtes du squelette (×2).
    double minRadiusUm = maxWidthUm / 2.0;
    for (const auto& e : graph.edges) {
        for (const double rad : e.local_radii_um) {
            if (rad > 0.0) {
                minRadiusUm = std::min(minRadiusUm, rad);
            }
        }
    }
    const double minWidthUm = 2.0 * minRadiusUm;

    // Longueur estimée = longueur du SQUELETTE (l'axe réel), pas aire/largeur :
    // une forme compacte (disque) a un squelette court, une bande un squelette
    // long. C'est ce qui distingue une direction claire d'une direction ambiguë.
    double skelLenUm = 0.0;
    for (const auto& e : graph.edges) {
        skelLenUm += e.length_um;
    }

    r.mean_width_mm = meanWidthUm / 1000.0;
    r.maximum_width_mm = maxWidthUm / 1000.0;
    r.minimum_width_mm = minWidthUm / 1000.0;
    r.estimated_length_mm = skelLenUm / 1000.0;
    r.width_variation = meanWidthUm > 0.0 ? (maxWidthUm - minWidthUm) / meanWidthUm : 0.0;

    const double elongation = meanWidthUm > 0.0 ? skelLenUm / meanWidthUm : 0.0;
    r.is_elongated = elongation >= th.min_elongation;
    r.has_wide_area = maxWidthUm > static_cast<double>(th.max_satin_width.value);
    r.has_ambiguous_direction = !r.is_elongated;

    // Décision (ordre de priorité).
    const auto issue = [&](const std::string& m) { r.issues.push_back({m}); };

    if (region.outer.nodes.size() < 3 || netAreaUm2 <= 0.0) {
        r.status = SatinabilityStatus::Unsuitable;
        issue("Géométrie invalide ou aire nulle.");
        r.confidence = 1.0;
        return r;
    }
    if (r.hole_count > 0) {
        r.status = SatinabilityStatus::Unsuitable;
        issue("La région contient un ou plusieurs trous : satin automatique refusé "
              "(essayez un remplissage tatami ou une décomposition manuelle).");
        r.confidence = 0.9;
        return r;
    }
    if (maxWidthUm < static_cast<double>(th.min_satin_width.value)) {
        r.status = SatinabilityStatus::Unsuitable;
        issue("Région trop étroite pour un satin.");
        r.confidence = 0.8;
        return r;
    }
    // Direction ambiguë (forme compacte, squelette court) AVANT le test de
    // branche : un disque a un squelette court et parfois quelques spurs ; il
    // doit être « Ambigu », pas « branché ».
    if (r.has_ambiguous_direction) {
        r.status = SatinabilityStatus::Ambiguous;
        issue("Forme peu allongée : direction du satin ambiguë (proche du "
              "circulaire). Choisissez tatami, contour ou un axe manuel.");
        r.confidence = 0.7;
        return r;
    }
    // Forme branchée (allongée avec jonctions) : plusieurs colonnes.
    if (r.junction_count > 0 || r.endpoint_count > 2) {
        r.status = SatinabilityStatus::RequiresDecomposition;
        issue("Forme branchée (" + std::to_string(r.junction_count) + " jonction(s), " +
              std::to_string(r.endpoint_count) +
              " extrémités) : décomposition en plusieurs colonnes nécessaire.");
        r.confidence = 0.7;
        return r;
    }
    if (r.has_wide_area) {
        r.status = SatinabilityStatus::Unsuitable;
        issue("Zone trop large pour un satin (envisagez un tatami).");
        r.confidence = 0.8;
        return r;
    }
    if (r.width_variation > 0.8) {
        r.status = SatinabilityStatus::SuitableWithWarnings;
        issue("Largeur très variable : vérifiez la géométrie.");
        r.confidence = 0.6;
        return r;
    }
    r.status = SatinabilityStatus::Suitable;
    r.confidence = 0.85;
    return r;
}

}  // namespace openstitch::auto_satin
