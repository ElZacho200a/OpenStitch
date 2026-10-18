// SPDX-License-Identifier: Apache-2.0
#include "export_image.hpp"

#include <QImage>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <unordered_map>

namespace openstitch::desktop {

namespace {

// Au-delà, le rendu dépasse en pratique la centaine de millisecondes (mesure indicative : le
// rendu réaliste traite de l'ordre de quelques millions de pixels par seconde et par cœur).
constexpr double kHeavyPixels = 400'000.0;
constexpr std::size_t kHeavyStitches = 20'000;

} // namespace

QString imageFileExtension(ImageFileFormat format) {
    return format == ImageFileFormat::Jpeg ? QStringLiteral("jpg") : QStringLiteral("png");
}

bool exportImageIsHeavy(const stitch::StitchStats& stats, const ExportImageSettings& settings) {
    if (stats.stitches >= kHeavyStitches) {
        return true;
    }
    const double margin = std::max(0.0, settings.presentation.margin_mm);
    const double ppm = std::max(0.5, settings.presentation.px_per_mm);
    const double widthMm =
        (stats.bounds.max.x.value - stats.bounds.min.x.value) / 1000.0 + 2 * margin;
    const double heightMm =
        (stats.bounds.max.y.value - stats.bounds.min.y.value) / 1000.0 + 2 * margin;
    return widthMm * ppm * heightMm * ppm >= kHeavyPixels;
}

QImage render_export_image(const document::Project& project, const stitch::StitchSequence& sequence,
                           const ExportImageSettings& settings) {
    stitch_render::PresentationOptions options = settings.presentation;
    if (settings.format == ImageFileFormat::Jpeg) {
        options.params.fabric_opaque = true; // pas de transparence en JPEG
    }

    stitch_render::RasterImage raster;
    if (project.embroidery_objects.empty() && project.imported_design) {
        // Design importé : une couleur par bloc de couleur (palette de repli si le fichier n'en
        // porte pas), chaque brin étant rattaché à son bloc.
        const auto& blocks = project.imported_design->color_blocks;
        const auto blockSequence = stitch_render::assign_color_blocks(sequence);
        raster = stitch_render::render_sequence(
            blockSequence,
            [&blocks](ObjectId id) -> std::optional<std::array<std::uint8_t, 3>> {
                const std::size_t block =
                    id.value == 0 ? 0 : static_cast<std::size_t>(id.value - 1);
                return block < blocks.size() ? blocks[block].rgb
                                             : stitch_render::block_palette_color(block);
            },
            options);
    } else {
        std::unordered_map<std::uint64_t, std::array<std::uint8_t, 3>> colorOf;
        for (const auto& emb : project.embroidery_objects) {
            colorOf[emb.id.value] = emb.rgb;
        }
        raster = stitch_render::render_sequence(
            sequence,
            [&colorOf](ObjectId id) -> std::optional<std::array<std::uint8_t, 3>> {
                const auto it = colorOf.find(id.value);
                return it != colorOf.end() ? it->second : std::array<std::uint8_t, 3>{0, 0, 0};
            },
            options);
    }
    if (raster.width <= 0 || raster.height <= 0) {
        return {};
    }
    // RGBA « droit » : le PNG transparent garde des couleurs exactes sur les bords du fil.
    const std::vector<std::uint8_t> straight = stitch_render::to_straight_rgba(raster);
    const QImage view(straight.data(), raster.width, raster.height, raster.width * 4,
                      QImage::Format_RGBA8888);
    QImage copy = view.copy(); // détache du tampon local
    if (settings.format == ImageFileFormat::Jpeg) {
        return copy.convertToFormat(QImage::Format_RGB888);
    }
    return copy;
}

bool write_export_image(const QImage& image, const QString& path,
                        const ExportImageSettings& settings, QString* error) {
    const auto fail = [&](const QString& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (image.isNull()) {
        return fail(QObject::tr("Aucune image à écrire."));
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return fail(file.errorString());
    }
    const bool jpeg = settings.format == ImageFileFormat::Jpeg;
    const int quality = jpeg ? std::clamp(settings.jpegQuality, 1, 100) : -1;
    if (!image.save(&file, jpeg ? "JPG" : "PNG", quality)) {
        file.cancelWriting();
        return fail(QObject::tr("Le codage de l'image a échoué."));
    }
    if (!file.commit()) {
        return fail(file.errorString());
    }
    return true;
}

} // namespace openstitch::desktop
