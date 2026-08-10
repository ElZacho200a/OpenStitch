// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/dxf.hpp"

#include <fmt/format.h>

#include <charconv>
#include <cmath>
#include <fstream>
#include <numbers>
#include <optional>
#include <string>

#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/primitives.hpp"

namespace openstitch::formats {

namespace {

// --- Lecture bas niveau : liste de paires (code de groupe, valeur) ---------
// Le format DXF ASCII est une suite de lignes alternées code/valeur ; aucune
// autre structure au niveau lexical (les sections/entités sont une
// convention portée par le code 0, interprétée plus haut).

struct DxfPair {
    int code{0};
    std::string value;
};

std::string trim(std::string_view s) {
    std::size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start &&
          (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        --end;
    }
    return std::string(s.substr(start, end - start));
}

std::vector<DxfPair> tokenize(std::span<const std::uint8_t> bytes) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::vector<DxfPair> pairs;
    std::size_t pos = 0;
    const auto nextLine = [&]() -> std::optional<std::string> {
        if (pos >= text.size()) {
            return std::nullopt;
        }
        const std::size_t end = text.find('\n', pos);
        const std::string_view raw =
            (end == std::string_view::npos) ? text.substr(pos) : text.substr(pos, end - pos);
        pos = (end == std::string_view::npos) ? text.size() : end + 1;
        return trim(raw);
    };
    while (true) {
        const auto codeLine = nextLine();
        if (!codeLine) {
            break;
        }
        if (codeLine->empty()) {
            continue;  // ligne vide isolée (tolérance) : pas une paire valide
        }
        const auto valueLine = nextLine();
        if (!valueLine) {
            break;  // code sans valeur (fichier tronqué) : arrête proprement
        }
        int code = 0;
        const auto res = std::from_chars(codeLine->data(), codeLine->data() + codeLine->size(), code);
        if (res.ec != std::errc{}) {
            continue;  // ligne de code illisible : ignorée plutôt qu'un échec total
        }
        pairs.push_back({code, *valueLine});
    }
    return pairs;
}

std::optional<double> parse_double(const std::string& s) {
    std::size_t start = 0;
    if (!s.empty() && s[0] == '+') {
        start = 1;  // std::from_chars n'accepte pas de '+' initial
    }
    double v{};
    const auto res = std::from_chars(s.data() + start, s.data() + s.size(), v);
    if (res.ec != std::errc{}) {
        return std::nullopt;
    }
    return v;
}

std::optional<int> parse_int(const std::string& s) {
    int v{};
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{}) {
        return std::nullopt;
    }
    return v;
}

Vec2um mm_to_vec(double xMm, double yMm) {
    return Vec2um{to_micrometers(Millimeters{xMm}), to_micrometers(Millimeters{yMm})};
}

geometry::PathNode corner_node(Vec2um pos) {
    return geometry::PathNode{pos, geometry::NodeType::Corner, std::nullopt, std::nullopt};
}

