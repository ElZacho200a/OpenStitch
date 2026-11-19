// SPDX-License-Identifier: Apache-2.0
// Moteur de thèmes du Studio (apps/desktop_v2/core/theme_*) : format, validation, héritage,
// migration, sécurité des chemins et des archives, activation avec retour arrière, thèmes livrés.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "preferences.hpp"
#include "theme_library.hpp"
#include "theme_package.hpp"
#include "theme_schema.hpp"

using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::theme;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        path = fs::temp_directory_path() / ("os-theme-test-" + std::to_string(counter++) + "-" +
                                            std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string manifest(const std::string& id, const std::string& extra = "") {
    return R"({"schema":"openstitch-theme","schemaVersion":1,"id":")" + id + R"(","name":")" + id +
           "\"" + extra + "}";
}

Color colorOf(const ResolvedTheme& t, const std::string& key) {
    return std::get<Color>(t.tokens.at(key));
}

double numberOf(const ResolvedTheme& t, const std::string& key) {
    return std::get<double>(t.tokens.at(key));
}

bool mentions(const std::vector<Diagnostic>& ds, const std::string& text) {
    for (const auto& d : ds) {
        if (d.message.find(text) != std::string::npos || d.where.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

ThemeLibrary builtinLibrary() {
    return ThemeLibrary({{fs::path(OPENSTITCH_THEMES_DIR), true}});
}

} // namespace

TEST_CASE("theme schema tables are sorted and defaults are valid", "[theme]") {
    const auto tokens = tokenSchema();
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        CHECK(tokens[i - 1].key < tokens[i].key);
    }
    const auto props = stylePropertySchema();
    for (std::size_t i = 1; i < props.size(); ++i) {
        CHECK(props[i - 1].key < props[i].key);
    }
    const auto comps = componentSchema();
    for (std::size_t i = 1; i < comps.size(); ++i) {
        CHECK(comps[i - 1].id < comps[i].id);
    }
    for (const auto appearance : {Appearance::Dark, Appearance::Light}) {
        const auto t = builtinFallback(appearance);
        CHECK(t.diagnostics.empty());
        CHECK(t.tokens.size() == tokens.size());
        CHECK(colorOf(t, "color.focus") == colorOf(t, "color.accent"));
    }
    CHECK(formatColor(*parseColor("#ABCDEF")) == "#abcdef");
    CHECK(parseColor("#12345678")->a == 0x78);
    CHECK(parseColor("#fff")->g == 255);
    CHECK_FALSE(parseColor("#12"));
    CHECK_FALSE(parseColor("rgb(1,2,3)"));
}

TEST_CASE("builtin neo-studio files equal the compiled defaults", "[theme]") {
    const auto library = builtinLibrary();
    for (const auto appearance : {Appearance::Dark, Appearance::Light}) {
        const auto files = library.resolve("neo-studio", appearance);
        INFO(appearanceId(appearance));
        REQUIRE_FALSE(hasErrors(files.diagnostics));
        const auto compiled = builtinFallback(appearance);
        CHECK(files.appearance == appearance);
        CHECK(files.tokens == compiled.tokens);
    }
}

TEST_CASE("every builtin theme resolves cleanly and is readable", "[theme]") {
    const auto library = builtinLibrary();
    REQUIRE(library.find("neo-studio"));
    REQUIRE(library.find("bubblegum"));
    REQUIRE(library.find("neo-cobalt"));
    for (const auto& entry : library.entries()) {
        INFO(entry.dir.string());
        REQUIRE(entry.loadable());
        for (const auto appearance : entry.package->manifest.variants.empty()
                                         ? std::vector<Appearance>{Appearance::Dark}
                                         : entry.package->manifest.variants) {
            const auto t = library.resolve(entry.id(), appearance);
            std::string report;
            for (const auto& d : t.diagnostics) {
                report += d.where + " : " + d.message + "\n";
            }
            INFO(report);
            CHECK(t.diagnostics.empty()); // ni erreur, ni avertissement, contrastes compris
            // Contrat historique du design (test_studio_design) : texte >= 4,5:1 sur les six fonds.
            for (const char* fg :
                 {"color.foreground", "color.muted", "color.faint", "color.accent", "color.error",
                  "color.warning", "color.success", "color.info", "color.secondaryAccent"}) {
                for (const char* bg : {"color.background", "color.surface", "color.panel",
                                       "color.raised", "color.hover", "color.accentSoft"}) {
                    INFO(std::string(fg) + " on " + bg);
                    CHECK(contrastRatio(colorOf(t, fg), colorOf(t, bg)) >= 4.5);
                }
            }
            for (const char* bg : {"color.background", "color.surface", "color.panel",
                                   "color.raised", "color.hover"}) {
                CHECK(contrastRatio(colorOf(t, "color.accent"), colorOf(t, bg)) >= 3.0);
            }
            CHECK(contrastRatio(colorOf(t, "color.accentInk"), colorOf(t, "color.accentPressed")) >=
                  4.5);
            CHECK(numberOf(t, "shape.focusWidth") >= 2);
        }
    }
}

TEST_CASE("a valid theme loads with its metadata", "[theme]") {
    TempDir tmp;
    write(tmp.path / "mint" / "manifest.json",
          manifest("mint",
                   R"(,"author":"A","description":"D","version":"2.1.0","variants":["light"])"));
    write(tmp.path / "mint" / "tokens.json",
          R"({"color":{"accent":"#0a7d6e"},"shape":{"radius":12}})");
    ThemeLibrary library({{tmp.path, false}});
    REQUIRE(library.find("mint"));
    const auto t = library.resolve("mint", Appearance::Dark);
    CHECK_FALSE(hasErrors(t.diagnostics));
    CHECK(t.appearance == Appearance::Light); // seule variante proposée
    CHECK(t.manifest.author == "A");
    CHECK(t.manifest.version == "2.1.0");
    CHECK(formatColor(colorOf(t, "color.accent")) == "#0a7d6e");
    CHECK(numberOf(t, "shape.radius") == 12);
    CHECK(numberOf(t, "size.controlHeight") == 38); // repli
}

TEST_CASE("invalid manifests are rejected", "[theme]") {
    const auto rejected = [](const std::string& text) {
        std::vector<Diagnostic> ds;
        const auto m = parseManifest(text, ds);
        return !m && hasErrors(ds);
    };
    CHECK(rejected("{not json"));
    CHECK(rejected("[]"));
    CHECK(rejected(R"({"schemaVersion":1,"name":"x"})"));               // id manquant
    CHECK(rejected(R"({"schemaVersion":1,"id":"Bad Id","name":"x"})")); // id invalide
    CHECK(rejected(R"({"schemaVersion":1,"id":"ok"})"));                // nom manquant
    CHECK(rejected(R"({"id":"ok","name":"x"})"));                       // version manquante
    CHECK(rejected(R"({"schemaVersion":1,"id":"ok","name":"x","parent":"ok"})")); // parent = soi
    CHECK(rejected(R"({"schema":"other","schemaVersion":1,"id":"ok","name":"x"})"));
    CHECK(rejected(R"({"schemaVersion":1,"id":"ok","name":"x","variants":["sepia"]})"));
    std::vector<Diagnostic> ds;
    CHECK(parseManifest(manifest("ok"), ds));
    CHECK(ds.empty());
    // Écriture puis relecture du manifeste.
    ThemeManifest m;
    m.id = "round-trip";
    m.name = "Aller-retour";
    m.parent = "neo-studio";
    m.variants = {Appearance::Light};
    m.fonts = {"assets/fonts/a.ttf"};
    std::vector<Diagnostic> again;
    CHECK(parseManifest(manifestToJson(m), again) == m);
}

TEST_CASE("newer formats and incompatible engines are refused", "[theme]") {
    std::vector<Diagnostic> ds;
    CHECK_FALSE(parseManifest(R"({"schemaVersion":)" + std::to_string(kSchemaVersion + 1) +
                                  R"(,"id":"ok","name":"x"})",
                              ds));
    CHECK(mentions(ds, "plus récent"));
    ds.clear();
    CHECK_FALSE(parseManifest(manifest("ok", R"(,"compatibility":{"minEngine":99})"), ds));
    CHECK(mentions(ds, "incompatible"));
    ds.clear();
    CHECK(parseManifest(manifest("ok", R"(,"compatibility":{"minEngine":1,"maxEngine":1})"), ds));
}

TEST_CASE("partial themes inherit from their parent", "[theme]") {
    TempDir tmp;
    write(tmp.path / "base" / "manifest.json", manifest("base", R"(,"variants":["dark","light"])"));
    write(tmp.path / "base" / "tokens.json",
          R"({"color":{"accent":"#111111","panel":"#222222"},"shape":{"radius":20},
              "variants":{"light":{"color":{"panel":"#eeeeee"}}}})");
    write(tmp.path / "base" / "components.json",
          R"({"button":{"normal":{"radius":30,"fill":"#333333"},"hover":{"fill":"#444444"}}})");
    write(tmp.path / "child" / "manifest.json", manifest("child", R"(,"parent":"base")"));
    write(tmp.path / "child" / "tokens.json", R"({"color":{"accent":"#abcdef"}})");
    write(tmp.path / "child" / "components.json", R"({"button":{"hover":{"gloss":0.5}}})");
    ThemeLibrary library({{tmp.path, false}});
    const auto dark = library.resolve("child", Appearance::Dark);
    REQUIRE_FALSE(hasErrors(dark.diagnostics));
    CHECK(dark.chain == std::vector<std::string>{"base", "child"});
    CHECK(formatColor(colorOf(dark, "color.accent")) == "#abcdef"); // enfant
    CHECK(formatColor(colorOf(dark, "color.panel")) == "#222222");  // parent
    CHECK(numberOf(dark, "shape.radius") == 20);
    CHECK(numberOf(dark, "space.gap") == 10); // schéma
    const auto light = library.resolve("child", Appearance::Light);
    CHECK(light.appearance == Appearance::Light); // variantes héritées du parent
    CHECK(formatColor(colorOf(light, "color.panel")) == "#eeeeee");
    const auto& hover = light.styles.at("button").at("default").at("hover");
    CHECK(std::get<double>(hover.at("radius")) == 30);                  // normal du parent
    CHECK(formatColor(std::get<Color>(hover.at("fill"))) == "#444444"); // survol du parent
    CHECK(std::get<double>(hover.at("gloss")) == 0.5);                  // ajout de l'enfant
    CHECK_FALSE(light.styles.at("button").at("default").at("normal").contains("gloss"));
}

TEST_CASE("variant styles override default states", "[theme]") {
    TempDir tmp;
    write(tmp.path / "v" / "manifest.json", manifest("var"));
    write(tmp.path / "v" / "components.json",
          R"({"button":{"fill":"#010101","hover":{"fill":"#020202","lift":2},
              "emphasized":{"fill":"#030303","pressed":{"scale":0.9}}}})");
    ThemeLibrary library({{tmp.path, false}});
    const auto t = library.resolve("var", Appearance::Dark);
    REQUIRE_FALSE(hasErrors(t.diagnostics));
    const auto& button = t.styles.at("button");
    CHECK(formatColor(std::get<Color>(button.at("default").at("normal").at("fill"))) == "#010101");
    CHECK(formatColor(std::get<Color>(button.at("emphasized").at("hover").at("fill"))) ==
          "#030303");
    CHECK(std::get<double>(button.at("emphasized").at("hover").at("lift")) == 2);
    CHECK(std::get<double>(button.at("emphasized").at("pressed").at("scale")) == 0.9);
    CHECK(button.at("quiet").at("hover").contains("lift")); // variante sans surcharge = défaut
}

TEST_CASE("inheritance cycles and missing parents are errors", "[theme]") {
    TempDir tmp;
    write(tmp.path / "a" / "manifest.json", manifest("aa", R"(,"parent":"bb")"));
    write(tmp.path / "b" / "manifest.json", manifest("bb", R"(,"parent":"aa")"));
    write(tmp.path / "c" / "manifest.json", manifest("cc", R"(,"parent":"missing")"));
    ThemeLibrary library({{tmp.path, false}});
    const auto cycle = library.resolve("aa", Appearance::Dark);
    CHECK(hasErrors(cycle.diagnostics));
    CHECK(mentions(cycle.diagnostics, "circulaire"));
    CHECK(cycle.tokens.size() == tokenSchema().size()); // toujours complet (repli)
    const auto orphan = library.resolve("cc", Appearance::Dark);
    CHECK(mentions(orphan.diagnostics, "parent introuvable"));
    CHECK(hasErrors(library.resolve("nope", Appearance::Dark).diagnostics));
}

TEST_CASE("token references resolve and broken values fall back", "[theme]") {
    TempDir tmp;
    write(tmp.path / "r" / "manifest.json", manifest("ref"));
    write(tmp.path / "r" / "tokens.json", R"({
        "color":{"accent":"#123456","info":"{color.accent}","error":"{color.warning}",
                 "warning":"{color.error}","success":"{shape.radius}","muted":"pink"},
        "shape":{"radius":500,"cardRadius":"{custom.big}"},
        "custom":{"big":30,"brand":"#ff00ff"},
        "nonsense":{"key":1}
    })");
    ThemeLibrary library({{tmp.path, false}});
    const auto t = library.resolve("ref", Appearance::Dark);
    CHECK_FALSE(hasErrors(t.diagnostics)); // valeurs fautives = avertissements, pas de refus
    const auto fallback = builtinFallback(Appearance::Dark);
    CHECK(colorOf(t, "color.info") == colorOf(t, "color.accent"));
    CHECK(mentions(t.diagnostics, "circulaire"));
    CHECK(colorOf(t, "color.success") == colorOf(fallback, "color.success")); // autre type
    CHECK(colorOf(t, "color.muted") == colorOf(fallback, "color.muted"));     // invalide
    CHECK(numberOf(t, "shape.radius") == 8);                                  // hors bornes
    CHECK(numberOf(t, "shape.cardRadius") == 30);                             // via custom.*
    CHECK(formatColor(std::get<Color>(t.tokens.at("custom.brand"))) == "#ff00ff");
    CHECK(mentions(t.diagnostics, "nonsense.key"));
}

