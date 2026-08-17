// SPDX-License-Identifier: Apache-2.0
#pragma once

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

}  // namespace openstitch::autodigitize
