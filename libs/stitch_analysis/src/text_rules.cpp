// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/text_rules.hpp"

#include <variant>

namespace openstitch::stitch_analysis {

std::vector<Finding> analyze_text_objects(const document::Project& project) {
    std::vector<Finding> findings;
    for (const document::TextObject& text : project.text_objects) {
        // Première lettre cousue et lettres cousues en contour faute de trait assez large.
        ObjectId first{};
        int thin_contours = 0;
        for (const document::EmbroideryObject& emb : project.embroidery_objects) {
            const document::VectorObject* vector = project.findObject(emb.source_vector);
            if (vector == nullptr || !vector->text_owner || *vector->text_owner != text.id) {
                continue;
            }
            if (first.value == 0) {
                first = emb.id;
            }
            if (text.fill != document::TextFill::Contour &&
                std::holds_alternative<document::RunningStitchParams>(emb.params)) {
                ++thin_contours;
            }
        }

        const std::string mm = format_mm_fr(static_cast<double>(text.cap_height.value));
        if (text.cap_height < document::kTextMinCapHeight) {
            findings.push_back({Severity::Warning, "texte-trop-petit",
                                "Texte de " + mm + " mm : illisible une fois brodé.", text.origin,
                                first, "Agrandissez le texte (5 mm minimum conseillé)."});
        } else if (text.cap_height < document::kTextSatinMinCapHeight &&
                   text.fill != document::TextFill::Contour) {
            findings.push_back({Severity::Warning, "texte-trop-petit",
                                "Texte de " + mm +
                                    " mm : en dessous de 5 mm le satin est fragile.",
                                text.origin, first,
                                "Agrandissez le texte ou choisissez une police à traits épais."});
        }
        if (thin_contours > 0) {
            findings.push_back({Severity::Warning, "trait-trop-fin",
                                std::to_string(thin_contours) +
                                    " lettre(s) au trait de moins de 1 mm : cousues en contour "
                                    "au lieu de satin.",
                                text.origin, first,
                                "Agrandissez le texte ou choisissez une police plus grasse."});
        }
    }
    return findings;
}

} // namespace openstitch::stitch_analysis
