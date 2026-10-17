// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_render/raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <thread>
#include <utility>

namespace openstitch::stitch_render {

namespace {

// Échantillons de la table d'ombrage sur la section du fil (u de -1 à +1).
constexpr int kLut = 32;
constexpr int kSinLut = 1024;

// Lumière : vient du haut à gauche de l'écran (Y vers le bas), élévation fixe.
constexpr float kLightX = -0.64F;
constexpr float kLightY = -0.77F;
constexpr float kLightPlanar = 0.72F;
constexpr float kLightZ = 0.70F;

std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float hash01(std::int32_t x, std::int32_t y, std::uint32_t seed) {
    const std::uint32_t h = hash32(static_cast<std::uint32_t>(x) * 0x9e3779b1U ^
                                   hash32(static_cast<std::uint32_t>(y) + seed));
    return static_cast<float>(h >> 8) * (1.0F / 16777216.0F);
}

const std::array<float, kSinLut>& sin_table() {
    static const std::array<float, kSinLut> table = [] {
        std::array<float, kSinLut> t{};
        for (int i = 0; i < kSinLut; ++i) {
            t[static_cast<std::size_t>(i)] = static_cast<float>(
                std::sin(2.0 * std::numbers::pi * static_cast<double>(i) / kSinLut));
        }
        return t;
    }();
    return table;
}

// sin(2 pi * turns) par table.
float sin_turns(const std::array<float, kSinLut>& table, float turns) {
    const float frac = turns - std::floor(turns);
    int idx = static_cast<int>(frac * kSinLut);
    idx = std::clamp(idx, 0, kSinLut - 1);
    return table[static_cast<std::size_t>(idx)];
}

float pow28(float x) {
    const float x2 = x * x;
    const float x4 = x2 * x2;
    const float x8 = x4 * x4;
    const float x16 = x8 * x8;
    return x16 * x8 * x4;
}

std::uint8_t to_byte(float v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0F, 255.0F) + 0.5F);
}

struct Prepared {
    float ax{};
    float ay{};
    float dx{1};
    float dy{0};
    float len{};
    float half{};
    float inv_half{};
    float turns_per_px{};
    float twist_amp{};
    float variation{1};
    std::array<float, 3> rgb{};
    std::array<float, kLut + 1> shade{};
    std::array<float, kLut + 1> spec{};
    int bx0{}, by0{}, bx1{}, by1{};
    float shadow_dx{};
    float shadow_dy{};
    float soft{};
    float shadow_alpha{};
    int sx0{}, sy0{}, sx1{}, sy1{};
    // Ombre de contact : liseré serré et sombre au pied du fil, en plus de la
    // pénombre large (`shadow_*`).
    float contact_dx{};
    float contact_dy{};
    float contact_soft{};
    float contact_alpha{};
    // Irrégularités le long du fil (qualité High) : ondulation de brillance,
    // phase de la torsion, trou de pénétration d'aiguille aux extrémités.
    float wob_freq{};
    float wob_phase{};
    float wob_phase2{};
    float wob_amp{};
    float twist_phase{};
    float hole_r{};
    float hole_strength{};
};

// Intersection fenêtre/boîte : coordonnées pixel, bornes hautes exclues.
bool clip_box(float x0, float y0, float x1, float y1, int w, int h, int& ox0, int& oy0, int& ox1,
              int& oy1) {
    ox0 = std::max(0, static_cast<int>(std::floor(x0)));
    oy0 = std::max(0, static_cast<int>(std::floor(y0)));
    ox1 = std::min(w, static_cast<int>(std::ceil(x1)));
    oy1 = std::min(h, static_cast<int>(std::ceil(y1)));
    return ox0 < ox1 && oy0 < oy1;
}

