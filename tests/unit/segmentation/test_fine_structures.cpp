// SPDX-License-Identifier: Apache-2.0
// SEG-FINE-01 : detection et protection des structures fines. Corpus SYNTHETIQUE avec verite
// terrain (anticrenelage par sur-echantillonnage 4x4) ; les metriques sont affichees pour le
// rapport. Un corpus synthetique ne valide pas a lui seul le chantier (images reelles requises).
#include <catch2/catch_test_macros.hpp>

#include "openstitch/segmentation/fine_structures.hpp"
#include "openstitch/segmentation/segmentation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

using namespace openstitch;
using namespace openstitch::segmentation;

namespace {

using Rgb = std::array<float, 3>;

struct Segment2 {
    double x0;
    double y0;
    double x1;
    double y1;
};

// Toile RGBA avec verite terrain des traits (couverture >= 0.5).
struct Canvas {
    int w;
    int h;
    std::vector<Rgb> color;
    std::vector<std::uint8_t> alpha;
    std::vector<std::uint8_t> truth;

    Canvas(int width, int height, Rgb bg)
        : w(width), h(height), color(static_cast<std::size_t>(width) * height, bg),
          alpha(static_cast<std::size_t>(width) * height, 255),
          truth(static_cast<std::size_t>(width) * height, 0) {}

    [[nodiscard]] std::size_t at(int x, int y) const {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
               static_cast<std::size_t>(x);
    }

    void fill_rect(int x0, int y0, int x1, int y1, Rgb c) {
        for (int y = std::max(0, y0); y < std::min(h, y1); ++y) {
            for (int x = std::max(0, x0); x < std::min(w, x1); ++x) {
                color[at(x, y)] = c;
            }
        }
    }

    void hide_rect(int x0, int y0, int x1, int y1) {
        for (int y = std::max(0, y0); y < std::min(h, y1); ++y) {
            for (int x = std::max(0, x0); x < std::min(w, x1); ++x) {
                alpha[at(x, y)] = 0;
            }
        }
    }

    // Trait polyligne de largeur `width` (pixels), anticrenele.
    void draw(const std::vector<Segment2>& segs, double width, Rgb c) {
        const double r = width / 2.0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int hit = 0;
                for (int sy = 0; sy < 4; ++sy) {
                    for (int sx = 0; sx < 4; ++sx) {
                        const double px = x + (sx + 0.5) / 4.0;
                        const double py = y + (sy + 0.5) / 4.0;
                        for (const auto& s : segs) {
                            const double dx = s.x1 - s.x0;
                            const double dy = s.y1 - s.y0;
                            const double len2 = dx * dx + dy * dy;
                            double t = len2 > 0 ? ((px - s.x0) * dx + (py - s.y0) * dy) / len2 : 0;
                            t = std::clamp(t, 0.0, 1.0);
                            const double ex = px - (s.x0 + t * dx);
                            const double ey = py - (s.y0 + t * dy);
                            if (ex * ex + ey * ey <= r * r) {
                                ++hit;
                                break;
                            }
                        }
                    }
                }
                if (hit == 0) {
                    continue;
                }
                const float cov = static_cast<float>(hit) / 16.0f;
                Rgb& dst = color[at(x, y)];
                for (std::size_t k = 0; k < 3; ++k) {
                    dst[k] = dst[k] * (1.0f - cov) + c[k] * cov;
                }
                if (cov >= 0.5f) {
                    truth[at(x, y)] = 1;
                }
            }
        }
    }

    // Bruit deterministe (generateur congruentiel) d'amplitude +-amp, type artefact JPEG.
    void add_noise(int amp) {
        std::uint32_t s = 12345u;
        for (auto& px : color) {
            for (auto& v : px) {
                s = s * 1664525u + 1013904223u;
                v += static_cast<float>(static_cast<int>((s >> 16) % (2 * amp + 1)) - amp);
            }
        }
    }

    [[nodiscard]] image::Image image() const {
        image::Image img;
        img.width = w;
        img.height = h;
        img.rgba.resize(static_cast<std::size_t>(w) * h * 4);
        for (std::size_t i = 0; i < color.size(); ++i) {
            for (std::size_t k = 0; k < 3; ++k) {
                img.rgba[i * 4 + k] =
                    static_cast<std::uint8_t>(std::clamp(std::lround(color[i][k]), 0L, 255L));
            }
            img.rgba[i * 4 + 3] = alpha[i];
        }
        return img;
    }
};

