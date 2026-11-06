// SPDX-License-Identifier: Apache-2.0
#include "preferences.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

namespace openstitch::desktop_v2 {

namespace {
std::string trim(std::string_view s) {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string_view::npos) {
        return {};
    }
    return std::string(s.substr(b, s.find_last_not_of(" \t\r") - b + 1));
}

bool validTheme(const std::string& t) {
    return t == "system" || t == "light" || t == "dark";
}
} // namespace

double densityScale(UiDensity density) {
    switch (density) {
    case UiDensity::Compact:
        return 0.8;
    case UiDensity::Spacious:
        return 1.3;
    case UiDensity::Normal:
        break;
    }
    return 1.0;
}

std::string densityId(UiDensity density) {
    switch (density) {
    case UiDensity::Compact:
        return "compact";
    case UiDensity::Spacious:
        return "spacious";
    case UiDensity::Normal:
        break;
    }
    return "normal";
}

std::optional<UiDensity> parseDensity(std::string_view id) {
    if (id == "compact") {
        return UiDensity::Compact;
    }
    if (id == "normal") {
        return UiDensity::Normal;
    }
    if (id == "spacious") {
        return UiDensity::Spacious;
    }
    return std::nullopt;
}

std::string densityLabel(UiDensity density) {
    switch (density) {
    case UiDensity::Compact:
        return "Compacte";
    case UiDensity::Spacious:
        return "Aérée";
    case UiDensity::Normal:
        break;
    }
    return "Normale";
}

std::string preferencesToText(const Preferences& p) {
    return "theme=" + p.theme + "\ndensity=" + densityId(p.density) + "\n";
}

Preferences preferencesFromText(std::string_view text) {
    Preferences out;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(std::string_view(line).substr(0, eq));
        const std::string value = trim(std::string_view(line).substr(eq + 1));
        if (key == "theme" && validTheme(value)) {
            out.theme = value;
        } else if (key == "density") {
            if (const auto d = parseDensity(value)) {
                out.density = *d;
            }
        }
    }
    return out;
}

bool savePreferences(const std::filesystem::path& file, const Preferences& preferences) {
    std::error_code ec;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), ec);
    }
    // Écriture atomique : fichier temporaire puis renommage, jamais un fichier tronqué.
    const auto tmp = std::filesystem::path(file).concat(".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << preferencesToText(preferences);
        if (!out) {
            return false;
        }
    }
    std::filesystem::rename(tmp, file, ec);
    return !ec;
}

Preferences loadPreferences(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream text;
    text << in.rdbuf();
    return preferencesFromText(text.str());
}

} // namespace openstitch::desktop_v2