// Colonnes [x0, x1) de la ligne `y` susceptibles d'être à moins de `reach` de la
// capsule d'origine (cx, cy) : rectangle orienté englobant la capsule, avec une
// marge d'un pixel. Conservatif -- les pixels écartés auraient une couverture
// nulle -- donc sans effet sur le résultat, mais un long brin diagonal ne
// parcourt plus toute sa boîte englobante. Faux si la ligne ne la coupe pas.
bool row_span(const Prepared& p, float cx, float cy, float reach, int y, int& x0, int& x1) {
    float lo = -1.0e30F;
    float hi = 1.0e30F;
    const float py = static_cast<float>(y) + 0.5F - cy;
    // t = abscisse du centre du pixel relative à cx ; a * t + c doit rester
    // dans [vlo, vhi].
    const auto slab = [&](float a, float c, float vlo, float vhi) {
        if (std::fabs(a) < 1e-6F) {
            return c >= vlo && c <= vhi;
        }
        float t0 = (vlo - c) / a;
        float t1 = (vhi - c) / a;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        lo = std::max(lo, t0);
        hi = std::min(hi, t1);
        return lo <= hi;
    };
    // d = -dy * t + dx * py ; s = dx * t + dy * py.
    if (!slab(-p.dy, p.dx * py, -reach, reach) || !slab(p.dx, p.dy * py, -reach, p.len + reach)) {
        return false;
    }
    x0 = static_cast<int>(std::floor(lo + cx - 0.5F)) - 1;
    x1 = static_cast<int>(std::ceil(hi + cx - 0.5F)) + 2;
    return true;
}

// Ondulation lisse et déterministe le long du fil, dans [-1, 1].
float wobble(const std::array<float, kSinLut>& table, const Prepared& p, float s) {
    return 0.6F * sin_turns(table, s * p.wob_freq + p.wob_phase) +
           0.4F * sin_turns(table, s * p.wob_freq * 2.7F + p.wob_phase2);
}

