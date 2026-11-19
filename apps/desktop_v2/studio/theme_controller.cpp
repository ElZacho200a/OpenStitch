// SPDX-License-Identifier: Apache-2.0
#include "theme_controller.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

#include <algorithm>
#include <string_view>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "preferences_controller.hpp"

#ifndef OPENSTITCH_THEMES_SOURCE_DIR
#define OPENSTITCH_THEMES_SOURCE_DIR ""
#endif

namespace openstitch::desktop_v2::studio {

namespace {

using theme::Appearance;
using theme::Diagnostic;
using theme::TokenValue;

QString fromUtf8(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString fileUrl(const std::filesystem::path& path) {
    return path.empty() ? QString{}
                        : QUrl::fromLocalFile(QString::fromStdWString(path.wstring())).toString();
}

std::filesystem::path localPath(const QUrl& url) {
    const auto local = url.isLocalFile() ? url.toLocalFile() : url.toString();
    return std::filesystem::path(local.toStdWString());
}

QColor toQColor(theme::Color c) {
    return QColor(c.r, c.g, c.b, c.a);
}

QVariant toVariant(const TokenValue& value, bool asset) {
    if (const auto* c = std::get_if<theme::Color>(&value)) {
        return toQColor(*c);
    }
    if (const auto* d = std::get_if<double>(&value)) {
        return *d;
    }
    const auto& text = std::get<std::string>(value);
    if (asset) {
        return fileUrl(std::filesystem::path(std::u8string(text.begin(), text.end())));
    }
    return fromUtf8(text);
}

QVariantList toList(const std::vector<Diagnostic>& diagnostics) {
    QVariantList list;
    for (const auto& d : diagnostics) {
        list.append(
            QVariantMap{{QStringLiteral("severity"), d.severity == Diagnostic::Severity::Error
                                                         ? QStringLiteral("error")
                                                         : QStringLiteral("warning")},
                        {QStringLiteral("where"), fromUtf8(d.where)},
                        {QStringLiteral("message"), fromUtf8(d.message)}});
    }
    return list;
}

QStringList messages(const std::vector<Diagnostic>& diagnostics) {
    QStringList out;
    for (const auto& d : diagnostics) {
        out << (d.severity == Diagnostic::Severity::Error ? QStringLiteral("Erreur : ")
                                                          : QStringLiteral("Avertissement : ")) +
                   fromUtf8(d.where) + QStringLiteral(" — ") + fromUtf8(d.message);
    }
    return out;
}

QString firstError(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& d : diagnostics) {
        if (d.severity == Diagnostic::Severity::Error) {
            return fromUtf8(d.message);
        }
    }
    return {};
}

} // namespace

ThemeController::ThemeController(std::filesystem::path builtinDir, std::filesystem::path userDir,
                                 PreferencesController* prefs, QObject* parent)
    : QObject(parent), prefs_(prefs) {
    std::error_code ec;
    std::filesystem::create_directories(userDir, ec);
    manager_ = std::make_unique<theme::ThemeManager>(
        theme::ThemeLibrary({{std::move(builtinDir), true}, {std::move(userDir), false}}));
    prefsThemeId_ = prefs_ ? prefs_->themeId() : QString{};
    const auto preferred = prefsThemeId_.toStdString();
    const auto outcome = manager_->start(preferred, requestedAppearance());
    if (!outcome.ok && !preferred.empty()) {
        setError(QStringLiteral("Thème « %1 » écarté au démarrage : %2")
                     .arg(fromUtf8(preferred), firstError(outcome.diagnostics)));
    }
    reloadTimer_.setSingleShot(true);
    reloadTimer_.setInterval(250);
    connect(&reloadTimer_, &QTimer::timeout, this, &ThemeController::reload);
    // Rechargement à chaud : un fichier du thème actif modifié sur le disque.
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this] { reloadTimer_.start(); });
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this,
            [this] { reloadTimer_.start(); });
    if (prefs_) {
        connect(prefs_, &PreferencesController::changed, this, &ThemeController::followPreferences);
    }
    if (auto* hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, &ThemeController::followPreferences);
    }
    refreshLibrary();
    apply();
}

ThemeController::~ThemeController() = default;

std::filesystem::path ThemeController::defaultBuiltinDir() {
    const auto deployed =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("themes"));
    if (QFileInfo(deployed).isDir()) {
        return std::filesystem::path(deployed.toStdWString());
    }
    const std::string_view dir{OPENSTITCH_THEMES_SOURCE_DIR};
    return std::filesystem::path(std::u8string(dir.begin(), dir.end()));
}

QString ThemeController::activeId() const {
    return fromUtf8(manager_->activeId());
}

QString ThemeController::userThemesDir() const {
    const auto root = manager_->library().userRoot();
    return root ? QString::fromStdWString(root->wstring()) : QString{};
}

