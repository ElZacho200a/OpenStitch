// SPDX-License-Identifier: Apache-2.0
#pragma once

// Moteur de thèmes -- lecture d'un dossier de thème, validation, migration, héritage et
// résolution en valeurs finales (docs/desktop-v2/22-moteur-de-themes.md).
//
// Un dossier de thème contient `manifest.json` et, facultativement, `tokens.json`,
// `animations.json` (jetons `motion.*`), `components.json`, `icons.json` (tracés SVG nommés) et un
// dossier `assets/` (icônes, polices, textures). Rien n'y est exécuté : seules des données typées
// sont lues, toute valeur hors schéma est signalée puis remplacée par son repli, et tout chemin
// est confiné au dossier du thème.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "theme_schema.hpp"

namespace openstitch::desktop_v2::theme {

struct Diagnostic {
    enum class Severity { Warning, Error };
    Severity severity{Severity::Warning};
    std::string where;   // fichier ou clé (« tokens.json:color.accent »)
    std::string message; // en français, prêt à afficher
    bool operator==(const Diagnostic&) const = default;
};
[[nodiscard]] bool hasErrors(const std::vector<Diagnostic>& diagnostics);

struct ThemeManifest {
    int schemaVersion{kSchemaVersion};
    std::string id;
    std::string name;
    std::string description;
    std::string author;
    std::string version{"1.0.0"};
    std::string license;
    std::string parent; // vide = aucun ; sinon identifiant du thème dont on hérite
    int minEngine{1};
    int maxEngine{0};                 // 0 = pas de borne haute
    std::vector<Appearance> variants; // vide = hérité du parent (sinon sombre)
    std::optional<Appearance> defaultVariant;
    std::string preview;            // chemin relatif d'une image d'aperçu (facultatif)
    std::vector<std::string> fonts; // chemins relatifs de .ttf/.otf
    bool operator==(const ThemeManifest&) const = default;
};

// Identifiant : [a-z0-9][a-z0-9-]{1,47}.
[[nodiscard]] bool isValidThemeId(std::string_view id);

// Lit et valide `manifest.json`. Les erreurs bloquantes (JSON invalide, champ requis manquant,
// format plus récent, moteur incompatible) sont dans `diagnostics` et le résultat est nullopt.
[[nodiscard]] std::optional<ThemeManifest> parseManifest(std::string_view json,
                                                         std::vector<Diagnostic>& diagnostics);
[[nodiscard]] std::string manifestToJson(const ThemeManifest& manifest);

// --- Chemins sûrs ----------------------------------------------------------------------------
// Chemin relatif d'un fichier de thème ou d'une entrée d'archive, normalisé en « a/b/c ».
// Refusés : vide, absolu, lettre de lecteur, « .. », « : » (flux NTFS), caractères de contrôle,
// segments vides, plus de 200 caractères. Les « \ » sont lus comme des « / ».
[[nodiscard]] std::optional<std::string> sanitizeRelativePath(std::string_view path);
// Résout `relative` sous `root` puis vérifie que le chemin final (liens résolus) reste dans
// `root`. nullopt si refusé ou hors du dossier.
[[nodiscard]] std::optional<std::filesystem::path> resolveInside(const std::filesystem::path& root,
                                                                 std::string_view relative);

enum class AssetKind { Font, Icon, Image, Data };
// Extensions autorisées : polices .ttf .otf ; icônes .svg .png ; images .png .jpg .jpeg .webp
// .svg ; données .json .txt .md. Tout le reste (.qml, .js, .exe…) est refusé.
[[nodiscard]] bool isAllowedExtension(AssetKind kind, const std::filesystem::path& path);
[[nodiscard]] bool isAllowedThemeFile(const std::filesystem::path& path);
inline constexpr std::uintmax_t kMaxThemeFileBytes = 8u * 1024u * 1024u;
inline constexpr std::uintmax_t kMaxJsonBytes = 1024u * 1024u;

// --- Contenu brut d'un thème -----------------------------------------------------------------
using RawScalar = std::variant<double, bool, std::string>;

struct ThemeLayer {
    std::map<std::string, RawScalar> tokens;                         // « color.accent » -> …
    std::map<Appearance, std::map<std::string, RawScalar>> variants; // surcharges par apparence
    // « composant/variante/état » -> propriété -> valeur. Variante « default » si absente.
    std::map<std::string, std::map<std::string, RawScalar>> styles;
    std::map<std::string, std::string> iconPaths;           // nom -> tracé SVG (icons.json)
    std::map<std::string, std::filesystem::path> iconFiles; // nom -> assets/icons/<nom>.svg|png
};

struct ThemePackage {
    std::filesystem::path dir;
    ThemeManifest manifest;
    ThemeLayer layer;
    bool builtin{false};
    std::vector<Diagnostic> diagnostics;
};

// Charge un dossier : manifeste (bloquant) puis fichiers de jetons, composants, icônes
// (non bloquants : chaque défaut est un avertissement et la valeur est ignorée).
[[nodiscard]] std::optional<ThemePackage>
loadPackage(const std::filesystem::path& dir, bool builtin, std::vector<Diagnostic>& diagnostics);

// Migration des jetons d'un format antérieur vers kSchemaVersion. Format 0 : clés plates de
// l'ancien Theme.qml (« accent », « radius », « bodySize »…).
void migrateTokens(int fromVersion, std::map<std::string, RawScalar>& tokens,
                   std::vector<Diagnostic>& diagnostics);

// --- Thème résolu ----------------------------------------------------------------------------
struct ResolvedTheme {
    ThemeManifest manifest;         // celui du thème demandé (feuille de la chaîne)
    std::vector<std::string> chain; // identifiants de la racine à la feuille
    Appearance appearance{Appearance::Dark};
    std::map<std::string, TokenValue> tokens; // tous les jetons du schéma + « custom.* »
    // composant -> variante (« default », « emphasized »…) -> état -> propriété -> valeur. Les
    // états sont déjà fusionnés (normal ⊂ état ; variante ⊂ défaut). Clairsemé : une propriété
    // absente garde le style intégré du composant QML.
    std::map<std::string,
             std::map<std::string, std::map<std::string, std::map<std::string, TokenValue>>>>
        styles;
    std::map<std::string, std::string> iconPaths;
    std::map<std::string, std::filesystem::path> iconFiles;
    std::vector<std::filesystem::path> fonts; // fichiers existants, racine d'abord
    std::filesystem::path preview;            // vide si absent
    std::vector<Diagnostic> diagnostics;
};

// Thème intégré au binaire (Neo Studio sans aucun fichier) : toujours disponible.
[[nodiscard]] ResolvedTheme builtinFallback(Appearance appearance);

// Trouve un paquet par identifiant (la bibliothèque de thèmes fournit cette fonction).
using PackageLookup = std::function<const ThemePackage*(std::string_view id)>;

inline constexpr int kMaxInheritanceDepth = 8;

// Résout `id` : chaîne d'héritage (cycle, parent absent, profondeur > 8 = erreur), choix de
// l'apparence (celle demandée si le thème la propose, sinon sa variante par défaut), fusion
// racine -> feuille, références « {famille.nom} », typage et bornes, chemins d'assets. Le
// résultat est toujours complet ; `hasErrors(result.diagnostics)` dit s'il est activable.
[[nodiscard]] ResolvedTheme resolveTheme(std::string_view id, Appearance requested,
                                         const PackageLookup& lookup);

// Contrôle de lisibilité (WCAG) des paires texte / fond essentielles : avertissements seulement.
[[nodiscard]] std::vector<Diagnostic> checkContrast(const ResolvedTheme& theme);

} // namespace openstitch::desktop_v2::theme
