// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/pull_test_piece.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>
#include <utility>

#include "openstitch/stitch_generation/pull_map.hpp"

namespace openstitch::stitch_generation {

namespace {

std::int32_t to_um(double mm) {
    return static_cast<std::int32_t>(std::lround(mm * 1000.0));
}

Vec2um at(double x_mm, double y_mm) {
    return {{to_um(x_mm)}, {to_um(y_mm)}};
}

// Construit la séquence : une source par élément (un saut sépare les éléments, donc aucun segment
// ne relie deux éléments), toutes les pénétrations en couche supérieure.
class Builder {
public:
    void feature() { ++source_; }

    void jump(double x, double y) { push(stitch::CommandType::Jump, at(x, y)); }

    std::size_t stitch(double x, double y) {
        push(stitch::CommandType::Stitch, at(x, y));
        return piece_.sequence.commands.size() - 1;
    }

    void metric(std::string id, std::string description, std::string variable, std::size_t a,
                std::size_t b) {
        const Vec2um pa = piece_.sequence.commands[a].pos;
        const Vec2um pb = piece_.sequence.commands[b].pos;
        const double nominal = std::hypot(static_cast<double>(pb.x.value - pa.x.value),
                                          static_cast<double>(pb.y.value - pa.y.value)) /
                               1000.0;
        piece_.metrics.push_back(
            {std::move(id), std::move(description), std::move(variable), a, b, nominal});
    }

    PullTestPiece take() { return std::move(piece_); }

private:
    void push(stitch::CommandType type, Vec2um pos) {
        stitch::StitchCommand c;
        c.pos = pos;
        c.type = type;
        c.source = ObjectId{source_};
        c.pass = stitch::StitchPass::TopStitch;
        piece_.sequence.commands.push_back(c);
    }

