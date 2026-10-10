// SPDX-License-Identifier: Apache-2.0
#include "font_catalog.hpp"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QResource>
#include <QStandardPaths>

#include <algorithm>
#include <filesystem>
#include <set>

// Les ressources d'une bibliothèque statique ne sont enregistrées que si on les réclame
// explicitement ; la macro doit s'exécuter à la portée globale (hors de tout espace de noms,
// y compris anonyme).
static void initLetteringFontResources() {
    Q_INIT_RESOURCE(lettering_fonts);
}

namespace openstitch::desktop {

namespace {

struct Builtin {
    const char* id;
    const char* display;
    const char* family;
    const char* style;
    const char* resource;
};

// Polices libres de droits embarquées (resources/fonts/, licence : bitstream-vera-license.txt).
// La graisse « Gras » est la plus adaptée au satin (traits d'environ 2,5 mm à 12 mm de hauteur).
constexpr Builtin kBuiltins[] = {
    {"vera-sans-bold", "Vera Sans Gras (intégrée)", "Bitstream Vera Sans", "Bold",
     ":/fonts/VeraBd.ttf"},
    {"vera-sans", "Vera Sans (intégrée)", "Bitstream Vera Sans", "Roman", ":/fonts/Vera.ttf"},
};

// Chemins mémorisés dans le document : UTF-8. std::filesystem::path(char8_t) les convertit
// correctement (chemins non ASCII sous Windows compris).
std::filesystem::path pathFromUtf8(const std::string& utf8) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

QString cacheKey(const document::TextFontRef& ref) {
    if (!ref.builtin.empty()) {
        return QStringLiteral("builtin:") + QString::fromStdString(ref.builtin);
    }
    return QStringLiteral("file:") + QString::fromStdString(ref.file) + QLatin1Char(':') +
           QString::number(ref.face_index);
}

} // namespace

FontCatalog& FontCatalog::instance() {
    static FontCatalog catalog;
    return catalog;
}

void FontCatalog::setSystemFontDirsForTesting(const QStringList& dirs) {
    overrideDirs_ = dirs;
    scanned_ = false;
    entries_.clear();
    loaded_.clear();
}

void FontCatalog::scan() {
    scanned_ = true;
    entries_.clear();
    initLetteringFontResources();
    for (const Builtin& b : kBuiltins) {
        FontEntry e;
        e.display = QString::fromUtf8(b.display);
        e.family = QString::fromUtf8(b.family);
        e.style = QString::fromUtf8(b.style);
        e.builtin = QString::fromLatin1(b.id);
        entries_.push_back(e);
    }

    const QStringList dirs =
        overrideDirs_ ? *overrideDirs_
                      : QStandardPaths::standardLocations(QStandardPaths::FontsLocation);
    QList<FontEntry> installed;
    std::set<QString> seen; // dédoublonne les familles/styles présents dans plusieurs dossiers
    int files = 0;
    constexpr int kMaxFiles = 4000; // garde-fou : un dossier de polices absurde ne fige pas l'IHM
    for (const QString& dir : dirs) {
        QDirIterator it(dir, {QStringLiteral("*.ttf"), QStringLiteral("*.otf"),
                              QStringLiteral("*.ttc"), QStringLiteral("*.otc")},
                        QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
        while (it.hasNext() && files < kMaxFiles) {
            const QString path = it.next();
            ++files;
            const auto faces =
                lettering::inspect_font_file(pathFromUtf8(path.toUtf8().toStdString()));
            for (const auto& face : faces) {
                FontEntry e;
                e.family = QString::fromStdString(face.family);
                e.style = QString::fromStdString(face.style);
                const bool plain = e.style.isEmpty() || e.style == QLatin1String("Regular") ||
                                   e.style == QLatin1String("Roman");
                e.display = plain ? e.family : e.family + QLatin1Char(' ') + e.style;
                e.path = QDir::toNativeSeparators(path);
                e.faceIndex = face.face_index;
                if (seen.insert(e.display.toLower()).second) {
                    installed.push_back(e);
                }
            }
        }
    }
    std::sort(installed.begin(), installed.end(), [](const FontEntry& a, const FontEntry& b) {
        return a.display.compare(b.display, Qt::CaseInsensitive) < 0;
    });
    entries_.append(installed);
}

const QList<FontEntry>& FontCatalog::entries() {
    if (!scanned_) {
        scan();
    }
    return entries_;
}

document::TextFontRef FontCatalog::refOf(const FontEntry& entry) {
    document::TextFontRef ref;
    ref.family = entry.family.toStdString();
    ref.file = entry.path.toStdString();
    ref.builtin = entry.builtin.toStdString();
    ref.face_index = entry.faceIndex;
    return ref;
}

int FontCatalog::indexOf(const document::TextFontRef& ref) {
    const auto& list = entries();
    if (!ref.builtin.empty()) {
        for (int i = 0; i < list.size(); ++i) {
            if (list[i].builtin.toStdString() == ref.builtin) {
                return i;
            }
        }
        return -1;
    }
    for (int i = 0; i < list.size(); ++i) {
        if (!ref.file.empty() && list[i].path.toStdString() == ref.file &&
            list[i].faceIndex == ref.face_index) {
            return i;
        }
    }
    // Fichier déplacé ou projet venu d'une autre machine : même famille, de préférence « Regular ».
    int best = -1;
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].family.toStdString() == ref.family && !ref.family.empty()) {
            const bool plain = list[i].style.isEmpty() || list[i].style == QLatin1String("Regular");
            if (best < 0 || plain) {
                best = i;
            }
        }
    }
    return best;
}

