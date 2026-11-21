// SPDX-License-Identifier: Apache-2.0
#include "theme_schema.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

namespace openstitch::desktop_v2::theme {

namespace {

constexpr std::string_view kEasings =
    "Linear|InQuad|OutQuad|InOutQuad|InCubic|OutCubic|InOutCubic|OutQuart|InOutQuart|OutQuint|"
    "InOutQuint|InSine|OutSine|InOutSine|OutExpo|InOutExpo|OutCirc|InOutCirc|OutBack|InOutBack|"
    "OutElastic|OutBounce";

using T = TokenType;

// Neo Studio (docs/desktop-v2/21-studio-neo-theme.md) : valeurs historiques de Theme.qml. TRIÉ
// par clé (recherche dichotomique) ; `test_theme_engine` vérifie l'ordre et l'égalité avec
// resources/themes/neo-studio.
constexpr std::array kTokens{
    TokenSpec{"color.accent", T::Color, "#ef91bb", "#b02a66", 0, 0, {}, "action, sélection"},
    TokenSpec{"color.accentInk", T::Color, "#281a28", "#ffffff", 0, 0, {}, "texte sur accent"},
    TokenSpec{"color.accentPressed", T::Color, "#dc79a6", "#9e245c"},
    TokenSpec{"color.accentSoft", T::Color, "#4b344c", "#f6d9e6", 0, 0, {}, "fond sélectionné"},
    TokenSpec{"color.background", T::Color, "#171b25", "#f3f4f7", 0, 0, {}, "plan de travail"},
    TokenSpec{"color.border", T::Color, "#445069", "#aeb4c2"},
    TokenSpec{"color.deskDot", T::Color, "#262d3b", "#d5d9e2", 0, 0, {}, "semis du plan"},
    TokenSpec{"color.divider", T::Color, "#313b4b", "#d3d7e0"},
    TokenSpec{"color.error", T::Color, "#f1a0ad", "#ac3649"},
    TokenSpec{"color.faint", T::Color, "#a3b2c6", "#565d6e"},
    TokenSpec{"color.focus", T::Color, "{color.accent}", {}, 0, 0, {}, "anneau de focus"},
    TokenSpec{"color.foreground", T::Color, "#f1f4fa", "#1b1d24"},
    TokenSpec{"color.gloss", T::Color, "#ffffff", {}, 0, 0, {}, "reflet des surfaces"},
    TokenSpec{"color.guideCyan", T::Color, "#00bcd4"},
    TokenSpec{"color.guideGreen", T::Color, "#43a047"},
    TokenSpec{"color.guideMagenta", T::Color, "#d81b60"},
    TokenSpec{"color.guideRed", T::Color, "#e53935"},
    TokenSpec{"color.hover", T::Color, "#374153", "#d9dde7"},
    TokenSpec{"color.info", T::Color, "#8ad4f0", "#0f6585"},
    TokenSpec{"color.muted", T::Color, "#bdc8d8", "#3f4554"},
    TokenSpec{"color.outline", T::Color, "#6b7a94", "#7a8194", 0, 0, {}, ">= 3:1 sur surfaces"},
    TokenSpec{"color.panel", T::Color, "#1d2330", "#ffffff"},
    TokenSpec{"color.raised", T::Color, "#2c3545", "#e4e7ee"},
    TokenSpec{"color.secondaryAccent", T::Color, "#b7a6f2", "#6446b0"},
    TokenSpec{"color.shadow", T::Color, "#000000"},
    TokenSpec{"color.success", T::Color, "#93d5b9", "#2c6b4b"},
    TokenSpec{"color.surface", T::Color, "#222936", "#e9ebf0"},
    TokenSpec{"color.warning", T::Color, "#ebc47c", "#83531a"},
    TokenSpec{
        "color.zoneDetail", T::Color, "#ff8c00", "#ff8c00", 0, 0, {}, "carte des zones de détail"},
    TokenSpec{"color.zoneLinework",
              T::Color,
              "#00beff",
              "#00beff",
              0,
              0,
              {},
              "carte des zones de traits fins"},
    TokenSpec{"effect.deskStep", T::Number, "24", {}, 4, 200, {}, "pas du semis (px)"},
    TokenSpec{"effect.deskTexture", T::Asset, "", {}, 0, 0, {}, "texture du plan de travail"},
    TokenSpec{"effect.deskTextureOpacity", T::Number, "0", {}, 0, 1},
    TokenSpec{"effect.shadowLayers", T::Number, "3", {}, 0, 6, {}, "couches de l'ombre douce"},
    TokenSpec{"effect.shadowOffset", T::Number, "4", {}, -24, 24},
    TokenSpec{"effect.shadowOpacity", T::Number, "0.2", "0.07", 0, 1},
    TokenSpec{"effect.shadowSpread", T::Number, "3", {}, 0, 24, {}, "étalement par couche"},
    TokenSpec{"font.bodySize", T::Number, "13", {}, 8, 32},
    TokenSpec{"font.bodyWeight", T::Number, "400", {}, 100, 900},
    TokenSpec{"font.brandSize", T::Number, "20", {}, 8, 48},
    TokenSpec{"font.captionSize", T::Number, "11", {}, 8, 32},
    TokenSpec{"font.family",
              T::Text,
              "Segoe UI Variable, sans-serif",
              {},
              0,
              0,
              {},
              "familles par ordre de préférence"},
    TokenSpec{"font.headingFamily", T::Text, "{font.family}"},
    TokenSpec{"font.headingSize", T::Number, "16", {}, 8, 40},
    TokenSpec{"font.headingWeight", T::Number, "600", {}, 100, 900},
    TokenSpec{"font.heroSize", T::Number, "22", {}, 8, 56},
    TokenSpec{"font.letterSpacing", T::Number, "0", {}, -2, 4},
    TokenSpec{"font.sectionLetterSpacing", T::Number, "1", {}, -2, 6},
    TokenSpec{"font.smallSize", T::Number, "12", {}, 8, 32},
    TokenSpec{"font.strongWeight", T::Number, "600", {}, 100, 900},
    TokenSpec{"icon.size", T::Number, "18", {}, 10, 40},
    TokenSpec{"icon.strokeWidth", T::Number, "1.45", {}, 0.5, 4},
    TokenSpec{"motion.durationMs", T::Number, "150", {}, 0, 2000},
    TokenSpec{"motion.easing", T::Easing, "OutCubic"},
    TokenSpec{"motion.easingEmphasis", T::Easing, "OutCubic", {}, 0, 0, {}, "survol, ouverture"},
    TokenSpec{"motion.fastMs", T::Number, "90", {}, 0, 2000},
    TokenSpec{"motion.hoverLift", T::Number, "0", {}, 0, 8, {}, "élévation au survol (px)"},
    TokenSpec{"motion.popupScale", T::Number, "1", {}, 0.5, 1, {}, "échelle de départ"},
    TokenSpec{"motion.pressScale", T::Number, "0.97", {}, 0.5, 1},
    TokenSpec{"motion.slowMs", T::Number, "200", {}, 0, 3000},
    TokenSpec{"shape.borderWidth", T::Number, "1", {}, 0, 8},
    TokenSpec{"shape.cardRadius", T::Number, "14", {}, 0, 64},
    TokenSpec{"shape.focusWidth", T::Number, "2", {}, 2, 6, {}, "jamais nul : focus visible"},
    TokenSpec{"shape.radius", T::Number, "8", {}, 0, 64},
    TokenSpec{"size.controlHeight", T::Number, "38", {}, 20, 80},
    TokenSpec{"size.inspectorWidth", T::Number, "294", {}, 200, 600},
    TokenSpec{"size.railWidth", T::Number, "76", {}, 40, 160},
    TokenSpec{"size.rowHeight", T::Number, "42", {}, 20, 96},
    TokenSpec{"space.gap", T::Number, "10", {}, 0, 48},
    TokenSpec{"space.panelPadding", T::Number, "18", {}, 0, 64},
};

constexpr std::array kStyleProperties{
    StylePropertySpec{"border", T::Color},
    StylePropertySpec{"borderWidth", T::Number, 0, 8},
    StylePropertySpec{"fill", T::Color, 0, 0, {}, "couleur de fond (début du dégradé)"},
    StylePropertySpec{"fillEnd", T::Color, 0, 0, {}, "fin du dégradé ; absent = aplat"},
    StylePropertySpec{"fontFamily", T::Text},
    StylePropertySpec{"fontWeight", T::Number, 100, 900},
    StylePropertySpec{"gloss", T::Number, 0, 1, {}, "reflet brillant (opacité)"},
    StylePropertySpec{"glossHeight", T::Number, 0, 1, {}, "part de la hauteur"},
    StylePropertySpec{"gradient", T::Choice, 0, 0, "vertical|horizontal"},
    StylePropertySpec{"height", T::Number, 2, 120},
    StylePropertySpec{"icon", T::Color},
    StylePropertySpec{"innerHighlight", T::Number, 0, 1, {}, "liseré intérieur clair (relief)"},
    StylePropertySpec{"lift", T::Number, -8, 8, {}, "décalage vertical (px, + = vers le haut)"},
    StylePropertySpec{"opacity", T::Number, 0, 1},
    StylePropertySpec{"paddingX", T::Number, 0, 64},
    StylePropertySpec{"radius", T::Number, 0, 64},
    StylePropertySpec{"scale", T::Number, 0.5, 1.5},
    StylePropertySpec{"shadowColor", T::Color},
    StylePropertySpec{"shadowLayers", T::Number, 0, 6},
    StylePropertySpec{"shadowOffset", T::Number, -24, 24},
    StylePropertySpec{"shadowOpacity", T::Number, 0, 1},
    StylePropertySpec{"shadowSpread", T::Number, 0, 24},
    StylePropertySpec{"text", T::Color},
    StylePropertySpec{"texture", T::Asset},
    StylePropertySpec{"textureOpacity", T::Number, 0, 1},
};

constexpr std::array<std::string_view, 6> kStates{"normal",   "hover", "pressed",
                                                  "selected", "focus", "disabled"};

constexpr std::array kComponents{
    ComponentSpec{"button", "emphasized|quiet", "StudioButton"},
    ComponentSpec{"card", "", "StudioCard : cartes flottantes"},
    ComponentSpec{"checkbox", "", "indicateur de StudioCheckBox"},
    ComponentSpec{"desk", "", "plan de travail derrière les cartes"},
    ComponentSpec{"header", "", "barre d'en-tête unique"},
    ComponentSpec{"input", "", "StudioTextField, StudioSpinBox, StudioComboBox"},
    ComponentSpec{"listItem", "", "lignes de l'arbre d'objets"},
    ComponentSpec{"panel", "", "StudioPanel"},
    ComponentSpec{"popup", "", "dialogues, menus, popovers"},
    ComponentSpec{"slider.fill", "", "partie remplie du curseur"},
    ComponentSpec{"slider.handle", "", "poignée du curseur"},
    ComponentSpec{"slider.track", "", "rail du curseur"},
    ComponentSpec{"switch.thumb", "", "pastille de StudioSwitch"},
    ComponentSpec{"switch.track", "", "rail de StudioSwitch"},
    ComponentSpec{"tab", "", "onglets d'espaces"},
    ComponentSpec{"tooltip", "", "StudioToolTip"},
};

template <class Spec, std::size_t N>
const Spec* findSorted(const std::array<Spec, N>& table, std::string_view key) {
    const auto it = std::lower_bound(table.begin(), table.end(), key,
                                     [](const Spec& s, std::string_view k) { return s.key < k; });
    return it != table.end() && it->key == key ? &*it : nullptr;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

double channel(std::uint8_t v) {
    const double c = v / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(Color c) {
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}

bool inChoices(std::string_view choices, std::string_view value) {
    while (!choices.empty()) {
        const auto bar = choices.find('|');
        if (choices.substr(0, bar) == value) {
            return true;
        }
        if (bar == std::string_view::npos) {
            break;
        }
        choices.remove_prefix(bar + 1);
    }
    return false;
}

} // namespace

std::optional<Color> parseColor(std::string_view text) {
    if (text == "transparent") {
        return Color{0, 0, 0, 0};
    }
    if (text.size() < 4 || text.front() != '#') {
        return std::nullopt;
    }
    text.remove_prefix(1);
    std::array<int, 8> d{};
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i >= d.size() || (d[i] = hexDigit(text[i])) < 0) {
            return std::nullopt;
        }
    }
    const auto pair = [&](std::size_t i) {
        return static_cast<std::uint8_t>(d[i] * 16 + d[i + 1]);
    };
    const auto single = [&](std::size_t i) { return static_cast<std::uint8_t>(d[i] * 17); };
    switch (text.size()) {
    case 3:
        return Color{single(0), single(1), single(2), 255};
    case 6:
        return Color{pair(0), pair(2), pair(4), 255};
    case 8:
        return Color{pair(0), pair(2), pair(4), pair(6)};
    default:
        return std::nullopt;
    }
}

std::string formatColor(Color color) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string out = "#";
    const auto put = [&](std::uint8_t v) {
        out += kHex[v >> 4];
        out += kHex[v & 15];
    };
    put(color.r);
    put(color.g);
    put(color.b);
    if (color.a != 255) {
        put(color.a);
    }
    return out;
}