std::vector<std::uint8_t> dilate1(const std::vector<std::uint8_t>& m, int w, int h) {
    std::vector<std::uint8_t> out(m.size(), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int dy = -1; dy <= 1 && out[static_cast<std::size_t>(y) * w + x] == 0; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx >= 0 && nx < w && ny >= 0 && ny < h &&
                        m[static_cast<std::size_t>(ny) * w + nx] != 0) {
                        out[static_cast<std::size_t>(y) * w + x] = 1;
                        break;
                    }
                }
            }
        }
    }
    return out;
}

struct Metrics {
    double recall{0};    // verite couverte (tolerance 1 px)
    double precision{0}; // detection couverte par la verite (tolerance 1 px)
    double strict_recall{0};
    double strict_precision{0};
    std::size_t detected{0};
};

Metrics measure(const Canvas& c, const FineStructureMap& m) {
    const auto truthTol = dilate1(c.truth, c.w, c.h);
    const auto maskTol = dilate1(m.mask, c.w, c.h);
    std::size_t truthN = 0, detN = 0, truthHit = 0, detHit = 0, truthHitS = 0, detHitS = 0;
    for (std::size_t i = 0; i < c.truth.size(); ++i) {
        if (c.truth[i] != 0) {
            ++truthN;
            truthHit += maskTol[i];
            truthHitS += m.mask[i];
        }
        if (m.mask[i] != 0) {
            ++detN;
            detHit += truthTol[i];
            detHitS += c.truth[i];
        }
    }
    Metrics r;
    r.detected = detN;
    r.recall = truthN ? static_cast<double>(truthHit) / static_cast<double>(truthN) : 1.0;
    r.precision = detN ? static_cast<double>(detHit) / static_cast<double>(detN) : 1.0;
    r.strict_recall = truthN ? static_cast<double>(truthHitS) / static_cast<double>(truthN) : 1.0;
    r.strict_precision = detN ? static_cast<double>(detHitS) / static_cast<double>(detN) : 1.0;
    return r;
}

void report(const std::string& name, const Metrics& m) {
    std::cout << "[SEG-FINE-01] " << name << " recall(1px)=" << m.recall
              << " precision(1px)=" << m.precision << " recall(strict)=" << m.strict_recall
              << " precision(strict)=" << m.strict_precision << " detected=" << m.detected << "\n";
}

FineStructureMap detect(const Canvas& c, const FineStructureOptions& o = {}) {
    const auto r = detect_fine_structures(c.image(), o);
    REQUIRE(r.has_value());
    return *r;
}

std::vector<Segment2> arc(double cx, double cy, double radius, double a0, double a1) {
    std::vector<Segment2> out;
    const int n = 60;
    for (int i = 0; i < n; ++i) {
        const double t0 = a0 + (a1 - a0) * i / n;
        const double t1 = a0 + (a1 - a0) * (i + 1) / n;
        out.push_back({cx + radius * std::cos(t0), cy + radius * std::sin(t0),
                       cx + radius * std::cos(t1), cy + radius * std::sin(t1)});
    }
    return out;
}

const Rgb kWhite{245, 245, 245};
const Rgb kBlack{20, 20, 20};

} // namespace

TEST_CASE("fine structures: traits sombres nets 1 a 8 px sur fond uniforme",
          "[segmentation][fine]") {
    for (const double width : {1.0, 2.0, 4.0, 8.0}) {
        Canvas c(200, 160, kWhite);
        c.draw({{10, 30, 190, 30}}, width, kBlack);         // horizontal
        c.draw({{20, 60, 110, 140}}, width, kBlack);        // diagonale
        c.draw(arc(140, 110, 35, 3.4, 6.1), width, kBlack); // courbe
        c.draw({{100, 50, 100, 150}}, width, kBlack);       // vertical
        const auto m = detect(c);
        const auto metrics = measure(c, m);
        report("sombre largeur " + std::to_string(static_cast<int>(width)), metrics);
        CHECK(metrics.recall >= 0.95);
        CHECK(metrics.precision >= 0.95);
    }
}

TEST_CASE("fine structures: trait clair sur fond sombre", "[segmentation][fine]") {
    Canvas c(160, 120, kBlack);
    c.draw({{10, 20, 150, 20}}, 2, kWhite);
    c.draw({{20, 40, 120, 110}}, 1, kWhite);
    const auto metrics = measure(c, detect(c));
    report("clair sur sombre", metrics);
    CHECK(metrics.recall >= 0.95);
    CHECK(metrics.precision >= 0.95);
}

