// SPDX-License-Identifier: Apache-2.0
#include "realistic_preferences.hpp"

#include <QSettings>

#include <algorithm>

namespace openstitch::desktop {

namespace {

constexpr auto kEnabled = "view/realistic/enabled";
constexpr auto kThreadWidth = "view/realistic/threadWidthMm";
constexpr auto kRelief = "view/realistic/relief";
constexpr auto kSheen = "view/realistic/sheen";
constexpr auto kTwist = "view/realistic/twist";
constexpr auto kShadow = "view/realistic/shadow";
constexpr auto kFabricRgb = "view/realistic/fabricRgb";
constexpr auto kFabricOpaque = "view/realistic/fabricOpaque";
constexpr auto kFabricTexture = "view/realistic/fabricTexture";
constexpr auto kFabricRelief = "view/realistic/fabricRelief";
constexpr auto kQuality = "view/realistic/quality";

// 0xRRGGBB <-> {r, g, b}
int packRgb(const std::array<std::uint8_t, 3>& rgb) {
    return (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
}

std::array<std::uint8_t, 3> unpackRgb(int packed) {
    return {static_cast<std::uint8_t>((packed >> 16) & 0xFF),
            static_cast<std::uint8_t>((packed >> 8) & 0xFF),
            static_cast<std::uint8_t>(packed & 0xFF)};
}

} // namespace

RealisticPreferences loadRealisticPreferences() {
    using stitch_render::FabricTexture;
    using stitch_render::Quality;
    QSettings s;
    RealisticPreferences prefs;
    stitch_render::RenderParams& p = prefs.params;
    prefs.enabled = s.value(kEnabled, prefs.enabled).toBool();
    p.thread_width_mm = s.value(kThreadWidth, p.thread_width_mm).toDouble();
    p.relief = s.value(kRelief, p.relief).toDouble();
    p.sheen = s.value(kSheen, p.sheen).toDouble();
    p.twist = s.value(kTwist, p.twist).toDouble();
    p.shadow = s.value(kShadow, p.shadow).toDouble();
    p.fabric_rgb = unpackRgb(s.value(kFabricRgb, packRgb(p.fabric_rgb)).toInt());
    p.fabric_opaque = s.value(kFabricOpaque, p.fabric_opaque).toBool();
    const int texture = s.value(kFabricTexture, static_cast<int>(p.fabric_texture)).toInt();
    p.fabric_texture = (texture >= 0 && texture <= static_cast<int>(FabricTexture::Felt))
                           ? static_cast<FabricTexture>(texture)
                           : p.fabric_texture;
    p.fabric_relief = s.value(kFabricRelief, p.fabric_relief).toDouble();
    p.quality =
        s.value(kQuality, static_cast<int>(p.quality)).toInt() == static_cast<int>(Quality::Fast)
            ? Quality::Fast
            : Quality::High;
    p = stitch_render::sanitized(p);
    return prefs;
}

void saveRealisticPreferences(const RealisticPreferences& prefs) {
    QSettings s;
    const stitch_render::RenderParams p = stitch_render::sanitized(prefs.params);
    s.setValue(kEnabled, prefs.enabled);
    s.setValue(kThreadWidth, p.thread_width_mm);
    s.setValue(kRelief, p.relief);
    s.setValue(kSheen, p.sheen);
    s.setValue(kTwist, p.twist);
    s.setValue(kShadow, p.shadow);
    s.setValue(kFabricRgb, packRgb(p.fabric_rgb));
    s.setValue(kFabricOpaque, p.fabric_opaque);
    s.setValue(kFabricTexture, static_cast<int>(p.fabric_texture));
    s.setValue(kFabricRelief, p.fabric_relief);
    s.setValue(kQuality, static_cast<int>(p.quality));
}

} // namespace openstitch::desktop
