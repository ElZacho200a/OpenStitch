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
    // Moteur topologique : squelette, decomposition en sections ouvertes,
    // barreaux et routage par source commune. Active par defaut.
    //
    // Sur une région dont le squelette est BRANCHÉ (au moins une jonction
    // réelle), passe TOUJOURS d'abord par la décomposition guidée par
    // squelette (SGSD : `libs/satin_planning`, découpage en sous-régions
    // simples + recherche à faisceau + fusion) — plus d'échappatoire vers
    // l'ancien appel direct à `build_satin_columns` sur la région entière,
    // c'est désormais le seul chemin pour une région branchée (gain de
    // couverture réel et mesuré sur le corpus de test, +4,6 à +7,4 points
    // selon la forme, cf. `openstitch-cli sgsd-debug` et
    // docs/source/satin.md). L'appel direct à `build_satin_columns` reste
    // néanmoins utilisé en INTERNE comme filet de sécurité structurel quand
    // SGSD ne résout rien (région non branchée dès le départ, ou cas
    // topologique non couvert comme un anneau — squelette en boucle fermée
    // sans jonction détectable) : jamais une zone laissée sans le moindre
    // point, cf. `try_sgsd_decomposition` dans autodigitize.cpp.
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

// Convertit une colonne squelette (`auto_satin::SatinColumnGeometry`, mode
// Legacy, OU `auto_satin::ParametricSatinObject`, mode Parametric — mêmes
// noms de champs rail_a/rail_b/rungs/section_*/*_junction, cf.
// libs/auto_satin/include/openstitch/auto_satin/satin_column.hpp) en
// `document::SatinParams`. Point de conversion UNIQUE partagé par
// l'autonumérisation (ci-dessus) et les créations satin manuelles
// (apps/desktop) — `stitch_generation::fill_satin_columns` aplatit
// `rail_a`/`rail_b` (Bézier ou polyligne dense indifféremment) à la demande,
// donc un seul point d'entrée suffit pour les deux modes.
template <typename ColumnLike>
[[nodiscard]] document::SatinParams satin_params_from_column(const ColumnLike& col, Micrometers density,
                                                              Micrometers pull_compensation,
                                                              bool center_underlay,
                                                              Micrometers max_width) {
    document::SatinParams sp;
    sp.rail_a = col.rail_a;
    sp.rail_b = col.rail_b;
    sp.density = density;
    sp.pull_compensation = pull_compensation;
    sp.center_underlay = center_underlay;
    sp.max_width = max_width;
    sp.rungs.reserve(col.rungs.size());
    for (const auto& rung : col.rungs) {
        sp.rungs.push_back(document::SatinRung{rung.a, rung.b});
    }
    if (col.section_count > 1 || col.start_junction || col.end_junction) {
        sp.topology = document::SatinSectionTopology{col.section_index, col.section_count,
                                                     col.start_junction, col.end_junction};
    }
    return sp;
}

// Une section satin déjà convertie en géométrie éditable
// (`document::SatinParams`) et en bande approximative de la surface qu'elle
// couvre (utile à l'appelant pour calculer un éventuel repli, ex. tatami sur
// le reliquat non couvert). Vue UNIFORME sur `auto_satin::SatinColumnGeometry`
// (Legacy) et `auto_satin::ParametricSatinObject` (Parametric).
struct BuiltSatinSection {
    document::SatinParams params;
    geometry::Path strip;
};

// Résultat de `build_satin_sections` : les sections construites, plus le
// contexte nécessaire pour un aperçu ou un message d'erreur côté appelant.
struct SatinBuildReport {
    std::vector<BuiltSatinSection> sections;
    // Vrai si la décomposition guidée par squelette (SGSD) a été utilisée
    // (région branchée). Faux si l'appel direct historique à
    // `build_satin_columns` a suffi (région déjà simple) ou si RIEN n'a pu
    // être construit.
    bool used_sgsd{false};
    // Vrai si une partie MESURABLE de la région source reste non couverte par
    // les sections ci-dessus -- calculé géométriquement (région moins bandes
    // réellement produites), avec un seuil mixte fixe+proportionnel pour
    // tolérer le reliquat NATUREL d'une pointe/jonction (quelques mm² même
    // quand tout réussit, § docs/source/satin.md) sans le confondre avec un
    // vrai trou. Détecte aussi bien un chemin SGSD non isolé/refusé qu'une
    // sous-région ACCEPTÉE (au moins une colonne produite) mais dont la
    // géométrie ne couvre qu'une fraction de sa propre surface -- défaut réel
    // trouvé le 2026-08-14 sur une boucle/contre-poinçon de lettre isolée par
    // SGSD mais trop ronde pour un unique ruban, invisible à l'ancien signal
    // purement structurel. Signal pour un éventuel remplissage de repli côté
    // appelant.
    bool structural_gap{false};
    std::vector<std::string> warnings;
    // Diagnostic de satinabilité de la région ENTIÈRE, calculé une seule
    // fois en amont (que SGSD s'engage ou non) -- toujours renseigné sauf
    // échec d'analyse pur (forme dégénérée). Utile pour un aperçu utilisateur
    // (statut, largeurs, nombre de branches) même quand la région a ensuite
    // été décomposée en plusieurs sous-régions.
    std::optional<auto_satin::SatinabilityReport> whole_region_report;
    // Message de refus le plus pertinent si `sections` est vide (celui de
    // l'appel direct, ou un résumé synthétique si SGSD a tout tenté sans
    // succès) -- vide si `sections` est non vide.
    std::string refusal;
};

// Construit réellement les colonnes satin sur `region`, en passant TOUJOURS
// par la décomposition guidée par squelette (SGSD, `libs/satin_planning`)
// quand son squelette est BRANCHÉ (au moins une jonction réelle) — gain de
// couverture réel et mesuré sur le corpus de test (+4,6 à +7,4 points selon
// la forme, cf. `openstitch-cli sgsd-debug` et docs/source/satin.md). Repli
// INTERNE sur l'appel direct historique à `build_satin_columns` dès que SGSD
// ne résout rien (région non branchée dès le départ, ou cas topologique non
// couvert comme un anneau — squelette en boucle fermée sans jonction
// détectable) : jamais une zone laissée sans le moindre point.
//
// Point d'entrée UNIQUE partagé par l'auto-numérisation (`auto_digitize`
// ci-dessus) et la création satin manuelle (`apps/desktop/main_window.cpp`)
// — mêmes garanties de couverture partout, un seul endroit à faire évoluer.
// `warningLabel`, si non vide, préfixe chaque message de `warnings` (ex.
// "Région 12" côté auto-numérisation).
[[nodiscard]] SatinBuildReport build_satin_sections(const geometry::PathSet& region,
                                                    const auto_satin::SatinColumnsParameters& genParams,
                                                    Micrometers density, Micrometers pullCompensation,
                                                    bool centerUnderlay, Micrometers maxWidth,
                                                    const std::string& warningLabel = {});

}  // namespace openstitch::autodigitize
