// SPDX-License-Identifier: Apache-2.0
#include "theme_library.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace openstitch::desktop_v2::theme {

namespace {

using Severity = Diagnostic::Severity;

constexpr std::size_t kMaxImportFiles = 512;
constexpr std::uintmax_t kMaxImportBytes = 64u * 1024u * 1024u;

void warn(std::vector<Diagnostic>& out, std::string where, std::string message) {
    out.push_back({Severity::Warning, std::move(where), std::move(message)});
}

void fail(std::vector<Diagnostic>& out, std::string where, std::string message) {
    out.push_back({Severity::Error, std::move(where), std::move(message)});
}

std::string lowerExtension(std::string_view name) {
    const auto dot = name.rfind('.');
    std::string ext(dot == std::string_view::npos ? std::string_view{} : name.substr(dot));
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

} // namespace

std::string ThemeEntry::id() const {
    return package ? package->manifest.id : dir.filename().string();
}

ThemeLibrary::ThemeLibrary(std::vector<ThemeRoot> roots) : roots_(std::move(roots)) {
    rescan();
}

void ThemeLibrary::rescan() {
    entries_.clear();
    // Livrés d'abord : un thème utilisateur ne peut pas en masquer un.
    std::vector<const ThemeRoot*> ordered;
    for (const auto& root : roots_) {
        if (root.builtin) {
            ordered.push_back(&root);
        }
    }
    for (const auto& root : roots_) {
        if (!root.builtin) {
            ordered.push_back(&root);
        }
    }
    for (const auto* root : ordered) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root->dir, ec)) {
            continue;
        }
        std::vector<std::filesystem::path> dirs;
        for (const auto& item : std::filesystem::directory_iterator(root->dir, ec)) {
            const auto name = item.path().filename().string();
            if (item.is_directory(ec) && !item.is_symlink(ec) && !name.starts_with(".") &&
                std::filesystem::is_regular_file(item.path() / "manifest.json", ec)) {
                dirs.push_back(item.path());
            }
        }
        std::sort(dirs.begin(), dirs.end());
        for (const auto& dir : dirs) {
            ThemeEntry entry;
            entry.dir = dir;
            entry.builtin = root->builtin;
            entry.package = loadPackage(dir, root->builtin, entry.diagnostics);
            if (entry.package && find(entry.package->manifest.id)) {
                fail(entry.diagnostics, "manifest.json:id",
                     "identifiant déjà utilisé par un autre thème : « " +
                         entry.package->manifest.id + " »");
                entry.package.reset();
            }
            entries_.push_back(std::move(entry));
        }
    }
}

const ThemeEntry* ThemeLibrary::entry(std::string_view id) const {
    for (const auto& e : entries_) {
        if (e.package && e.package->manifest.id == id) {
            return &e;
        }
    }
    return nullptr;
}

const ThemePackage* ThemeLibrary::find(std::string_view id) const {
    const auto* e = entry(id);
    return e ? &*e->package : nullptr;
}

PackageLookup ThemeLibrary::lookup() const {
    return [this](std::string_view id) { return find(id); };
}

ResolvedTheme ThemeLibrary::resolve(std::string_view id, Appearance appearance) const {
    return resolveTheme(id, appearance, lookup());
}

std::optional<std::filesystem::path> ThemeLibrary::userRoot() const {
    for (const auto& root : roots_) {
        if (!root.builtin) {
            return root.dir;
        }
    }
    return std::nullopt;
}

