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

// Analyse par défaut de l'auto-satin : celle de `AutoSatinParameters` plus
// l'élagage à erreur bornée (`GraphCleanupParameters::max_protrusion`, 0,4 mm ou
// 15 % du rayon de la jonction). Sans effet sur un contour propre (corpus de
// formes : mêmes colonnes, même couverture) ; sur un contour bruité (pixels,
// vectorisation), il retire les rameaux dont la suppression ne change la forme
// reconstruite que de moins que ce seuil, donc moins de colonnes parasites.
inline AutoSatinParameters stable_skeleton_analysis() {
    AutoSatinParameters p;
    p.cleanup.max_protrusion = Micrometers{400};
    p.cleanup.max_protrusion_to_radius = 0.15;
    return p;
}

// Méthode de génération des traversées d'une région (docs/auto-satin-research.md).
// Le squelette est la référence et le défaut ; une méthode n'est ajoutée ici que
// lorsqu'elle est réellement livrée.
enum class SatinStrategy {
    Skeleton,      // Zhang-Suen, axes, traversées orientées (moteur actuel)
    ParallelSweep, // balayage parallèle : droites à angle constant, bandes continues
};

struct SkeletonSatinParameters {
    // Méthode utilisée par `generate_satin` (façade). `generate_skeleton_satin` l'ignore.
    SatinStrategy strategy{SatinStrategy::Skeleton};

    // Balayage parallèle seulement : angle global des traversées. Auto (défaut) : perpendiculaire
    // à l'axe principal d'inertie de la région (les fils traversent la largeur de la forme) ;
    // horizontal si la forme est quasi isotrope. Sinon `sweep_angle_rad` (radians, repère modèle).
    bool sweep_auto_angle{true};
    double sweep_angle_rad{0.0};

    AutoSatinParameters analysis{stable_skeleton_analysis()}; // rasterisation, élagage du graphe

    Micrometers spacing{400};           // ρ : espacement cible au bord le plus écarté
    Micrometers min_thread_length{300}; // sous cette longueur, une traversée n'est pas émise
    Micrometers cell_overlap{200};      // recouvrement entre cellules voisines (ε)
    double bend_threshold_deg{35.0};    // virage assimilé à un coude (coupe en onglet)

    // Forme compacte (disque, pétale) : le squelette se réduit à un point et la
    // région est normalement refusée. Vrai : un axe synthétique droit, porté par
    // l'axe principal d'inertie et passant par le centre, remplace le squelette ;
    // les traversées lui sont perpendiculaires (satin « en cordes parallèles »,
    // comme on coud un rond à la main). Les cordes valent au plus le diamètre :
    // à borner par l'appelant (fractionnement `Lmax` ensuite). Faux par défaut.
    bool compact_axis{false};

    std::vector<SkeletonSatinGuide> guides;

    // Calcule la couverture estimée (diagnostic non bloquant) : coûteux, donc à la
    // demande (aperçu, inspecteur), jamais pendant la génération de points.
    bool measure_coverage{false};
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
    int fan_chords{0};                 // cordes d'éventail ajoutées aux coupes de coude
    int trimmed_crossings{0};          // traversées raccourcies pour ne couper aucune voisine
    int radius_guard_hits{0};          // traversées anormalement longues pour le rayon inscrit
    int orphan_guides{0};              // guides trop loin de l'axe pour être appliqués
    int pieces{0};                     // morceaux de colonne après coupe aux coudes
    std::vector<std::string> messages; // refus et avertissements nommés

    // Remplis seulement si `measure_coverage` : part de la région balayée par les fils
    // (0 à 1), recouvrement moyen (1 = aucun fil en double) et aire non couverte.
    bool coverage_measured{false};
    double coverage_ratio{0.0};
    double overlap_ratio{0.0};
    double uncovered_area_mm2{0.0};
    // Densité et localisation, aussi sur demande : nombre maximal de triangles de fil sur un
    // même pixel de 100 µm (pic de densité), et boîte englobante des pixels non couverts
    // (`uncovered_min` / `uncovered_max`, repère modèle, valides si `uncovered_area_mm2 > 0`).
    int overlap_peak{0};
    Vec2um uncovered_min{};
    Vec2um uncovered_max{};
};

struct SkeletonSatinResult {
    std::vector<SkeletonSatinColumn> columns;
    // Axes de référence lissés (une polyligne par colonne, dans le même ordre que
    // les chaînes du squelette) : pour visualiser le squelette. Un axe fermé
    // (anneau) répète son premier point en dernier.
    std::vector<std::vector<Vec2um>> axes;
    SkeletonSatinDiagnostics diagnostics;
};

// Génère les traversées de la région. Déterministe. Ne lève jamais d'échec
// silencieux : ce qui n'est pas traité (région compacte, anneau pur) est nommé
// dans `diagnostics.messages` et la colonne correspondante est omise.
[[nodiscard]] Result<SkeletonSatinResult>
generate_skeleton_satin(const geometry::PathSet& region, const SkeletonSatinParameters& params);

// État de la mémoire des résultats de `generate_skeleton_satin` (partagée par tout le processus,
// bornée en octets) : pour les tests et le banc, jamais pour décider d'un résultat.
struct SkeletonResultCacheStats {
    std::size_t entries{0};
    std::size_t bytes{0};
    std::size_t hits{0};
    std::size_t misses{0};
};
[[nodiscard]] SkeletonResultCacheStats skeleton_result_cache_stats();

// SVG de diagnostic (mm, Y vers le bas) : contour de la région, axes de référence
// (tirets), traversées (trait fin) et zigzag de chaque colonne. Pour inspecter
// traversées, orientations et zones non couvertes sans lancer l'interface. Pure,
// déterministe.
[[nodiscard]] std::string skeleton_satin_to_svg(const geometry::PathSet& region,
                                                const SkeletonSatinResult& result);

} // namespace openstitch::auto_satin