    PullTestPiece piece_;
    std::uint64_t source_{0};
};

// Croix de repérage de 4 mm : retourne (pointe gauche, pointe basse).
std::pair<std::size_t, std::size_t> cross(Builder& b, double cx, double cy) {
    b.feature();
    b.jump(cx - 2.0, cy);
    const std::size_t left = b.stitch(cx - 2.0, cy);
    b.stitch(cx + 2.0, cy);
    b.jump(cx, cy - 2.0);
    const std::size_t bottom = b.stitch(cx, cy - 2.0);
    b.stitch(cx, cy + 2.0);
    return {left, bottom};
}

struct Column {
    std::size_t first_left{0};
    std::size_t last_left{0};
    std::size_t mid_left{0};
    std::size_t mid_right{0};
};

// Colonne de satin : `length` mm le long de la direction (-sin a, cos a), `width` mm en travers
// (direction du point : (cos a, sin a)), points en aller-retour tous les 0,4 mm. `a` = angle du
// point par rapport à l'axe X (la chaîne).
Column satin_column(Builder& b, double cx, double cy, double width, double length,
                    double angle_rad) {
    constexpr double kSpacing = 0.4;
    const int rows = static_cast<int>(std::floor(length / kSpacing)) + 1;
    const double ux = std::cos(angle_rad); // direction du point
    const double uy = std::sin(angle_rad);
    const double vx = -uy; // direction de la colonne
    const double vy = ux;
    Column col;
    b.feature();
    for (int i = 0; i < rows; ++i) {
        const double t = (static_cast<double>(i) - (rows - 1) * 0.5) * kSpacing;
        const double sx = cx + vx * t;
        const double sy = cy + vy * t;
        const bool forward = i % 2 == 0;
        const double s0 = forward ? -width * 0.5 : width * 0.5;
        const double s1 = -s0;
        if (i == 0) {
            b.jump(sx + ux * s0, sy + uy * s0);
        }
        const std::size_t p0 = b.stitch(sx + ux * s0, sy + uy * s0);
        const std::size_t p1 = b.stitch(sx + ux * s1, sy + uy * s1);
        const std::size_t left = forward ? p0 : p1;
        const std::size_t right = forward ? p1 : p0;
        if (i == 0) {
            col.first_left = left;
        }
        if (i == rows / 2) {
            col.mid_left = left;
            col.mid_right = right;
        }
        col.last_left = left;
    }
    return col;
}

struct Fill {
    std::size_t first_left{0};
    std::size_t last_left{0};
    std::size_t mid_left{0};
    std::size_t mid_right{0};
    std::size_t row20_right{0}; // extrémité droite de la rangée à mi-hauteur exacte
};

// Carré de tatami `side` mm : rangées horizontales (point le long de X), espacement `spacing` mm.
Fill tatami_square(Builder& b, double x0, double y0, double side, double spacing) {
    const int rows = static_cast<int>(std::floor(side / spacing)) + 1;
    Fill f;
    b.feature();
    for (int i = 0; i < rows; ++i) {
        const double y = y0 + static_cast<double>(i) * spacing;
        const bool forward = i % 2 == 0;
        const double xa = forward ? x0 : x0 + side;
        const double xb = forward ? x0 + side : x0;
        if (i == 0) {
            b.jump(xa, y);
        }
        const std::size_t pa = b.stitch(xa, y);
        const std::size_t pb = b.stitch(xb, y);
        const std::size_t left = forward ? pa : pb;
        const std::size_t right = forward ? pb : pa;
        if (i == 0) {
            f.first_left = left;
        }
        if (i == rows / 2) {
            f.mid_left = left;
            f.mid_right = right;
            f.row20_right = right;
        }
        f.last_left = left;
    }
    return f;
}

// Contour carré en pas de 4 mm avec un sommet au milieu du côté droit ; renvoie l'indice de ce
// sommet (x0 + side, y0 + side / 2).
std::size_t square_outline(Builder& b, double x0, double y0, double side) {
    b.feature();
    b.jump(x0, y0);
    b.stitch(x0, y0);
    b.stitch(x0 + side, y0);
    const std::size_t mid = b.stitch(x0 + side, y0 + side * 0.5);
    b.stitch(x0 + side, y0 + side);
    b.stitch(x0, y0 + side);
    b.stitch(x0, y0);
    return mid;
}

} // namespace

PullTestPiece build_pull_test_piece() {
    Builder b;

    // 1. Croix de repérage, cousues en premier : elles suivent tout le tirage de la pièce.
    const auto bl = cross(b, 5.0, 5.0);
    const auto br = cross(b, 125.0, 5.0);
    const auto tl = cross(b, 5.0, 95.0);
    const auto tr = cross(b, 125.0, 95.0);
    b.metric("ref_span_bottom", "Croix bas-gauche -> bas-droite (pointes gauches), sens chaine",
             "global", bl.first, br.first);
    b.metric("ref_span_top", "Croix haut-gauche -> haut-droite (pointes gauches), sens chaine",
             "global", tl.first, tr.first);
    b.metric("ref_span_left", "Croix bas-gauche -> haut-gauche (pointes basses), sens trame",
             "grain", bl.second, tl.second);
    b.metric("ref_span_right", "Croix bas-droite -> haut-droite (pointes basses), sens trame",
             "grain", br.second, tr.second);

    // 2. Colonnes de satin de largeurs croissantes (point en travers de X, colonne le long de Y).
    const double widths[] = {3.0, 5.0, 8.0, 12.0};
    const double centersX[] = {12.0, 26.0, 42.0, 62.0};
    for (int i = 0; i < 4; ++i) {
        const Column c = satin_column(b, centersX[i], 27.0, widths[i], 30.0, 0.0);
        const std::string w = std::to_string(static_cast<int>(widths[i]));
        b.metric("satin_w" + w + "_width",
                 "Satin " + w + " mm : largeur de la colonne (rangée centrale), sens du point",
                 "gain", c.mid_left, c.mid_right);
        b.metric("satin_w" + w + "_length",
                 "Satin " + w + " mm : longueur de la colonne (première à dernière rangée)", "gain",
                 c.first_left, c.last_left);
    }

    // 3. Colonnes de 4 mm à 0 / 45 / 90 degres de la chaine : meme colonne, sens du point
    // different.
    const double angles[] = {0.0, 45.0, 90.0};
    const double angleX[] = {90.0, 105.0, 120.0};
    for (int i = 0; i < 3; ++i) {
        const Column c =
            satin_column(b, angleX[i], 27.0, 4.0, 14.0, angles[i] * std::numbers::pi / 180.0);
        const std::string a = std::to_string(static_cast<int>(angles[i]));
        b.metric("grain_a" + a + "_width",
                 "Colonne 4 mm, point a " + a + " deg de la chaine : largeur", "grain", c.mid_left,
                 c.mid_right);
    }

    // 4. Carres de tatami 20 x 20 mm a trois densites (espacement des rangees).
    const double spacings[] = {0.6, 0.4, 0.25};
    const double fillX[] = {8.0, 34.0, 60.0};
    const char* names[] = {"low", "mid", "high"};
    for (int i = 0; i < 3; ++i) {
        const Fill f = tatami_square(b, fillX[i], 52.0, 20.0, spacings[i]);
        const std::string n = names[i];
        b.metric("tatami_" + n + "_width",
                 "Tatami 20 mm, rangees a " + std::to_string(spacings[i]).substr(0, 4) +
                     " mm : largeur (rangee centrale), sens du point",
                 "compression", f.mid_left, f.mid_right);
        b.metric("tatami_" + n + "_height",
                 "Tatami 20 mm, rangees a " + std::to_string(spacings[i]).substr(0, 4) +
                     " mm : hauteur (premiere a derniere rangee), sens perpendiculaire",
                 "compression", f.first_left, f.last_left);
    }

    // 5. Ordre de couture : remplissage puis contour, et contour puis remplissage.
    {
        const Fill f = tatami_square(b, 88.0, 52.0, 16.0, 0.4);
        const std::size_t o = square_outline(b, 88.0, 52.0, 16.0);
        b.metric("order_fill_then_outline_gap",
                 "Remplissage puis contour : jour entre le bord droit du remplissage et le contour",
                 "ordre", f.row20_right, o);
    }
    {
        const std::size_t o = square_outline(b, 108.0, 52.0, 16.0);
        const Fill f = tatami_square(b, 108.0, 52.0, 16.0, 0.4);
        b.metric("order_outline_then_fill_gap",
                 "Contour puis remplissage : jour entre le bord droit du remplissage et le contour",
                 "ordre", f.row20_right, o);
    }
    return b.take();
}

std::vector<PullMetricResult> predict_pull_metrics(const PullTestPiece& piece,
                                                   const FabricResponse& fabric) {
    const stitch::StitchSequence sim = simulate_fabric_pull(piece.sequence, fabric);
    std::vector<PullMetricResult> out;
    out.reserve(piece.metrics.size());
    for (const PullMetric& m : piece.metrics) {
        const Vec2um a = sim.commands[m.first].pos;
        const Vec2um b = sim.commands[m.second].pos;
        PullMetricResult r;
        r.metric = m;
        r.predicted_mm = std::hypot(static_cast<double>(b.x.value - a.x.value),
                                    static_cast<double>(b.y.value - a.y.value)) /
                         1000.0;
        out.push_back(std::move(r));
    }
    return out;
}

std::string pull_metrics_csv(const std::vector<PullMetricResult>& results) {
    std::string csv = "id;description;variable;nominal_mm;predicted_mm;measured_mm;error_mm\n";
    char buf[64];
    const auto num = [&buf](double v) {
        std::snprintf(buf, sizeof(buf), "%.3f", v);
        return std::string(buf);
    };
    for (const PullMetricResult& r : results) {
        csv += r.metric.id + ";" + r.metric.description + ";" + r.metric.variable + ";" +
               num(r.metric.nominal_mm) + ";" + num(r.predicted_mm) + ";" +
               (r.has_measure ? num(r.measured_mm) : std::string()) + ";" +
               (r.has_measure ? num(r.error_mm) : std::string()) + "\n";
    }
    return csv;
}

std::size_t apply_measurements_csv(std::vector<PullMetricResult>& results, std::string_view csv) {
    std::size_t kept = 0;
    std::size_t pos = 0;
    while (pos < csv.size()) {
        std::size_t end = csv.find('\n', pos);
        if (end == std::string_view::npos) {
            end = csv.size();
        }
        std::string_view line = csv.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        std::vector<std::string_view> fields;
        std::size_t start = 0;
        while (true) {
            const std::size_t sep = line.find(';', start);
            if (sep == std::string_view::npos) {
                fields.push_back(line.substr(start));
                break;
            }
            fields.push_back(line.substr(start, sep - start));
            start = sep + 1;
        }
        if (fields.size() < 6 || fields[5].empty()) {
            continue;
        }
        std::string value(fields[5]);
        std::replace(value.begin(), value.end(), ',', '.');
        double measured = 0.0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), measured);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
            !(measured > 0.0) || !std::isfinite(measured)) {
            continue;
        }
        for (PullMetricResult& r : results) {
            if (r.metric.id == fields[0]) {
                r.has_measure = true;
                r.measured_mm = measured;
                r.error_mm = r.predicted_mm - measured;
                ++kept;
                break;
            }
        }
    }
    return kept;
}