double contrastRatio(Color a, Color b) {
    auto x = luminance(a);
    auto y = luminance(b);
    if (x < y) {
        std::swap(x, y);
    }
    return (x + 0.05) / (y + 0.05);
}

std::string_view appearanceId(Appearance appearance) {
    return appearance == Appearance::Light ? "light" : "dark";
}

std::optional<Appearance> parseAppearance(std::string_view id) {
    if (id == "dark") {
        return Appearance::Dark;
    }
    if (id == "light") {
        return Appearance::Light;
    }
    return std::nullopt;
}

std::span<const TokenSpec> tokenSchema() {
    return kTokens;
}

const TokenSpec* findToken(std::string_view key) {
    return findSorted(kTokens, key);
}

bool isCustomTokenKey(std::string_view key) {
    return key.starts_with("custom.") && key.size() > 7;
}

std::span<const StylePropertySpec> stylePropertySchema() {
    return kStyleProperties;
}

const StylePropertySpec* findStyleProperty(std::string_view key) {
    return findSorted(kStyleProperties, key);
}

std::span<const std::string_view> componentStates() {
    return kStates;
}

std::span<const ComponentSpec> componentSchema() {
    return kComponents;
}

const ComponentSpec* findComponent(std::string_view id) {
    const auto it =
        std::lower_bound(kComponents.begin(), kComponents.end(), id,
                         [](const ComponentSpec& s, std::string_view k) { return s.id < k; });
    return it != kComponents.end() && it->id == id ? &*it : nullptr;
}

