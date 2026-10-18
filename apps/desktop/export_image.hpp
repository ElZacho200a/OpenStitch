// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QImage>
#include <QString>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_render/presentation.hpp"

namespace openstitch::desktop {

// HP-FMT-011 : export du motif en image (PNG / JPEG). Tout le calcul (brins, fils texturés, tissu)
// vient de `stitch_render::render_sequence` (Qt-free) ; ce fichier ne fait que fournir la couleur
// de chaque objet et convertir le résultat en `QImage` pour l'écriture par Qt.
enum class ImageFileFormat : int { Png = 0, Jpeg = 1 };

struct ExportImageSettings {
    stitch_render::PresentationOptions presentation{};
    ImageFileFormat format{ImageFileFormat::Png};
    int jpegQuality{92}; // 1..100, JPEG seulement

    bool operator==(const ExportImageSettings&) const = default;
};

// Extension (sans point, minuscule) du format : « png » ou « jpg ».
[[nodiscard]] QString imageFileExtension(ImageFileFormat format);

// Le rendu risque-t-il de dépasser ~100 ms (donc à faire hors du fil d'interface) ? Estimation
// d'après le nombre de points et la taille de l'image demandée -- le résultat ne change pas, seul
// le mode d'exécution (fenêtre d'attente) en dépend.
[[nodiscard]] bool exportImageIsHeavy(const stitch::StitchStats& stats,
                                      const ExportImageSettings& settings);

// Image du motif. `sequence` DOIT être `stitch_generation::effective_sequence(project)` (jamais la
// séquence brute). Couleur de chaque brin : celle de l'objet source ; pour un design importé
// (aucun objet brodé), la couleur de son bloc de couleur. Image nulle si rien n'est cousu.
// Un JPEG n'a pas de transparence : le fond de tissu y est toujours opaque.
[[nodiscard]] QImage render_export_image(const document::Project& project,
                                         const stitch::StitchSequence& sequence,
                                         const ExportImageSettings& settings);

// Écrit l'image dans `path` (écriture atomique : l'ancien fichier reste intact en cas d'échec).
// Renvoie faux et renseigne `error` (si non nul) sinon.
bool write_export_image(const QImage& image, const QString& path,
                        const ExportImageSettings& settings, QString* error = nullptr);

} // namespace openstitch::desktop