ThemeLibrary::ImportResult ThemeLibrary::importDirectory(const std::filesystem::path& source,
                                                         bool replace) {
    ImportResult result;
    auto& diagnostics = result.diagnostics;
    const auto target = userRoot();
    if (!target) {
        fail(diagnostics, "import", "aucun dossier de thèmes utilisateur");
        return result;
    }
    auto candidate = loadPackage(source, false, diagnostics);
    if (!candidate) {
        return result;
    }
    result.id = candidate->manifest.id;
    if (const auto* existing = entry(result.id)) {
        if (existing->builtin) {
            fail(diagnostics, "manifest.json:id",
                 "« " + result.id + " » est un thème livré avec le Studio : changez l'identifiant");
            return result;
        }
        if (!replace) {
            fail(diagnostics, "manifest.json:id",
                 "un thème « " + result.id + " » est déjà installé");
            return result;
        }
    }
    // Validation complète contre la bibliothèque (parents) avant toute écriture.
    const auto resolved = resolveTheme(result.id, Appearance::Dark, [&](std::string_view id) {
        return id == result.id ? &*candidate : find(id);
    });
    for (const auto& d : resolved.diagnostics) {
        if (std::find(diagnostics.begin(), diagnostics.end(), d) == diagnostics.end()) {
            diagnostics.push_back(d);
        }
    }
    if (hasErrors(diagnostics)) {
        return result;
    }

    // Inventaire : fichiers ordinaires autorisés, aucun lien, bornes de nombre et de taille.
    std::error_code ec;
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> files;
    std::uintmax_t total = 0;
    const auto root = std::filesystem::weakly_canonical(source, ec);
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        const auto& item = *it;
        const auto rel = std::filesystem::relative(item.path(), root, ec).generic_string();
        if (item.is_symlink(ec)) {
            warn(diagnostics, rel, "lien symbolique ignoré");
            if (item.is_directory(ec)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (item.is_directory(ec)) {
            continue;
        }
        if (!sanitizeRelativePath(rel) || !isAllowedThemeFile(item.path())) {
            warn(diagnostics, rel, "fichier ignoré (type non autorisé dans un thème)");
            continue;
        }
        const auto size = item.file_size(ec);
        if (size > kMaxThemeFileBytes) {
            fail(diagnostics, rel, "fichier trop volumineux");
            return result;
        }
        total += size;
        files.emplace_back(item.path(), std::filesystem::u8path(rel));
        if (files.size() > kMaxImportFiles || total > kMaxImportBytes) {
            fail(diagnostics, "import", "thème trop volumineux (fichiers ou octets)");
            return result;
        }
    }
    if (ec) {
        fail(diagnostics, "import", "lecture du dossier impossible : " + ec.message());
        return result;
    }

    const auto staging = *target / ("." + result.id + ".import");
    std::filesystem::remove_all(staging, ec);
    std::filesystem::create_directories(staging, ec);
    for (const auto& [from, rel] : files) {
        std::filesystem::create_directories((staging / rel).parent_path(), ec);
        std::filesystem::copy_file(from, staging / rel,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            fail(diagnostics, rel.generic_string(), "copie impossible : " + ec.message());
            std::filesystem::remove_all(staging, ec);
            return result;
        }
    }
    const auto final = *target / result.id;
    std::filesystem::remove_all(final, ec);
    std::filesystem::rename(staging, final, ec);
    if (ec) {
        fail(diagnostics, "import", "installation impossible : " + ec.message());
        std::filesystem::remove_all(staging, ec);
        return result;
    }
    rescan();
    result.ok = find(result.id) != nullptr;
    return result;
}

bool ThemeLibrary::remove(std::string_view id) {
    const auto* e = entry(id);
    if (!e || e->builtin) {
        return false;
    }
    std::error_code ec;
    std::filesystem::remove_all(e->dir, ec);
    rescan();
    return !ec;
}

ThemeLibrary::ImportResult ThemeLibrary::createDerived(std::string_view parentId,
                                                       std::string_view id, std::string_view name) {
    ImportResult result;
    result.id = std::string(id);
    const auto target = userRoot();
    if (!target) {
        fail(result.diagnostics, "création", "aucun dossier de thèmes utilisateur");
        return result;
    }
    if (!isValidThemeId(id)) {
        fail(result.diagnostics, "id", "identifiant invalide : minuscules, chiffres et tirets");
        return result;
    }
    if (entry(id)) {
        fail(result.diagnostics, "id", "un thème « " + std::string(id) + " » existe déjà");
        return result;
    }
    if (!find(parentId)) {
        fail(result.diagnostics, "parent",
             "thème parent introuvable : « " + std::string(parentId) + " »");
        return result;
    }
    ThemeManifest manifest;
    manifest.id = std::string(id);
    manifest.name = name.empty() ? std::string(id) : std::string(name).substr(0, 64);
    manifest.parent = std::string(parentId);
    manifest.description = "Dérivé de « " + find(parentId)->manifest.name + " ».";
    const auto dir = *target / std::string(id);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto put = [&](const char* file, const std::string& text) {
        std::ofstream out(dir / file, std::ios::binary | std::ios::trunc);
        out << text;
        return static_cast<bool>(out);
    };
    if (ec || !put("manifest.json", manifestToJson(manifest)) ||
        !put("tokens.json",
             "{\n  \"$comment\": \"Ajoutez ici les jetons qui diffèrent du "
             "parent, par ex. \\\"color\\\": { \\\"accent\\\": \\\"#2a7fff\\\" }.\"\n}\n") ||
        !put("components.json", "{\n}\n")) {
        fail(result.diagnostics, "création", "écriture impossible dans " + dir.string());
        return result;
    }
    rescan();
    result.ok = find(id) != nullptr;
    return result;
}

ArchivePlan planArchiveExtraction(const std::vector<ArchiveEntry>& entries) {
    ArchivePlan plan;
    auto& diagnostics = plan.diagnostics;
    if (entries.size() > kMaxArchiveEntries) {
        fail(diagnostics, "archive", "trop d'entrées (" + std::to_string(entries.size()) + ")");
        return plan;
    }
    std::uintmax_t total = 0;
    std::vector<std::pair<std::size_t, std::string>> clean;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (e.symlink) {
            fail(diagnostics, e.name, "lien symbolique refusé dans une archive de thème");
            return plan;
        }
        auto name = e.name;
        while (e.directory && !name.empty() && (name.back() == '/' || name.back() == '\\')) {
            name.pop_back();
        }
        const auto safe = sanitizeRelativePath(name);
        if (!safe) {
            fail(diagnostics, e.name, "nom d'entrée dangereux refusé");
            return plan;
        }
        if (e.directory) {
            continue;
        }
        if (e.size > kMaxThemeFileBytes) {
            fail(diagnostics, e.name, "fichier trop volumineux");
            return plan;
        }
        total += e.size;
        if (total > kMaxArchiveBytes) {
            fail(diagnostics, "archive", "contenu décompressé trop volumineux");
            return plan;
        }
        clean.emplace_back(i, *safe);
    }
    // Dossier racine unique : « bubblegum/manifest.json » -> « manifest.json ».
    std::string prefix;
    const bool rootManifest = std::any_of(
        clean.begin(), clean.end(), [](const auto& c) { return c.second == "manifest.json"; });
    if (!rootManifest && !clean.empty()) {
        const auto first = clean.front().second.substr(0, clean.front().second.find('/') + 1);
        const bool shared = first.size() > 1 && first.back() == '/' &&
                            std::all_of(clean.begin(), clean.end(),
                                        [&](const auto& c) { return c.second.starts_with(first); });
        if (shared) {
            prefix = first;
        }
    }
    bool manifest = false;
    for (const auto& [index, path] : clean) {
        const auto relative = path.substr(prefix.size());
        if (relative == "manifest.json") {
            manifest = true;
        }
        const auto ext = lowerExtension(relative);
        if (!isAllowedThemeFile(std::filesystem::path("x" + ext))) {
            warn(diagnostics, entries[index].name, "fichier ignoré (type non autorisé)");
            continue;
        }
        plan.items.push_back({index, relative});
    }
    if (!manifest) {
        fail(diagnostics, "archive", "manifest.json introuvable à la racine du thème");
    }
    return plan;
}

