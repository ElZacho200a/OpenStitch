// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/export_limits.hpp"

#include <fmt/format.h>

#include <cstdlib>

#include "codec_common.hpp"

namespace openstitch::formats {

std::vector<ExportIssue> check_export_limits(const stitch::StitchSequence& sequence,
                                             const FormatInfo& format,
                                             const MachineExportOptions& options) {
    std::vector<ExportIssue> issues;
    const MachineConstraints& c = format.default_constraints;
    const auto prepared = detail::prepare_design(sequence, options);

    // Couleurs.
    const auto colors = static_cast<int>(prepared.segments.size());
    if (c.max_colors && colors > *c.max_colors) {
        issues.push_back({ExportIssueSeverity::Error,
                          fmt::format("{} blocs de couleur : le format {} en accepte au plus {}.",
                                      colors, format.display_name, *c.max_colors)});
    }
    if (!format.carries_colors && colors > 1) {
        issues.push_back(
            {ExportIssueSeverity::Info,
             fmt::format("Le format {} ne conserve pas les couleurs : les {} changements de "
                         "couleur deviennent de simples arrêts machine.",
                         format.display_name, colors - 1)});
    }

    // Étendue et déplacements découpés.
    bool any = false;
    std::int64_t minX = 0;
    std::int64_t maxX = 0;
    std::int64_t minY = 0;
    std::int64_t maxY = 0;
    std::size_t longMoves = 0;
    const std::int64_t maxStepUm = static_cast<std::int64_t>(c.max_record_delta) * c.resolution_um;
    const stitch::StitchCommand* prev = nullptr;
    for (const auto& cmd : prepared.sequence.commands) {
        if (cmd.type == stitch::CommandType::End) {
            continue;
        }
        const std::int64_t x = cmd.pos.x.value;
        const std::int64_t y = cmd.pos.y.value;
        if (!any) {
            minX = maxX = x;
            minY = maxY = y;
            any = true;
        }
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
        if (prev != nullptr) {
            const std::int64_t dx = std::llabs(x - prev->pos.x.value);
            const std::int64_t dy = std::llabs(y - prev->pos.y.value);
            if (dx > maxStepUm || dy > maxStepUm) {
                ++longMoves;
            }
        }
        prev = &cmd;
    }
    if (!any) {
        return issues;
    }
    const std::int64_t w = maxX - minX;
    const std::int64_t h = maxY - minY;
    if (c.max_extent_um && (w > *c.max_extent_um || h > *c.max_extent_um)) {
        issues.push_back(
            {ExportIssueSeverity::Error,
             fmt::format("Motif de {:.1f} x {:.1f} mm : dépasse la plage codable du format {} "
                         "({:.1f} mm par côté).",
                         static_cast<double>(w) / 1000.0, static_cast<double>(h) / 1000.0,
                         format.display_name, static_cast<double>(*c.max_extent_um) / 1000.0)});
    }
    if (longMoves > 0) {
        issues.push_back(
            {ExportIssueSeverity::Info,
             fmt::format("{} déplacement(s) dépassent {:.1f} mm, maximum d'un enregistrement {} : "
                         "ils seront découpés en plusieurs enregistrements.",
                         longMoves, static_cast<double>(maxStepUm) / 1000.0, format.display_name)});
    }

    // Cadres connus de la marque (avertissements, jamais bloquants).
    const double wMm = static_cast<double>(w) / 1000.0;
    const double hMm = static_cast<double>(h) / 1000.0;
    if (format.id == "jef" && (wMm >= 200.0 || hMm >= 200.0)) {
        issues.push_back({ExportIssueSeverity::Warning,
                          fmt::format("Motif de {:.1f} x {:.1f} mm : hors des cadres Janome connus "
                                      "(200 x 200 mm maximum).",
                                      wMm, hMm)});
    }
    if (format.id == "pes") {
        const bool fits = (wMm <= 360.0 && hMm <= 200.0) || (wMm <= 200.0 && hMm <= 360.0);
        if (!fits) {
            issues.push_back(
                {ExportIssueSeverity::Warning,
                 fmt::format("Motif de {:.1f} x {:.1f} mm : au-delà des plus grands cadres "
                             "Brother courants (360 x 200 mm).",
                             wMm, hMm)});
        }
    }
    return issues;
}

} // namespace openstitch::formats
