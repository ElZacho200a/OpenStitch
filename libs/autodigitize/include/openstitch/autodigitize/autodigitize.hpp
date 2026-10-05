// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/vector_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/satin_coverage/coverage.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::autodigitize {

struct AutoOptions {
    Millimeters mm_per_px{25.4 / 96.0};
    Micrometers simplify_tolerance{200};
    // Largeur moyenne max pour proposer un satin (au-delà : tatami).
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
    // Active la classification automatique satin/tatami par forme (§24 du
    // plan de refonte satin, 2026-08-14 : "AutoChoice", une RECOMMANDATION
    // automatique de l'auto-numérisation, distincte d'un choix satin
    // explicitement forcé par l'utilisateur ailleurs dans l'application).
    // Quand une région est classée satin, elle passe TOUJOURS par le planner
    // récursif unifié (`satin_planning::create_satin_plan`, via
    // `build_satin_sections` ci-dessous) — jamais un appel direct sur la
    // région entière ni un refus global : cf. docs/source/satin.md.
    bool use_auto_satin{true};
    // Satin automatique NAÏF (rails_from_contour) : désactivé par défaut. Ses
    // deux rails « bouts les plus éloignés » débordent sur les formes concaves
    // ou branchues (les rungs enjambent les creux). Tant que le vrai moteur
    // auto-satin (squelette) ne génère pas la géométrie, on remplit ces zones
    // en tatami — découpé proprement sur la région, donc sans débordement.
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
    // bords partagés (au lieu du retrait `TatamiParams::inset`, qui laissait
    // un interstice de chaque côté). Bords extérieurs du motif et contact du
    // fond ignoré : retrait conservé. La surface élargie est portée par
    // l'objet vectoriel (le paramètre `inset` passe alors à 0). 0 = désactivé.
    Micrometers fill_overlap{300};
    // Ordre de couture du résultat en « couches » (`optimization::
    // OrderStrategy::LayeredColorThenProximity`) : grandes zones de fond
    // d'abord, détails posés dessus ensuite, couleurs toujours regroupées.
    // Les sections satin d'une même région restent contiguës (routage).
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
};

// Objets produits par l'autonumérisation : toujours ÉDITABLES (§13). Le type
// de chaque objet est choisi par la forme de sa région (satin pour les bandes
// fines, tatami pour les zones pleines, contour pour les petites régions).
struct AutoResult {
    std::vector<document::VectorObject> vectors;
    std::vector<document::EmbroideryObject> embroideries;
    // Avertissements non bloquants du moteur auto-satin (squelette), reportés
    // ici pour que l'appelant puisse les montrer à l'utilisateur. Quand une
    // branche de squelette est rejetée (ex. trop large pour du satin, aucune
    // section transversale valide sur son axe), la zone qu'elle couvrait
    // reçoit désormais un remplissage tatami de repli (§ ci-dessous, jamais
    // laissée sans le moindre point) -- ces avertissements restent utiles
    // pour EXPLIQUER pourquoi cette zone est en tatami plutôt qu'en satin
    // comme le reste de la région (ex. un empattement trop large, cf.
    // docs/source/satin.md § lettre en T d'un logo réel). Préfixé par la
    // région source pour localiser le problème.
    std::vector<std::string> warnings;
};

// Construit les objets à partir d'une segmentation. Les identifiants sont
// alloués via `ids` (le générateur du document), donc jamais réutilisés.
[[nodiscard]] Result<AutoResult> auto_digitize(const segmentation::Segmentation& seg,
                                               IdGenerator<ObjectId>& ids,
                                               const AutoOptions& options);

// Même classification AutoChoice (satin/tatami/contour selon la forme) et
// même moteur de construction que `auto_digitize`, mais à partir d'objets
// vectoriels DÉJÀ existants -- typiquement un import SVG direct
// (`formats::read_svg_file`), qui produit directement des `VectorObject`
// sans jamais passer par image/segmentation/vectorisation. `AutoResult::
// vectors` ne contient donc PAS les objets d'entrée (déjà dans le document
// de l'appelant) : seulement d'éventuels nouveaux vecteurs de repli, pour un
// reliquat satin non couvert (même garantie de couverture que
// `auto_digitize` — jamais une zone laissée sans le moindre point).
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
// `auto_digitize` reste le seul appelant ICI, au meme titre que n'importe
// quel autre consommateur desormais.

} // namespace openstitch::autodigitize