// Prépare un brin : géométrie en pixels, tables d'ombrage cylindrique, boîtes
// englobantes (fil et ombre) bornées à la fenêtre. Faux si hors fenêtre.
bool prepare(const ThreadSegment& seg, std::size_t index, const RenderParams& params,
             const RasterView& view, bool high, Prepared& out) {
    const double ppm = view.px_per_mm;
    const float ax = static_cast<float>((static_cast<double>(seg.ax) - view.rect.x0) * ppm);
    const float ay = static_cast<float>((static_cast<double>(seg.ay) - view.rect.y0) * ppm);
    const float bx = static_cast<float>((static_cast<double>(seg.bx) - view.rect.x0) * ppm);
    const float by = static_cast<float>((static_cast<double>(seg.by) - view.rect.y0) * ppm);
    const float half = std::max(0.5F, static_cast<float>(params.thread_width_mm * ppm * 0.5));
    const float vx = bx - ax;
    const float vy = by - ay;
    const float len = std::sqrt(vx * vx + vy * vy);

    const float soft = 0.5F + 0.5F * half;
    const float shadow_dist = high && params.shadow > 0.0
                                  ? half * (0.45F + 0.9F * static_cast<float>(params.shadow))
                                  : 0.0F;
    const float sdx = 0.6F * shadow_dist;
    const float sdy = 0.8F * shadow_dist;

    const float minx = std::min(ax, bx);
    const float maxx = std::max(ax, bx);
    const float miny = std::min(ay, by);
    const float maxy = std::max(ay, by);
    const float reach = half + 1.0F;
    int tx0 = 0;
    int ty0 = 0;
    int tx1 = 0;
    int ty1 = 0;
    const bool thread_in = clip_box(minx - reach, miny - reach, maxx + reach, maxy + reach,
                                    view.width, view.height, tx0, ty0, tx1, ty1);
    int sx0 = 0;
    int sy0 = 0;
    int sx1 = 0;
    int sy1 = 0;
    const bool shadow_in =
        shadow_dist > 0.0F &&
        clip_box(minx - reach - soft, miny - reach - soft, maxx + sdx + reach + soft,
                 maxy + sdy + reach + soft, view.width, view.height, sx0, sy0, sx1, sy1);
    if (!thread_in && !shadow_in) {
        return false;
    }

    out.ax = ax;
    out.ay = ay;
    if (len > 1e-4F) {
        out.dx = vx / len;
        out.dy = vy / len;
    } else {
        out.dx = 1.0F;
        out.dy = 0.0F;
    }
    out.len = len;
    out.half = half;
    out.inv_half = 1.0F / half;
    out.bx0 = thread_in ? tx0 : 0;
    out.by0 = thread_in ? ty0 : 0;
    out.bx1 = thread_in ? tx1 : 0;
    out.by1 = thread_in ? ty1 : 0;
    out.sx0 = shadow_in ? sx0 : 0;
    out.sy0 = shadow_in ? sy0 : 0;
    out.sx1 = shadow_in ? sx1 : 0;
    out.sy1 = shadow_in ? sy1 : 0;
    out.shadow_dx = sdx;
    out.shadow_dy = sdy;
    out.soft = soft;
    // Pénombre large + liseré de contact serré au pied du fil.
    out.shadow_alpha = 0.45F * static_cast<float>(params.shadow);
    const float contact_dist = 0.3F * shadow_dist;
    out.contact_dx = 0.6F * contact_dist;
    out.contact_dy = 0.8F * contact_dist;
    out.contact_soft = 0.35F + 0.15F * half;
    out.contact_alpha = 0.40F * static_cast<float>(params.shadow);

    // Variation d'un brin à l'autre (déterministe, fonction de l'indice) : sans
    // elle, un satin dense paraît artificiellement uniforme.
    const auto idx = static_cast<std::int32_t>(index);
    out.variation = 1.0F + (hash01(idx, 7, 0x51edU) - 0.5F) * 0.10F;
    out.rgb = {static_cast<float>(seg.rgb[0]), static_cast<float>(seg.rgb[1]),
               static_cast<float>(seg.rgb[2])};
    out.wob_amp = 0.0F;
    out.hole_r = 0.0F;
    out.hole_strength = 0.0F;
    out.twist_phase = 0.0F;
    if (high) {
        // Léger décalage chaud/froid d'un brin à l'autre (±1,5 %).
        const float tint = (hash01(idx, 9, 0x7a3U) - 0.5F) * 0.03F;
        out.rgb[0] *= 1.0F + tint;
        out.rgb[2] *= 1.0F - tint;
        // Ondulation de brillance le long du fil (longueur d'onde ~1,3 à 2,1 mm),
        // estompée quand elle approche la taille du pixel.
        const float wavelength_px =
            static_cast<float>(1.3 * ppm) * (1.0F + 0.6F * hash01(idx, 11, 0xa17U));
        out.wob_freq = 1.0F / std::max(wavelength_px, 1.0F);
        out.wob_phase = hash01(idx, 13, 0x2bU);
        out.wob_phase2 = hash01(idx, 17, 0x9dU);
        out.wob_amp = 0.06F * std::clamp((wavelength_px - 6.0F) / 6.0F, 0.0F, 1.0F);
        // Trou de pénétration d'aiguille : visible seulement si le fil couvre
        // quelques pixels.
        if (half >= 2.0F) {
            out.hole_r = 0.75F * half;
            out.hole_strength = 0.5F * (0.4F + 0.6F * static_cast<float>(params.relief));
        }
    }

    // Torsion : stries obliques de pas ~1,3 diamètre (±15 % d'un brin à l'autre,
    // phase propre à chaque brin) ; estompées quand le pas approche la taille du
    // pixel (repliement).
    out.turns_per_px = 0.0F;
    out.twist_amp = 0.0F;
    if (high && params.twist > 0.0) {
        const double pitch_scale = 1.3 * (0.85 + 0.3 * static_cast<double>(hash01(idx, 19, 0x35U)));
        const float pitch_px = static_cast<float>(params.thread_width_mm * pitch_scale * ppm);
        if (pitch_px >= 3.0F) {
            out.turns_per_px = 1.0F / pitch_px;
            out.twist_amp = static_cast<float>(params.twist) * 0.30F *
                            std::clamp((pitch_px - 3.0F) / 4.0F, 0.0F, 1.0F);
            out.twist_phase = hash01(idx, 23, 0xc1U);
        }
    }

    // Section cylindrique : normale (u, sqrt(1-u^2)) dans le plan
    // (perpendiculaire au fil, vers l'observateur), lumière projetée sur la
    // perpendiculaire -> le fil s'éclaire différemment selon son orientation.
    const float perp_x = -out.dy;
    const float perp_y = out.dx;
    const float lp = (kLightX * perp_x + kLightY * perp_y) * kLightPlanar;
    const float lz = kLightZ;
    const float lnorm = std::sqrt(lp * lp + lz * lz);
    const float relief = static_cast<float>(params.relief);
    const float sheen = static_cast<float>(params.sheen);
    // Vecteur demi-angle (observateur le long de +z).
    float hx = lp / lnorm;
    float hz = lz / lnorm + 1.0F;
    const float hn = std::sqrt(hx * hx + hz * hz);
    hx /= hn;
    hz /= hn;
    for (int k = 0; k <= kLut; ++k) {
        const float u = -1.0F + 2.0F * static_cast<float>(k) / static_cast<float>(kLut);
        const float nz = std::sqrt(std::max(0.0F, 1.0F - u * u));
        const float diffuse = std::max(0.0F, u * lp + nz * lz) / lnorm;
        const float ratio = diffuse / (lz / lnorm);
        const float raw = 0.25F + 0.75F * ratio;
        // Assombrissement des bords (le fil plonge dans l'ombre sur ses flancs).
        const float rim = 0.55F + 0.45F * std::sqrt(nz);
        const float lit = (1.0F - relief) + relief * raw * rim;
        out.shade[static_cast<std::size_t>(k)] = lit;
        out.spec[static_cast<std::size_t>(k)] =
            pow28(std::max(0.0F, u * hx + nz * hz)) * sheen * 0.75F;
    }
    return true;
}