ThemeManager::ThemeManager(ThemeLibrary library)
    : library_(std::move(library)), current_(builtinFallback(Appearance::Dark)),
      activeId_(kDefaultThemeId) {}

ThemeManager::Outcome ThemeManager::activate(std::string_view id, Appearance appearance) {
    Outcome outcome;
    auto resolved = library_.resolve(id, appearance);
    outcome.diagnostics = resolved.diagnostics;
    if (hasErrors(resolved.diagnostics)) {
        return outcome; // le thème courant reste en place
    }
    current_ = std::move(resolved);
    activeId_ = std::string(id);
    appearance_ = appearance;
    outcome.ok = true;
    return outcome;
}

ThemeManager::Outcome ThemeManager::start(std::string_view preferred, Appearance appearance) {
    appearance_ = appearance;
    if (!preferred.empty()) {
        auto outcome = activate(preferred, appearance);
        if (outcome.ok) {
            return outcome;
        }
        if (preferred != kDefaultThemeId) {
            auto fallback = activate(kDefaultThemeId, appearance);
            if (fallback.ok) {
                fallback.ok = false; // signale que le thème préféré a été écarté
                for (auto& d : fallback.diagnostics) {
                    outcome.diagnostics.push_back(std::move(d));
                }
                return outcome;
            }
        }
        current_ = builtinFallback(appearance);
        activeId_ = std::string(kDefaultThemeId);
        return outcome;
    }
    auto outcome = activate(kDefaultThemeId, appearance);
    if (!outcome.ok) {
        current_ = builtinFallback(appearance);
        activeId_ = std::string(kDefaultThemeId);
    }
    return outcome;
}

ThemeManager::Outcome ThemeManager::setAppearance(Appearance appearance) {
    if (!library_.find(activeId_)) {
        appearance_ = appearance;
        current_ = builtinFallback(appearance);
        return {true, {}};
    }
    return activate(activeId_, appearance);
}

ThemeManager::Outcome ThemeManager::reload() {
    library_.rescan();
    const auto id = activeId_;
    return activate(id, appearance_);
}

} // namespace openstitch::desktop_v2::theme
