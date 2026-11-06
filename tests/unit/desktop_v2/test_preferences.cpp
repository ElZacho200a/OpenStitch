// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <fstream>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "preferences.hpp"

using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

namespace {
fs::path tempDir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("openstitch_prefs_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}
} // namespace

TEST_CASE("density scales are ordered and ids round-trip", "[desktop_v2][prefs]") {
    CHECK(densityScale(UiDensity::Compact) < densityScale(UiDensity::Normal));
    CHECK(densityScale(UiDensity::Normal) < densityScale(UiDensity::Spacious));
    CHECK(densityScale(UiDensity::Normal) == Catch::Approx(1.0));
    for (const auto d : {UiDensity::Compact, UiDensity::Normal, UiDensity::Spacious}) {
        CHECK(parseDensity(densityId(d)) == d);
        CHECK_FALSE(densityLabel(d).empty());
    }
    CHECK_FALSE(parseDensity("huge").has_value());
    CHECK_FALSE(parseDensity("").has_value());
}

TEST_CASE("preferences round-trip through text and recover per key", "[desktop_v2][prefs]") {
    Preferences p;
    p.theme = "dark";
    p.density = UiDensity::Spacious;
    CHECK(preferencesFromText(preferencesToText(p)) == p);
    CHECK(preferencesToText(p) == preferencesToText(p)); // déterministe

    CHECK(preferencesFromText("") == Preferences{});
    CHECK(preferencesFromText("garbage\n===\n") == Preferences{});
    // Une clé invalide retombe sur SON défaut sans toucher l'autre.
    const auto mixed = preferencesFromText("theme=purple\ndensity=compact\n");
    CHECK(mixed.theme == "system");
    CHECK(mixed.density == UiDensity::Compact);
    const auto other = preferencesFromText("theme=light\ndensity=gigantic\n");
    CHECK(other.theme == "light");
    CHECK(other.density == UiDensity::Normal);
    // Espaces et fins de ligne Windows tolérés.
    const auto crlf = preferencesFromText(" theme = dark \r\n density = compact \r\n");
    CHECK(crlf.theme == "dark");
    CHECK(crlf.density == UiDensity::Compact);
}

TEST_CASE("preferences are saved atomically and a missing file gives defaults",
          "[desktop_v2][prefs]") {
    const auto dir = tempDir("save");
    const auto file = dir / "sub" / "preferences.txt";
    CHECK(loadPreferences(file) == Preferences{});
    Preferences p;
    p.theme = "light";
    p.density = UiDensity::Compact;
    REQUIRE(savePreferences(file, p));
    CHECK(loadPreferences(file) == p);
    CHECK_FALSE(fs::exists(fs::path(file).concat(".tmp"))); // pas de fichier temporaire oublié
    // Un fichier tronqué ou corrompu ne fait jamais échouer le chargement.
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << "theme=da";
    }
    CHECK(loadPreferences(file) == Preferences{});
    fs::remove_all(dir);
}
