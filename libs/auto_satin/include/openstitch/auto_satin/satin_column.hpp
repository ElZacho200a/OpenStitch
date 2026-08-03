// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/satinability.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin {

// Barreau (rung) : segment transversal reliant les deux rails. `a` est du côté
// du rail A (gauche du sens de parcours), `b` du côté du rail B.
struct SatinRung {
    Vec2um a{};
    Vec2um b{};
};

// Géométrie d'une colonne satin ÉDITABLE : deux rails ouverts et les barreaux
// qui les découpent en intervalles correspondants. Les rails viennent des
// sections transversales de l'axe (squelette), PAS d'une découpe du contour.
struct SatinColumnGeometry {
    geometry::Path rail_a;
    geometry::Path rail_b;
    std::vector<SatinRung> rungs;
    std::uint32_t section_index{0};
    std::uint32_t section_count{1};
    std::optional<std::uint32_t> start_junction;
    std::optional<std::uint32_t> end_junction;
    double length_um{0.0};
    double mean_width_um{0.0};
    double min_width_um{0.0};
    double max_width_um{0.0};
};

struct SatinColumnsParameters {
    AutoSatinParameters analysis{};
    Micrometers station_spacing{500};       // pas d'échantillonnage de l'axe (0,5 mm)
    Micrometers rung_max_spacing{2'500};     // barreau au moins tous les 2,5 mm
    double rung_angle_threshold_deg{18.0};   // barreau si l'axe tourne au-delà
    double rung_width_ratio{0.30};           // barreau si la largeur varie au-delà
    int axis_smoothing_iterations{2};        // lissage Chaikin de l'axe
    int max_junctions{2};                    // au-delà : refus (trop complexe)
    // Un bout OUVERT (sans jonction) du squelette s'arrête, par construction du
    // transformée de distance/amincissement, sensiblement avant le bord réel de
    // la région (un embout arrondi ou pointu n'est pas couvert). Étend chaque
    // bout ouvert en ré-échantillonnant des sections transversales le long de la
    // tangente sortante tant qu'elles rétrécissent, jusqu'au bord réel (bissection).
    bool extend_open_ends{true};
    Micrometers tip_min_width{50};  // largeur plancher du dernier barreau (jamais nul)
    // Un bout de JONCTION (Y/T/croix) souffre du défaut inverse : la section
    // transversale d'une branche, calculée depuis sa seule tangente locale,
    // balaie le bourrelet de la confluence (pas la ceinture réelle de CETTE
    // branche) dès qu'on approche du nœud du squelette — la largeur mesurée
    // dérive alors nettement. On ampute cette queue instable puis on ancre
    // chaque rail (indépendamment) sur le sommet REFLEX (concave) du contour
    // le plus proche, dans ce rayon de recherche.
    bool anchor_junction_ends{true};
    Micrometers junction_anchor_radius{6'000};  // 6 mm : porte les encoches usuelles

    // § audit génération partielle (formes concaves/larges, `build_column`) :
    // ces trois seuils bornent à quel point un trou ou une irrégularité
    // locale peut être toléré avant de refuser la colonne entière plutôt que
    // de produire un fragment (rails discontinus, éventails/pointes
    // artificielles, zones non couvertes).
    //
    // Trou (entre deux stations valides consécutives, avant ou après le
    // nettoyage anti-croisement) au-delà duquel la colonne est refusée,
    // exprimé en multiple de `station_spacing`.
    double max_station_gap_ratio{5.0};
    // Fraction minimale de la longueur d'axe rééchantillonné qui doit être
    // effectivement convertie en stations valides (avant extension des
    // bouts) ; en dessous, trop de petits trous isolés se sont accumulés.
    double min_axis_coverage_ratio{0.85};
    // Saut de largeur maximal toléré entre deux stations adjacentes du
    // résultat final, en fraction de la plus grande des deux largeurs.
    double max_adjacent_width_jump_ratio{0.75};
};

struct SatinColumnsResult {
    SatinabilityStatus status{SatinabilityStatus::Unsuitable};
    std::vector<SatinColumnGeometry> columns;
    std::vector<std::string> warnings;
    std::string refusal;         // non vide = refus explicite (aucune colonne)
    SatinabilityReport report;   // rapport de satinabilité (diagnostic UI)
    AutoSatinDebug debug;        // étapes intermédiaires (SVG)
};

// Construit une ou plusieurs colonnes satin depuis une région vectorielle.
// - Suitable          -> une colonne (l'axe principal) ;
// - RequiresDecomposition (Y/T) -> une colonne par branche menant à une extrémité ;
// - Ambiguous / Unsuitable      -> refus explicite (aucune colonne).
// Déterministe. Ne modifie pas la région source.
[[nodiscard]] SatinColumnsResult build_satin_columns(const geometry::PathSet& region,
                                                     const SatinColumnsParameters& params);

}  // namespace openstitch::auto_satin
