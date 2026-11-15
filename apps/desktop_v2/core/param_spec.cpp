// SPDX-License-Identifier: Apache-2.0
#include "param_spec.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <numbers>

namespace openstitch::desktop_v2 {

namespace {

using K = ParamKind;

constexpr std::string_view kPull =
    "manual|Manuelle;auto|Automatique;adaptive|Adaptative;predicted|Prédictive";
constexpr std::string_view kUnderlay = "manual|Manuelle;auto|Automatique";
constexpr std::string_view kRunStyle =
    "run|Point droit;bean|Point triple (bean);backstitch|Point arrière;stem|Point tige";
constexpr std::string_view kCap =
    "flat|Plate;rounded|Arrondie;tapered|Effilée;automatic|Automatique";
constexpr std::string_view kLock =
    "none|Aucun;backforth|Aller-retour;triangle|Triangle;zigzag|Micro-zigzag";
constexpr std::string_view kShort =
    "disabled|Aucun;remove_and_redistribute|Supprimer et répartir;single_inset|Un retrait;"
    "multi_level_inset|Retraits étagés";
constexpr std::string_view kSplit =
    "disabled|Aucun;simple|Simple;staggered|Décalé;deterministic_jitter|Décalage pseudo-aléatoire";
constexpr std::string_view kCorner = "none|Aucun;miter|Onglet;overlap|Chevauchement;cut|Coupe";
constexpr std::string_view kJagged = "none|Aucun;side_a|Côté A;side_b|Côté B;both|Les deux côtés";
constexpr std::string_view kStrategy =
    "skeleton|Squelette;parallel_sweep|Balayage parallèle;medial_axis|Axe médian;"
    "guided_satin|Satin guidé";
constexpr std::string_view kBlanketStyle = "blanket|Boutonnière;zigzag|Zigzag";
constexpr std::string_view kBlanketSide = "left|Gauche;right|Droite;center|Centré";
constexpr std::string_view kMotif =
    "wave|Vague;zigzag|Zigzag;leaf|Feuille;heart|Coeur;diamond|Losange;scallop|Feston";
constexpr std::string_view kTack = "straight|Droit;zigzag|Zigzag;blanket|Boutonnière";
constexpr std::string_view kGradientMethod =
    "directional|Directionnel;satin_interlaced|Satin entrelacé;long_and_short|Passé empiétant;"
    "controlled_scatter|Dispersion contrôlée;cross_hatching|Hachures croisées";

// type, clé, libellé, nature, min, max, pas, unité[, options]
const std::vector<ParamSpec>& table() {
    static const std::vector<ParamSpec> kTable{
        {"running", "stitch_length_mm", "Longueur de point", K::Number, 0.5, 12.0, 0.1, "mm"},
        {"running", "repeats", "Passages", K::Integer, 1, 3, 1, ""},

        {"tatami", "angle_deg", "Angle des rangées", K::Number, -360.0, 360.0, 5.0, "°"},
        {"tatami", "row_spacing_mm", "Écart entre rangées", K::Number, 0.1, 2.0, 0.05, "mm"},
        {"tatami", "stitch_length_mm", "Longueur de point", K::Number, 1.0, 7.0, 0.1, "mm"},
        {"tatami", "inset_mm", "Retrait du bord", K::Number, 0.0, 2.0, 0.05, "mm"},
        {"tatami", "pull_compensation_mm", "Compensation de tirage", K::Number, 0.0, 1.5, 0.05,
         "mm"},
        {"tatami", "underlay_edge", "Sous-couche de contour", K::Bool, 0, 1, 1, ""},
        {"tatami", "underlay_parallel", "Sous-couche de rangées", K::Bool, 0, 1, 1, ""},

        // Réglages avancés (clés et plages du registre de champs de `set_params`).
        {"tatami", "stagger", "Décalage des rangées (motif en quinconce)", K::Integer, 1, 8, 1, ""},
        {"tatami", "row_end_clip_mm", "Raccourcissement des fins de rangée", K::Number, 0.0, 5.0,
         0.1, "mm"},
        {"tatami", "edge_outline", "Contour de bordure", K::Bool, 0, 1, 1, ""},
        {"directional", "stagger", "Décalage des rangées (motif en quinconce)", K::Integer, 1, 8, 1,
         ""},
        {"directional", "spacing_regularity", "Régularité de l'écart", K::Number, 0.0, 1.0, 0.05,
         ""},
        {"directional", "edge_weight", "Poids des bords", K::Number, 0.0, 1.0, 0.05, ""},
        {"directional", "underlay_inset_mm", "Retrait de la sous-couche", K::Number, 0.0, 5.0, 0.1,
         "mm"},
        {"directional", "underlay_spacing_mm", "Écart de la sous-couche", K::Number, 0.1, 10.0, 0.1,
         "mm"},
        {"directional", "underlay_zigzag", "Sous-couche en zigzag", K::Bool, 0, 1, 1, ""},
        {"directional", "sector_overlap_mm", "Recouvrement des secteurs", K::Number, 0.0, 1.0, 0.05,
         "mm"},
        {"directional", "edge_outline", "Contour de bordure", K::Bool, 0, 1, 1, ""},
        {"satin", "max_stitch_length_mm", "Longueur maximale d'un point", K::Number, 1.0, 15.0, 0.5,
         "mm"},
        {"satin", "max_width_hard_mm", "Largeur au-delà de laquelle la colonne est refusée",
         K::Number, 1.0, 60.0, 0.5, "mm"},

        {"directional", "row_spacing_mm", "Écart entre rangées", K::Number, 0.1, 2.0, 0.05, "mm"},
        {"directional", "stitch_length_mm", "Longueur de point", K::Number, 1.0, 7.0, 0.1, "mm"},
        {"directional", "inset_mm", "Retrait du bord", K::Number, 0.0, 2.0, 0.05, "mm"},
        {"directional", "pull_compensation_mm", "Compensation de tirage", K::Number, 0.0, 1.5, 0.05,
         "mm"},
        {"directional", "underlay_edge", "Sous-couche de contour", K::Bool, 0, 1, 1, ""},

        {"satin", "density_mm", "Densité (écart des points)", K::Number, 0.1, 2.0, 0.05, "mm"},
        {"satin", "pull_compensation_mm", "Compensation de tirage", K::Number, 0.0, 1.5, 0.05,
         "mm"},
        {"satin", "max_width_mm", "Largeur maximale", K::Number, 1.0, 48.0, 0.5, "mm"},
        {"satin", "center_underlay", "Sous-couche centrale", K::Bool, 0, 1, 1, ""},
        {"satin", "underlay_edge", "Sous-couche de bord", K::Bool, 0, 1, 1, ""},

        // Sous-couches et compensation de tirage (modes manuel / auto / adaptatif / PRÉDICTIF).
        {"tatami", "pull_mode", "Mode de compensation", K::Choice, 0, 3, 1, "", kPull},
        {"tatami", "underlay_mode", "Sous-couche", K::Choice, 0, 1, 1, "", kUnderlay},
        {"tatami", "underlay_zigzag", "Sous-couche en zigzag", K::Bool, 0, 1, 1, ""},
        {"tatami", "underlay_inset_mm", "Retrait de la sous-couche", K::Number, 0.0, 3.0, 0.1,
         "mm"},
        {"tatami", "underlay_spacing_mm", "Écart de la sous-couche", K::Number, 0.5, 6.0, 0.1,
         "mm"},
        {"tatami", "hidden_underpath", "Liaisons cachées", K::Bool, 0, 1, 1, ""},
        {"directional", "pull_mode", "Mode de compensation", K::Choice, 0, 3, 1, "", kPull},
        {"directional", "underlay_mode", "Sous-couche", K::Choice, 0, 1, 1, "", kUnderlay},
        {"directional", "underlay_parallel", "Sous-couche de rangées", K::Bool, 0, 1, 1, ""},
        {"directional", "hidden_underpath", "Liaisons cachées", K::Bool, 0, 1, 1, ""},
        {"satin", "pull_mode", "Mode de compensation", K::Choice, 0, 3, 1, "", kPull},
        {"satin", "underlay_mode", "Sous-couche", K::Choice, 0, 1, 1, "", kUnderlay},
        {"satin", "underlay_zigzag", "Sous-couche en zigzag", K::Bool, 0, 1, 1, ""},

        // Remplissages spéciaux.
        {"spiral", "row_spacing_mm", "Écart entre tours", K::Number, 0.1, 2.0, 0.05, "mm"},
        {"spiral", "stitch_length_mm", "Longueur de point", K::Number, 1.0, 7.0, 0.1, "mm"},
        {"spiral", "inset_mm", "Retrait du bord", K::Number, 0.0, 2.0, 0.05, "mm"},
        {"spiral", "clockwise", "Sens horaire", K::Bool, 0, 1, 1, ""},
        {"spiral", "pull_mode", "Mode de compensation", K::Choice, 0, 3, 1, "", kPull},
        {"cross_stitch", "cell_mm", "Côté d'une cellule", K::Number, 1.0, 8.0, 0.25, "mm"},
        {"cross_stitch", "grid_angle_deg", "Angle de la grille", K::Number, -360.0, 360.0, 5.0,
         "°"},
        {"cross_stitch", "full_cross", "Croix complète", K::Bool, 0, 1, 1, ""},
        {"cross_stitch", "passes", "Passes", K::Integer, 1, 3, 1, ""},
        {"applique", "placement_length_mm", "Point de placement", K::Number, 1.0, 7.0, 0.25, "mm"},
        {"applique", "tack_width_mm", "Largeur du maintien", K::Number, 0.2, 20.0, 0.25, "mm"},
        {"applique", "tack_spacing_mm", "Écart du maintien", K::Number, 0.3, 20.0, 0.25, "mm"},
        {"applique", "cover_width_mm", "Largeur du satin de couverture", K::Number, 0.5, 20.0, 0.25,
         "mm"},
        {"applique", "cover_density_mm", "Densité de la couverture", K::Number, 0.2, 1.5, 0.05,
         "mm"},
        {"applique", "stops", "Arrêts machine", K::Bool, 0, 1, 1, ""},
    };
    return kTable;
}

// Champs communs au satin à rails et à l'auto-satin (mêmes clés, mêmes plages que le registre de
// `set_params`). `withShared` : les six premiers n'existent déjà dans la table que pour `satin`.
void appendSatinCommon(std::vector<ParamSpec>& t, std::string_view type, bool withShared) {
    if (withShared) {
        t.push_back({type, "pull_compensation_mm", "Compensation de tirage", K::Number, 0.0, 2.0,
                     0.05, "mm"});
        t.push_back({type, "pull_mode", "Mode de compensation", K::Choice, 0, 3, 1, "", kPull});
        t.push_back({type, "center_underlay", "Sous-couche centrale", K::Bool, 0, 1, 1, ""});
        t.push_back({type, "underlay_edge", "Sous-couche de bord", K::Bool, 0, 1, 1, ""});
        t.push_back({type, "underlay_zigzag", "Sous-couche en zigzag", K::Bool, 0, 1, 1, ""});
        t.push_back({type, "underlay_mode", "Sous-couche", K::Choice, 0, 1, 1, "", kUnderlay});
    }
    t.push_back({type, "pull_left_mm", "Compensation côté A", K::Number, 0.0, 3.0, 0.05, "mm"});
    t.push_back({type, "pull_right_mm", "Compensation côté B", K::Number, 0.0, 3.0, 0.05, "mm"});
    t.push_back({type, "push_start_mm", "Extension au départ", K::Number, 0.0, 3.0, 0.05, "mm"});
    t.push_back({type, "push_end_mm", "Extension à la fin", K::Number, 0.0, 3.0, 0.05, "mm"});
    t.push_back({type, "cap_start", "Extrémité de départ", K::Choice, 0, 3, 1, "", kCap});
    t.push_back({type, "cap_end", "Extrémité de fin", K::Choice, 0, 3, 1, "", kCap});
    t.push_back({type, "lock_start", "Point d'arrêt au départ", K::Choice, 0, 3, 1, "", kLock});
    t.push_back({type, "lock_end", "Point d'arrêt à la fin", K::Choice, 0, 3, 1, "", kLock});
    t.push_back(
        {type, "lock_length_mm", "Longueur du point d'arrêt", K::Number, 0.2, 3.0, 0.1, "mm"});
    t.push_back({type, "lock_passes", "Passes du point d'arrêt", K::Integer, 1, 4, 1, ""});
    t.push_back({type, "entry_point_mm", "Point d'entrée", K::Point, -5000, 5000, 0.1, "mm"});
    t.push_back({type, "exit_point_mm", "Point de sortie", K::Point, -5000, 5000, 0.1, "mm"});
    t.push_back({type, "short_stitch", "Points courts", K::Choice, 0, 3, 1, "", kShort});
    t.push_back(
        {type, "split_stitch", "Fractionnement des points longs", K::Choice, 0, 3, 1, "", kSplit});
    t.push_back({type, "auto_spacing", "Écart selon la largeur locale", K::Bool, 0, 1, 1, ""});
    t.push_back({type, "corner_mode", "Coins vifs", K::Choice, 0, 3, 1, "", kCorner});
    t.push_back({type, "corner_threshold_deg", "Seuil de coin", K::Number, 15.0, 150.0, 5.0, "°"});
    t.push_back({type, "jagged", "Bord irrégulier", K::Choice, 0, 3, 1, "", kJagged});
    t.push_back({type, "jagged_amplitude_mm", "Amplitude du bord irrégulier", K::Number, 0.2, 6.0,
                 0.1, "mm"});
    t.push_back(
        {type, "jagged_seed", "Graine du bord irrégulier", K::Integer, 0, 4294967295.0, 1, ""});
}

// Table complète : tout champ du registre de `set_params` y figure (test de complétude).
std::vector<ParamSpec> completeTable() {
    std::vector<ParamSpec> t = table();
    const auto add = [&](const ParamSpec& s) { t.push_back(s); };
    // Contour.
    add({"running", "min_length_mm", "Longueur minimale d'un point", K::Number, 0.1, 20.0, 0.1,
         "mm"});
    add({"running", "style", "Style de point", K::Choice, 0, 3, 1, "", kRunStyle});
    add({"running", "stem_offset_mm", "Décalage du point tige", K::Number, 0.1, 2.0, 0.05, "mm"});
    // Tatami / directionnel.
    add({"tatami", "entry_point_mm", "Point de départ du remplissage", K::Point, -5000, 5000, 0.1,
         "mm"});
    add({"directional", "guides", "Courbes guides", K::List, 0, 64, 1, ""});
    add({"directional", "break_lines", "Lignes de rupture", K::List, 0, 64, 1, ""});
    add({"directional", "density_gradient", "Dégradé de densité", K::List, 0, 1, 1, ""});
    add({"directional", "handmade", "Aspect fait main", K::Bool, 0, 1, 1, ""});
    add({"directional", "handmade_intensity", "Intensité du fait main", K::Integer, 0, 100, 5,
         "%"});
    add({"directional", "seed", "Graine du hasard", K::Integer, 0, 4294967295.0, 1, ""});
    // Satin à rails et auto-satin.
    appendSatinCommon(t, "satin", false);
    add({"satin", "rungs", "Barreaux", K::List, 0, 2000, 1, ""});
    appendSatinCommon(t, "auto_satin", true);
    add({"auto_satin", "strategy", "Méthode de calcul", K::Choice, 0, 3, 1, "", kStrategy});
    add({"auto_satin", "spacing_mm", "Écart entre traversées", K::Number, 0.1, 2.0, 0.05, "mm"});
    add({"auto_satin", "split_threshold_mm", "Seuil de fractionnement", K::Number, 1.0, 20.0, 0.5,
         "mm"});
    add({"auto_satin", "split_length_mm", "Longueur d'un segment fractionné", K::Number, 0.5, 20.0,
         0.5, "mm"});
    add({"auto_satin", "compact_axis", "Axe compact (formes courtes)", K::Bool, 0, 1, 1, ""});
    add({"auto_satin", "sweep_angle_deg", "Angle de balayage", K::OptionalNumber, -360.0, 360.0,
         5.0, "°"});
    add({"auto_satin", "guides", "Guides d'orientation", K::List, 0, 64, 1, ""});
    // Boutonnière / zigzag.
    add({"blanket", "style", "Style", K::Choice, 0, 1, 1, "", kBlanketStyle});
    add({"blanket", "width_mm", "Largeur de la bande", K::Number, 0.2, 20.0, 0.25, "mm"});
    add({"blanket", "spacing_mm", "Écart entre stations", K::Number, 0.3, 20.0, 0.25, "mm"});
    add({"blanket", "side", "Côté du tracé", K::Choice, 0, 2, 1, "", kBlanketSide});
    // Concentrique.
    add({"contour_fill", "row_spacing_mm", "Écart entre anneaux", K::Number, 0.1, 10.0, 0.05,
         "mm"});
    add({"contour_fill", "stitch_length_mm", "Longueur de point", K::Number, 1.0, 7.0, 0.1, "mm"});
    add({"contour_fill", "min_length_mm", "Longueur minimale d'un point", K::Number, 0.0, 7.0, 0.1,
         "mm"});
    add({"contour_fill", "inset_mm", "Retrait du bord", K::Number, 0.0, 10.0, 0.05, "mm"});
    add({"contour_fill", "spiral", "Anneaux en spirale continue", K::Bool, 0, 1, 1, ""});
    add({"contour_fill", "hidden_underpath", "Liaisons cachées", K::Bool, 0, 1, 1, ""});
    add({"contour_fill", "pull_compensation_mm", "Compensation de tirage", K::Number, 0.0, 3.0,
         0.05, "mm"});
    add({"contour_fill", "pull_mode", "Mode de compensation", K::Choice, 0, 3, 1, "", kPull});
    // Motif le long du contour et remplissage à motif.
    add({"motif_run", "motif", "Motif", K::Choice, 0, 5, 1, "", kMotif});
    add({"motif_run", "size_mm", "Hauteur du motif", K::Number, 0.5, 40.0, 0.5, "mm"});
    add({"motif_run", "spacing_mm", "Longueur d'une répétition", K::Number, 1.0, 100.0, 0.5, "mm"});
    add({"motif_run", "stitch_length_mm", "Longueur de point", K::Number, 1.0, 7.0, 0.1, "mm"});
    add({"motif_run", "min_length_mm", "Longueur minimale d'un point", K::Number, 0.0, 7.0, 0.1,
         "mm"});
    add({"motif_run", "alternate", "Motif en miroir une fois sur deux", K::Bool, 0, 1, 1, ""});
    add({"motif_fill", "motif", "Motif", K::Choice, 0, 5, 1, "", kMotif});
    add({"motif_fill", "size_mm", "Taille du motif", K::Number, 0.5, 40.0, 0.5, "mm"});
    add({"motif_fill", "spacing_mm", "Pas de la grille", K::Number, 1.0, 100.0, 0.5, "mm"});
    add({"motif_fill", "angle_deg", "Angle de la grille", K::Number, -360.0, 360.0, 5.0, "°"});
    add({"motif_fill", "stagger", "Une rangée sur deux décalée", K::Bool, 0, 1, 1, ""});
    add({"motif_fill", "inset_mm", "Retrait du bord", K::Number, 0.0, 10.0, 0.05, "mm"});
    add({"motif_fill", "stitch_length_mm", "Longueur de point", K::Number, 1.0, 7.0, 0.1, "mm"});
    add({"motif_fill", "min_length_mm", "Longueur minimale d'un point", K::Number, 0.0, 7.0, 0.1,
         "mm"});
    add({"motif_fill", "hidden_underpath", "Liaisons cachées", K::Bool, 0, 1, 1, ""});
    add({"motif_fill", "pull_compensation_mm", "Compensation de tirage", K::Number, 0.0, 3.0, 0.05,
         "mm"});
    add({"motif_fill", "pull_mode", "Mode de compensation", K::Choice, 0, 3, 1, "", kPull});
    // Radial.
    add({"radial", "row_spacing_mm", "Écart entre rayons", K::Number, 0.1, 10.0, 0.05, "mm"});
    add({"radial", "stitch_length_mm", "Longueur de point", K::Number, 1.0, 7.0, 0.1, "mm"});
    add({"radial", "min_length_mm", "Longueur minimale d'un point", K::Number, 0.0, 7.0, 0.1,
         "mm"});
    add({"radial", "inset_mm", "Retrait du bord", K::Number, 0.0, 10.0, 0.05, "mm"});
    add({"radial", "center_mm", "Centre des rayons", K::Point, -5000, 5000, 0.1, "mm"});
    add({"radial", "hidden_underpath", "Liaisons cachées", K::Bool, 0, 1, 1, ""});
    add({"radial", "pull_compensation_mm", "Compensation de tirage", K::Number, 0.0, 3.0, 0.05,
         "mm"});
    add({"radial", "pull_mode", "Mode de compensation", K::Choice, 0, 3, 1, "", kPull});
    // Spirale (complète les lignes de base).
    add({"spiral", "min_length_mm", "Longueur minimale d'un point", K::Number, 0.0, 7.0, 0.1,
         "mm"});
    add({"spiral", "center_mm", "Centre de la spirale", K::Point, -5000, 5000, 0.1, "mm"});
    add({"spiral", "hidden_underpath", "Liaisons cachées", K::Bool, 0, 1, 1, ""});
    add({"spiral", "pull_compensation_mm", "Compensation de tirage", K::Number, 0.0, 3.0, 0.05,
         "mm"});
    // Croisé.
    add({"crosshatch", "angle_deg", "Angle de la première couche", K::Number, -360.0, 360.0, 5.0,
         "°"});
    add({"crosshatch", "layers", "Couches", K::Integer, 2, 3, 1, ""});
    add({"crosshatch", "row_spacing_mm", "Écart entre rangées", K::Number, 0.3, 10.0, 0.05, "mm"});
    add({"crosshatch", "stitch_length_mm", "Longueur de point", K::Number, 1.0, 7.0, 0.1, "mm"});
    add({"crosshatch", "inset_mm", "Retrait du bord", K::Number, 0.0, 10.0, 0.05, "mm"});
    add({"crosshatch", "hidden_underpath", "Liaisons cachées", K::Bool, 0, 1, 1, ""});
    add({"crosshatch", "pull_compensation_mm", "Compensation de tirage", K::Number, 0.0, 3.0, 0.05,
         "mm"});
    add({"crosshatch", "pull_mode", "Mode de compensation", K::Choice, 0, 3, 1, "", kPull});
    // Point de croix et appliqué (complètent les lignes de base).
    add({"cross_stitch", "hidden_underpath", "Liaisons cachées", K::Bool, 0, 1, 1, ""});
    add({"applique", "tack", "Point de maintien", K::Choice, 0, 2, 1, "", kTack});
    add({"applique", "cut_offset_mm", "Décalage du contour de découpe", K::Number, -5.0, 5.0, 0.25,
         "mm"});
    // Fondu multicolore (mission 2026-11-14). `palette` et `transition` restent hors de cette
    // table (commande dédiée du panneau de fondu, pas un champ numérique simple) -- comme
    // `rail_a`/`rail_b` pour le satin.
    add({"multicolor_gradient", "method", "Méthode", K::Choice, 0, 4, 1, "", kGradientMethod});
    add({"multicolor_gradient", "complementary", "Rampes complémentaires", K::Bool, 0, 1, 1, ""});
    add({"multicolor_gradient", "satin_density_mm", "Densité du satin entrelacé", K::Number, 0.1,
         2.0, 0.05, "mm"});
    add({"multicolor_gradient", "crosshatch_layers", "Couches (hachures croisées)", K::Integer, 2,
         3, 1, ""});
    add({"multicolor_gradient", "crosshatch_angle_deg", "Angle (hachures croisées)", K::Number,
         -360.0, 360.0, 5.0, "°"});
    return t;
}

const std::vector<ParamSpec>& fullTable() {
    static const std::vector<ParamSpec> kFull = completeTable();
    return kFull;
}

double mm(Micrometers um) {
    return static_cast<double>(um.value) / 1000.0;
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    return buf;
}

} // namespace

std::vector<std::pair<std::string, std::string>> paramOptions(const ParamSpec& spec) {
    std::vector<std::pair<std::string, std::string>> out;
    std::string_view rest = spec.options;
    while (!rest.empty()) {
        const auto semi = rest.find(';');
        const std::string_view item = rest.substr(0, semi);
        const auto bar = item.find('|');
        out.emplace_back(std::string(item.substr(0, bar)), bar == std::string_view::npos
                                                               ? std::string(item)
                                                               : std::string(item.substr(bar + 1)));
        rest = semi == std::string_view::npos ? std::string_view{} : rest.substr(semi + 1);
    }
    return out;
}

std::vector<ParamSpec> allParamSpecs(std::string_view typeId) {
    std::vector<ParamSpec> out;
    for (const auto& s : fullTable()) {
        if (s.type == typeId) {
            out.push_back(s);
        }
    }
    return out;
}

std::vector<ParamSpec> paramSpecs(std::string_view typeId) {
    std::vector<ParamSpec> out;
    for (const auto& s : allParamSpecs(typeId)) {
        if (isScalarKind(s.kind)) {
            out.push_back(s);
        }
    }
    return out;
}

const std::vector<std::string_view>& paramTypeIds() {
    static const std::vector<std::string_view> kIds{
        "running", "tatami",       "satin",        "directional", "auto_satin",
        "blanket", "contour_fill", "motif_run",    "motif_fill",  "radial",
        "spiral",  "crosshatch",   "cross_stitch", "applique",    "multicolor_gradient"};
    return kIds;
}

namespace {
// Minuscules sans accents pour la recherche : « sous-couche » trouve « Sous-couche », « ecart »
// trouve « Écart ». Couvre les lettres accentuées du français ; le reste est copié tel quel.
std::string foldForSearch(std::string_view text) {
    static const std::pair<std::string_view, char> kMap[] = {
        {"é", 'e'}, {"è", 'e'}, {"ê", 'e'}, {"ë", 'e'}, {"à", 'a'}, {"â", 'a'},
        {"î", 'i'}, {"ï", 'i'}, {"ô", 'o'}, {"ù", 'u'}, {"û", 'u'}, {"ç", 'c'},
        {"É", 'e'}, {"È", 'e'}, {"À", 'a'}, {"Ê", 'e'}, {"Ç", 'c'}};
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        bool mapped = false;
        for (const auto& [from, to] : kMap) {
            if (text.compare(i, from.size(), from) == 0) {
                out.push_back(to);
                i += from.size();
                mapped = true;
                break;
            }
        }
        if (!mapped) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(text[i]))));
            ++i;
        }
    }
    return out;
}
} // namespace