// Tissu : couleur de fond et texture procédurale, ancrée au repère du monde
// (la texture ne « glisse » donc pas quand la fenêtre de rendu change).
void fill_fabric_row(const RenderParams& params, const RasterView& view, bool high, int y,
                     std::uint8_t* row) {
    const auto& table = sin_table();
    const float base_r = static_cast<float>(params.fabric_rgb[0]);
    const float base_g = static_cast<float>(params.fabric_rgb[1]);
    const float base_b = static_cast<float>(params.fabric_rgb[2]);
    const float rel = static_cast<float>(params.fabric_relief);
    const double ppm = view.px_per_mm;
    const double wy = view.rect.y0 + (static_cast<double>(y) + 0.5) / ppm;

    constexpr double kWeavePitch = 0.42;
    constexpr double kFeltFine = 0.12;
    constexpr double kFeltCoarse = 0.55;
    const float weave_fade =
        std::clamp(static_cast<float>(kWeavePitch * ppm - 2.0) / 4.0F, 0.0F, 1.0F);
    const float felt_fade = std::clamp(static_cast<float>(kFeltFine * ppm - 0.8), 0.0F, 1.0F);
    const bool textured = high && params.fabric_texture != FabricTexture::Plain && rel > 0.0F;

    for (int x = 0; x < view.width; ++x) {
        float f = 1.0F;
        if (textured) {
            const double wx = view.rect.x0 + (static_cast<double>(x) + 0.5) / ppm;
            if (params.fabric_texture == FabricTexture::Weave) {
                const double fx = wx / kWeavePitch;
                const double fy = wy / kWeavePitch;
                const auto ix = static_cast<std::int32_t>(std::floor(fx));
                const auto iy = static_cast<std::int32_t>(std::floor(fy));
                const float across = ((ix + iy) & 1) != 0 ? static_cast<float>(fx - ix)
                                                          : static_cast<float>(fy - iy);
                // Bosse du fil de tissage : sin(pi * t).
                const float bump = sin_turns(table, across * 0.5F);
                const float height = weave_fade * bump + (1.0F - weave_fade) * 0.637F;
                f = 1.0F - rel * 0.24F * (1.0F - height);
                f += (hash01(ix, iy, 0x77U) - 0.5F) * 0.06F * rel * weave_fade;
            } else {
                const auto fx = static_cast<std::int32_t>(std::floor(wx / kFeltFine));
                const auto fy = static_cast<std::int32_t>(std::floor(wy / kFeltFine));
                const auto cx = static_cast<std::int32_t>(std::floor(wx / kFeltCoarse));
                const auto cy = static_cast<std::int32_t>(std::floor(wy / kFeltCoarse));
                f = 1.0F + rel * ((hash01(fx, fy, 0x11U) - 0.5F) * 0.22F * felt_fade +
                                  (hash01(cx, cy, 0x22U) - 0.5F) * 0.12F);
            }
        }
        std::uint8_t* px = row + static_cast<std::size_t>(x) * 4;
        px[0] = to_byte(base_r * f);
        px[1] = to_byte(base_g * f);
        px[2] = to_byte(base_b * f);
        px[3] = 255;
    }
}

