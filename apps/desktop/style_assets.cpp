// SPDX-License-Identifier: Apache-2.0
#include "style_assets.hpp"

#include <QBuffer>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QResource>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "openstitch/core/log.hpp"

namespace openstitch::desktop::style_assets {

namespace {

enum class Shape { Check, Dash, Dot, ChevronDown, ChevronUp, ChevronRight };

struct Glyph {
    const char* name;
    Shape shape;
    QColor Tokens::*color;
    int size; // taille logique (px)
};

// Tailles logiques : contenu d'un indicateur de 16 px (bordure de 2 px) = 12 ;
// chevrons = 10 (`width/height: 10px` côté QSS).
const std::vector<Glyph>& glyphs() {
    static const std::vector<Glyph> kGlyphs = {
        {"check.png", Shape::Check, &Tokens::onAccent, 12},
        {"check-disabled.png", Shape::Check, &Tokens::textDisabled, 12},
        {"dash.png", Shape::Dash, &Tokens::onAccent, 12},
        {"dash-disabled.png", Shape::Dash, &Tokens::textDisabled, 12},
        {"dot.png", Shape::Dot, &Tokens::onAccent, 12},
        {"dot-disabled.png", Shape::Dot, &Tokens::textDisabled, 12},
        {"chevron-down.png", Shape::ChevronDown, &Tokens::icon, 10},
        {"chevron-down-disabled.png", Shape::ChevronDown, &Tokens::textDisabled, 10},
        {"chevron-up.png", Shape::ChevronUp, &Tokens::icon, 10},
        {"chevron-up-disabled.png", Shape::ChevronUp, &Tokens::textDisabled, 10},
        {"chevron-right.png", Shape::ChevronRight, &Tokens::icon, 10},
        {"chevron-right-disabled.png", Shape::ChevronRight, &Tokens::textDisabled, 10},
    };
    return kGlyphs;
}

QString g_root;              // racine enregistrée (« :/openstitch-N »), vide si aucune
QByteArray g_blob;           // octets rcc : DOIVENT rester vivants tant qu'enregistrés
int g_counter = 0;           // n de « openstitch-<n> »
bool g_forceFailure = false; // crochet de test

QImage render(const Glyph& g, const Tokens& t, int scale) {
    const int px = g.size * scale;
    QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(scale, scale);
    const QColor col = t.*(g.color);
    const double s = g.size;
    QPen pen(col);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setBrush(Qt::NoBrush);
    switch (g.shape) {
    case Shape::Check: {
        pen.setWidthF(1.8);
        p.setPen(pen);
        QPainterPath path;
        path.moveTo(s * 0.17, s * 0.53);
        path.lineTo(s * 0.42, s * 0.77);
        path.lineTo(s * 0.84, s * 0.26);
        p.drawPath(path);
        break;
    }
    case Shape::Dash:
        pen.setWidthF(2.0);
        p.setPen(pen);
        p.drawLine(QPointF(s * 0.2, s * 0.5), QPointF(s * 0.8, s * 0.5));
        break;
    case Shape::Dot:
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawEllipse(QPointF(s / 2, s / 2), s * 0.28, s * 0.28);
        break;
    case Shape::ChevronDown:
    case Shape::ChevronUp:
    case Shape::ChevronRight: {
        pen.setWidthF(1.6);
        p.setPen(pen);
        QPainterPath path;
        if (g.shape == Shape::ChevronDown) {
            path.moveTo(s * 0.15, s * 0.35);
            path.lineTo(s * 0.5, s * 0.7);
            path.lineTo(s * 0.85, s * 0.35);
        } else if (g.shape == Shape::ChevronUp) {
            path.moveTo(s * 0.15, s * 0.65);
            path.lineTo(s * 0.5, s * 0.3);
            path.lineTo(s * 0.85, s * 0.65);
        } else {
            path.moveTo(s * 0.35, s * 0.15);
            path.lineTo(s * 0.7, s * 0.5);
            path.lineTo(s * 0.35, s * 0.85);
        }
        p.drawPath(path);
        break;
    }
    }
    p.end();
    return img;
}

QByteArray png_bytes(const QImage& img) {
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

void put_u16(QByteArray& b, std::uint16_t v) {
    b.append(static_cast<char>((v >> 8) & 0xFF));
    b.append(static_cast<char>(v & 0xFF));
}

void put_u32(QByteArray& b, std::uint32_t v) {
    b.append(static_cast<char>((v >> 24) & 0xFF));
    b.append(static_cast<char>((v >> 16) & 0xFF));
    b.append(static_cast<char>((v >> 8) & 0xFF));
    b.append(static_cast<char>(v & 0xFF));
}

// Hash des noms de l'arbre rcc (identique à qt_hash de Qt).
std::uint32_t rcc_hash(const QString& key) {
    std::uint32_t h = 0;
    for (const QChar c : key) {
        h = (h << 4) + c.unicode();
        h ^= (h & 0xf0000000U) >> 23;
        h &= 0x0fffffffU;
    }
    return h;
}

struct FileEntry {
    QString name;
    QByteArray data;
    std::uint32_t hash{};
    std::uint32_t nameOffset{};
    std::uint32_t dataOffset{};
};

// Sérialiseur « qres » version 1, répertoire racine plat (nœuds de 14 octets).
// Disposition : en-tête (20) | données | noms | arbre.
QByteArray write_qres(std::vector<FileEntry> files) {
    QByteArray dataBlob;
    QByteArray names;
    // Nom du nœud racine (jamais lu) : entrée vide en tête.
    put_u16(names, 0);
    put_u32(names, 0);
    for (auto& f : files) {
        f.hash = rcc_hash(f.name);
        f.dataOffset = static_cast<std::uint32_t>(dataBlob.size());
        put_u32(dataBlob, static_cast<std::uint32_t>(f.data.size()));
        dataBlob.append(f.data);
        f.nameOffset = static_cast<std::uint32_t>(names.size());
        put_u16(names, static_cast<std::uint16_t>(f.name.size()));
        put_u32(names, f.hash);
        for (const QChar c : f.name) {
            put_u16(names, c.unicode());
        }
    }
    // Les enfants d'un répertoire sont triés par hash (recherche dichotomique).
    std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
        return a.hash != b.hash ? a.hash < b.hash : a.name < b.name;
    });