ParamLevel paramLevel(const ParamSpec& spec) {
    static constexpr std::string_view kAdvanced[] = {
        "underlay_inset_mm", "underlay_spacing_mm", "underlay_zigzag",      "hidden_underpath",
        "stagger",           "row_end_clip_mm",     "edge_outline",         "spacing_regularity",
        "edge_weight",       "sector_overlap_mm",   "max_stitch_length_mm", "max_width_hard_mm",
        "underlay_mode"};
    // Expert : graines, seuils, collections et points d'entrée / sortie.
    static constexpr std::string_view kExpert[] = {"seed",
                                                   "jagged_seed",
                                                   "handmade_intensity",
                                                   "corner_threshold_deg",
                                                   "jagged_amplitude_mm",
                                                   "stem_offset_mm",
                                                   "min_length_mm",
                                                   "push_start_mm",
                                                   "push_end_mm",
                                                   "pull_left_mm",
                                                   "pull_right_mm",
                                                   "guides",
                                                   "break_lines",
                                                   "density_gradient",
                                                   "rungs",
                                                   "split_threshold_mm",
                                                   "split_length_mm",
                                                   "entry_point_mm",
                                                   "exit_point_mm",
                                                   "center_mm",
                                                   "sweep_angle_deg",
                                                   "lock_passes",
                                                   "lock_length_mm",
                                                   "cut_offset_mm"};
    // Avancé : finitions et variantes rares.
    static constexpr std::string_view kAdvancedMore[] = {
        "cap_start",    "cap_end",      "lock_start",  "lock_end",  "short_stitch",
        "split_stitch", "auto_spacing", "corner_mode", "jagged",    "handmade",
        "compact_axis", "style",        "tack",        "alternate", "hidden_underpath"};
    if (std::find(std::begin(kExpert), std::end(kExpert), spec.key) != std::end(kExpert)) {
        return ParamLevel::Expert;
    }
    if (std::find(std::begin(kAdvancedMore), std::end(kAdvancedMore), spec.key) !=
        std::end(kAdvancedMore)) {
        // « style » est le réglage principal du contour et de la boutonnière : essentiel.
        return spec.key == "style" ? ParamLevel::Essential : ParamLevel::Advanced;
    }
    return std::find(std::begin(kAdvanced), std::end(kAdvanced), spec.key) != std::end(kAdvanced)
               ? ParamLevel::Advanced
               : ParamLevel::Essential;
}