void draw_shadow(const Prepared& p, const RasterView& view, int row0, int row1,
                 std::vector<std::uint8_t>& rgba) {
    const int y_begin = std::max(p.sy0, row0);
    const int y_end = std::min(p.sy1, row1);
    if (y_begin >= y_end) {
        return;
    }
    // Pénombre : capsule décalée de `shadow_d*`, rampe large.
    const float cx = p.ax + p.shadow_dx;
    const float cy = p.ay + p.shadow_dy;
    const float reach = p.half + p.soft;
    const float inv_ramp = 1.0F / (1.2F * p.soft);
    // Contact : capsule peu décalée, rampe serrée.
    const float ccx = p.ax + p.contact_dx;
    const float ccy = p.ay + p.contact_dy;
    const float creach = p.half + p.contact_soft;
    const float cinv_ramp = 1.0F / p.contact_soft;
    for (int y = y_begin; y < y_end; ++y) {
        const float py = static_cast<float>(y) + 0.5F - cy;
        const float cpy = static_cast<float>(y) + 0.5F - ccy;
        int a0 = 0;
        int a1 = 0;
        int b0 = 0;
        int b1 = 0;
        const bool pen_row = row_span(p, cx, cy, reach, y, a0, a1);
        const bool con_row = row_span(p, ccx, ccy, creach, y, b0, b1);
        if (!pen_row && !con_row) {
            continue;
        }
        int x_begin = p.sx1;
        int x_end = p.sx0;
        if (pen_row) {
            x_begin = std::min(x_begin, a0);
            x_end = std::max(x_end, a1);
        }
        if (con_row) {
            x_begin = std::min(x_begin, b0);
            x_end = std::max(x_end, b1);
        }
        x_begin = std::max(x_begin, p.sx0);
        x_end = std::min(x_end, p.sx1);
        std::uint8_t* row =
            rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(view.width) * 4;
        for (int x = x_begin; x < x_end; ++x) {
            const float px = static_cast<float>(x) + 0.5F - cx;
            const float s = px * p.dx + py * p.dy;
            const float d = -px * p.dy + py * p.dx;
            const float ds = s < 0.0F ? s : (s > p.len ? s - p.len : 0.0F);
            const float dist = ds == 0.0F ? std::fabs(d) : std::sqrt(ds * ds + d * d);
            const float cov = std::clamp((reach - dist) * inv_ramp, 0.0F, 1.0F);
            float alpha = p.shadow_alpha * cov;
            const float cpx = static_cast<float>(x) + 0.5F - ccx;
            const float cs = cpx * p.dx + cpy * p.dy;
            const float cd = -cpx * p.dy + cpy * p.dx;
            const float cds = cs < 0.0F ? cs : (cs > p.len ? cs - p.len : 0.0F);
            const float cdist = cds == 0.0F ? std::fabs(cd) : std::sqrt(cds * cds + cd * cd);
            const float ccov = std::clamp((creach - cdist) * cinv_ramp, 0.0F, 1.0F);
            // Les deux ombres se composent comme deux couches translucides.
            alpha = 1.0F - (1.0F - alpha) * (1.0F - p.contact_alpha * ccov);
            const int a = static_cast<int>(alpha * 255.0F + 0.5F);
            if (a <= 0) {
                continue;
            }
            std::uint8_t* px4 = row + static_cast<std::size_t>(x) * 4;
            const int keep = 255 - a;
            px4[0] = static_cast<std::uint8_t>((px4[0] * keep + 127) / 255);
            px4[1] = static_cast<std::uint8_t>((px4[1] * keep + 127) / 255);
            px4[2] = static_cast<std::uint8_t>((px4[2] * keep + 127) / 255);
            px4[3] = static_cast<std::uint8_t>(a + (px4[3] * keep + 127) / 255);
        }
    }
}