TEST_CASE("relative paths stay inside the theme folder", "[theme][security]") {
    CHECK(sanitizeRelativePath("assets/icons/a.svg") == "assets/icons/a.svg");
    CHECK(sanitizeRelativePath("assets\\fonts\\b.ttf") == "assets/fonts/b.ttf");
    for (const char* bad :
         {"", "../x.png", "a/../../x.png", "/etc/passwd", "\\\\server\\share", "C:/Windows/x.png",
          "c:x.png", "a//b.png", "a/./b.png", "x.png:ads", "a\nb.png", "nul.", "dir /x.png"}) {
        INFO(bad);
        CHECK_FALSE(sanitizeRelativePath(bad));
    }
    TempDir tmp;
    write(tmp.path / "theme" / "assets" / "a.png", "x");
    write(tmp.path / "secret.png", "x");
    CHECK(resolveInside(tmp.path / "theme", "assets/a.png"));
    CHECK_FALSE(resolveInside(tmp.path / "theme", "../secret.png"));
    // Lien symbolique sortant du dossier (si le système permet d'en créer).
    std::error_code ec;
    fs::create_directory_symlink(tmp.path, tmp.path / "theme" / "escape", ec);
    if (!ec) {
        CHECK_FALSE(resolveInside(tmp.path / "theme", "escape/secret.png"));
    }
    CHECK(isAllowedThemeFile("a.PNG"));
    CHECK_FALSE(isAllowedThemeFile("evil.qml"));
    CHECK_FALSE(isAllowedThemeFile("evil.js"));
    CHECK_FALSE(isAllowedThemeFile("evil.exe"));
}

