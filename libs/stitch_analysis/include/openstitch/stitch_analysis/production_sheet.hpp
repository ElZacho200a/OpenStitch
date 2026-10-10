// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"

namespace openstitch::stitch_analysis {

// HP-PROD-001 : fiche de production. Structure de données PURE (aucun Qt, aucune
// mise en page) calculée depuis la séquence EFFECTIVE du projet ; le bureau la
// met en page pour l'impression / le PDF, le CLI la sérialise (JSON, HTML).
//
// Hypothèses de durée (HP-PROD-002, documentées dans la fiche elle-même) :
//   minutes = points / vitesse + (changements de fil x durée) + (coupes x durée)
// Les valeurs par défaut sont celles d'une machine domestique/semi-pro courante ;
// elles se règlent dans `ProductionOptions`.
struct ProductionOptions {
    std::string project_name;
    // Date d'édition « AAAA-MM-JJ », fournie par l'appelant : la bibliothèque ne
    // lit jamais l'horloge (sortie déterministe).
    std::string date;
    std::string notes; // notes libres de l'utilisateur, reprises telles quelles

    double stitches_per_minute{700.0}; // plage usuelle 600-800 points/min
    double color_change_seconds{30.0}; // temps d'un changement de fil
    double trim_seconds{2.0};          // temps d'une coupe

    // Vrai : le cadre est `project.canvas` et sert aussi de zone d'analyse
    // (« hors cadre »). Faux (DST relu, aucun cadre connu) : pas de cadre.
    bool use_project_canvas{true};
    AnalysisOptions analysis{};
};

struct ProductionObjectRef {
    ObjectId id{};
    std::string name; // vide : objet inconnu / design importé (id = 0)
};

struct ProductionBlock {
    std::size_t number{0}; // 1..N, dans l'ordre de couture
    std::array<std::uint8_t, 3> rgb{};
    // Référence de fil lisible (« Madeira Polyneon 1919 - Bleu roi »), vide si le
    // bloc n'a pas de `thread_key` (cas le plus courant tant que les objets n'en
    // portent pas) ou si le nuancier est inconnu.
    std::string thread_label;
    std::size_t stitches{0};
    std::size_t jumps{0};
    std::size_t trims{0};
    double thread_length_mm{0.0};
    double minutes{0.0}; // durée estimée du bloc (changement de fil du bloc inclus)
    std::vector<ProductionObjectRef> objects; // ordre de première apparition
};

struct ProductionFinding {
    Severity severity{Severity::Warning};
    std::string category;
    std::string message;
    std::string hint;
    ObjectId object{};
    std::string object_name;
};

// Trait du motif pour l'aperçu : points en millimètres, repère ÉCRAN (Y vers le bas).
struct ProductionStroke {
    std::size_t block{0}; // indice dans `blocks`
    std::vector<std::pair<float, float>> points;
};

struct ProductionSheet {
    std::string project_name;
    std::string date;
    std::string notes;

    double width_mm{0.0}; // boîte englobante des points cousus
    double height_mm{0.0};
    std::optional<std::pair<double, double>> frame_mm; // largeur x hauteur du cadre
    bool fits_frame{true};                             // vrai si pas de cadre

    std::size_t stitches{0};
    std::size_t jumps{0};
    std::size_t trims{0};
    std::size_t color_changes{0};
    double thread_length_m{0.0};

    double stitches_per_minute{700.0};
    double color_change_seconds{30.0};
    double trim_seconds{2.0};
    double estimated_minutes{0.0};

    std::vector<ProductionBlock> blocks;
    std::vector<ProductionFinding> findings;
    std::map<std::string, std::size_t> suppressed; // problèmes masqués par catégorie
    std::vector<ProductionStroke> preview;         // exclu du JSON
};

// Construit la fiche. `sequence` DOIT être `effective_sequence(project)` (ou la
// séquence d'un DST relu). Pure et déterministe.
[[nodiscard]] ProductionSheet make_production_sheet(const document::Project& project,
                                                    const stitch::StitchSequence& sequence,
                                                    const ProductionOptions& options = {});

// « 1 h 05 min », « 12 min », « < 1 min » (français, sans locale).
[[nodiscard]] std::string format_duration_fr(double minutes);

// JSON stable (clés dans un ordre fixe, nombres à précision fixe, sans aperçu).
[[nodiscard]] std::string production_to_json(const ProductionSheet& sheet);

struct ProductionHtmlOptions {
    // Source de l'image d'aperçu (URL, chemin ou ressource de QTextDocument). Vide :
    // l'aperçu est dessiné en SVG intégré (lignes colorées).
    std::string preview_src;
    // Taille d'affichage de l'aperçu, en pixels CSS (hauteur déduite du rapport).
    int preview_width_px{560};
    // Faux : sans preview_src, aucun aperçu n'est intégré (QTextDocument ignore le SVG).
    bool embed_svg_fallback{true};
};

// Page HTML autonome de la fiche (tableau des blocs, avertissements, notes).
// Écrite dans le sous-ensemble de HTML/CSS que `QTextDocument` sait rendre : le
// bureau imprime exactement ce document (le format de page, A4 portrait, est
// fixé par l'appelant).
[[nodiscard]] std::string production_to_html(const ProductionSheet& sheet,
                                             const ProductionHtmlOptions& options = {});

// Aperçu du motif en SVG (lignes colorées par bloc), dimensions en mm.
[[nodiscard]] std::string production_preview_svg(const ProductionSheet& sheet);

} // namespace openstitch::stitch_analysis