double pull_metrics_rms(const std::vector<PullMetricResult>& results) {
    double sum = 0.0;
    std::size_t n = 0;
    for (const PullMetricResult& r : results) {
        if (r.has_measure) {
            sum += r.error_mm * r.error_mm;
            ++n;
        }
    }
    return n == 0 ? 0.0 : std::sqrt(sum / static_cast<double>(n));
}

PullFit fit_pull_response(const PullTestPiece& piece, const std::vector<PullMetricResult>& measured,
                          const FabricResponse& base) {
    PullFit fit;
    fit.response = base;
    for (const PullMetricResult& r : measured) {
        fit.used += r.has_measure ? 1 : 0;
    }
    fit.enough_data = fit.used >= 6;
    const auto score = [&](const FabricResponse& f) {
        std::vector<PullMetricResult> predicted = predict_pull_metrics(piece, f);
        for (std::size_t i = 0; i < predicted.size() && i < measured.size(); ++i) {
            predicted[i].has_measure = measured[i].has_measure;
            predicted[i].measured_mm = measured[i].measured_mm;
            predicted[i].error_mm = predicted[i].predicted_mm - measured[i].measured_mm;
        }
        return pull_metrics_rms(predicted);
    };
    fit.rms_before_mm = score(base);
    fit.rms_after_mm = fit.rms_before_mm;
    if (fit.used == 0) {
        return fit;
    }
    const double compressions[] = {0.0, 0.03, 0.06, 0.12, 0.25};
    const double softnesses[] = {1.0, 1.15, 1.3, 1.5, 2.0};
    for (int g = 1; g <= 12; ++g) {
        for (const double c : compressions) {
            for (const double s : softnesses) {
                FabricResponse f = base;
                f.gain = 0.005 * g;
                f.compression = c;
                f.grain_anisotropy = s;
                const double rms = score(f);
                if (rms < fit.rms_after_mm - 1e-12) { // strict : premier meilleur, déterministe
                    fit.rms_after_mm = rms;
                    fit.response = f;
                }
            }
        }
    }
    return fit;
}

} // namespace openstitch::stitch_generation