std::shared_ptr<lettering::Font> FontCatalog::load(const document::TextFontRef& ref,
                                                   QString* error) {
    initLetteringFontResources();
    const auto remember = [&](const QString& key, std::shared_ptr<lettering::Font> font) {
        loaded_.push_back({key, font});
        return font;
    };
    const QString key = cacheKey(ref);
    for (const auto& [k, font] : loaded_) {
        if (k == key) {
            return font;
        }
    }
    const auto fail = [&](const QString& message) -> std::shared_ptr<lettering::Font> {
        if (error != nullptr) {
            *error = message;
        }
        return nullptr;
    };

    if (!ref.builtin.empty()) {
        for (const Builtin& b : kBuiltins) {
            if (ref.builtin == b.id) {
                QFile file(QString::fromLatin1(b.resource));
                if (!file.open(QIODevice::ReadOnly)) {
                    return fail(QObject::tr("Police intégrée illisible : %1")
                                    .arg(QString::fromLatin1(b.resource)));
                }
                const QByteArray data = file.readAll();
                auto font = lettering::Font::from_memory(
                    std::vector<std::uint8_t>(data.begin(), data.end()), ref.face_index);
                if (!font) {
                    return fail(QString::fromStdString(font.error().message));
                }
                return remember(key, std::make_shared<lettering::Font>(std::move(*font)));
            }
        }
        return fail(QObject::tr("Police intégrée inconnue : %1").arg(QString::fromStdString(ref.builtin)));
    }

    if (!ref.file.empty()) {
        auto font = lettering::Font::from_file(pathFromUtf8(ref.file), ref.face_index);
        if (font) {
            return remember(key, std::make_shared<lettering::Font>(std::move(*font)));
        }
    }
    // Repli par famille (le fichier a disparu : autre machine, police désinstallée).
    const int index = indexOf(ref);
    if (index >= 0) {
        const FontEntry& entry = entries()[index];
        if (entry.builtin.isEmpty() && entry.path.toStdString() == ref.file) {
            return fail(QObject::tr("Police illisible : %1").arg(entry.display));
        }
        document::TextFontRef fallback = refOf(entry);
        return load(fallback, error);
    }
    return fail(QObject::tr("Police introuvable : %1. Choisissez une autre police pour modifier "
                            "ce texte (les lettres déjà générées restent cousues).")
                    .arg(QString::fromStdString(ref.family.empty() ? ref.file : ref.family)));
}

} // namespace openstitch::desktop