void draw_thread(const Prepared& p, const RasterView& view, bool high, int row0, int row1,
                 std::vector<std::uint8_t>& rgba) {
    const int y_begin = std::max(p.by0, row0);
    const int y_end = std::min(p.by1, row1);
    if (y_begin >= y_end) {
        return;
    }
    const auto& table = sin_table();
    const float hole_r2 = p.hole_r * p.hole_r;
    for (int y = y_begin; y < y_end; ++y) {
        const float py = static_cast<float>(y) + 0.5F - p.ay;
        int x_begin = 0;
        int x_end = 0;
        if (!row_span(p, p.ax, p.ay, p.half + 1.0F, y, x_begin, x_end)) {
            continue;
        }
        x_begin = std::max(x_begin, p.bx0);
        x_end = std::min(x_end, p.bx1);
        std::uint8_t* row =
            rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(view.width) * 4;
        for (int x = x_begin; x < x_end; ++x) {
            const float px = static_cast<float>(x) + 0.5F - p.ax;
            const float s = px * p.dx + py * p.dy;
            const float d = -px * p.dy + py * p.dx;
            const float ds = s < 0.0F ? s : (s > p.len ? s - p.len : 0.0F);
            const float dist = ds == 0.0F ? std::fabs(d) : std::sqrt(ds * ds + d * d);
            const float cov = std::min(p.half + 0.5F - dist, 1.0F);
            if (cov <= 0.0F) {
                continue;
            }
            const float u = std::clamp(d * p.inv_half, -1.0F, 1.0F);
            const float fi = (u + 1.0F) * 0.5F * static_cast<float>(kLut);
            const int i0 = std::min(static_cast<int>(fi), kLut - 1);
            const float fr = fi - static_cast<float>(i0);
            const auto k0 = static_cast<std::size_t>(i0);
            const float shade = p.shade[k0] + (p.shade[k0 + 1] - p.shade[k0]) * fr;
            float spec = p.spec[k0] + (p.spec[k0 + 1] - p.spec[k0]) * fr;
            float lit = shade * p.variation;
            if (high) {
                // Plongée des extrémités dans le tissu.
                const float outside = std::min(std::fabs(ds) * p.inv_half, 1.0F);
                lit *= 1.0F - 0.38F * outside * outside;
                // Ondulation de brillance le long du fil : casse l'uniformité
                // des satins denses.
                const float w = p.wob_amp > 0.0F ? wobble(table, p, s) : 0.0F;
                lit *= 1.0F + p.wob_amp * w;
                spec *= 1.0F + 6.0F * p.wob_amp * w;
                if (p.twist_amp > 0.0F) {
                    const float m =
                        sin_turns(table, s * p.turns_per_px + u * 0.35F + p.twist_phase);
                    const float amp = p.twist_amp * (1.0F + 0.5F * w);
                    lit *= 1.0F + amp * m;
                    spec *= 1.0F + 1.6F * amp * m;
                }
                // Trou de pénétration d'aiguille aux deux extrémités du brin.
                if (p.hole_r > 0.0F) {
                    const float sb = s - p.len;
                    float near2 = hole_r2;
                    if (s < p.hole_r) {
                        near2 = std::min(near2, s * s + d * d);
                    }
                    if (sb > -p.hole_r) {
                        near2 = std::min(near2, sb * sb + d * d);
                    }
                    if (near2 < hole_r2) {
                        const float t = 1.0F - near2 / hole_r2;
                        lit *= 1.0F - p.hole_strength * t * t;
                        spec *= 1.0F - t;
                    }
                }
            }
            spec = std::max(0.0F, spec);
            const float r = p.rgb[0] * lit;
            const float g = p.rgb[1] * lit;
            const float b = p.rgb[2] * lit;
            const float sr = r + (255.0F - r) * std::min(spec, 1.0F);
            const float sg = g + (255.0F - g) * std::min(spec, 1.0F);
            const float sb = b + (255.0F - b) * std::min(spec, 1.0F);
            const int a = static_cast<int>(std::min(cov, 1.0F) * 255.0F + 0.5F);
            const int keep = 255 - a;
            std::uint8_t* px4 = row + static_cast<std::size_t>(x) * 4;
            // Source prémultipliée par la couverture, composée « over ».
            px4[0] = static_cast<std::uint8_t>((to_byte(sr) * a + px4[0] * keep + 127) / 255);
            px4[1] = static_cast<std::uint8_t>((to_byte(sg) * a + px4[1] * keep + 127) / 255);
            px4[2] = static_cast<std::uint8_t>((to_byte(sb) * a + px4[2] * keep + 127) / 255);
            px4[3] = static_cast<std::uint8_t>(a + (px4[3] * keep + 127) / 255);
        }
    }
}

