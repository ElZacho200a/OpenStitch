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

// Mode de géométrie produit par `build_satin_columns` (§ refonte auto-satin
// paramétrique). `Legacy` : le pipeline historique (rails denses, une station
// = un nœud), inchangé, remplit `SatinColumnsResult::columns`. `Parametric` :
// le nouveau pipeline (rails Bézier épars couplés par quelques paires
// structurantes), remplit `SatinColumnsResult::parametric_columns`. Les deux
// partagent la même couche d'analyse (sections transversales, amputation de
// jonction, extension des bouts ouverts) ; seule la FINALISATION diffère —
// cf. `docs/source/satin.md`, section « objets satin paramétriques ».
enum class SatinGeometryMode : std::uint8_t { Legacy, Parametric };

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
    // Largeur plancher du dernier barreau (jamais nul) : 300 µm, pas la valeur
    // d'origine (50 µm). Défaut trouvé sur un projet réel (logo circulaire à
    // motif de circuit imprimé finement détaillé) : la quasi-totalité des
    // colonnes satin de l'image ont au moins une extrémité OUVERTE (pointe
    // de trace, plot de connecteur...), et chacune fermait donc sur un
    // dernier barreau de ~50 µm -- en-dessous même du pas de quantification
    // DST (0,1 mm, cf. formats/dst.hpp), donc un point de couture
    // pratiquement inexploitable par une machine réelle. 300 µm reste
    // nettement plus étroit que `min_satin_width` (0,8 mm, le seuil « satin
    // digne de ce nom » pour toute la colonne) -- la pointe reste une
    // pointe -- tout en dépassant largement le pas DST avec une marge de
    // sécurité (x3).
    Micrometers tip_min_width{300};
    // Un bout de JONCTION (Y/T/croix) souffre du défaut inverse : la section
    // transversale d'une branche, calculée depuis sa seule tangente locale,
    // balaie le bourrelet de la confluence (pas la ceinture réelle de CETTE
    // branche) dès qu'on approche du nœud du squelette — la largeur mesurée
    // dérive alors nettement. On ampute cette queue instable puis on ancre
    // chaque rail (indépendamment) sur le sommet REFLEX (concave) du contour
    // le plus proche, dans ce rayon de recherche.
    bool anchor_junction_ends{true};
    Micrometers junction_anchor_radius{6'000};  // 6 mm : porte les encoches usuelles

    // Plafond de SÉCURITÉ pour le rayon local de traitement d'une jonction
    // (§ StableBranchEnd / JunctionSeparator) — PAS la valeur réellement
    // utilisée : le rayon effectif (`JunctionCore::local_radius_um`) est
    // dérivé des données réelles (distance à la plus proche
    // `StableBranchEndInfo`), toujours ≤ ce plafond. N'intervient que pour
    // borner un cas dégénéré (branches très courtes) ; ne doit jamais être
    // lu comme « le rayon du noyau ».
    Micrometers junction_core_radius{10'000};  // 10 mm (plafond, rarement atteint)

    // Aire, au-delà de laquelle un `JunctionCore` résiduel est jugé
    // significatif : `JunctionCore::requires_fill` est alors levé pour
    // signaler qu'un objet de remplissage séparé (tatami) devrait être créé
    // plutôt que de laisser cette zone non couturée. La synthèse de cet
    // objet elle-même n'est pas implémentée ici (hors périmètre) : seul le
    // diagnostic (aire + contour) est produit.
    double junction_core_significant_area_um2{500'000.0};  // 0,5 mm²

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

    // --- § objets satin paramétriques (mode `Parametric` uniquement) -------
    SatinGeometryMode geometry_mode{SatinGeometryMode::Legacy};

    // Sélection des paires structurantes (§ étape 4) : une paire est ajoutée
    // si la retirer ferait dépasser l'une de ces erreurs. `bezier_fit_tolerance`
    // borne l'écart perpendiculaire station->courbe ajustée ; les trois
    // suivants bornent respectivement l'écart de largeur (fraction de la
    // largeur locale), l'écart de tangente, et l'espacement entre deux
    // paires retenues consécutives (le long de l'axe).
    Micrometers bezier_fit_tolerance{150};              // 0,15 mm
    double maximum_width_error_ratio{0.12};              // 12 % de la largeur locale
    double maximum_tangent_error_deg{12.0};
    Micrometers maximum_control_pair_spacing{15'000};    // 15 mm : jamais un rail sans guide sur une longue distance
    Micrometers minimum_control_pair_spacing{300};       // 0,3 mm : ne pas sur-raffiner un bruit de station

    // Recouvrement local à une jonction (§ étape 9) : chaque branche est
    // prolongée un peu au-delà de sa dernière section stable, DANS la
    // confluence, jamais jusqu'à un point partagé. Fraction de la largeur
    // locale de la branche, bornée par un plancher/plafond absolu.
    double junction_overlap_ratio{0.6};
    Micrometers junction_overlap_min{300};    // 0,3 mm
    Micrometers junction_overlap_max{2'500};  // 2,5 mm : jamais loin dans la branche voisine
};

// Zone centrale d'une jonction à 2+ branches non couverte par les colonnes
// (§ StableBranchEnd / JunctionSeparator) : ce qui reste de la région locale
// de jonction une fois les secteurs de branche retirés — jamais calculée
// comme « région entière moins colonnes », toujours restreinte à la région
// locale (disque de rayon `local_radius_um` autour du nœud). `boundary` est
// le polygone des `JunctionSeparatorInfo` de cette jonction, dans l'ordre
// angulaire (simple par construction — sommets triés par angle autour d'un
// centre commun). Diagnostic pur : ne participe à aucune génération de
// points ; si `requires_fill`, un objet de remplissage séparé est signalé
// (non synthétisé ici).
struct JunctionCore {
    std::uint32_t junction_id{0};
    std::vector<Vec2um> boundary;
    double area_um2{0.0};
    double configured_radius_um{0.0};    // plafond de sécurité (`junction_core_radius`), PAS le rayon réel
    double local_radius_um{0.0};         // rayon local réellement utilisé (données réelles, ≤ configured)
    double actual_max_radius_um{0.0};    // distance MESURÉE du point le plus éloigné du noyau au nœud
    bool requires_fill{false};           // aire au-delà du seuil de significativité : à remplir séparément
};

// Dernière section transversale RÉELLEMENT stable d'une branche à une
// jonction (§ StableBranchEnd) : marque la fin du corps régulier de la
// colonne et le point d'entrée du traitement spécial de jonction. Jamais
// déplacée — capture telle quelle une station déjà présente dans le rail.
// Diagnostic (SVG, tests) uniquement.
struct StableBranchEndInfo {
    std::uint32_t junction_id{0};
    std::size_t column_index{0};
    bool at_end{false};
    Vec2um rail_a_point{};
    Vec2um rail_b_point{};
};

// Frontière locale PARTAGÉE entre deux branches angulairement adjacentes
// (§ JunctionSeparator) : construite dans la zone de confluence (sommet
// reflex local du contour, ou à défaut intersection de la bissectrice
// angulaire avec le contour) — jamais une section transversale classique, et
// jamais un point sur un rail. `column_index_before`/`column_index_after`
// identifient les deux branches qu'elle sépare (ordre angulaire). Diagnostic
// (SVG, tests) uniquement.
struct JunctionSeparatorInfo {
    std::uint32_t junction_id{0};
    Vec2um point{};
    std::size_t column_index_before{0};
    std::size_t column_index_after{0};
};

// Secteur local attribué à une branche à l'intérieur de la région de
// jonction : borné par ses deux `JunctionSeparatorInfo` voisins, l'arc de
// contour réel jusqu'à son propre `StableBranchEndInfo`, et le segment
// transversal de ce dernier. Diagnostic (SVG, tests) uniquement — ne
// construit ni ne modifie aucun rail.
struct JunctionSectorInfo {
    std::uint32_t junction_id{0};
    std::size_t column_index{0};
    std::vector<Vec2um> boundary;
};

// Paire structurante entre les deux rails (§ étape 4) : une station DENSE
// retenue parce que la retirer dégraderait l'ajustement Bézier au-delà des
// seuils configurés (`bezier_fit_tolerance`/`maximum_width_error_ratio`/
// `maximum_tangent_error_deg`), ou parce qu'elle marque un événement
// structurel (extrémité, paire de jonction). Sert à la fois d'ancrage au fit
// Bézier ET de barreau pour la génération de points -- `rungs` (hérité) en
// est la projection minimale, pour compatibilité avec
// `stitch_generation::fill_satin_columns`/`document::SatinParams`.
struct SatinControlPair {
    Vec2um axis_point;
    Vec2um rail_a_point;
    Vec2um rail_b_point;
    Vec2um tangent;              // direction (offset non unitaire, convention `PathNode::tan_out`)
    double width_um{0.0};
    bool structural{true};
    bool junction_pair{false};   // paire terminale de recouvrement de jonction (§ étape 9)
    bool open_tip_pair{false};   // paire terminale d'un bout ouvert (§ étape 8)
};

// Ligne d'angle explicite (§ étape 7) : contrainte d'orientation entre les
// deux rails, à une abscisse curviligne donnée sur chacun (rail APLATI, pas
// un paramètre Bézier brut). Dans cette implémentation, chaque
// `SatinControlPair` produit exactement une `SatinAngleGuide` -- les deux
// notions coïncident tant qu'aucune édition manuelle ne les dissocie
// (§ compatibilité éditeur, non implémentée dans cette passe).
struct SatinAngleGuide {
    Vec2um rail_a_point;
    Vec2um rail_b_point;
    double rail_a_arc_um{0.0};
    double rail_b_arc_um{0.0};
    bool structural{true};
};

// Objet satin paramétrique (§ refonte auto-satin, remplace le pipeline
// `SatinColumnGeometry` en mode `Parametric`) : deux rails Bézier ÉPARS
// (quelques `geometry::PathNode` à tangentes, jamais une station = un nœud),
// couplés par un petit nombre de `SatinControlPair`/`SatinAngleGuide`. Les
// points de couture NE FONT PAS PARTIE de cette représentation : ils sont
// dérivés à la demande par `stitch_generation::fill_satin_columns`, qui
// aplatit `rail_a`/`rail_b` (`geometry::flatten`) avant de les parcourir.
struct ParametricSatinObject {
    geometry::Path rail_a;
    geometry::Path rail_b;
    std::vector<SatinControlPair> control_pairs;
    std::vector<SatinAngleGuide> angle_guides;
    std::vector<SatinRung> rungs;  // = angle_guides projetés en points (compat SatinParams)

    std::uint32_t section_index{0};
    std::uint32_t section_count{1};
    std::optional<std::uint32_t> start_junction;
    std::optional<std::uint32_t> end_junction;

    double mean_width_um{0.0};
    double min_width_um{0.0};
    double max_width_um{0.0};
    double length_um{0.0};

    // Recouvrement RÉELLEMENT appliqué à chaque bout de jonction (0 si ce
    // bout est ouvert, ou si `junction_overlap_*` n'a rien pu ajouter).
    double start_overlap_um{0.0};
    double end_overlap_um{0.0};

    // Diagnostic d'ajustement (§ SVG de debug, étape 15).
    std::size_t raw_station_count{0};
    double max_fit_error_um{0.0};
};

// Ordre de couture déterministe à une jonction (§ étape 10) : indices dans
// `SatinColumnsResult::parametric_columns`, du premier cousu (dessous) au
// dernier (dessus, masque les transitions des précédents). Heuristique :
// branche la plus large d'abord, la plus fine en dernier — cf.
// `docs/source/satin.md`.
struct SatinJunctionPlan {
    std::uint32_t junction_id{0};
    std::vector<std::uint32_t> stitch_order;
};

struct SatinColumnsResult {
    SatinabilityStatus status{SatinabilityStatus::Unsuitable};
    std::vector<SatinColumnGeometry> columns;  // mode Legacy
    std::vector<ParametricSatinObject> parametric_columns;  // mode Parametric
    std::vector<SatinJunctionPlan> junction_plans;          // mode Parametric
    std::vector<std::string> warnings;
    std::string refusal;         // non vide = refus explicite (aucune colonne)
    SatinabilityReport report;   // rapport de satinabilité (diagnostic UI)
    AutoSatinDebug debug;        // étapes intermédiaires (SVG)
    std::vector<JunctionCore> junction_cores;              // zones centrales residuelles (diagnostic, Legacy)
    std::vector<StableBranchEndInfo> stable_branch_ends;    // diagnostic (SVG, tests, Legacy)
    std::vector<JunctionSeparatorInfo> junction_separators; // diagnostic (SVG, tests, Legacy)
    std::vector<JunctionSectorInfo> junction_sectors;       // diagnostic (SVG, tests, Legacy)
};

// Construit une ou plusieurs colonnes satin depuis une région vectorielle.
// - Suitable          -> une colonne (l'axe principal) ;
// - RequiresDecomposition (Y/T) -> une colonne par branche menant à une extrémité ;
// - Ambiguous / Unsuitable      -> refus explicite (aucune colonne).
// Déterministe. Ne modifie pas la région source.
[[nodiscard]] SatinColumnsResult build_satin_columns(const geometry::PathSet& region,
                                                     const SatinColumnsParameters& params);

}  // namespace openstitch::auto_satin