// Échantillonne l'arc porté par un bulge LWPOLYLINE (segment courbe entre
// deux sommets) en points intermédiaires, ajoutés à `out` (p0/p1 exclus : déjà
// présents dans la liste appelante). Bulge = tan(angle_inclus/4) (convention
// DXF ; signe positif = antihoraire) ; la sagitte (écart corde-arc) vaut
// exactement bulge * demi-corde, ce qui donne centre et rayon sans ambiguïté.
void sample_bulge_arc(Vec2um p0Um, Vec2um p1Um, double bulge, std::vector<Vec2um>& out) {
    const double x0 = to_millimeters(p0Um.x).value;
    const double y0 = to_millimeters(p0Um.y).value;
    const double x1 = to_millimeters(p1Um.x).value;
    const double y1 = to_millimeters(p1Um.y).value;
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double d = std::hypot(dx, dy);
    if (d < 1e-6 || std::abs(bulge) < 1e-9) {
        return;  // corde dégénérée ou bulge nul : segment droit, rien à insérer
    }
    const double ux = dx / d;
    const double uy = dy / d;
    // Normale DROITE (rotation -90°) : donne, combinée à la sagitte signée
    // ci-dessous, le centre cohérent avec la convention DXF du bulge (positif
    // = balayage antihoraire de p0 vers p1 -- vérifié par rotation complexe
    // exacte : p0=(0,0), p1=(10,0), bulge=tan(22,5°) -> centre (5,5), pas
    // (5,-5), alors que la normale gauche donnerait ce dernier, faux).
    const double nx = uy;
    const double ny = -ux;
    const double halfChord = d / 2.0;
    const double sagitta = bulge * halfChord;
    const double radiusSigned = (halfChord * halfChord + sagitta * sagitta) / (2.0 * sagitta);
    const double mx = (x0 + x1) / 2.0;
    const double my = (y0 + y1) / 2.0;
    const double cx = mx + nx * (sagitta - radiusSigned);
    const double cy = my + ny * (sagitta - radiusSigned);
    const double radius = std::abs(radiusSigned);
    const double theta = 4.0 * std::atan(bulge);  // balayage signé (rad)
    const double startAngle = std::atan2(y0 - cy, x0 - cx);
    constexpr double kStepRad = 5.0 * std::numbers::pi / 180.0;  // ~5° par segment
    const int steps = std::max(1, static_cast<int>(std::lround(std::abs(theta) / kStepRad)));
    for (int s = 1; s < steps; ++s) {
        const double a = startAngle + theta * (static_cast<double>(s) / steps);
        out.push_back(mm_to_vec(cx + radius * std::cos(a), cy + radius * std::sin(a)));
    }
}

std::optional<geometry::Path> parse_line(std::span<const DxfPair> entity) {
    std::optional<double> x1, y1, x2, y2;
    for (const auto& g : entity) {
        if (g.code == 10) {
            x1 = parse_double(g.value);
        } else if (g.code == 20) {
            y1 = parse_double(g.value);
        } else if (g.code == 11) {
            x2 = parse_double(g.value);
        } else if (g.code == 21) {
            y2 = parse_double(g.value);
        }
    }
    if (!x1 || !y1 || !x2 || !y2) {
        return std::nullopt;
    }
    geometry::Path path;
    path.closed = false;
    path.nodes.push_back(corner_node(mm_to_vec(*x1, *y1)));
    path.nodes.push_back(corner_node(mm_to_vec(*x2, *y2)));
    return path;
}

std::optional<geometry::Path> parse_lwpolyline(std::span<const DxfPair> entity) {
    struct LwVertex {
        double x{0};
        double y{0};
        double bulge{0};
    };
    std::vector<LwVertex> verts;
    bool closed = false;
    for (const auto& g : entity) {
        if (g.code == 70) {
            closed = (parse_int(g.value).value_or(0) & 1) != 0;
        } else if (g.code == 10) {
            verts.push_back(LwVertex{parse_double(g.value).value_or(0.0), 0.0, 0.0});
        } else if (g.code == 20 && !verts.empty()) {
            verts.back().y = parse_double(g.value).value_or(0.0);
        } else if (g.code == 42 && !verts.empty()) {
            verts.back().bulge = parse_double(g.value).value_or(0.0);
        }
    }
    if (verts.size() < 2) {
        return std::nullopt;
    }
    std::vector<Vec2um> points;
    const std::size_t n = verts.size();
    for (std::size_t k = 0; k < n; ++k) {
        const Vec2um p0 = mm_to_vec(verts[k].x, verts[k].y);
        points.push_back(p0);
        const bool hasNext = closed || k + 1 < n;
        if (hasNext && verts[k].bulge != 0.0) {
            const Vec2um p1 = mm_to_vec(verts[(k + 1) % n].x, verts[(k + 1) % n].y);
            sample_bulge_arc(p0, p1, verts[k].bulge, points);
        }
    }
    geometry::Path path;
    path.closed = closed;
    path.nodes.reserve(points.size());
    for (const Vec2um& p : points) {
        path.nodes.push_back(corner_node(p));
    }
    return path;
}