bool ThemeController::systemReducedMotion() const {
#ifdef Q_OS_WIN
    // « Afficher les animations dans Windows » (Accessibilité > Effets visuels).
    BOOL animations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0)) {
        return animations == FALSE;
    }
#endif
    return false;
}

Appearance ThemeController::requestedAppearance() const {
    if (prefs_) {
        return prefs_->effectiveTheme() == QLatin1String("light") ? Appearance::Light
                                                                  : Appearance::Dark;
    }
    return Appearance::Dark;
}

void ThemeController::followPreferences() {
    const auto wanted = requestedAppearance();
    const bool idChanged = prefs_ && prefs_->themeId() != prefsThemeId_;
    if (wanted == manager_->appearance() && !idChanged) {
        return;
    }
    if (idChanged) {
        // Préférences modifiées ailleurs (bouton « Valeurs par défaut ») : on suit.
        prefsThemeId_ = prefs_->themeId();
        if (!manager_->activate(prefsThemeId_.toStdString(), wanted).ok) {
            (void)manager_->activate(std::string(theme::kDefaultThemeId), wanted);
        }
    } else {
        (void)manager_->setAppearance(wanted);
    }
    apply();
}

void ThemeController::setError(const QString& message) {
    if (message == lastError_) {
        return;
    }
    lastError_ = message;
    emit lastErrorChanged();
}

QString ThemeController::resolveFamily(const std::string& list) const {
    const auto families = QFontDatabase::families();
    const auto candidates = fromUtf8(list).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (auto candidate : candidates) {
        candidate = candidate.trimmed();
        if (candidate == QLatin1String("sans-serif") || candidate == QLatin1String("serif") ||
            candidate == QLatin1String("monospace")) {
            return candidate; // famille générique : Qt choisit la police système
        }
        for (const auto& family : families) {
            if (family.compare(candidate, Qt::CaseInsensitive) == 0) {
                return family;
            }
        }
    }
    return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
}

void ThemeController::apply() {
    const auto& resolved = manager_->current();
    // Polices embarquées : enregistrées une fois par session (jamais retirées : un composant
    // peut encore les référencer pendant la transition).
    for (const auto& font : resolved.fonts) {
        const auto key = font.string();
        if (!loadedFonts_.contains(key)) {
            loadedFonts_[key] =
                QFontDatabase::addApplicationFont(QString::fromStdWString(font.wstring()));
        }
    }
    QVariantMap tokens;
    for (const auto& [key, value] : resolved.tokens) {
        const auto* spec = theme::findToken(key);
        tokens.insert(fromUtf8(key),
                      toVariant(value, spec && spec->type == theme::TokenType::Asset));
    }
    QVariantMap styles;
    for (const auto& [component, variants] : resolved.styles) {
        QVariantMap byVariant;
        for (const auto& [variant, states] : variants) {
            QVariantMap byState;
            for (const auto& [state, props] : states) {
                QVariantMap map;
                for (const auto& [prop, value] : props) {
                    const auto* spec = theme::findStyleProperty(prop);
                    map.insert(fromUtf8(prop),
                               toVariant(value, spec && spec->type == theme::TokenType::Asset));
                }
                byState.insert(fromUtf8(state), map);
            }
            byVariant.insert(fromUtf8(variant), byState);
        }
        styles.insert(fromUtf8(component), byVariant);
    }
    QVariantMap icons;
    for (const auto& [name, path] : resolved.iconPaths) {
        icons.insert(fromUtf8(name), QVariantMap{{QStringLiteral("path"), fromUtf8(path)}});
    }
    for (const auto& [name, file] : resolved.iconFiles) {
        icons.insert(fromUtf8(name), QVariantMap{{QStringLiteral("source"), fileUrl(file)}});
    }
    const auto family = resolveFamily(std::get<std::string>(resolved.tokens.at("font.family")));
    const auto heading =
        resolveFamily(std::get<std::string>(resolved.tokens.at("font.headingFamily")));
    QVariantMap meta{
        {QStringLiteral("id"), fromUtf8(manager_->activeId())},
        {QStringLiteral("name"), fromUtf8(resolved.manifest.name)},
        {QStringLiteral("appearance"),
         fromUtf8(std::string(theme::appearanceId(resolved.appearance)))},
        {QStringLiteral("fontFamily"), family},
        {QStringLiteral("headingFamily"), heading},
        {QStringLiteral("deskTexture"), toVariant(resolved.tokens.at("effect.deskTexture"), true)},
    };
    tokens_ = std::move(tokens);
    styles_ = std::move(styles);
    icons_ = std::move(icons);
    meta_ = std::move(meta);
    diagnostics_ = toList(resolved.diagnostics);
    // Police par défaut des contrôles non habillés : celle du thème.
    QFont base = QGuiApplication::font();
    if (base.family() != family) {
        base.setFamily(family);
        QGuiApplication::setFont(base);
    }
    ++revision_;
    watchActive();
    emit themeChanged();
}

