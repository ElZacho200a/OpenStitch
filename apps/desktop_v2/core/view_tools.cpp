// SPDX-License-Identifier: Apache-2.0
#include "view_tools.hpp"

#include <cmath>
#include <numbers>

#include "openstitch/core/atomic_file.hpp"

namespace openstitch::desktop_v2 {

Measurement measure(PointMm from, PointMm to) {
    Measurement m;
    m.dxMm = to.x - from.x;
    m.dyMm = to.y - from.y;
    m.distanceMm = std::hypot(m.dxMm, m.dyMm);
    m.angleDeg = m.distanceMm > 0.0 ? std::atan2(m.dyMm, m.dxMm) * 180.0 / std::numbers::pi : 0.0;
    return m;
}

GridLines gridLines(const PreviewImage& frame, double spacingMm) {
    GridLines out;
    if (!(spacingMm > 0.0) || !std::isfinite(spacingMm) || frame.px_per_mm <= 0.0 ||
        frame.width <= 0 || frame.height <= 0) {
        return out;
    }
    const double x0 = frame.pxToMmX(0);
    const double x1 = frame.pxToMmX(frame.width);
    const double y0 = frame.pxToMmY(0);
    const double y1 = frame.pxToMmY(frame.height);
    const double cols = (x1 - x0) / spacingMm;
    const double rows = (y1 - y0) / spacingMm;
    if (cols + rows > static_cast<double>(kMaxGridLines)) {
        return out;
    }
    // Multiples de l'écart par rapport à l'origine (0, 0) du modèle, bords exclus.
    for (long k = static_cast<long>(std::ceil(x0 / spacingMm)); k * spacingMm <= x1; ++k) {
        out.verticalPx.push_back(frame.mmToPxX(static_cast<double>(k) * spacingMm));
    }
    for (long k = static_cast<long>(std::ceil(y0 / spacingMm)); k * spacingMm <= y1; ++k) {
        out.horizontalPx.push_back(frame.mmToPxY(static_cast<double>(k) * spacingMm));
    }
    return out;
}

Result<void> export_preview_png(std::shared_ptr<const document::Project> project,
                                const std::filesystem::path& path, double pxPerMm,
                                const TaskContext& task) {
    if (!(pxPerMm >= 1.0 && pxPerMm <= 20.0)) {
        return fail(ErrorCategory::UserInput, "Résolution hors limites",
                    "Choisissez entre 1 et 20 pixels par millimètre.");
    }
    auto preview = render_preview(std::move(project), 0, pxPerMm, task);
    if (!preview) {
        return std::unexpected(preview.error());
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    return write_file_atomic(path, std::span<const std::uint8_t>(preview->png));
}

} // namespace openstitch::desktop_v2
