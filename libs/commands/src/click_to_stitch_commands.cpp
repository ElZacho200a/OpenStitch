// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/click_to_stitch_commands.hpp"

#include <utility>
#include <variant>

#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/commands/project_commands.hpp"

namespace openstitch::commands {

namespace {

constexpr std::size_t kMinRegionPx = 4; // en dessous : un point de bruit, pas une zone

const char* label_of(ClickToStitchKind kind) {
    switch (kind) {
    case ClickToStitchKind::Auto:
        return "Clic-pour-broder";
    case ClickToStitchKind::Fill:
        return "Clic-pour-broder (remplissage)";
    case ClickToStitchKind::Satin:
        return "Clic-pour-broder (satin)";
    case ClickToStitchKind::Contour:
        return "Clic-pour-broder (contour)";
    }
    return "Clic-pour-broder";
}

ClickToStitchResult failure(std::string message) {
    ClickToStitchResult out;
    out.error = std::move(message);
    return out;
}

} // namespace

ClickToStitchResult make_click_to_stitch_command(IdGenerator<ObjectId>& ids,
                                                 const segmentation::GrownRegion& region,
                                                 const ClickToStitchOptions& options) {
    if (region.pixel_count < kMinRegionPx) {
        return failure("Zone trop petite pour être brodée.");
    }
    const segmentation::Segmentation seg = segmentation::segmentation_from_grown(region);
    if (seg.region_count() == 0) {
        return failure("Zone vide.");
    }

    autodigitize::AutoOptions auto_options;
    auto_options.mm_per_px = options.mm_per_px;
    auto_options.simplify_tolerance = options.simplify_tolerance;
    auto_options.skip_largest_region = false;
    auto_options.min_region_area_mm2 = 0.0; // une seule région : rien à fusionner
    switch (options.kind) {
    case ClickToStitchKind::Auto:
        auto_options.satin_bands = true; // une bande étroite devient un satin, le reste un tatami
        break;
    case ClickToStitchKind::Fill:
        auto_options.min_fill_area_mm2 = 0.0; // jamais de repli en contour : toujours un tatami
        break;
    case ClickToStitchKind::Satin:
    case ClickToStitchKind::Contour:
        break; // les paramètres sont remplacés plus bas
    }

    // Les identifiants viennent du générateur du document (jamais réutilisés) ; l'annulation retire
    // exactement ces objets, le rétablissement les remet sous les mêmes identifiants.
    auto digitized = autodigitize::auto_digitize(seg, ids, auto_options);
    if (!digitized) {
        return failure(digitized.error().message);
    }
    if (digitized->vectors.empty() || digitized->embroideries.empty()) {
        return failure("Cette zone ne donne aucun objet à broder (trop petite ou trop fine).");
    }

    for (auto& vec : digitized->vectors) {
        vec.name = "Zone cliquée";
        vec.rgb = region.mean_rgb;
        vec.source_region = std::nullopt; // la région est jetable : elle ne vit pas dans le projet
    }
    for (auto& emb : digitized->embroideries) {
        emb.rgb = region.mean_rgb;
        switch (options.kind) {
        case ClickToStitchKind::Auto:
            break;
        case ClickToStitchKind::Fill:
            emb.intent = document::EmbroideryIntent::ForcedUserChoice;
            break;
        case ClickToStitchKind::Satin:
            emb.params = document::AutoSatinParams{};
            emb.name = "Satin zone cliquée";
            emb.intent = document::EmbroideryIntent::ForcedUserChoice;
            break;
        case ClickToStitchKind::Contour: {
            document::RunningStitchParams rp;
            rp.repeats = 1;
            emb.params = rp;
            emb.name = "Contour zone cliquée";
            emb.intent = document::EmbroideryIntent::ForcedUserChoice;
            break;
        }
        }
    }

    ClickToStitchResult out;
    out.vector_id = digitized->vectors.front().id;
    out.embroidery_id = digitized->embroideries.front().id;
    out.command = std::make_unique<AddObjectBatchCommand>(
        std::move(digitized->vectors), std::move(digitized->embroideries), label_of(options.kind));
    return out;
}

} // namespace openstitch::commands
