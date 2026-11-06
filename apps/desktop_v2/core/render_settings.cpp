// SPDX-License-Identifier: Apache-2.0
#include "render_settings.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace openstitch::desktop_v2 {

namespace {
double clampTo(double v, double lo, double hi) {
    return std::isfinite(v) ? std::clamp(v, lo, hi) : lo;
}

bool parseDouble(std::string_view s, double& out) {
    const std::string copy(s);
    char* end = nullptr;
    const double v = std::strtod(copy.c_str(), &end);
    if (end == copy.c_str() || *end != '\0' || !std::isfinite(v)) {
        return false;
    }
    out = v;
    return true;
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    return buf;
}
} // namespace

RenderSettings clamped(RenderSettings s) {
    s.pxPerMm = clampTo(s.pxPerMm, kMinPxPerMm, kMaxPxPerMm);
    s.threadWidthMm = clampTo(s.threadWidthMm, 0.1, 1.0);
    s.relief = clampTo(s.relief, 0.0, 1.0);
    s.sheen = clampTo(s.sheen, 0.0, 1.0);
    s.twist = clampTo(s.twist, 0.0, 1.0);
    s.shadow = clampTo(s.shadow, 0.0, 1.0);
    s.fabricRelief = clampTo(s.fabricRelief, 0.0, 1.0);
    return s;
}

std::optional<RenderSettings> renderPreset(std::string_view id) {
    RenderSettings s; // défauts = « équilibré » (le rendu d'origine de la V2)
    if (id == "balanced") {
        return s;
    }
    if (id == "draft") {
        s.pxPerMm = 1.5;
        s.relief = 0.4;
        s.twist = 0.0;
        s.shadow = 0.3;
        s.highQuality = false;
        return s;
    }
    if (id == "high") {
        s.pxPerMm = 6.0;
        s.relief = 0.8;
        s.twist = 0.6;
        s.shadow = 0.6;
        return s;
    }
    if (id == "photo") {
        s.pxPerMm = 10.0;
        s.threadWidthMm = 0.3;
        s.relief = 0.9;
        s.sheen = 0.7;
        s.twist = 0.8;
        s.shadow = 0.7;
        s.fabricWeave = true;
        s.fabricRelief = 0.5;
        return s;
    }
    return std::nullopt;
}

std::string renderPresetLabel(std::string_view id) {
    if (id == "draft") {
        return "Brouillon (rapide)";
    }
    if (id == "balanced") {
        return "Équilibré";
    }
    if (id == "high") {
        return "Haute qualité";
    }
    if (id == "photo") {
        return "Photo (lent)";
    }
    return "Personnalisé";
}

std::string matchingRenderPreset(const RenderSettings& settings) {
    for (const char* id : {"draft", "balanced", "high", "photo"}) {
        if (renderPreset(id) == settings) {
            return id;
        }
    }
    return {};
}

std::string renderSettingsToText(const RenderSettings& s) {
    char rgb[8];
    std::snprintf(rgb, sizeof rgb, "%02X%02X%02X", s.fabricRgb[0], s.fabricRgb[1], s.fabricRgb[2]);
    return "render.px_per_mm=" + num(s.pxPerMm) + "\nrender.thread_mm=" + num(s.threadWidthMm) +
           "\nrender.relief=" + num(s.relief) + "\nrender.sheen=" + num(s.sheen) +
           "\nrender.twist=" + num(s.twist) + "\nrender.shadow=" + num(s.shadow) +
           "\nrender.fabric=" + rgb + "\nrender.weave=" + (s.fabricWeave ? "1" : "0") +
           "\nrender.fabric_relief=" + num(s.fabricRelief) +
           "\nrender.quality=" + (s.highQuality ? "high" : "fast") + "\n";
}

bool renderSettingsApply(RenderSettings& s, std::string_view key, std::string_view value) {
    double v = 0.0;
    const auto number = [&](double& target, double lo, double hi) {
        if (!parseDouble(value, v)) {
            return false;
        }
        target = std::clamp(v, lo, hi);
        return true;
    };
    if (key == "px_per_mm") {
        return number(s.pxPerMm, kMinPxPerMm, kMaxPxPerMm);
    }
    if (key == "thread_mm") {
        return number(s.threadWidthMm, 0.1, 1.0);
    }
    if (key == "relief") {
        return number(s.relief, 0.0, 1.0);
    }
    if (key == "sheen") {
        return number(s.sheen, 0.0, 1.0);
    }
    if (key == "twist") {
        return number(s.twist, 0.0, 1.0);
    }
    if (key == "shadow") {
        return number(s.shadow, 0.0, 1.0);
    }
    if (key == "fabric_relief") {
        return number(s.fabricRelief, 0.0, 1.0);
    }
    if (key == "weave") {
        if (value != "0" && value != "1") {
            return false;
        }
        s.fabricWeave = value == "1";
        return true;
    }
    if (key == "quality") {
        if (value != "high" && value != "fast") {
            return false;
        }
        s.highQuality = value == "high";
        return true;
    }
    if (key == "fabric") {
        if (value.size() != 6) {
            return false;
        }
        unsigned int packed = 0;
        const auto res = std::from_chars(value.data(), value.data() + 6, packed, 16);
        if (res.ec != std::errc{} || res.ptr != value.data() + 6) {
            return false;
        }
        s.fabricRgb = {static_cast<std::uint8_t>(packed >> 16),
                       static_cast<std::uint8_t>((packed >> 8) & 0xFF),
                       static_cast<std::uint8_t>(packed & 0xFF)};
        return true;
    }
    return false;
}

} // namespace openstitch::desktop_v2