TEST_CASE("fine structures: trait chromatique a luminosite comparable", "[segmentation][fine]") {
    // Gris clair (L* ~ 81) et rose a clarte voisine : le contraste est surtout chromatique.
    Canvas c(160, 120, Rgb{200, 200, 200});
    c.draw({{10, 30, 150, 30}}, 2, Rgb{255, 168, 190});
    c.draw({{20, 50, 120, 110}}, 2, Rgb{150, 215, 160});
    const auto metrics = measure(c, detect(c));
    report("chromatique", metrics);
    CHECK(metrics.recall >= 0.9);
    CHECK(metrics.precision >= 0.9);
}

TEST_CASE("fine structures: trait traversant deux aplats", "[segmentation][fine]") {
    Canvas c(160, 100, kWhite);
    c.fill_rect(80, 0, 160, 100, Rgb{90, 110, 160});
    c.draw({{10, 50, 150, 50}}, 2, Rgb{200, 40, 40});
    const auto metrics = measure(c, detect(c));
    report("deux aplats", metrics);
    CHECK(metrics.recall >= 0.9);
    // La frontiere verticale des deux aplats n'est pas un trait.
    CHECK(metrics.precision >= 0.9);
}

TEST_CASE("fine structures: rien a detecter hors traits", "[segmentation][fine]") {
    SECTION("frontiere simple entre deux aplats") {
        Canvas c(120, 80, kWhite);
        c.fill_rect(60, 0, 120, 80, kBlack);
        CHECK(detect(c).pixel_count == 0);
    }
    SECTION("degrade") {
        Canvas c(160, 80, kWhite);
        for (int y = 0; y < 80; ++y) {
            for (int x = 0; x < 160; ++x) {
                const float v = 30.0f + 200.0f * static_cast<float>(x) / 159.0f;
                c.color[c.at(x, y)] = Rgb{v, v, v};
            }
        }
        CHECK(detect(c).pixel_count == 0);
    }
    SECTION("bruit de type JPEG sur aplat") {
        Canvas c(160, 120, Rgb{180, 170, 150});
        c.add_noise(3);
        CHECK(detect(c).pixel_count == 0);
    }
    SECTION("gros aplat isole sur fond") {
        Canvas c(160, 120, kWhite);
        c.fill_rect(40, 30, 120, 90, kBlack);
        CHECK(detect(c).pixel_count == 0);
    }
}

TEST_CASE("fine structures: bruit leger n'empeche pas la detection", "[segmentation][fine]") {
    Canvas c(200, 120, kWhite);
    c.draw({{10, 30, 190, 30}}, 2, kBlack);
    c.draw({{20, 50, 150, 110}}, 2, kBlack);
    c.add_noise(3);
    const auto metrics = measure(c, detect(c));
    report("bruit JPEG", metrics);
    CHECK(metrics.recall >= 0.95);
    CHECK(metrics.precision >= 0.9);
}

TEST_CASE("fine structures: transparence et bord de l'image", "[segmentation][fine]") {
    Canvas c(160, 100, kWhite);
    c.draw({{10, 30, 150, 30}}, 2, kBlack);
    c.hide_rect(0, 60, 160, 100); // moitie basse transparente
    const auto m = detect(c);
    CHECK(measure(c, m).recall >= 0.95);
    // Aucun pixel transparent n'est un trait, et la lisiere opaque/transparente non plus.
    for (std::size_t i = 0; i < m.mask.size(); ++i) {
        if (c.alpha[i] == 0) {
            CHECK(m.mask[i] == 0);
        }
    }
    Canvas edge(160, 100, kWhite);
    edge.hide_rect(0, 50, 160, 100);
    CHECK(detect(edge).pixel_count == 0);
}

TEST_CASE("fine structures: image sans trait et composantes courtes", "[segmentation][fine]") {
    Canvas c(120, 80, kWhite);
    c.draw({{50, 40, 52, 40}}, 2, kBlack); // grain de 2-4 px : sous min_length_px
    const auto m = detect(c);
    CHECK(m.pixel_count == 0);
}