void ThemeController::watchActive() {
    if (!watcher_.files().isEmpty()) {
        watcher_.removePaths(watcher_.files());
    }
    if (!watcher_.directories().isEmpty()) {
        watcher_.removePaths(watcher_.directories());
    }
    const auto* entry = manager_->library().entry(manager_->activeId());
    if (!entry) {
        return;
    }
    const auto dir = QString::fromStdWString(entry->dir.wstring());
    QStringList paths{dir};
    for (const char* file :
         {"manifest.json", "tokens.json", "animations.json", "components.json", "icons.json"}) {
        const auto path = QDir(dir).filePath(QLatin1String(file));
        if (QFileInfo::exists(path)) {
            paths << path;
        }
    }
    watcher_.addPaths(paths);
}

void ThemeController::refreshLibrary() {
    const auto& library = manager_->library();
    QVariantList list;
    for (const auto& entry : library.entries()) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), fromUtf8(entry.id()));
        item.insert(QStringLiteral("builtin"), entry.builtin);
        item.insert(QStringLiteral("loadable"), entry.loadable());
        item.insert(QStringLiteral("dir"), QString::fromStdWString(entry.dir.wstring()));
        std::vector<Diagnostic> diagnostics = entry.diagnostics;
        if (entry.package) {
            const auto& m = entry.package->manifest;
            item.insert(QStringLiteral("name"), fromUtf8(m.name));
            item.insert(QStringLiteral("description"), fromUtf8(m.description));
            item.insert(QStringLiteral("author"), fromUtf8(m.author));
            item.insert(QStringLiteral("version"), fromUtf8(m.version));
            item.insert(QStringLiteral("parent"), fromUtf8(m.parent));
            // Aperçu : quelques couleurs du thème résolu (mini-maquette dessinée en QML) et
            // l'image d'aperçu si le thème en fournit une.
            const auto resolved = library.resolve(m.id, requestedAppearance());
            for (const auto& d : resolved.diagnostics) {
                if (std::find(diagnostics.begin(), diagnostics.end(), d) == diagnostics.end()) {
                    diagnostics.push_back(d);
                }
            }
            QVariantMap swatches;
            for (const char* key : {"color.background", "color.panel", "color.raised",
                                    "color.accent", "color.accentInk", "color.foreground",
                                    "color.muted", "color.secondaryAccent", "color.info"}) {
                swatches.insert(QString::fromLatin1(key).mid(6),
                                toVariant(resolved.tokens.at(key), false));
            }
            swatches.insert(QStringLiteral("radius"),
                            toVariant(resolved.tokens.at("shape.radius"), false));
            item.insert(QStringLiteral("swatches"), swatches);
            item.insert(QStringLiteral("preview"), fileUrl(resolved.preview));
            QStringList variants;
            for (const auto v : m.variants) {
                variants << fromUtf8(std::string(theme::appearanceId(v)));
            }
            item.insert(QStringLiteral("variants"), variants);
        } else {
            item.insert(QStringLiteral("name"), fromUtf8(entry.dir.filename().string()));
        }
        int errors = 0;
        int warnings = 0;
        for (const auto& d : diagnostics) {
            (d.severity == Diagnostic::Severity::Error ? errors : warnings) += 1;
        }
        item.insert(QStringLiteral("errorCount"), errors);
        item.insert(QStringLiteral("warningCount"), warnings);
        item.insert(QStringLiteral("messages"), messages(diagnostics));
        list.append(item);
    }
    themes_ = std::move(list);
    emit libraryChanged();
}

bool ThemeController::activate(const QString& id, bool persist) {
    const auto outcome = manager_->activate(id.toStdString(), requestedAppearance());
    if (!outcome.ok) {
        setError(QStringLiteral("Thème « %1 » non activé : %2")
                     .arg(id, firstError(outcome.diagnostics)));
        return false;
    }
    setError({});
    if (prefs_ && persist) {
        prefsThemeId_ = id; // avant setThemeId : followPreferences n'a rien à refaire
        prefs_->setThemeId(id);
    }
    apply();
    return true;
}

void ThemeController::resetToDefault() {
    (void)activate(fromUtf8(std::string(theme::kDefaultThemeId)));
}

void ThemeController::reload() {
    const auto outcome = manager_->reload();
    if (!outcome.ok) {
        setError(QStringLiteral("Rechargement refusé, thème précédent conservé : %1")
                     .arg(firstError(outcome.diagnostics)));
    } else {
        setError({});
    }
    refreshLibrary();
    apply();
}