std::string_view paramGroup(const ParamSpec& spec) {
    const std::string_view key = spec.key;
    const auto has = [&](std::string_view part) {
        return key.find(part) != std::string_view::npos;
    };
    if (has("underlay") || key == "hidden_underpath") {
        return "Sous-couche";
    }
    if (has("pull") || has("push")) {
        return "Compensation de tirage";
    }
    if (has("lock_") || key == "entry_point_mm" || key == "exit_point_mm") {
        return "Entrée, sortie et fixation";
    }
    if (has("cap_")) {
        return "Extrémités";
    }
    if (has("corner") || has("jagged")) {
        return "Coins et bords";
    }
    if (has("handmade") || key == "seed") {
        return "Aspect fait main";
    }
    if (key == "guides" || key == "break_lines" || key == "density_gradient" || key == "rungs" ||
        key == "center_mm" || key == "sweep_angle_deg" || key == "strategy") {
        return "Géométrie et guides";
    }
    return "Points";
}

bool paramMatches(const ParamSpec& spec, std::string_view query) {
    const std::string needle = foldForSearch(query);
    if (needle.empty()) {
        return true;
    }
    return foldForSearch(spec.label).find(needle) != std::string::npos ||
           foldForSearch(spec.key).find(needle) != std::string::npos;
}

