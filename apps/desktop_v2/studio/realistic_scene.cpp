// SPDX-License-Identifier: Apache-2.0
#include "realistic_scene.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace openstitch::desktop_v2::studio {
namespace {
// Texcoords : u dans la cellule d'orientation, v le long du fil (0..1 sur toute la longueur).
void quad(RealisticVertex* out, float ax, float ay, float bx, float by, float halfW, float ext,
          float u0, float u1) {
    float dx = bx - ax;
    float dy = by - ay;
    const float len = std::hypot(dx, dy);
    if (len > 1e-6F) {
        dx /= len;
        dy /= len;
    } else {
        dx = 1.0F;
        dy = 0.0F;
    }
    const float px = -dy * halfW; // perpendiculaire : côté u = u1
    const float py = dx * halfW;
    const float sx = ax - dx * ext;
    const float sy = ay - dy * ext;
    const float ex = bx + dx * ext;
    const float ey = by + dy * ext;
    const RealisticVertex a{sx - px, sy - py, u0, 0.0F}; // départ, côté -perp
    const RealisticVertex b{sx + px, sy + py, u1, 0.0F}; // départ, côté +perp
    const RealisticVertex c{ex - px, ey - py, u0, 1.0F};
    const RealisticVertex d{ex + px, ey + py, u1, 1.0F};
    out[0] = a;
    out[1] = b;
    out[2] = c;
    out[3] = c;
    out[4] = b;
    out[5] = d;
}
} // namespace

RealisticStyle realisticStyleFrom(const RenderSettings& settings) {
    const auto s = clamped(settings);
    RealisticStyle style;
    style.threadWidthMm = s.threadWidthMm;
    style.relief = s.relief;
    style.sheen = s.sheen;
    style.twist = s.twist;
    style.shadow = s.shadow;
    style.fabricRgb = s.fabricRgb;
    style.fabricWeave = s.fabricWeave;
    style.fabricRelief = s.fabricRelief;
    return style;
}

std::vector<RealisticRun> buildRealisticRuns(std::span<const StudioSegment> segments) {
    std::vector<RealisticRun> runs;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& rgb = segments[i].thread.rgb;
        if (runs.empty() || runs.back().rgb != rgb) {
            runs.push_back({rgb, i, 1});
        } else {
            ++runs.back().count;
        }
    }
    return runs;
}

std::vector<RealisticChunk> buildRealisticChunks(std::span<const RealisticRun> runs,
                                                 std::size_t maxSegments) {
    maxSegments = std::max<std::size_t>(1, maxSegments);
    std::vector<RealisticChunk> chunks;
    for (std::size_t r = 0; r < runs.size(); ++r) {
        for (std::size_t done = 0; done < runs[r].count; done += maxSegments) {
            chunks.push_back(
                {r, runs[r].first + done, std::min(maxSegments, runs[r].count - done)});
        }
    }
    return chunks;
}

int orientationBin(float dx, float dy) {
    // L'éclairage dépend de l'angle de la direction (la perpendiculaire (-dy, dx) comme le moteur
    // CPU en est une rotation fixe) : on partitionne [0, 2 pi) en kOrientationBins.
    const double angle = std::atan2(static_cast<double>(dy), static_cast<double>(dx));
    double turns = angle / (2.0 * std::numbers::pi);
    turns -= std::floor(turns); // [0, 1)
    return std::min(kOrientationBins - 1, static_cast<int>(turns * kOrientationBins));
}

std::size_t writeThreadQuads(std::span<const StudioSegment> segments, const RealisticStyle& style,
                             RealisticVertex* out) {
    const float halfW = static_cast<float>(style.threadWidthMm) * 0.5F;
    const float step = 1.0F / static_cast<float>(kOrientationBins);
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& t = segments[i].thread;
        float dx = t.bx - t.ax;
        float dy = t.by - t.ay;
        const float len = std::hypot(dx, dy);
        if (len > 1e-6F) {
            dx /= len;
            dy /= len;
        } else {
            dx = 1.0F;
            dy = 0.0F;
        }
        const float u0 = static_cast<float>(orientationBin(dx, dy)) * step;
        quad(out + i * kVerticesPerSegment, t.ax, t.ay, t.bx, t.by, halfW, halfW, u0, u0 + step);
    }
    return segments.size() * kVerticesPerSegment;
}

ShadowOffset shadowOffset(double threadWidthMm) {
    // Lumière venant du haut-gauche (cf. libs/stitch_render/src/raster.cpp) : l'ombre tombe
    // vers le bas-droite.
    const auto w = static_cast<float>(threadWidthMm);
    return {0.64F * 0.5F * w, 0.77F * 0.5F * w};
}

std::size_t writeShadowQuads(std::span<const StudioSegment> segments, const RealisticStyle& style,
                             RealisticVertex* out) {
    const float halfW = static_cast<float>(style.threadWidthMm) * 0.5F * 1.2F;
    const auto off = shadowOffset(style.threadWidthMm);
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& t = segments[i].thread;
        quad(out + i * kVerticesPerSegment, t.ax + off.dx, t.ay + off.dy, t.bx + off.dx,
             t.by + off.dy, halfW, halfW, 0.0F, 1.0F);
    }
    return segments.size() * kVerticesPerSegment;
}

} // namespace openstitch::desktop_v2::studio