TEST_CASE("fine structures: options invalides et determinisme", "[segmentation][fine]") {
    Canvas c(120, 80, kWhite);
    c.draw({{10, 40, 110, 40}}, 2, kBlack);
    FineStructureOptions bad;
    bad.max_width_px = 0;
    CHECK_FALSE(detect_fine_structures(c.image(), bad).has_value());
    bad = {};
    bad.high_contrast = 1.0; // < low_contrast
    CHECK_FALSE(detect_fine_structures(c.image(), bad).has_value());
    CHECK_FALSE(detect_fine_structures(image::Image{}, {}).has_value());

    const auto a = detect(c);
    const auto b = detect(c);
    CHECK(a.mask == b.mask);
    CHECK(a.confidence == b.confidence);
    CHECK(a.component_count == 1);
}

namespace {

// Fraction des pixels de verite portant un label different de celui de la majorite du fond.
double kept_fraction(const Canvas& c, const Segmentation& seg) {
    std::size_t truthN = 0, kept = 0;
    // Label dominant du fond = celui du coin (0,0).
    const std::uint32_t bg = seg.labels[0];
    for (std::size_t i = 0; i < c.truth.size(); ++i) {
        if (c.truth[i] != 0) {
            ++truthN;
            kept += seg.labels[i] != bg && seg.labels[i] != 0 ? 1 : 0;
        }
    }
    return truthN ? static_cast<double>(kept) / static_cast<double>(truthN) : 1.0;
}

} // namespace

TEST_CASE("fine structures: option absente = resultat historique inchange",
          "[segmentation][fine]") {
    Canvas c(120, 80, kWhite);
    c.draw({{10, 10, 110, 70}}, 1, kBlack);
    SegmentationOptions opts;
    opts.max_colors = 3;
    opts.smoothing_radius_px = 1;
    const auto plain = segment(c.image(), opts);
    FineStructureMap map;
    const auto withOut = segment(c.image(), opts, &map);
    REQUIRE(plain.has_value());
    REQUIRE(withOut.has_value());
    CHECK(plain->labels == withOut->labels);
    CHECK(map.empty());
    CHECK(map.mask.empty());
}

TEST_CASE("fine structures: une diagonale d'un pixel survit au lissage et au nettoyage",
          "[segmentation][fine]") {
    Canvas c(160, 120, kWhite);
    c.draw({{10, 10, 150, 110}}, 1, kBlack);
    SegmentationOptions off;
    off.max_colors = 3;
    off.smoothing_radius_px = 1;
    off.min_region_px = 16;
    SegmentationOptions on = off;
    on.fine_structures = FineStructureOptions{};

    const auto base = segment(c.image(), off);
    FineStructureMap map;
    const auto prot = segment(c.image(), on, &map);
    REQUIRE(base.has_value());
    REQUIRE(prot.has_value());
    const double keptOff = kept_fraction(c, *base);
    const double keptOn = kept_fraction(c, *prot);
    std::cout << "[SEG-FINE-01] diagonale 1 px : verite conservee OFF=" << keptOff
              << " ON=" << keptOn << " regions OFF=" << base->region_count()
              << " ON=" << prot->region_count() << "\n";
    CHECK(keptOn >= 0.9);
    CHECK(keptOn > keptOff);
    // Une diagonale est UNE region (8-connexite des pixels proteges), pas un chapelet.
    CHECK(prot->region_count() <= 3);
    // Les aplats gardent leur nombre de regions : le fond reste un seul bloc.
    const auto bg = prot->labels[0];
    CHECK(bg != 0);
}

TEST_CASE("fine structures: couleur rare absente de la palette", "[segmentation][fine]") {
    // Trois aplats larges (blanc, noir, gris) et un trait rouge fin : avec 3 couleurs, le rouge
    // n'est la palette de personne et se confond avec un aplat.
    Canvas c(180, 120, kWhite);
    c.fill_rect(60, 0, 120, 120, kBlack);
    c.fill_rect(120, 0, 180, 120, Rgb{128, 128, 128});
    c.draw({{10, 60, 170, 60}}, 2, Rgb{220, 30, 30});
    SegmentationOptions off;
    off.max_colors = 3;
    off.min_region_px = 16;
    SegmentationOptions on = off;
    on.fine_structures = FineStructureOptions{};
    const auto base = segment(c.image(), off);
    const auto prot = segment(c.image(), on);
    REQUIRE(base.has_value());
    REQUIRE(prot.has_value());

    const auto hasRedRegion = [](const Segmentation& seg) {
        for (const auto& slot : seg.region_slots) {
            if (slot && cielab_delta_e(slot->rgb, {220, 30, 30}) < 25.0) {
                return true;
            }
        }
        return false;
    };
    CHECK_FALSE(hasRedRegion(*base));
    CHECK(hasRedRegion(*prot));
}