namespace {

using openstitch::Angle;

double degOf(Angle a) {
    return a.radians * 180.0 / std::numbers::pi;
}
double b2d(bool v) {
    return v ? 1.0 : 0.0;
}
template <class E> double en(E e) {
    return static_cast<double>(static_cast<int>(e));
}

// Une ligne par champ du registre de `set_params` : lecture en unité d'affichage.
#define PS_MM(k, m)                                                                                \
    if (key == k)                                                                                  \
    return mm(p.m)
#define PS_DEG(k, m)                                                                               \
    if (key == k)                                                                                  \
    return degOf(p.m)
#define PS_B(k, m)                                                                                 \
    if (key == k)                                                                                  \
    return b2d(p.m)
#define PS_N(k, m)                                                                                 \
    if (key == k)                                                                                  \
    return static_cast<double>(p.m)
#define PS_E(k, m)                                                                                 \
    if (key == k)                                                                                  \
    return en(p.m)
#define PS_PT(k, m)                                                                                \
    if (key == k)                                                                                  \
    return b2d(p.m.has_value())
#define PS_LIST(k, m)                                                                              \
    if (key == k)                                                                                  \
    return static_cast<double>(p.m.size())

using Opt = std::optional<double>;

Opt readP(const document::RunningStitchParams& p, std::string_view key) {
    PS_MM("stitch_length_mm", stitch_length);
    PS_MM("min_length_mm", min_length);
    PS_N("repeats", repeats);
    PS_E("style", style);
    PS_MM("stem_offset_mm", stem_offset);
    return std::nullopt;
}

Opt readP(const document::TatamiParams& p, std::string_view key) {
    PS_DEG("angle_deg", angle);
    PS_MM("row_spacing_mm", row_spacing);
    PS_MM("stitch_length_mm", stitch_length);
    PS_MM("inset_mm", inset);
    PS_MM("pull_compensation_mm", pull_compensation);
    PS_B("underlay_edge", underlay_edge);
    PS_B("underlay_parallel", underlay_parallel);
    PS_E("pull_mode", pull_mode);
    PS_E("underlay_mode", underlay_mode);
    PS_B("underlay_zigzag", underlay_zigzag);
    PS_MM("underlay_inset_mm", underlay_inset);
    PS_MM("underlay_spacing_mm", underlay_spacing);
    PS_B("hidden_underpath", hidden_underpath);
    PS_N("stagger", stagger);
    PS_MM("row_end_clip_mm", row_end_clip);
    PS_B("edge_outline", edge_outline);
    PS_PT("entry_point_mm", entry_point);
    return std::nullopt;
}

Opt readP(const document::DirectionalFillParams& p, std::string_view key) {
    PS_MM("row_spacing_mm", row_spacing);
    PS_MM("stitch_length_mm", stitch_length);
    PS_MM("inset_mm", inset);
    PS_MM("pull_compensation_mm", pull_compensation);
    PS_B("underlay_edge", underlay_edge);
    PS_E("pull_mode", pull_mode);
    PS_E("underlay_mode", underlay_mode);
    PS_B("underlay_parallel", underlay_parallel);
    PS_B("hidden_underpath", hidden_underpath);
    PS_N("stagger", stagger);
    if (key == "spacing_regularity")
        return p.spacing_regularity;
    if (key == "edge_weight")
        return p.edge_weight;
    PS_MM("underlay_inset_mm", underlay_inset);
    PS_MM("underlay_spacing_mm", underlay_spacing);
    PS_B("underlay_zigzag", underlay_zigzag);
    PS_MM("sector_overlap_mm", sector_overlap);
    PS_B("edge_outline", edge_outline);
    PS_LIST("guides", guides);
    PS_LIST("break_lines", break_lines);
    if (key == "density_gradient")
        return b2d(p.density_gradient.has_value());
    PS_B("handmade", handmade);
    PS_N("handmade_intensity", handmade_intensity);
    PS_N("seed", seed);
    return std::nullopt;
}

// Champs communs au satin à rails et à l'auto-satin.
template <class P> Opt readSatinCommon(const P& p, std::string_view key) {
    PS_MM("pull_compensation_mm", pull_compensation);
    PS_E("pull_mode", pull_mode);
    PS_B("center_underlay", center_underlay);
    PS_B("underlay_edge", underlay_edge);
    PS_B("underlay_zigzag", underlay_zigzag);
    PS_MM("pull_left_mm", pull_left);
    PS_MM("pull_right_mm", pull_right);
    PS_MM("push_start_mm", push_start);
    PS_MM("push_end_mm", push_end);
    PS_E("cap_start", cap_start);
    PS_E("cap_end", cap_end);
    PS_E("lock_start", lock_start);
    PS_E("lock_end", lock_end);
    PS_MM("lock_length_mm", lock_length);
    PS_N("lock_passes", lock_passes);
    PS_PT("entry_point_mm", entry_point);
    PS_PT("exit_point_mm", exit_point);
    PS_E("underlay_mode", underlay_mode);
    PS_E("short_stitch", short_stitch);
    PS_E("split_stitch", split_stitch);
    PS_B("auto_spacing", auto_spacing);
    PS_E("corner_mode", corner_mode);
    PS_DEG("corner_threshold_deg", corner_threshold);
    PS_E("jagged", jagged);
    PS_MM("jagged_amplitude_mm", jagged_amplitude);
    PS_N("jagged_seed", jagged_seed);
    return std::nullopt;
}

Opt readP(const document::SatinParams& p, std::string_view key) {
    PS_MM("density_mm", density);
    PS_MM("max_width_mm", max_width);
    PS_MM("max_width_hard_mm", max_width_hard);
    PS_MM("max_stitch_length_mm", max_stitch_length);
    PS_LIST("rungs", rungs);
    return readSatinCommon(p, key);
}

Opt readP(const document::AutoSatinParams& p, std::string_view key) {
    PS_MM("spacing_mm", spacing);
    PS_MM("split_threshold_mm", split_threshold);
    PS_MM("split_length_mm", split_length);
    PS_B("compact_axis", compact_axis);
    PS_E("strategy", strategy);
    if (key == "sweep_angle_deg")
        return p.sweep_angle ? degOf(*p.sweep_angle) : 0.0;
    PS_LIST("guides", guides);
    return readSatinCommon(p, key);
}

Opt readP(const document::BlanketStitchParams& p, std::string_view key) {
    PS_E("style", style);
    PS_MM("width_mm", width);
    PS_MM("spacing_mm", spacing);
    PS_E("side", side);
    return std::nullopt;
}

Opt readP(const document::ContourFillParams& p, std::string_view key) {
    PS_MM("row_spacing_mm", row_spacing);
    PS_MM("stitch_length_mm", stitch_length);
    PS_MM("min_length_mm", min_length);
    PS_MM("inset_mm", inset);
    PS_B("spiral", spiral);
    PS_B("hidden_underpath", hidden_underpath);
    PS_MM("pull_compensation_mm", pull_compensation);
    PS_E("pull_mode", pull_mode);
    return std::nullopt;
}

Opt readP(const document::MotifRunParams& p, std::string_view key) {
    PS_E("motif", motif);
    PS_MM("size_mm", size);
    PS_MM("spacing_mm", spacing);
    PS_MM("stitch_length_mm", stitch_length);
    PS_MM("min_length_mm", min_length);
    PS_B("alternate", alternate);
    return std::nullopt;
}

Opt readP(const document::MotifFillParams& p, std::string_view key) {
    PS_E("motif", motif);
    PS_MM("size_mm", size);
    PS_MM("spacing_mm", spacing);
    PS_DEG("angle_deg", angle);
    PS_B("stagger", stagger);
    PS_MM("inset_mm", inset);
    PS_MM("stitch_length_mm", stitch_length);
    PS_MM("min_length_mm", min_length);
    PS_B("hidden_underpath", hidden_underpath);
    PS_MM("pull_compensation_mm", pull_compensation);
    PS_E("pull_mode", pull_mode);
    return std::nullopt;
}

Opt readP(const document::RadialFillParams& p, std::string_view key) {
    PS_MM("row_spacing_mm", row_spacing);
    PS_MM("stitch_length_mm", stitch_length);
    PS_MM("min_length_mm", min_length);
    PS_MM("inset_mm", inset);
    PS_PT("center_mm", center);
    PS_B("hidden_underpath", hidden_underpath);
    PS_MM("pull_compensation_mm", pull_compensation);
    PS_E("pull_mode", pull_mode);
    return std::nullopt;
}

Opt readP(const document::SpiralFillParams& p, std::string_view key) {
    PS_MM("row_spacing_mm", row_spacing);
    PS_MM("stitch_length_mm", stitch_length);
    PS_MM("min_length_mm", min_length);
    PS_MM("inset_mm", inset);
    PS_PT("center_mm", center);
    PS_B("clockwise", clockwise);
    PS_B("hidden_underpath", hidden_underpath);
    PS_MM("pull_compensation_mm", pull_compensation);
    PS_E("pull_mode", pull_mode);
    return std::nullopt;
}

Opt readP(const document::CrosshatchFillParams& p, std::string_view key) {
    PS_DEG("angle_deg", angle);
    PS_N("layers", layers);
    PS_MM("row_spacing_mm", row_spacing);
    PS_MM("stitch_length_mm", stitch_length);
    PS_MM("inset_mm", inset);
    PS_B("hidden_underpath", hidden_underpath);
    PS_MM("pull_compensation_mm", pull_compensation);
    PS_E("pull_mode", pull_mode);
    return std::nullopt;
}

Opt readP(const document::CrossStitchParams& p, std::string_view key) {
    PS_MM("cell_mm", cell);
    PS_DEG("grid_angle_deg", grid_angle);
    PS_B("full_cross", full_cross);
    PS_N("passes", passes);
    PS_B("hidden_underpath", hidden_underpath);
    return std::nullopt;
}

Opt readP(const document::MulticolorGradientParams& p, std::string_view key) {
    // Palette/transition ne sont pas des champs numériques simples (édition par la commande
    // dédiée du panneau de fondu, pas le glisser-valeur générique) ; `method` reste un simple
    // choix comme les autres énumérations de ce fichier.
    PS_E("method", method);
    PS_B("complementary", complementary);
    PS_MM("satin_density_mm", satin_density);
    PS_N("crosshatch_layers", crosshatch_layers);
    PS_DEG("crosshatch_angle_deg", crosshatch_angle);
    return std::nullopt;
}

Opt readP(const document::AppliqueParams& p, std::string_view key) {
    PS_MM("placement_length_mm", placement_length);
    PS_E("tack", tack);
    PS_MM("tack_width_mm", tack_width);
    PS_MM("tack_spacing_mm", tack_spacing);
    PS_MM("cover_width_mm", cover_width);
    PS_MM("cover_density_mm", cover_density);
    PS_MM("cut_offset_mm", cut_offset);
    PS_B("stops", stops);
    return std::nullopt;
}

#undef PS_MM
#undef PS_DEG
#undef PS_B
#undef PS_N
#undef PS_E
#undef PS_PT
#undef PS_LIST

// Point facultatif d'un champ Point de l'objet.
std::optional<Vec2um> pointOf(const document::EmbroideryObject& o, std::string_view key) {
    using namespace document;
    if (const auto* t = std::get_if<TatamiParams>(&o.params)) {
        return key == "entry_point_mm" ? t->entry_point : std::nullopt;
    }
    if (const auto* s = std::get_if<SatinParams>(&o.params)) {
        return key == "entry_point_mm"  ? s->entry_point
               : key == "exit_point_mm" ? s->exit_point
                                        : std::nullopt;
    }
    if (const auto* a = std::get_if<AutoSatinParams>(&o.params)) {
        return key == "entry_point_mm"  ? a->entry_point
               : key == "exit_point_mm" ? a->exit_point
                                        : std::nullopt;
    }
    if (const auto* r = std::get_if<RadialFillParams>(&o.params)) {
        return key == "center_mm" ? r->center : std::nullopt;
    }
    if (const auto* sp = std::get_if<SpiralFillParams>(&o.params)) {
        return key == "center_mm" ? sp->center : std::nullopt;
    }
    return std::nullopt;
}

const ParamSpec* findSpec(std::string_view typeId, std::string_view key) {
    for (const auto& s : fullTable()) {
        if (s.type == typeId && s.key == key) {
            return &s;
        }
    }
    return nullptr;
}

} // namespace

