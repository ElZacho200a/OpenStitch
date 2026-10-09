// SPDX-License-Identifier: Apache-2.0
// Auto-satin par squelette et traversées orientées (spec :
// specs/plans/satin-squelette-traversees.md). Remplace la construction de deux
// rails appariés : chaque traversée est l'intervalle intérieur d'une droite
// orientée qui passe par un échantillon de l'axe, sans rail ni appariement.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin {

// Guide d'orientation posé par l'utilisateur. Ancré GÉOMÉTRIQUEMENT (point du
// repère du modèle, projeté sur l'axe à la génération) : les identifiants du
// graphe du squelette changent à chaque édition du contour et ne sont jamais
// persistés.
struct SkeletonSatinGuide {
    Vec2um anchor{};
    // Angle en radians modulo π. Absolu : repère modèle, axe X vers la droite.
    // Relatif : écart à la perpendiculaire à l'axe (0 = perpendiculaire).
    double angle_rad{0.0};
    bool absolute{false};
};

struct SkeletonSatinParameters {
    AutoSatinParameters analysis{}; // rasterisation, élagage du graphe

    Micrometers spacing{400};           // ρ : espacement cible au bord le plus écarté
    Micrometers min_thread_length{300}; // sous cette longueur, une traversée n'est pas émise
    Micrometers cell_overlap{200};      // recouvrement entre cellules voisines (ε)
    double bend_threshold_deg{35.0};    // virage assimilé à un coude (coupe en onglet)

    std::vector<SkeletonSatinGuide> guides;
};

// Une traversée : extrémité A du côté droit de l'axe, B du côté gauche.
struct SkeletonSatinCrossing {
    Vec2um a{};
    Vec2um b{};
};

// Traversées consécutives d'une branche (chaîne d'arêtes du squelette entre deux
// jonctions ou extrémités). Chaque colonne se coud d'un seul tenant ; deux
// colonnes sont séparées par un déplacement.
struct SkeletonSatinColumn {
    std::vector<SkeletonSatinCrossing> crossings;
};

struct SkeletonSatinDiagnostics {
    int outside_samples{0};            // échantillons abandonnés (hors région)
    int too_short{0};                  // traversées plus courtes que `min_thread_length`
    int clamped_angle{0};              // orientations ramenées au plancher |sin(g-α)|
    int radius_guard_hits{0};          // traversées anormalement longues pour le rayon inscrit
    int orphan_guides{0};              // guides trop loin de l'axe pour être appliqués
    int pieces{0};                     // morceaux de colonne après coupe aux coudes
    std::vector<std::string> messages; // refus et avertissements nommés
};

struct SkeletonSatinResult {
    std::vector<SkeletonSatinColumn> columns;
    SkeletonSatinDiagnostics diagnostics;
};

// Génère les traversées de la région. Déterministe. Ne lève jamais d'échec
// silencieux : ce qui n'est pas traité (région compacte, anneau pur) est nommé
// dans `diagnostics.messages` et la colonne correspondante est omise.
[[nodiscard]] Result<SkeletonSatinResult>
generate_skeleton_satin(const geometry::PathSet& region, const SkeletonSatinParameters& params);

} // namespace openstitch::auto_satin
