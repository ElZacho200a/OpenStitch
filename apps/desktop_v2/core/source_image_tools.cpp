// SPDX-License-Identifier: Apache-2.0
#include "source_image_tools.hpp"

#include <algorithm>
#include <cmath>

#include "openstitch/commands/click_to_stitch_commands.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/image/ops.hpp"
#include "openstitch/segmentation/region_growing.hpp"

namespace openstitch::desktop_v2 {

namespace {
constexpr std::size_t kHoleFillPx = 12;
ServiceResult refuse(std::string why) {
    return {false, std::move(why)};
}
} // namespace

Result<image::Image> processedImage(const document::Project& project) {
    if (project.original.empty()) {
        return fail(ErrorCategory::OperationImpossible, "Ce projet n'a pas d'image source",
                    "Importez une image (Fichier > Ouvrir) pour utiliser cet outil.");
    }
    return image::apply_pipeline(project.original, project.ops);
}

std::optional<PixelPoint> pixelUnder(const image::Image& img, double mmPerPx, double x, double y) {
    if (img.empty() || !(mmPerPx > 0.0)) {
        return std::nullopt;
    }
    const double wMm = img.width * mmPerPx;
    const double hMm = img.height * mmPerPx;
    const int px = static_cast<int>(std::floor((x + wMm / 2.0) / mmPerPx));
    const int py = static_cast<int>(std::floor((y + hMm / 2.0) / mmPerPx));
    if (px < 0 || py < 0 || px >= img.width || py >= img.height) {
        return std::nullopt;
    }
    return PixelPoint{px, py};
}

std::pair<double, double> pixelCentreMm(const image::Image& img, double mmPerPx, PixelPoint p) {
    return {(p.x + 0.5) * mmPerPx - img.width * mmPerPx / 2.0,
            (p.y + 0.5) * mmPerPx - img.height * mmPerPx / 2.0};
}

ClickResult clickToStitch(DocumentSession& session, const image::Image& processed, double x,
                          double y, int tolerance, ClickKind kind) {
    const double mmPerPx = session.project().mm_per_px.value;
    const auto pixel = pixelUnder(processed, mmPerPx, x, y);
    if (!pixel) {
        return {false, "Cliquez sur l'image (affichez-la avec Affichage > Image source).", 0};
    }
    segmentation::GrowOptions grow;
    grow.tolerance = static_cast<double>(std::clamp(tolerance, 0, 100));
    grow.fill_holes_up_to_px = kHoleFillPx;
    auto region = segmentation::grow_region(processed, pixel->x, pixel->y, grow);
    if (!region) {
        return {false, region.error().message, 0};
    }
    commands::ClickToStitchOptions options;
    options.mm_per_px = session.project().mm_per_px;
    switch (kind) {
    case ClickKind::Fill:
        options.kind = commands::ClickToStitchKind::Fill;
        break;
    case ClickKind::Satin:
        options.kind = commands::ClickToStitchKind::Satin;
        break;
    case ClickKind::Contour:
        options.kind = commands::ClickToStitchKind::Contour;
        break;
    case ClickKind::Auto:
        options.kind = commands::ClickToStitchKind::Auto;
        break;
    }
    auto result = commands::make_click_to_stitch_command(
        session.projectForIdAllocation().object_ids, *region, options);
    if (!result.command) {
        return {false, result.error.empty() ? "Cette zone ne donne aucun objet." : result.error, 0};
    }
    const auto id = result.embroidery_id.value;
    session.execute(std::move(result.command));
    return {true, {}, id};
}

ServiceResult paintDetail(DocumentSession& session, const image::Image& processed,
                          const std::vector<std::pair<double, double>>& centres, double radiusMm,
                          bool erase) {
    const double mmPerPx = session.project().mm_per_px.value;
    if (processed.empty()) {
        return refuse("Ce projet n'a pas d'image source.");
    }
    if (!(radiusMm >= 0.2 && radiusMm <= 50.0)) {
        return refuse("Rayon du pinceau : entre 0,2 et 50 mm.");
    }
    std::vector<std::pair<int, int>> pixels;
    for (const auto& [x, y] : centres) {
        if (const auto p = pixelUnder(processed, mmPerPx, x, y)) {
            pixels.emplace_back(p->x, p->y);
        }
    }
    if (pixels.empty()) {
        return refuse("Le trait est en dehors de l'image.");
    }
    const int radiusPx = std::max(1, static_cast<int>(std::lround(radiusMm / mmPerPx)));
    session.execute(std::make_unique<commands::PaintDetailMaskCommand>(
        processed.width, processed.height, std::move(pixels), radiusPx, erase, 0.5));
    return {true, {}};
}

ServiceResult clearDetail(DocumentSession& session) {
    if (session.project().detail_mask.empty()) {
        return refuse("Aucune zone de détail à effacer.");
    }
    session.execute(std::make_unique<commands::ClearDetailMaskCommand>());
    return {true, {}};
}

int detailWeightAt(const document::Project& project, const image::Image& processed, double x,
                   double y) {
    const auto p = pixelUnder(processed, project.mm_per_px.value, x, y);
    if (!p || !project.detail_mask.matches(processed.width, processed.height)) {
        return 0;
    }
    return project.detail_mask
        .weights[static_cast<std::size_t>(p->y) * static_cast<std::size_t>(processed.width) +
                 static_cast<std::size_t>(p->x)];
}

} // namespace openstitch::desktop_v2