std::optional<double> readParam(const document::EmbroideryObject& o, std::string_view key) {
    return std::visit([&](const auto& p) { return readP(p, key); }, o.params);
}

std::optional<std::pair<double, double>> readPointMm(const document::EmbroideryObject& o,
                                                     std::string_view key) {
    const auto p = pointOf(o, key);
    if (!p) {
        return std::nullopt;
    }
    return std::pair{mm(p->x), mm(p->y)};
}

bool paramIsSet(const document::EmbroideryObject& o, std::string_view key) {
    if (key == "sweep_angle_deg") {
        const auto* a = std::get_if<document::AutoSatinParams>(&o.params);
        return a != nullptr && a->sweep_angle.has_value();
    }
    const auto v = readParam(o, key);
    return v.has_value() && *v > 0.5; // Point : 0 / 1 ; List : effectif
}

namespace {
std::string setParamsBatch(const std::vector<std::uint64_t>& ids, std::string_view key,
                           const std::string& literal) {
    std::string ops;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ops += std::string(i == 0 ? "" : ",") + R"({"op":"set_params","object":)" +
               std::to_string(ids[i]) + R"(,"params":{")" + std::string(key) + "\":" + literal +
               "}}";
    }
    return R"({"schema":"openstitch.batch/1","operations":[)" + ops + "]}";
}
} // namespace

