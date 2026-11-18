// SPDX-License-Identifier: Apache-2.0
#pragma once

// Moteur de thèmes -- bibliothèque (thèmes installés) et gestionnaire d'activation, sans Qt
// (docs/desktop-v2/22-moteur-de-themes.md).
//
// Deux racines au plus : les thèmes livrés avec le Studio (lecture seule) et le dossier
// utilisateur où l'import copie les thèmes. Un thème utilisateur ne peut pas masquer un thème
// livré (même identifiant refusé). L'activation n'échoue jamais en laissant l'interface sans
// thème : en cas d'erreur, le thème courant reste en place, et au démarrage le repli final est
// Neo Studio, puis le thème compilé dans le binaire.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "theme_package.hpp"

namespace openstitch::desktop_v2::theme {

inline constexpr std::string_view kDefaultThemeId = "neo-studio";

struct ThemeRoot {
    std::filesystem::path dir;
    bool builtin{false};
};

// Un dossier de thème découvert, valide ou non (les refusés restent listés avec leurs erreurs,
// pour que l'interface explique pourquoi un thème n'apparaît pas activable).
struct ThemeEntry {
    std::filesystem::path dir;
    bool builtin{false};
    std::optional<ThemePackage> package;
    std::vector<Diagnostic> diagnostics;
    [[nodiscard]] bool loadable() const { return package.has_value(); }
    [[nodiscard]] std::string id() const;
};

class ThemeLibrary {
public:
    explicit ThemeLibrary(std::vector<ThemeRoot> roots = {});

    // Relit les racines : un sous-dossier contenant `manifest.json` par thème, ordre stable
    // (livrés d'abord, puis par nom de dossier).
    void rescan();
    [[nodiscard]] const std::vector<ThemeEntry>& entries() const { return entries_; }
    [[nodiscard]] const ThemePackage* find(std::string_view id) const;
    [[nodiscard]] const ThemeEntry* entry(std::string_view id) const;
    [[nodiscard]] ResolvedTheme resolve(std::string_view id, Appearance appearance) const;
    [[nodiscard]] PackageLookup lookup() const;
    [[nodiscard]] std::optional<std::filesystem::path> userRoot() const;

    struct ImportResult {
        bool ok{false};
        std::string id;
        std::vector<Diagnostic> diagnostics;
    };
    // Valide `source` (manifeste, chaîne d'héritage contre la bibliothèque, résolution), puis
    // copie les seuls fichiers autorisés vers `<racine utilisateur>/<id>`. Refus : thème livré de
    // même id, thème existant sans `replace`, erreurs bloquantes, trop de fichiers ou d'octets.
    // La copie passe par un dossier temporaire puis un renommage : jamais de thème à moitié écrit.
    [[nodiscard]] ImportResult importDirectory(const std::filesystem::path& source, bool replace);
    // Supprime un thème utilisateur (jamais un thème livré).
    [[nodiscard]] bool remove(std::string_view id);
    // Point de départ d'un nouveau thème (et du futur éditeur graphique) : un enfant vide de
    // `parentId` dans le dossier utilisateur, prêt à être surchargé jeton par jeton.
    [[nodiscard]] ImportResult createDerived(std::string_view parentId, std::string_view id,
                                             std::string_view name);

private:
    std::vector<ThemeRoot> roots_;
    std::vector<ThemeEntry> entries_;
};

// --- Archives (.zip) -------------------------------------------------------------------------
// L'extraction elle-même est faite par l'adaptateur Qt ; le cœur décide QUOI extraire et OÙ.
struct ArchiveEntry {
    std::string name; // nom brut dans l'archive
    std::uintmax_t size{0};
    bool directory{false};
    bool symlink{false};
};
struct ArchivePlan {
    struct Item {
        std::size_t index{0}; // dans la liste fournie
        std::string relative; // chemin sûr sous le dossier du thème
    };
    std::vector<Item> items;
    std::vector<Diagnostic> diagnostics;
    [[nodiscard]] bool ok() const { return !hasErrors(diagnostics); }
};
inline constexpr std::size_t kMaxArchiveEntries = 512;
inline constexpr std::uintmax_t kMaxArchiveBytes = 64u * 1024u * 1024u;
// Refus global : trop d'entrées, taille totale, nom dangereux (« .. », absolu, lecteur…), lien
// symbolique, pas de `manifest.json`. Un dossier racine unique (« bubblegum/… ») est retiré.
// Les fichiers d'extension non autorisée sont ignorés avec un avertissement.
[[nodiscard]] ArchivePlan planArchiveExtraction(const std::vector<ArchiveEntry>& entries);

// --- Activation ------------------------------------------------------------------------------
class ThemeManager {
public:
    explicit ThemeManager(ThemeLibrary library);

    [[nodiscard]] ThemeLibrary& library() { return library_; }
    [[nodiscard]] const ThemeLibrary& library() const { return library_; }
    [[nodiscard]] const ResolvedTheme& current() const { return current_; }
    [[nodiscard]] const std::string& activeId() const { return activeId_; }
    [[nodiscard]] Appearance appearance() const { return appearance_; }

    struct Outcome {
        bool ok{false};
        std::vector<Diagnostic> diagnostics; // thème demandé (même en cas de refus)
    };
    // Active `id` pour `appearance`. Échec (introuvable, manifeste, héritage) : le thème courant
    // est conservé tel quel et `ok` est faux.
    Outcome activate(std::string_view id, Appearance appearance);
    // Démarrage : `preferred`, sinon Neo Studio, sinon le thème compilé. Toujours un thème.
    Outcome start(std::string_view preferred, Appearance appearance);
    // Même thème, autre apparence (préférence système/claire/sombre).
    Outcome setAppearance(Appearance appearance);
    // Recharge depuis le disque (thème modifié en place) ; garde l'ancien si le nouveau échoue.
    Outcome reload();

private:
    ThemeLibrary library_;
    ResolvedTheme current_;
    std::string activeId_;
    Appearance appearance_{Appearance::Dark};
};

} // namespace openstitch::desktop_v2::theme