    QByteArray tree;
    // Racine : nameOffset 0, flags Directory, nombre d'enfants, premier enfant = 1.
    put_u32(tree, 0);
    put_u16(tree, 0x02);
    put_u32(tree, static_cast<std::uint32_t>(files.size()));
    put_u32(tree, 1);
    for (const auto& f : files) {
        put_u32(tree, f.nameOffset);
        put_u16(tree, 0); // flags : non compressé
        put_u16(tree, 0); // pays
        put_u16(tree, 1); // langue : QLocale::C (valeur par défaut de rcc)
        put_u32(tree, f.dataOffset);
    }

    constexpr std::uint32_t kHeader = 20;
    const std::uint32_t dataOffset = kHeader;
    const std::uint32_t namesOffset = dataOffset + static_cast<std::uint32_t>(dataBlob.size());
    const std::uint32_t treeOffset = namesOffset + static_cast<std::uint32_t>(names.size());

    QByteArray out;
    out.append("qres", 4);
    put_u32(out, 1); // version
    put_u32(out, treeOffset);
    put_u32(out, dataOffset);
    put_u32(out, namesOffset);
    out.append(dataBlob);
    out.append(names);
    out.append(tree);
    return out;
}

const uchar* blob_ptr() {
    return reinterpret_cast<const uchar*>(g_blob.constData());
}

} // namespace

QStringList glyph_names() {
    QStringList out;
    for (const auto& g : glyphs()) {
        out << QString::fromLatin1(g.name);
    }
    return out;
}

QImage glyph_image(const QString& name, const Tokens& tokens, int scale) {
    for (const auto& g : glyphs()) {
        if (name == QLatin1String(g.name)) {
            return render(g, tokens, std::max(1, scale));
        }
    }
    return {};
}

QByteArray build_resource_blob(const Tokens& tokens) {
    std::vector<FileEntry> files;
    for (const auto& g : glyphs()) {
        const QString base = QString::fromLatin1(g.name);
        const QString stem = base.left(base.size() - 4); // sans « .png »
        FileEntry one;
        one.name = base;
        one.data = png_bytes(render(g, tokens, 1));
        files.push_back(std::move(one));
        FileEntry two;
        two.name = stem + QStringLiteral("@2x.png");
        two.data = png_bytes(render(g, tokens, 2));
        files.push_back(std::move(two));
    }
    return write_qres(std::move(files));
}

void uninstall() {
    if (g_root.isEmpty()) {
        return;
    }
    // La racine de mapping est donnée sans le préfixe « :/ ».
    QResource::unregisterResource(blob_ptr(), g_root.mid(1));
    g_root.clear();
    g_blob.clear();
}

QString install(const Tokens& tokens) {
    uninstall();
    if (g_forceFailure) {
        spdlog::warn("style_assets: enregistrement force en echec (test) ; QSS sans image");
        return {};
    }
    g_blob = build_resource_blob(tokens);
    ++g_counter;
    const QString mapRoot = QStringLiteral("/openstitch-%1").arg(g_counter);
    if (!QResource::registerResource(blob_ptr(), mapRoot)) {
        spdlog::warn("style_assets: registerResource a echoue ; QSS sans image");
        g_blob.clear();
        return {};
    }
    g_root = QStringLiteral(":") + mapRoot;
    return g_root;
}

QString currentRoot() {
    return g_root;
}

void forceFailureForTesting(bool fail) {
    g_forceFailure = fail;
}

} // namespace openstitch::desktop::style_assets
