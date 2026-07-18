// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/svg.hpp"

#include <fmt/format.h>

#include <fstream>

namespace openstitch::formats {

namespace {

double mmX(Vec2um p) {
    return static_cast<double>(p.x.value) / 1000.0;
}

// SVG a l'axe Y vers le bas ; le modèle est en Y vers le haut.
double mmY(Vec2um p) {
    return -static_cast<double>(p.y.value) / 1000.0;
}

}  // namespace

std::string to_diagnostic_svg(const stitch::StitchSequence& sequence) {
    const auto stats = stitch::compute_stats(sequence);
    const double margin = 2.0;
    const double minX = static_cast<double>(stats.bounds.min.x.value) / 1000.0 - margin;
    const double maxX = static_cast<double>(stats.bounds.max.x.value) / 1000.0 + margin;
    const double minY = -static_cast<double>(stats.bounds.max.y.value) / 1000.0 - margin;
    const double maxY = -static_cast<double>(stats.bounds.min.y.value) / 1000.0 + margin;

    std::string svg = fmt::format(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"{:.3f} {:.3f} {:.3f} {:.3f}\" "
        "width=\"{:.3f}mm\" height=\"{:.3f}mm\">\n",
        minX, minY, maxX - minX, maxY - minY, maxX - minX, maxY - minY);
    svg += fmt::format("<!-- points: {} sauts: {} coupes: {} changements: {} -->\n",
                       stats.stitches, stats.jumps, stats.trims, stats.color_changes);

    std::string sew;
    std::string jumps;
    std::string markers;
    bool hasPos = false;
    Vec2um last{};
    for (const auto& cmd : sequence.commands) {
        switch (cmd.type) {
        case stitch::CommandType::Stitch:
            if (hasPos) {
                sew += fmt::format("M{:.3f} {:.3f}L{:.3f} {:.3f}", mmX(last), mmY(last),
                                   mmX(cmd.pos), mmY(cmd.pos));
            }
            last = cmd.pos;
            hasPos = true;
            break;
        case stitch::CommandType::Jump:
            if (hasPos) {
                jumps += fmt::format("M{:.3f} {:.3f}L{:.3f} {:.3f}", mmX(last), mmY(last),
                                     mmX(cmd.pos), mmY(cmd.pos));
            }
            last = cmd.pos;
            hasPos = true;
            break;
        case stitch::CommandType::ColorChange:
            markers += fmt::format(
                "<circle cx=\"{:.3f}\" cy=\"{:.3f}\" r=\"0.8\" fill=\"none\" "
                "stroke=\"red\" stroke-width=\"0.3\"/>\n",
                mmX(cmd.pos), mmY(cmd.pos));
            break;
        case stitch::CommandType::Trim:
            markers += fmt::format(
                "<circle cx=\"{:.3f}\" cy=\"{:.3f}\" r=\"0.5\" fill=\"red\"/>\n", mmX(cmd.pos),
                mmY(cmd.pos));
            break;
        default:
            break;
        }
    }
    svg += fmt::format("<path d=\"{}\" fill=\"none\" stroke=\"black\" stroke-width=\"0.15\"/>\n",
                       sew);
    svg += fmt::format(
        "<path d=\"{}\" fill=\"none\" stroke=\"orange\" stroke-width=\"0.15\" "
        "stroke-dasharray=\"0.8 0.5\"/>\n",
        jumps);
    svg += markers;
    svg += "</svg>\n";
    return svg;
}

Result<void> write_svg_file(const std::filesystem::path& path,
                            const stitch::StitchSequence& sequence) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return fail(ErrorCategory::UserInput,
                    "Impossible d'écrire le fichier : " + path.string());
    }
    const std::string svg = to_diagnostic_svg(sequence);
    file << svg;
    if (!file) {
        return fail(ErrorCategory::Internal, "Échec d'écriture : " + path.string());
    }
    return {};
}

}  // namespace openstitch::formats