std::string batchSetPoint(const std::vector<std::uint64_t>& ids, std::string_view typeId,
                          std::string_view key, double xMm, double yMm) {
    const auto* spec = findSpec(typeId, key);
    if (ids.empty() || spec == nullptr || spec->kind != K::Point || !std::isfinite(xMm) ||
        !std::isfinite(yMm) || xMm < spec->min || xMm > spec->max || yMm < spec->min ||
        yMm > spec->max) {
        return {};
    }
    return setParamsBatch(ids, key, "[" + num(xMm) + "," + num(yMm) + "]");
}

std::string batchClearParam(const std::vector<std::uint64_t>& ids, std::string_view typeId,
                            std::string_view key) {
    const auto* spec = findSpec(typeId, key);
    if (ids.empty() || spec == nullptr) {
        return {};
    }
    if (spec->kind == K::Point || spec->kind == K::OptionalNumber || key == "density_gradient") {
        return setParamsBatch(ids, key, "null");
    }
    if (spec->kind == K::List) {
        return setParamsBatch(ids, key, "[]");
    }
    return {};
}

std::string batchSetParam(const std::vector<std::uint64_t>& ids, std::string_view typeId,
                          std::string_view key, double value) {
    if (ids.empty() || !std::isfinite(value)) {
        return {};
    }
    const auto* it = findSpec(typeId, key);
    if (it == nullptr || value < it->min - 1e-9 || value > it->max + 1e-9) {
        return {};
    }
    std::string literal;
    switch (it->kind) {
    case K::Point:
    case K::List:
        return {};
    case K::Bool:
        literal = value >= 0.5 ? "true" : "false";
        break;
    case K::Integer:
        literal = std::to_string(static_cast<long long>(std::llround(value)));
        break;
    case K::OptionalNumber:
    case K::Number:
        literal = num(value);
        break;
    case K::Choice: {
        const auto options = paramOptions(*it);
        const auto index = static_cast<std::size_t>(std::llround(value));
        if (value < -1e-9 || index >= options.size()) {
            return {};
        }
        literal = "\"" + options[index].first + "\"";
        break;
    }
    }
    std::string ops;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ops += std::string(i == 0 ? "" : ",") + R"({"op":"set_params","object":)" +
               std::to_string(ids[i]) + R"(,"params":{")" + std::string(key) + "\":" + literal +
               "}}";
    }
    return R"({"schema":"openstitch.batch/1","operations":[)" + ops + "]}";
}

} // namespace openstitch::desktop_v2
