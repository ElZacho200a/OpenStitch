// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_analysis {

// HP-PROD-003 : gabarit de placement imprimé à l'échelle 1:1. Structure pure (aucun Qt) :
// le motif est découpé en pages (papier A4 par défaut) qui se recouvrent d'une marge de
// recollage ; chaque page porte les traits du motif, la croix de centrage et les axes du
// motif (clipés à la page), des repères de recollage, un pied de page et, sur la première
// page, une règle de 100 mm qui permet de vérifier que l'imprimante n'a pas mis à l'échelle.
// Toutes les coordonnées sont en µm entiers, repère de la page (origine en haut à gauche,
// Y vers le bas).
struct PlacementTemplateOptions {
    Micrometers paper_width{210'000}; // papier en portrait (A4)
    Micrometers paper_height{297'000};
    Micrometers margin{10'000};        // marge non imprimable, de chaque côté
    Micrometers overlap{10'000};       // recouvrement entre deux pages voisines
    Micrometers ruler_length{100'000}; // règle de contrôle d'échelle
    bool landscape{false};
    std::string title; // texte du pied de page (nom du projet)
};

struct TemplateSegment {
    Vec2um a;
    Vec2um b;
};

struct TemplateText {
    Vec2um pos; // coin haut-gauche du texte
    std::string text;
    double size_pt{8.0};
};

struct TemplatePage {
    std::size_t column{0};
    std::size_t row{0};
    std::vector<TemplateSegment> stitches;  // traits du motif (couture)
    std::vector<TemplateSegment> axes;      // axes du motif (X et Y par le centre)
    std::vector<TemplateSegment> crosshair; // croix de centrage (seulement si dans la page)
    std::vector<TemplateSegment> marks;     // repères de recollage aux angles de la zone utile
    std::vector<TemplateSegment> ruler;     // règle de contrôle d'échelle (page 1 seulement)
    std::vector<TemplateText> texts;
};

struct PlacementTemplate {
    Micrometers page_width{0}; // dimensions effectives (orientation appliquée)
    Micrometers page_height{0};
    Micrometers design_width{0}; // boîte englobante des points cousus
    Micrometers design_height{0};
    std::size_t columns{0};
    std::size_t rows{0};
    std::vector<TemplatePage> pages; // ordre : ligne par ligne, de gauche à droite
};

// `sequence` DOIT être `effective_sequence(project)` (ou un DST relu). Le centre de la
// boîte englobante des points cousus est le centre du gabarit. Pure et déterministe.
[[nodiscard]] PlacementTemplate
make_placement_template(const stitch::StitchSequence& sequence,
                        const PlacementTemplateOptions& options = {});

// PDF 1.4 multi-pages à l'échelle exacte 1:1 (1 pt = 25,4/72 mm), sans date ni identifiant.
[[nodiscard]] std::string placement_template_to_pdf(const PlacementTemplate& tpl);

} // namespace openstitch::stitch_analysis