std::optional<geometry::Path> parse_circle(std::span<const DxfPair> entity) {
    std::optional<double> cx, cy, r;
    for (const auto& g : entity) {
        if (g.code == 10) {
            cx = parse_double(g.value);
        } else if (g.code == 20) {
            cy = parse_double(g.value);
        } else if (g.code == 40) {
            r = parse_double(g.value);
        }
    }
    if (!cx || !cy || !r || *r <= 0.0) {
        return std::nullopt;
    }
    const Vec2um center = mm_to_vec(*cx, *cy);
    const Micrometers rad = to_micrometers(Millimeters{*r});
    return geometry::ellipse_path(Vec2um{center.x - rad, center.y - rad},
                                  Vec2um{center.x + rad, center.y + rad});
}

std::optional<geometry::Path> parse_arc(std::span<const DxfPair> entity) {
    std::optional<double> cx, cy, r, a1, a2;
    for (const auto& g : entity) {
        if (g.code == 10) {
            cx = parse_double(g.value);
        } else if (g.code == 20) {
            cy = parse_double(g.value);
        } else if (g.code == 40) {
            r = parse_double(g.value);
        } else if (g.code == 50) {
            a1 = parse_double(g.value);
        } else if (g.code == 51) {
            a2 = parse_double(g.value);
        }
    }
    if (!cx || !cy || !r || !a1 || !a2 || *r <= 0.0) {
        return std::nullopt;
    }
    // DXF : angles en degrés, l'arc va toujours de start vers end en antihoraire.
    double sweepDeg = *a2 - *a1;
    while (sweepDeg <= 0.0) {
        sweepDeg += 360.0;
    }
    const double startRad = *a1 * std::numbers::pi / 180.0;
    const double sweepRad = sweepDeg * std::numbers::pi / 180.0;
    constexpr double kStepRad = 5.0 * std::numbers::pi / 180.0;
    const int steps = std::max(1, static_cast<int>(std::lround(sweepRad / kStepRad)));
    geometry::Path path;
    path.closed = false;
    for (int s = 0; s <= steps; ++s) {
        const double a = startRad + sweepRad * (static_cast<double>(s) / steps);
        path.nodes.push_back(
            corner_node(mm_to_vec(*cx + *r * std::cos(a), *cy + *r * std::sin(a))));
    }
    return path;
}

void write_group(std::string& out, int code, const std::string& value) {
    out += std::to_string(code);
    out += "\r\n";
    out += value;
    out += "\r\n";
}

void write_group(std::string& out, int code, double value) {
    write_group(out, code, fmt::format("{:.6f}", value));
}

void write_group_int(std::string& out, int code, int value) {
    write_group(out, code, std::to_string(value));
}

}  // namespace

