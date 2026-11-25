// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QVariant>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

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

} // namespace openstitch::desktop_v2::studio
