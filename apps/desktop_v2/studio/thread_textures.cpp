// SPDX-License-Identifier: Apache-2.0
#include "thread_textures.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace openstitch::desktop_v2::studio {
namespace {
// Mêmes constantes que libs/stitch_render/src/raster.cpp (cohérence avec la référence CPU).
constexpr float kLightX = -0.64F;
constexpr float kLightY = -0.77F;
constexpr float kLightPlanar = 0.72F;
constexpr float kLightZ = 0.70F;

std::uint32_t colorKey(std::array<std::uint8_t, 3> c) {
    return (static_cast<std::uint32_t>(c[0]) << 16) | (static_cast<std::uint32_t>(c[1]) << 8) |
           static_cast<std::uint32_t>(c[2]);
}

std::uint8_t byte(float v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0F, 255.0F) + 0.5F);
}

float pow28(float x) { // x^28 par élévations au carré
    const float x2 = x * x;
    const float x4 = x2 * x2;
    const float x8 = x4 * x4;
    const float x16 = x8 * x8;
    return x16 * x8 * x4;
}

void put(PixelImage& img, int x, int y, float r, float g, float b, float a) {
    auto* p = img.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                                 static_cast<std::size_t>(x)) *
                                    4;
    p[0] = byte(r * a);
    p[1] = byte(g * a);
    p[2] = byte(b * a);
    p[3] = byte(255.0F * a);
}

// Alpha de silhouette : bord doux en travers (u dans [-1, 1]), bouts effacés le long (t [0, 1]).
float silhouetteAlpha(float u, float t) {
    const float au = std::clamp((1.0F - std::abs(u)) / 0.18F, 0.0F, 1.0F);
    const float edge = std::min(t, 1.0F - t);
    const float at = std::clamp(edge / 0.14F, 0.0F, 1.0F);
    return au * (0.35F + 0.65F * at);
}
} // namespace

PixelImage generateThreadSprite(std::array<std::uint8_t, 3> rgb, const RealisticStyle& style) {
    PixelImage img;
    img.width = kSpriteCellWidth * kOrientationBins;
    img.height = kSpriteHeight;
    img.rgba.assign(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4,
                    0);
    const auto relief = static_cast<float>(style.relief);
    const auto sheen = static_cast<float>(style.sheen);
    const auto twist = static_cast<float>(style.twist);
    for (int bin = 0; bin < kOrientationBins; ++bin) {
        const double angle =
            (static_cast<double>(bin) + 0.5) / kOrientationBins * 2.0 * std::numbers::pi;
        const float dx = static_cast<float>(std::cos(angle));
        const float dy = static_cast<float>(std::sin(angle));
        const float perpX = -dy; // convention du moteur CPU
        const float perpY = dx;
        const float lp = (kLightX * perpX + kLightY * perpY) * kLightPlanar;
        const float lz = kLightZ;
        const float lnorm = std::sqrt(lp * lp + lz * lz);
        float hx = lp / lnorm;
        float hz = lz / lnorm + 1.0F;
        const float hn = std::sqrt(hx * hx + hz * hz);
        hx /= hn;
        hz /= hn;
        for (int cx = 0; cx < kSpriteCellWidth; ++cx) {
            const float u = -1.0F + 2.0F * (static_cast<float>(cx) + 0.5F) / kSpriteCellWidth;
            const float nz = std::sqrt(std::max(0.0F, 1.0F - u * u));
            const float diffuse = std::max(0.0F, u * lp + nz * lz) / lnorm;
            const float ratio = diffuse / (lz / lnorm);
            const float raw = 0.25F + 0.75F * ratio;
            const float rim = 0.55F + 0.45F * std::sqrt(nz);
            const float lit = (1.0F - relief) + relief * raw * rim;
            const float spec = pow28(std::max(0.0F, u * hx + nz * hz)) * sheen * 0.75F * 255.0F;
            for (int cy = 0; cy < kSpriteHeight; ++cy) {
                const float t = (static_cast<float>(cy) + 0.5F) / kSpriteHeight;
                // Torsion : léger éclaircissement périodique en diagonale (étirée avec le segment).
                const float ply =
                    1.0F + 0.30F * twist *
                               std::sin(2.0F * std::numbers::pi_v<float> * (2.0F * t + 0.5F * u));
                const float k = lit * ply;
                const float r = std::min(255.0F, static_cast<float>(rgb[0]) * k + spec);
                const float g = std::min(255.0F, static_cast<float>(rgb[1]) * k + spec);
                const float b = std::min(255.0F, static_cast<float>(rgb[2]) * k + spec);
                put(img, bin * kSpriteCellWidth + cx, cy, r, g, b, silhouetteAlpha(u, t));
            }
        }
    }
    return img;
}

PixelImage generateShadowSprite(const RealisticStyle& style) {
    PixelImage img;
    img.width = kSpriteCellWidth;
    img.height = kSpriteHeight;
    img.rgba.assign(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4,
                    0);
    const float strength = 0.65F * static_cast<float>(std::clamp(style.shadow, 0.0, 1.0));
    for (int cx = 0; cx < img.width; ++cx) {
        const float u = -1.0F + 2.0F * (static_cast<float>(cx) + 0.5F) / img.width;
        for (int cy = 0; cy < img.height; ++cy) {
            const float t = (static_cast<float>(cy) + 0.5F) / img.height;
            // Ombre plus douce que la silhouette : bords très estompés.
            const float soft = std::clamp(1.0F - u * u, 0.0F, 1.0F);
            put(img, cx, cy, 0.0F, 0.0F, 0.0F, strength * soft * silhouetteAlpha(0.0F, t));
        }
    }
    return img;
}

PixelImage generateFabricTile(const RealisticStyle& style) {
    PixelImage img;
    img.width = kFabricTilePixels;
    img.height = kFabricTilePixels;
    img.rgba.assign(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4,
                    255);
    const float relief = style.fabricWeave ? static_cast<float>(style.fabricRelief) : 0.0F;
    constexpr int cell = kFabricTilePixels / 8; // 8 x 8 fils par tuile
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const int cx = x / cell;
            const int cy = y / cell;
            const bool warpOver = ((cx + cy) & 1) == 0;
            // Bosse du fil : sinus dans le sens du fil dessus, l'autre sens dessous.
            const float fx = (static_cast<float>(x % cell) + 0.5F) / cell;
            const float fy = (static_cast<float>(y % cell) + 0.5F) / cell;
            const float bump = warpOver ? std::sin(std::numbers::pi_v<float> * fx)
                                        : std::sin(std::numbers::pi_v<float> * fy);
            const float k = 1.0F + relief * (0.28F * (bump - 0.6F) + (warpOver ? 0.05F : -0.05F));
            put(img, x, y, static_cast<float>(style.fabricRgb[0]) * k,
                static_cast<float>(style.fabricRgb[1]) * k,
                static_cast<float>(style.fabricRgb[2]) * k, 1.0F);
        }
    }
    return img;
}

bool ThreadSpriteCache::setStyle(const RealisticStyle& style) {
    const bool same =
        style.relief == style_.relief && style.sheen == style_.sheen && style.twist == style_.twist;
    style_ = style;
    if (same) {
        return false;
    }
    sprites_.clear();
    return true;
}

const PixelImage& ThreadSpriteCache::sprite(std::array<std::uint8_t, 3> rgb) {
    const auto key = colorKey(rgb);
    auto it = sprites_.find(key);
    if (it == sprites_.end()) {
        it = sprites_.emplace(key, generateThreadSprite(rgb, style_)).first;
        ++generated_;
    }
    return it->second;
}

} // namespace openstitch::desktop_v2::studio