Result<std::vector<geometry::Path>> decode_dxf(std::span<const std::uint8_t> bytes) {
    const std::vector<DxfPair> pairs = tokenize(bytes);
    if (pairs.empty()) {
        return fail(ErrorCategory::InvalidFile, "Fichier DXF vide ou illisible");
    }

    // Repère la section ENTITIES (0/SECTION puis 2/ENTITIES) ; les autres
    // sections (HEADER, TABLES, BLOCKS, OBJECTS...) n'intéressent pas
    // l'import de tracés et sont ignorées.
    std::size_t i = 0;
    bool foundEntities = false;
    for (; i + 1 < pairs.size(); ++i) {
        if (pairs[i].code == 0 && pairs[i].value == "SECTION" && pairs[i + 1].code == 2 &&
            pairs[i + 1].value == "ENTITIES") {
            foundEntities = true;
            i += 2;
            break;
        }
    }
    if (!foundEntities) {
        return fail(ErrorCategory::InvalidFile, "Aucune section ENTITIES trouvée dans le fichier DXF");
    }

    std::vector<geometry::Path> paths;
    while (i < pairs.size() && !(pairs[i].code == 0 && pairs[i].value == "ENDSEC")) {
        if (pairs[i].code != 0) {
            ++i;  // groupe orphelin hors de toute entité : ignoré, robustesse
            continue;
        }
        const std::string& entityType = pairs[i].value;
        const std::size_t start = i;
        ++i;
        while (i < pairs.size() && pairs[i].code != 0) {
            ++i;
        }
        const std::span<const DxfPair> entity(pairs.data() + start, i - start);
        std::optional<geometry::Path> parsed;
        if (entityType == "LINE") {
            parsed = parse_line(entity);
        } else if (entityType == "LWPOLYLINE") {
            parsed = parse_lwpolyline(entity);
        } else if (entityType == "CIRCLE") {
            parsed = parse_circle(entity);
        } else if (entityType == "ARC") {
            parsed = parse_arc(entity);
        }
        // Toute autre entité (SPLINE, POLYLINE/VERTEX historique, texte,
        // hachures...) : ignorée silencieusement, cf. commentaire d'en-tête.
        if (parsed && !parsed->nodes.empty()) {
            paths.push_back(std::move(*parsed));
        }
    }

    if (paths.empty()) {
        return fail(ErrorCategory::UnsupportedFormat,
                    "Aucune entité prise en charge (LINE/LWPOLYLINE/CIRCLE/ARC) trouvée dans le "
                    "fichier DXF");
    }
    return paths;
}

Result<std::vector<std::uint8_t>> encode_dxf(const std::vector<geometry::Path>& paths,
                                             const DxfWriteOptions& options) {
    std::string out;
    write_group(out, 0, "SECTION");
    write_group(out, 2, "HEADER");
    write_group(out, 9, "$ACADVER");
    write_group(out, 1, "AC1015");  // AutoCAD 2000 : LWPOLYLINE prise en charge, très large compatibilité
    write_group(out, 0, "ENDSEC");

    write_group(out, 0, "SECTION");
    write_group(out, 2, "ENTITIES");
    std::size_t emitted = 0;
    for (const auto& path : paths) {
        if (path.nodes.empty()) {
            continue;
        }
        const geometry::Polyline flat = geometry::flatten(path, options.flatten_tolerance);
        if (flat.points.size() < 2) {
            continue;
        }
        write_group(out, 0, "LWPOLYLINE");
        write_group(out, 8, "0");
        write_group_int(out, 90, static_cast<int>(flat.points.size()));
        write_group_int(out, 70, path.closed ? 1 : 0);
        for (const Vec2um& p : flat.points) {
            write_group(out, 10, to_millimeters(p.x).value);
            write_group(out, 20, to_millimeters(p.y).value);
        }
        ++emitted;
    }
    write_group(out, 0, "ENDSEC");
    write_group(out, 0, "EOF");

    if (emitted == 0) {
        return fail(ErrorCategory::UserInput, "Aucun tracé à exporter");
    }
    return std::vector<std::uint8_t>(out.begin(), out.end());
}

Result<void> write_dxf_file(const std::filesystem::path& path, const std::vector<geometry::Path>& paths,
                            const DxfWriteOptions& options) {
    auto bytes = encode_dxf(paths, options);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return fail(ErrorCategory::UserInput, "Impossible d'écrire le fichier : " + path.string());
    }
    file.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (!file) {
        return fail(ErrorCategory::Internal, "Échec d'écriture : " + path.string());
    }
    return {};
}

Result<std::vector<geometry::Path>> read_dxf_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCategory::UserInput, "Fichier introuvable ou illisible : " + path.string());
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    return decode_dxf(bytes);
}

}  // namespace openstitch::formats