TEST_CASE("archive plans refuse dangerous entries", "[theme][security]") {
    const auto plan = [](std::vector<ArchiveEntry> entries) {
        return planArchiveExtraction(entries);
    };
    const auto good = plan({{"bubble/", 0, true},
                            {"bubble/manifest.json", 100},
                            {"bubble/tokens.json", 100},
                            {"bubble/assets/icons/save.svg", 100},
                            {"bubble/script.qml", 10},
                            {"bubble/run.exe", 10}});
    REQUIRE(good.ok());
    REQUIRE(good.items.size() == 3); // racine retirée, .qml et .exe ignorés
    CHECK(good.items[0].relative == "manifest.json");
    CHECK(good.items[2].relative == "assets/icons/save.svg");
    CHECK(mentions(good.diagnostics, "script.qml"));
    CHECK_FALSE(plan({{"manifest.json", 1}, {"../evil.json", 1}}).ok());
    CHECK_FALSE(plan({{"manifest.json", 1}, {"/abs.json", 1}}).ok());
    CHECK_FALSE(plan({{"manifest.json", 1}, {"C:\\evil.json", 1}}).ok());
    CHECK_FALSE(plan({{"manifest.json", 1}, {"link.json", 1, false, true}}).ok());
    CHECK_FALSE(plan({{"manifest.json", kMaxThemeFileBytes + 1}}).ok());
    CHECK_FALSE(plan({{"tokens.json", 1}}).ok()); // pas de manifeste
    std::vector<ArchiveEntry> many(kMaxArchiveEntries + 1, ArchiveEntry{"a.json", 1});
    CHECK_FALSE(plan(many).ok());
    std::vector<ArchiveEntry> bomb{{"manifest.json", 1}};
    for (int i = 0; i < 20; ++i) {
        bomb.push_back({"f" + std::to_string(i) + ".png", kMaxThemeFileBytes});
    }
    CHECK_FALSE(plan(bomb).ok()); // total décompressé > 64 Mo
}

