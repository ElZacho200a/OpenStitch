// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <optional>

#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/project_ops/factory.hpp"

namespace openstitch::desktop_v2::studio {

// Réglages de segmentation envoyés par les panneaux QML, bornés. Seule source des clés et des
// bornes : l'aperçu (`ImageController`) et « Re-segmenter » (`RegionController`) doivent lire la
// même chose, sinon « Re-segmenter » ne rend plus ce que l'aperçu vient de montrer.
struct SegmentationSettings {
    int max_colors{8};
    int min_region_px{16};
    int smoothing_radius_px{3};
    // Expérimental (2026-11-17) : zones de fondu au lieu de la quantification, chaque zone
    // cousue en fondu de fils mélangés (`project_ops::DigitizeSettings::mode = "blend"`).
    bool blend_zones{false};
    int blend_threads{12};
    // Expérimental (SEG-FINE-01) : les traits fins (cordages, nervures) lus sur l'image
    // survivent au lissage et au nettoyage des petites régions.
    bool fine_structures{false};
};

[[nodiscard]] inline SegmentationSettings segmentationSettingsFrom(const QVariantMap& map,
                                                                   SegmentationSettings defaults) {
    const auto number = [&map](const char* key, double fallback) {
        const auto value = map.value(QLatin1String(key));
        bool ok = false;
        const double v = value.toDouble(&ok);
        return ok && std::isfinite(v) ? v : fallback;
    };
    SegmentationSettings out;
    out.max_colors = std::clamp(static_cast<int>(number("max_colors", defaults.max_colors)), 2, 64);
    out.min_region_px =
        std::clamp(static_cast<int>(number("min_region_px", defaults.min_region_px)), 1, 100000);
    // `smoothing_px` : ancienne clé, encore acceptée.
    out.smoothing_radius_px =
        std::clamp(static_cast<int>(number("smoothing_radius_px",
                                           number("smoothing_px", defaults.smoothing_radius_px))),
                   0, 10);
    out.blend_zones = map.value(QLatin1String("blend_zones"), defaults.blend_zones).toBool();
    out.fine_structures =
        map.value(QLatin1String("fine_structures"), defaults.fine_structures).toBool();
    out.blend_threads =
        std::clamp(static_cast<int>(number("blend_threads", defaults.blend_threads)), 2, 16);
    return out;
}

// Réglage avancé facultatif saisi dans un champ : absent, vide ou invalide (non numérique, négatif,
// au-delà de `max`) = std::nullopt, c'est-à-dire la valeur par défaut du moteur. Une valeur valide
// est transmise telle quelle (virgule décimale acceptée).
[[nodiscard]] inline std::optional<double>
optionalSettingFrom(const QVariantMap& map, const char* key, double max = HUGE_VAL) {
    const auto value = map.value(QLatin1String(key));
    if (!value.isValid() || value.isNull()) {
        return std::nullopt;
    }
    QString text = value.toString().trimmed();
    text.replace(QLatin1Char(','), QLatin1Char('.'));
    if (text.isEmpty()) {
        return std::nullopt;
    }
    bool ok = false;
    const double v = text.toDouble(&ok);
    if (!ok || !std::isfinite(v) || v < 0.0 || v > max) {
        return std::nullopt;
    }
    return v;
}

// Réglages de la génération des objets (« Régénérer les objets », « Re-segmenter », « Mettre à
// jour les formes ») : segmentation (mêmes clés que l'aperçu) et satin. Câblage seul : chaque
// valeur va telle quelle dans `project_ops::DigitizeSettings`, les choix restent au moteur.
//   satin_bands                 bool   bandes étroites cousues en satin
//   satin_compare               bool   meilleur satin mesuré (sans effet sans satin_bands)
//   satin_min_coverage          0..1   absent/vide = défaut du moteur
//   satin_compact_max_mm        mm     idem (avec satin_bands)
//   satin_compare_max_stitch_mm mm     idem (avec satin_compare)
[[nodiscard]] inline project_ops::DigitizeSettings
objectDigitizeSettingsFrom(const QVariantMap& map) {
    project_ops::DigitizeSettings digitize;
    const auto chosen = segmentationSettingsFrom(
        map, {digitize.max_colors, digitize.min_region_px, digitize.smoothing_px});
    digitize.max_colors = chosen.max_colors;
    digitize.min_region_px = chosen.min_region_px;
    digitize.smoothing_px = chosen.smoothing_radius_px;
    digitize.fine_structures = chosen.fine_structures;
    if (chosen.blend_zones) {
        // Expérimental : zones de fondu, chaque zone cousue en fondu de fils mélangés.
        digitize.mode = "blend";
        digitize.blend_threads = chosen.blend_threads;
    }
    digitize.satin_bands = map.value(QLatin1String("satin_bands"), false).toBool();
    // « Meilleur satin » n'existe qu'avec le satin : jamais transmis seul.
    digitize.satin_compare =
        digitize.satin_bands && map.value(QLatin1String("satin_compare"), false).toBool();
    if (digitize.satin_bands) {
        digitize.satin_min_coverage = optionalSettingFrom(map, "satin_min_coverage", 1.0);
        digitize.satin_compact_max_mm = optionalSettingFrom(map, "satin_compact_max_mm");
    }
    if (digitize.satin_compare) {
        digitize.satin_compare_max_stitch_mm =
            optionalSettingFrom(map, "satin_compare_max_stitch_mm");
    }
    return digitize;
}

// Même satin pour « Convertir en broderie » (espace Vecteurs), qui appelle directement
// `autodigitize::convert_vectors_to_embroidery` : seules les valeurs choisies remplacent celles
// du moteur (mm -> µm à la frontière, comme `project_ops::make_auto_options`).
inline void applySatinOptions(const QVariantMap& map, autodigitize::AutoOptions& options) {
    const auto satin = objectDigitizeSettingsFrom(map);
    options.satin_bands = satin.satin_bands;
    options.satin_compare = satin.satin_compare;
    if (satin.satin_min_coverage) {
        options.satin_min_coverage = *satin.satin_min_coverage;
    }
    if (satin.satin_compact_max_mm) {
        options.satin_compact_max_width = to_micrometers(Millimeters{*satin.satin_compact_max_mm});
    }
    if (satin.satin_compare_max_stitch_mm) {
        options.satin_compare_max_stitch =
            to_micrometers(Millimeters{*satin.satin_compare_max_stitch_mm});
    }
}

} // namespace openstitch::desktop_v2::studio
