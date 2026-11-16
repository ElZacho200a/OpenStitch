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
    return out;
}

} // namespace openstitch::desktop_v2::studio