QVariantMap ThemeController::importResult(const theme::ThemeLibrary::ImportResult& result) {
    bool exists = false;
    for (const auto& d : result.diagnostics) {
        exists = exists || d.message.find("déjà installé") != std::string::npos;
    }
    refreshLibrary();
    return QVariantMap{{QStringLiteral("ok"), result.ok},
                       {QStringLiteral("id"), fromUtf8(result.id)},
                       {QStringLiteral("exists"), exists},
                       {QStringLiteral("messages"), messages(result.diagnostics)}};
}

QVariantMap ThemeController::importFolder(const QUrl& folder, bool replace) {
    const auto result = manager_->library().importDirectory(localPath(folder), replace);
    if (result.ok && QString::fromStdString(result.id) == activeId()) {
        reload();
    }
    return importResult(result);
}

QVariantMap ThemeController::importArchive(const QUrl& archive, bool replace) {
    theme::ThemeLibrary::ImportResult failure;
    const auto fail = [&](const QString& message) {
        failure.diagnostics.push_back(
            {Diagnostic::Severity::Error, "archive", message.toStdString()});
        return importResult(failure);
    };
    const auto file = archive.isLocalFile() ? archive.toLocalFile() : archive.toString();
    QZipReader reader(file);
    if (!reader.isReadable() || reader.status() != QZipReader::NoError) {
        return fail(QStringLiteral("archive illisible (zip attendu)"));
    }
    const auto infos = reader.fileInfoList();
    std::vector<theme::ArchiveEntry> entries;
    entries.reserve(static_cast<std::size_t>(infos.size()));
    for (const auto& info : infos) {
        entries.push_back({info.filePath.toStdString(),
                           static_cast<std::uintmax_t>(std::max<qint64>(0, info.size)),
                           info.isDir != 0, info.isSymLink != 0});
    }
    const auto plan = theme::planArchiveExtraction(entries);
    if (!plan.ok()) {
        failure.diagnostics = plan.diagnostics;
        return importResult(failure);
    }
    QTemporaryDir staging;
    if (!staging.isValid()) {
        return fail(QStringLiteral("dossier temporaire indisponible"));
    }
    for (const auto& item : plan.items) {
        const auto data = reader.fileData(infos[static_cast<qsizetype>(item.index)].filePath);
        // La taille annoncée par l'archive peut mentir : on revérifie l'octet réel.
        if (static_cast<std::uintmax_t>(data.size()) > theme::kMaxThemeFileBytes) {
            return fail(QStringLiteral("fichier décompressé trop volumineux"));
        }
        const auto target = QDir(staging.path()).filePath(QString::fromStdString(item.relative));
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size()) {
            return fail(QStringLiteral("extraction impossible"));
        }
    }
    auto result = manager_->library().importDirectory(
        std::filesystem::path(staging.path().toStdWString()), replace);
    for (const auto& d : plan.diagnostics) {
        result.diagnostics.push_back(d);
    }
    if (result.ok && QString::fromStdString(result.id) == activeId()) {
        reload();
    }
    return importResult(result);
}

bool ThemeController::exportTheme(const QString& id, const QUrl& archive) {
    const auto* entry = manager_->library().entry(id.toStdString());
    if (!entry) {
        setError(QStringLiteral("Export impossible : thème « %1 » introuvable").arg(id));
        return false;
    }
    const auto root = QString::fromStdWString(entry->dir.wstring());
    const auto file = archive.isLocalFile() ? archive.toLocalFile() : archive.toString();
    QZipWriter writer(file);
    if (writer.status() != QZipWriter::NoError) {
        setError(QStringLiteral("Export impossible : %1 n'est pas inscriptible").arg(file));
        return false;
    }
    writer.setCompressionPolicy(QZipWriter::AutoCompress);
    QStringList files;
    QDirIterator it(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files << it.next();
    }
    files.sort(); // archive reproductible
    for (const auto& path : files) {
        const auto rel = QDir(root).relativeFilePath(path);
        if (!theme::isAllowedThemeFile(std::filesystem::path(path.toStdWString())) ||
            !theme::sanitizeRelativePath(rel.toStdString())) {
            continue;
        }
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly)) {
            continue;
        }
        writer.addFile(id + QLatin1Char('/') + rel, in.readAll());
    }
    writer.close();
    setError({});
    return writer.status() == QZipWriter::NoError;
}

QVariantMap ThemeController::createDerived(const QString& parentId, const QString& id,
                                           const QString& name) {
    const auto result = manager_->library().createDerived(parentId.toStdString(), id.toStdString(),
                                                          name.toStdString());
    return importResult(result);
}

bool ThemeController::removeTheme(const QString& id) {
    if (id == activeId()) {
        resetToDefault();
    }
    const bool ok = manager_->library().remove(id.toStdString());
    refreshLibrary();
    return ok;
}

} // namespace openstitch::desktop_v2::studio