bool componentHasVariant(const ComponentSpec& component, std::string_view variant) {
    return !variant.empty() && inChoices(component.variants, variant);
}

std::optional<TokenValue> parseTypedNumber(TokenType type, double min, double max, double number) {
    if (type != TokenType::Number || !std::isfinite(number) || number < min || number > max) {
        return std::nullopt;
    }
    return TokenValue{number};
}

std::optional<TokenValue> parseTypedValue(TokenType type, double min, double max,
                                          std::string_view choices, std::string_view text) {
    switch (type) {
    case TokenType::Color:
        if (const auto c = parseColor(text)) {
            return TokenValue{*c};
        }
        return std::nullopt;
    case TokenType::Number: {
        double v = 0;
        const auto* end = text.data() + text.size();
        const auto [ptr, ec] = std::from_chars(text.data(), end, v);
        if (ec != std::errc{} || ptr != end) {
            return std::nullopt;
        }
        return parseTypedNumber(type, min, max, v);
    }
    case TokenType::Text:
        if (text.size() > 256) {
            return std::nullopt;
        }
        return TokenValue{std::string(text)};
    case TokenType::Easing:
        if (!inChoices(kEasings, text)) {
            return std::nullopt;
        }
        return TokenValue{std::string(text)};
    case TokenType::Choice:
        if (!inChoices(choices, text)) {
            return std::nullopt;
        }
        return TokenValue{std::string(text)};
    case TokenType::Asset:
        if (text.size() > 240) {
            return std::nullopt;
        }
        return TokenValue{std::string(text)};
    }
    return std::nullopt;
}

} // namespace openstitch::desktop_v2::theme