TEST_CASE("import copies only allowed files and protects builtin themes", "[theme]") {
    TempDir tmp;
    const auto builtin = tmp.path / "builtin";
    const auto user = tmp.path / "user";
    write(builtin / "neo-studio" / "manifest.json", manifest("neo-studio"));
    write(tmp.path / "src" / "manifest.json", manifest("fresh", R"(,"parent":"neo-studio")"));
    write(tmp.path / "src" / "tokens.json", R"({"color":{"accent":"#aa3377"}})");
    write(tmp.path / "src" / "assets" / "icons" / "save.svg", "<svg/>");
    write(tmp.path / "src" / "hack.qml", "import QtQuick; Item {}");
    write(tmp.path / "src" / "tool.exe", "MZ");
    ThemeLibrary library({{builtin, true}, {user, false}});
    const auto result = library.importDirectory(tmp.path / "src", false);
    const std::string first = result.diagnostics.empty() ? "" : result.diagnostics.front().message;
    INFO(first);
    REQUIRE(result.ok);
    CHECK(fs::exists(user / "fresh" / "tokens.json"));
    CHECK(fs::exists(user / "fresh" / "assets" / "icons" / "save.svg"));
    CHECK_FALSE(fs::exists(user / "fresh" / "hack.qml"));
    CHECK_FALSE(fs::exists(user / "fresh" / "tool.exe"));
    CHECK(library.find("fresh"));
    CHECK(library.resolve("fresh", Appearance::Dark).iconFiles.contains("save"));
    // Réimport : refusé sans remplacement, accepté avec.
    CHECK_FALSE(library.importDirectory(tmp.path / "src", false).ok);
    CHECK(library.importDirectory(tmp.path / "src", true).ok);
    // Un thème livré ne peut pas être écrasé.
    write(tmp.path / "fake" / "manifest.json", manifest("neo-studio"));
    const auto fake = library.importDirectory(tmp.path / "fake", true);
    CHECK_FALSE(fake.ok);
    CHECK(mentions(fake.diagnostics, "livré"));
    // Thème cassé : rien n'est écrit.
    write(tmp.path / "broken" / "manifest.json", manifest("broken", R"(,"parent":"ghost")"));
    CHECK_FALSE(library.importDirectory(tmp.path / "broken", false).ok);
    CHECK_FALSE(fs::exists(user / "broken"));
    CHECK(library.remove("fresh"));
    CHECK_FALSE(library.remove("neo-studio"));
    CHECK_FALSE(library.find("fresh"));
}

TEST_CASE("missing fonts and icons degrade to fallbacks", "[theme]") {
    TempDir tmp;
    write(
        tmp.path / "f" / "manifest.json",
        manifest(
            "fonts",
            R"(,"fonts":["assets/fonts/missing.ttf","../outside.ttf","assets/fonts/x.exe"],"preview":"nope.png")"));
    write(
        tmp.path / "f" / "tokens.json",
        R"({"font":{"family":["Missing Sans","Segoe UI"]},"effect":{"deskTexture":"assets/none.png"}})");
    write(tmp.path / "f" / "icons.json",
          R"({"save":"M3 3 L17 17 Z","bad":"<script>alert(1)</script>","../x":"M0 0"})");
    write(tmp.path / "f" / "assets" / "icons" / "open.svg", "<svg/>");
    write(tmp.path / "f" / "assets" / "icons" / "evil.js", "x");
    ThemeLibrary library({{tmp.path, false}});
    const auto t = library.resolve("fonts", Appearance::Dark);
    CHECK_FALSE(hasErrors(t.diagnostics));
    CHECK(t.fonts.empty());
    CHECK(mentions(t.diagnostics, "police absente"));
    CHECK(mentions(t.diagnostics, "police refusée"));
    CHECK(std::get<std::string>(t.tokens.at("font.family")) == "Missing Sans, Segoe UI");
    CHECK(std::get<std::string>(t.tokens.at("effect.deskTexture")).empty());
    CHECK(mentions(t.diagnostics, "asset absent"));
    CHECK(t.iconPaths.contains("save"));
    CHECK_FALSE(t.iconPaths.contains("bad"));
    CHECK(t.iconFiles.contains("open"));
    CHECK_FALSE(t.iconFiles.contains("evil"));
    CHECK(t.preview.empty());
}

TEST_CASE("the manager keeps the previous theme when activation fails", "[theme]") {
    TempDir tmp;
    write(tmp.path / "neo" / "manifest.json",
          manifest("neo-studio", R"(,"variants":["dark","light"])"));
    write(tmp.path / "good" / "manifest.json", manifest("good"));
    write(tmp.path / "good" / "tokens.json", R"({"color":{"accent":"#00ff00"}})");
    write(tmp.path / "bad" / "manifest.json", manifest("bad", R"(,"parent":"ghost")"));
    ThemeManager manager(ThemeLibrary({{tmp.path, true}}));
    CHECK(manager.start("good", Appearance::Dark).ok);
    CHECK(manager.activeId() == "good");
    const auto before = manager.current().tokens;
    const auto outcome = manager.activate("bad", Appearance::Dark);
    CHECK_FALSE(outcome.ok);
    CHECK(mentions(outcome.diagnostics, "ghost"));
    CHECK(manager.activeId() == "good");
    CHECK(manager.current().tokens == before);
    CHECK(manager.setAppearance(Appearance::Light).ok);
    CHECK(manager.activeId() == "good");
    // Démarrage sur un thème préféré cassé : repli Neo Studio, signalé.
    ThemeManager second(ThemeLibrary({{tmp.path, true}}));
    CHECK_FALSE(second.start("bad", Appearance::Light).ok);
    CHECK(second.activeId() == "neo-studio");
    CHECK(second.current().appearance == Appearance::Light);
    // Aucun thème sur le disque : thème compilé, toujours utilisable.
    ThemeManager empty(ThemeLibrary({{tmp.path / "none", true}}));
    (void)empty.start("whatever", Appearance::Dark);
    CHECK(empty.activeId() == "neo-studio");
    CHECK(empty.current().tokens == builtinFallback(Appearance::Dark).tokens);
}

TEST_CASE("format 0 themes are migrated on read", "[theme]") {
    TempDir tmp;
    write(tmp.path / "old" / "manifest.json", R"({"schemaVersion":0,"id":"old","name":"Ancien"})");
    write(tmp.path / "old" / "tokens.json",
          R"({"accent":"#a1b2c3","radius":11,"bodySize":14,"unknownLegacy":1})");
    ThemeLibrary library({{tmp.path, false}});
    const auto t = library.resolve("old", Appearance::Dark);
    CHECK_FALSE(hasErrors(t.diagnostics));
    CHECK(formatColor(colorOf(t, "color.accent")) == "#a1b2c3");
    CHECK(numberOf(t, "shape.radius") == 11);
    CHECK(numberOf(t, "font.bodySize") == 14);
    CHECK(mentions(t.diagnostics, "migré"));
    CHECK(mentions(t.diagnostics, "unknownLegacy"));
}

TEST_CASE("the active theme id persists in the preferences", "[theme]") {
    Preferences p;
    CHECK(p.themeId == "neo-studio");
    p.themeId = "bubblegum";
    const auto back = preferencesFromText(preferencesToText(p));
    CHECK(back.themeId == "bubblegum");
    CHECK(preferencesFromText("themeId=../../evil\n").themeId == "neo-studio");
    TempDir tmp;
    REQUIRE(savePreferences(tmp.path / "preferences.txt", p));
    CHECK(loadPreferences(tmp.path / "preferences.txt").themeId == "bubblegum");
}

TEST_CASE("theme resolution is deterministic", "[theme]") {
    const auto library = builtinLibrary();
    const auto a = library.resolve("bubblegum", Appearance::Light);
    const auto b = library.resolve("bubblegum", Appearance::Light);
    CHECK(a.tokens == b.tokens);
    CHECK(a.styles == b.styles);
    CHECK(a.iconPaths == b.iconPaths);
    CHECK(a.fonts == b.fonts);
}

TEST_CASE("Theme.qml fallbacks match the compiled defaults", "[theme]") {
    std::ifstream in(fs::path(OPENSTITCH_STUDIO_QML_DIR) / "Theme.qml");
    std::stringstream text;
    text << in.rdbuf();
    const auto qml = text.str();
    REQUIRE_FALSE(qml.empty());
    const auto defaults = builtinFallback(Appearance::Dark);
    // tok("color.accent", "#ef91bb") / num("shape.radius", 8)
    const std::regex call(R"re((tok|num)\("([a-zA-Z.]+)", ("?)([^")]+)"?\))re");
    int checked = 0;
    for (auto it = std::sregex_iterator(qml.begin(), qml.end(), call); it != std::sregex_iterator();
         ++it) {
        const auto key = (*it)[2].str();
        const auto value = (*it)[4].str();
        INFO(key);
        REQUIRE(defaults.tokens.contains(key));
        const auto& expected = defaults.tokens.at(key);
        if (const auto* c = std::get_if<Color>(&expected)) {
            CHECK(formatColor(*c) == value);
        } else if (const auto* d = std::get_if<double>(&expected)) {
            CHECK(std::stod(value) == *d);
        } else {
            CHECK(std::get<std::string>(expected) == value);
        }
        ++checked;
    }
    CHECK(checked >= 40);
}
