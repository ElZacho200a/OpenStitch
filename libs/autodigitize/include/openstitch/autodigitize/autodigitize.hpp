// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <numbers>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/vector_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::autodigitize {

struct AutoOptions {
    Millimeters mm_per_px{25.4 / 96.0};
    Micrometers simplify_tolerance{200};
    // Ancien seuil satin : conserve pour compatibilite projet/API, ignore par
    // l'auto-broderie (qui ne produit plus que tatami ou contour).
    Micrometers satin_max_width{6'000};
    // Aire min (mm²) pour un remplissage ; en dessous, simple contour.
    double min_fill_area_mm2{4.0};
    // Ignore le FOND présumé : la couleur du plus gros morceau segmenté, et
    // TOUTE autre région de cette même couleur exacte (pas seulement ce
    // morceau) -- un fond peut se fragmenter en plusieurs régions disjointes
    // de la même couleur (ex. les zones hors d'un motif rond inscrit dans une
    // image carrée). Particulièrement utile sur une image SANS canal alpha :
    // sans transparence, le fond devient une région opaque comme les autres.
    bool skip_largest_region{false};
    // Deprecated/ignore : l'auto-broderie ne cree plus de satin automatiquement.
    // Ces champs restent pour ne pas casser les vieux appels/options, mais la
    // classification force maintenant tatami (zones) ou contour (petites zones).
    bool use_auto_satin{false};
    bool use_naive_satin{false};

    // --- Remplissages tatami (Lot B, audit marine plein cadre 2026-09-22) ---
    // Réglages appliqués aux tatami CRÉÉS par l'auto-numérisation, jamais aux
    // valeurs par défaut de `document::TatamiParams` (projets .osp existants
    // et objets manuels inchangés). Avant : tout à 0°, sans sous-couche.
    //
    // Orientation des rangées selon l'axe principal de la région (moments
    // d'inertie, `geometry::principal_axis`).
    bool auto_fill_angle{true};
    // Anisotropie (rapport des moments principaux) sous laquelle la forme est
    // jugée quasi isotrope : son axe n'a pas de sens stable, on prend
    // `isotropic_fill_angle`.
    double fill_isotropy_ratio{1.3};
    Angle isotropic_fill_angle{std::numbers::pi / 4.0}; // 45°
    // Deux tatami voisins (régions adjacentes) ne doivent pas avoir des
    // rangées presque parallèles (la frontière disparaît à l'œil) : la plus
    // petite des deux est décalée par pas de `fill_angle_search_step`.
    Angle min_neighbor_fill_angle_gap{std::numbers::pi / 9.0}; // 20°
    Angle fill_angle_search_step{std::numbers::pi / 36.0};     // 5°
    // Sous-couche selon l'aire nette de l'objet : aucune en dessous de
    // `underlay_edge_min_area_mm2`, contour seul jusqu'à
    // `underlay_parallel_min_area_mm2`, contour + rangées perpendiculaires
    // au-delà.
    bool auto_fill_underlay{true};
    double underlay_edge_min_area_mm2{20.0};
    double underlay_parallel_min_area_mm2{100.0};
    // Lot F : liaisons cousues cachées (underpath) plutôt que des sauts entre
    // les composantes de rangées d'un même tatami.
    bool fill_hidden_underpath{true};

    // --- Chevauchement et ordre (Lot C) ---
    // Débord de chaque tatami sur ses voisins BRODÉS, uniquement le long des
    // bords partagés. DÉSACTIVÉ PAR DÉFAUT (0) : la surface élargie était portée par
    // l'objet vectoriel lui-même, si bien que tout autre type de points créé ensuite sur
    // cette région (satin, contour, remplissage directionnel) débordait de sa zone
    // (+14 à +57 % d'aire mesurés). Les contours restent ceux de la segmentation ;
    // une valeur > 0 réactive le comportement historique (le paramètre `inset` des
    // tatamis voisins passe alors à 0).
    Micrometers fill_overlap{0};
    // Ordre de couture du résultat en « couches » (`optimization::
    // OrderStrategy::LayeredColorThenProximity`) : grandes zones de fond
    // d'abord, détails posés dessus ensuite, couleurs toujours regroupées.
    // Les objets issus d'une meme couleur restent contigus (routage).
    bool order_by_layers{true};
    double layer_large_area_ratio{0.25};

    // --- Fragments (Lot D) ---
    // Nettoyage d'une copie de travail de la segmentation avant numérisation.
    // Isthmes et lamelles plus étroits que cette largeur (ouverture
    // morphologique) rendus à la région voisine. 0 = désactivé.
    double min_feature_width_mm{1.2};
    // Régions plus petites que cette aire fusionnées avec la voisine de plus
    // longue frontière (jamais avec le fond ignoré). Seules les régions sans
    // autre voisine restent, et deviennent un contour (sous
    // `min_fill_area_mm2`). Aire mesurée comme celle du polygone vectorisé
    // (demi-pixel perdu le long des frontières, cf. merge_small_regions).
    // 0 = désactivé.
    double min_region_area_mm2{3.0};

    // --- Palette de fils (HP-THR-011) ---
    // Limite du nombre de COULEURS (donc de fils) du résultat : les couleurs de
    // régions les plus proches sont fusionnées (CIEDE2000, la plus grosse région
    // garde sa teinte) jusqu'à n'en garder que `max_threads`. Le fond ignoré ne
    // compte pas. 0 = pas de limite (comportement historique).
    std::size_t max_threads{0};
};

// Objets produits par l'autonumérisation : toujours EDITABLES (§13). Le type
// de chaque objet est choisi par la taille de sa region : tatami pour les zones
// remplissables, contour pour les petites regions.
struct AutoResult {
    std::vector<document::VectorObject> vectors;
    std::vector<document::EmbroideryObject> embroideries;
    // Avertissements non bloquants reportes a l'appelant (fragments ignores,
    // garde-fous des contours, etc.).
    std::vector<std::string> warnings;
};

// Construit les objets à partir d'une segmentation. Les identifiants sont
// alloués via `ids` (le générateur du document), donc jamais réutilisés.
[[nodiscard]] Result<AutoResult> auto_digitize(const segmentation::Segmentation& seg,
                                               IdGenerator<ObjectId>& ids,
                                               const AutoOptions& options);

// Même classification AutoChoice (tatami/contour selon la taille) et
// même moteur de construction que `auto_digitize`, mais à partir d'objets
// vectoriels DÉJÀ existants -- typiquement un import SVG direct
// (`formats::read_svg_file`), qui produit directement des `VectorObject`
// sans jamais passer par image/segmentation/vectorisation. `AutoResult::
// vectors` ne contient donc PAS les objets d'entrée (déjà dans le document
// de l'appelant).
[[nodiscard]] Result<AutoResult>
auto_digitize_vectors(const std::vector<document::VectorObject>& vectors,
                      IdGenerator<ObjectId>& ids, const AutoOptions& options);

// `satin_params_from_column`, `BuiltSatinSection`, `SatinBuildReport` et
// `build_satin_sections` vivaient ici jusqu'au 2026-08-17 (§4 de la mission
// de durcissement du contrat SatinPlanner) : cet adaptateur generique
// (`SatinPlan` -> sections satin editables) ne dependait d'AUCUNE primitive
// d'auto-classification d'image, ce qui forçait pourtant tout appelant
// (notamment les creations satin manuelles du desktop) a lier ce module
// entier -- accident historique, corrige par le deplacement vers
// `openstitch::satin_planning` (`openstitch/satin_planning/satin_sections.hpp`,
// inclus par `autodigitize.cpp`, jamais par ce header -- ce module reste un
// simple appelant, sans raison d'exposer ce type a SES propres consommateurs).
// L'auto-broderie n'appelle plus ce planner : satin manuel uniquement.

} // namespace openstitch::autodigitize