void render_band(const std::vector<Prepared>& prepared, const RenderParams& params,
                 const RasterView& view, bool high, int row0, int row1,
                 std::vector<std::uint8_t>& rgba) {
    const std::size_t stride = static_cast<std::size_t>(view.width) * 4;
    for (int y = row0; y < row1; ++y) {
        std::uint8_t* row = rgba.data() + static_cast<std::size_t>(y) * stride;
        if (params.fabric_opaque) {
            fill_fabric_row(params, view, high, y, row);
        }
    }
    for (const Prepared& p : prepared) {
        // Rejet rapide : ni le fil ni son ombre ne touchent cette bande.
        const bool thread_hit = p.bx0 < p.bx1 && p.by0 < row1 && p.by1 > row0;
        const bool shadow_hit = p.sx0 < p.sx1 && p.sy0 < row1 && p.sy1 > row0;
        if (shadow_hit) {
            draw_shadow(p, view, row0, row1, rgba);
        }
        if (thread_hit) {
            draw_thread(p, view, high, row0, row1, rgba);
        }
    }
}

} // namespace

Detail choose_detail(double px_per_mm, const RenderParams& params) {
    return params.thread_width_mm * px_per_mm >= kMinThreadPixels ? Detail::Realistic
                                                                  : Detail::Lines;
}

RasterView plan_view(const RectMm& region, double px_per_mm, std::size_t max_pixels) {
    if (region.empty() || !(px_per_mm > 0.0) || max_pixels == 0) {
        return {};
    }
    double ppm = px_per_mm;
    int w = 0;
    int h = 0;
    for (int guard = 0; guard < 64; ++guard) {
        w = std::max(1, static_cast<int>(std::ceil(region.width() * ppm)));
        h = std::max(1, static_cast<int>(std::ceil(region.height() * ppm)));
        if (static_cast<double>(w) * static_cast<double>(h) <= static_cast<double>(max_pixels)) {
            break;
        }
        const double ratio = std::sqrt(static_cast<double>(max_pixels) /
                                       (static_cast<double>(w) * static_cast<double>(h)));
        ppm *= std::min(ratio, 0.98);
    }
    RasterView view;
    view.px_per_mm = ppm;
    view.width = w;
    view.height = h;
    view.rect = RectMm{region.x0, region.y0, region.x0 + static_cast<double>(w) / ppm,
                       region.y0 + static_cast<double>(h) / ppm};
    return view;
}

RasterImage render_threads(const std::vector<ThreadSegment>& segments, const RenderParams& raw,
                           const RasterView& view) {
    RasterImage image;
    if (view.width <= 0 || view.height <= 0 || !(view.px_per_mm > 0.0)) {
        return image;
    }
    const RenderParams params = sanitized(raw);
    const bool high = params.quality == Quality::High;
    image.width = view.width;
    image.height = view.height;
    image.rgba.assign(
        static_cast<std::size_t>(view.width) * static_cast<std::size_t>(view.height) * 4, 0);

    std::vector<Prepared> prepared;
    prepared.reserve(segments.size());
    for (std::size_t i = 0; i < segments.size(); ++i) {
        Prepared p;
        if (prepare(segments[i], i, params, view, high, p)) {
            prepared.push_back(p);
        }
    }

    // Bandes de lignes indépendantes : chaque pixel est écrit par une seule
    // bande, dans l'ordre des brins -> résultat identique quel que soit le
    // nombre de fils d'exécution.
    const std::size_t pixels =
        static_cast<std::size_t>(view.width) * static_cast<std::size_t>(view.height);
    unsigned workers = std::max(1U, std::min(8U, std::thread::hardware_concurrency()));
    if (pixels < 200'000 || (prepared.size() < 64 && pixels < 2'000'000)) {
        workers = 1;
    }
    workers = std::min(workers, static_cast<unsigned>(view.height));
    if (workers <= 1) {
        render_band(prepared, params, view, high, 0, view.height, image.rgba);
        return image;
    }
    std::vector<std::thread> pool;
    pool.reserve(workers);
    const int rows = view.height;
    for (unsigned w = 0; w < workers; ++w) {
        const int row0 = static_cast<int>(static_cast<long long>(rows) * w / workers);
        const int row1 = static_cast<int>(static_cast<long long>(rows) * (w + 1) / workers);
        pool.emplace_back(
            [&, row0, row1] { render_band(prepared, params, view, high, row0, row1, image.rgba); });
    }
    for (auto& t : pool) {
        t.join();
    }
    return image;
}

} // namespace openstitch::stitch_render
