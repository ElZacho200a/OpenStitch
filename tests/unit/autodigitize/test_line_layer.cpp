// SPDX-License-Identifier: Apache-2.0
// Couche de traits sombres (HP-AUTO-005) : les traits fins sont séparés des remplissages, qui
// passent dessous sans trou, et cousus en dernier en point droit.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <variant>

#include "openstitch/autodigitize/line_layer.hpp"
#include "openstitch/segmentation/segmentation.hpp"

using namespace openstitch;
using namespace openstitch::autodigitize;

namespace {

void fill_rect(image::Image& img, int x0, int y0, int x1, int y1, std::uint8_t r, std::uint8_t g,
               std::uint8_t b) {
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            std::uint8_t* px = img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
            px[0] = r;
            px[1] = g;
            px[2] = b;
            px[3] = 255;
        }
    }
}

// 120 x 80 : fond beige, moitié gauche rouge, moitié droite bleue, un trait noir horizontal de
// 3 px qui traverse tout, un bloc noir épais de 20 x 20 px.
image::Image scene() {
    image::Image img;
    img.width = 120;
    img.height = 80;
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 0);
    fill_rect(img, 0, 0, 120, 80, 230, 220, 200);
    fill_rect(img, 10, 10, 60, 70, 200, 60, 40);
    fill_rect(img, 60, 10, 110, 70, 30, 80, 170);
    fill_rect(img, 10, 38, 110, 41, 5, 5, 15); // trait
    fill_rect(img, 85, 48, 105, 68, 5, 5, 15); // bloc épais
    return img;
}

AutoOptions options() {
    AutoOptions o;
    o.mm_per_px = Millimeters{0.25};
    o.skip_largest_region = true;
    o.min_fill_area_mm2 = 2.0;
    o.min_region_area_mm2 = 0.0;
    return o;
}

// Les couleurs des régions sont des moyennes : comparaison à une tolérance.
bool near_rgb(std::array<std::uint8_t, 3> a, std::array<std::uint8_t, 3> b) {
    int d = 0;
    for (int k = 0; k < 3; ++k) {
        d += std::abs(static_cast<int>(a[k]) - static_cast<int>(b[k]));
    }
    return d <= 30;
}

std::size_t count_rgb(const AutoResult& r, std::array<std::uint8_t, 3> rgb) {
    return static_cast<std::size_t>(
        std::count_if(r.embroideries.begin(), r.embroideries.end(), [&](const auto& e) {
            return near_rgb(e.rgb, rgb) && std::holds_alternative<document::TatamiParams>(e.params);
        }));
}

} // namespace

TEST_CASE("dark_thin_mask : le trait fin est trouve, le bloc epais non") {
    const auto img = scene();
    std::array<std::uint8_t, 3> mean{};
    const auto mask = segmentation::dark_thin_mask(img, 25.0, 6, &mean);
    REQUIRE(mask.size() == static_cast<std::size_t>(img.width) * img.height);
    const auto at = [&](int x, int y) { return mask[static_cast<std::size_t>(y) * img.width + x]; };
    CHECK(at(30, 39) == 1);  // milieu du trait
    CHECK(at(100, 59) == 0); // coeur du bloc épais
    CHECK(at(30, 20) == 0);  // aplat rouge, pas sombre
    CHECK(mean[0] < 40);
    // Largeur minimale < 2 : sans effet.
    CHECK(segmentation::dark_thin_mask(img, 25.0, 1).empty());
}

TEST_CASE("couche de traits : desactivee = auto_digitize") {
    const auto img = scene();
    const auto seg = segmentation::segment(img, {.max_colors = 4, .min_region_px = 4});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> idsA;
    IdGenerator<ObjectId> idsB;
    const auto plain = auto_digitize(*seg, idsA, options());
    LineLayerOptions off; // enabled = false
    const auto layered =
        auto_digitize_with_lines(*seg, img, idsB, options(), off, ContourOptions{});
    REQUIRE(plain.has_value());
    REQUIRE(layered.has_value());
    CHECK(layered->embroideries.size() == plain->embroideries.size());
    CHECK(layered->vectors.size() == plain->vectors.size());
}

TEST_CASE("couche de traits : remplissages sans trou, traits cousus en dernier") {
    const auto img = scene();
    const auto seg = segmentation::segment(img, {.max_colors = 4, .min_region_px = 4});
    REQUIRE(seg.has_value());

    IdGenerator<ObjectId> idsA;
    const auto plain = auto_digitize(*seg, idsA, options());
    REQUIRE(plain.has_value());
    // Sans la couche, le trait noir coupe chaque aplat en deux morceaux.
    const std::array<std::uint8_t, 3> red = {200, 60, 40};
    INFO("plain rouge=" << count_rgb(*plain, red) << " objets=" << plain->embroideries.size());
    CHECK(count_rgb(*plain, red) == 2);

    IdGenerator<ObjectId> ids;
    LineLayerOptions lines;
    lines.enabled = true;
    LineLayerMetrics metrics;
    ContourOptions co;
    co.technique = ContourTechnique::Running;
    const auto result = auto_digitize_with_lines(*seg, img, ids, options(), lines, co, &metrics);
    REQUIRE(result.has_value());
    CHECK(metrics.line_pixels >= 250); // le trait : 100 x 3 px, aux bords près
    CHECK(metrics.line_objects >= 1);
    // Les aplats passent sous le trait : un seul morceau rouge, un seul bleu.
    INFO("layered rouge=" << count_rgb(*result, red)
                          << " bleu=" << count_rgb(*result, {30, 80, 170})
                          << " objets=" << result->embroideries.size());
    CHECK(count_rgb(*result, red) == 1);
    CHECK(count_rgb(*result, {30, 80, 170}) == 1);
    // Les objets de ligne viennent APRÈS tous les remplissages et sont des points droits.
    const std::size_t lineObjects = metrics.line_objects;
    REQUIRE(result->embroideries.size() >= lineObjects);
    const std::size_t firstLine = result->embroideries.size() - lineObjects;
    for (std::size_t i = 0; i < result->embroideries.size(); ++i) {
        const bool isRunning =
            std::holds_alternative<document::RunningStitchParams>(result->embroideries[i].params);
        if (i >= firstLine) {
            CHECK(isRunning);
        }
    }
    CHECK(std::any_of(
        result->embroideries.begin(), result->embroideries.begin() + firstLine,
        [](const auto& e) { return std::holds_alternative<document::TatamiParams>(e.params); }));
    // Le bloc noir épais reste un remplissage, pas un trait.
    CHECK(count_rgb(*result, {5, 5, 15}) >= 1);

    // Déterminisme.
    IdGenerator<ObjectId> ids2;
    const auto again = auto_digitize_with_lines(*seg, img, ids2, options(), lines, co);
    REQUIRE(again.has_value());
    CHECK(again->embroideries.size() == result->embroideries.size());
    CHECK(again->vectors.size() == result->vectors.size());
}

namespace {

// Comme scene(), mais le trait est un noir bleuté proche du bloc épais sans lui être identique :
// la couleur moyenne des traits diffère de quelques unités du noir quantifié.
image::Image scene_near_black_line() {
    image::Image img = scene();
    fill_rect(img, 10, 38, 110, 41, 14, 20, 32);
    return img;
}

std::vector<std::array<std::uint8_t, 3>> dark_colours(const AutoResult& r) {
    std::vector<std::array<std::uint8_t, 3>> out;
    for (const auto& e : r.embroideries) {
        if (e.rgb[0] < 60 && e.rgb[1] < 60 && e.rgb[2] < 60 &&
            std::find(out.begin(), out.end(), e.rgb) == out.end()) {
            out.push_back(e.rgb);
        }
    }
    return out;
}

} // namespace

TEST_CASE("couche de traits : la couleur des traits reprend le noir de la palette") {
    const auto img = scene_near_black_line();
    const auto seg = segmentation::segment(img, {.max_colors = 4, .min_region_px = 4});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    LineLayerOptions lines;
    lines.enabled = true;
    LineLayerMetrics metrics;
    ContourOptions co;
    co.technique = ContourTechnique::Running;
    const auto result = auto_digitize_with_lines(*seg, img, ids, options(), lines, co, &metrics);
    REQUIRE(result.has_value());
    REQUIRE(metrics.line_objects >= 1);
    // Un seul fil sombre : pas de quasi-doublon (changement de fil de plus pour rien).
    CHECK(dark_colours(*result).size() == 1);
    // Les remplissages de cette couleur sont cousus juste avant les traits : un seul bloc de fil.
    const std::size_t firstLine = result->embroideries.size() - metrics.line_objects;
    const auto darkRgb = dark_colours(*result).front();
    std::size_t lastOther = 0;
    for (std::size_t i = 0; i < firstLine; ++i) {
        if (result->embroideries[i].rgb != darkRgb) {
            lastOther = i + 1;
        }
    }
    for (std::size_t i = lastOther; i < result->embroideries.size(); ++i) {
        CHECK(result->embroideries[i].rgb == darkRgb);
    }
}

TEST_CASE("auto_digitize : fill_inset regle le retrait des tatami crees") {
    const auto img = scene();
    const auto seg = segmentation::segment(img, {.max_colors = 4, .min_region_px = 4});
    REQUIRE(seg.has_value());
    const auto insets = [&](AutoOptions o) {
        IdGenerator<ObjectId> ids;
        const auto r = auto_digitize(*seg, ids, o);
        REQUIRE(r.has_value());
        std::vector<std::int32_t> out;
        for (const auto& e : r->embroideries) {
            if (const auto* tp = std::get_if<document::TatamiParams>(&e.params)) {
                out.push_back(tp->inset.value);
            }
        }
        return out;
    };
    const auto byDefault = insets(options());
    REQUIRE_FALSE(byDefault.empty());
    CHECK(std::all_of(byDefault.begin(), byDefault.end(), [](auto v) { return v == 200; }));
    AutoOptions closed = options();
    closed.fill_inset = Micrometers{0};
    const auto zero = insets(closed);
    REQUIRE(zero.size() == byDefault.size());
    CHECK(std::all_of(zero.begin(), zero.end(), [](auto v) { return v == 0; }));
}

namespace {

// 120 x 80 : fond beige, zone rouge à gauche, zone bleue à droite séparées par un trait noir
// vertical de 3 px (SÉPARATION), et un trait noir horizontal de 3 px entièrement dans la zone
// rouge (VRAI OBJET : son voisinage est une seule zone).
image::Image scene_separation_and_object() {
    image::Image img;
    img.width = 120;
    img.height = 80;
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 0);
    fill_rect(img, 0, 0, 120, 80, 230, 220, 200);
    fill_rect(img, 10, 10, 59, 70, 200, 60, 40);
    fill_rect(img, 62, 10, 110, 70, 30, 80, 170);
    fill_rect(img, 59, 10, 62, 70, 5, 5, 15); // séparation : 3 x 60 = 180 px
    fill_rect(img, 18, 38, 48, 41, 5, 5, 15); // objet : 30 x 3 = 90 px
    return img;
}

} // namespace

TEST_CASE("couche de traits : objects_only ne coud pas les separations entre deux zones") {
    const auto img = scene_separation_and_object();
    const auto seg = segmentation::segment(img, {.max_colors = 4, .min_region_px = 4});
    REQUIRE(seg.has_value());
    ContourOptions co;
    co.technique = ContourTechnique::Running;

    LineLayerOptions all;
    all.enabled = true;
    LineLayerMetrics mAll;
    IdGenerator<ObjectId> idsA;
    const auto rAll = auto_digitize_with_lines(*seg, img, idsA, options(), all, co, &mAll);
    REQUIRE(rAll.has_value());
    CHECK(mAll.separation_pixels == 0);
    CHECK(mAll.line_pixels >= 250); // séparation + objet

    LineLayerOptions only = all;
    only.objects_only = true;
    LineLayerMetrics mOnly;
    IdGenerator<ObjectId> idsB;
    const auto rOnly = auto_digitize_with_lines(*seg, img, idsB, options(), only, co, &mOnly);
    REQUIRE(rOnly.has_value());
    INFO("separation=" << mOnly.separation_pixels << " cousus=" << mOnly.line_pixels);
    CHECK(mOnly.separation_pixels >= 150); // le trait vertical
    CHECK(mOnly.line_pixels >= 60);        // le trait horizontal reste
    CHECK(mOnly.line_pixels <= 120);
    CHECK(mOnly.line_objects >= 1);
    CHECK(mOnly.line_objects <= mAll.line_objects);
    // Les zones restent d'un seul morceau de chaque côté de la séparation non cousue.
    CHECK(count_rgb(*rOnly, {200, 60, 40}) == 1);
    CHECK(count_rgb(*rOnly, {30, 80, 170}) == 1);

    // Déterminisme.
    IdGenerator<ObjectId> idsC;
    const auto again = auto_digitize_with_lines(*seg, img, idsC, options(), only, co);
    REQUIRE(again.has_value());
    CHECK(again->embroideries.size() == rOnly->embroideries.size());
}

namespace {

// 120 x 80 : fond beige, zone bleue, une barre noire verticale de 10 px (2,5 mm : plus large qu'un
// trait de 1,6 mm, plus étroite que 4 mm) et un trait noir horizontal de 3 px.
image::Image scene_bar() {
    image::Image img;
    img.width = 120;
    img.height = 80;
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 0);
    fill_rect(img, 0, 0, 120, 80, 230, 220, 200);
    fill_rect(img, 10, 10, 110, 70, 30, 80, 170);
    fill_rect(img, 30, 12, 40, 68, 5, 5, 15);  // barre : 10 x 56 px
    fill_rect(img, 50, 38, 100, 41, 5, 5, 15); // trait : 50 x 3 px
    return img;
}

} // namespace

TEST_CASE("couche de traits : bars_max_width_mm coud les barres epaisses a part") {
    const auto img = scene_bar();
    const auto seg = segmentation::segment(img, {.max_colors = 4, .min_region_px = 4});
    REQUIRE(seg.has_value());
    ContourOptions co;
    co.technique = ContourTechnique::Running;

    LineLayerOptions off;
    off.enabled = true;
    LineLayerMetrics mOff;
    IdGenerator<ObjectId> idsA;
    const auto rOff = auto_digitize_with_lines(*seg, img, idsA, options(), off, co, &mOff);
    REQUIRE(rOff.has_value());
    CHECK(mOff.bar_pixels == 0);
    CHECK(mOff.bar_objects == 0);

    LineLayerOptions on = off;
    on.bars_max_width_mm = 4.0;
    LineLayerMetrics mOn;
    IdGenerator<ObjectId> idsB;
    const auto rOn = auto_digitize_with_lines(*seg, img, idsB, options(), on, co, &mOn);
    REQUIRE(rOn.has_value());
    INFO("barres=" << mOn.bar_pixels << " objets=" << mOn.bar_objects
                   << " traits=" << mOn.line_pixels);
    CHECK(mOn.bar_pixels >= 400); // la barre : ~560 px, aux bords près
    CHECK(mOn.bar_objects >= 1);
    CHECK(mOn.line_pixels >= 100); // le trait fin reste un trait
    CHECK(mOn.line_pixels < 250);  // et la barre n'y est pas comptée
    // Les objets de barre sont de la couleur des traits et passent AVANT les traits en point droit.
    const std::size_t total = rOn->embroideries.size();
    REQUIRE(total >= mOn.bar_objects + mOn.line_objects);
    const std::size_t firstBar = total - mOn.line_objects - mOn.bar_objects;
    for (std::size_t i = firstBar; i < firstBar + mOn.bar_objects; ++i) {
        CHECK_FALSE(
            std::holds_alternative<document::RunningStitchParams>(rOn->embroideries[i].params));
        CHECK(near_rgb(rOn->embroideries[i].rgb, {5, 5, 15}));
    }
    for (std::size_t i = total - mOn.line_objects; i < total; ++i) {
        CHECK(std::holds_alternative<document::RunningStitchParams>(rOn->embroideries[i].params));
    }
    // Déterminisme.
    IdGenerator<ObjectId> idsC;
    const auto again = auto_digitize_with_lines(*seg, img, idsC, options(), on, co);
    REQUIRE(again.has_value());
    CHECK(again->embroideries.size() == rOn->embroideries.size());
}

TEST_CASE("couche de traits : min_line_length_mm ecarte les fragments isoles trop courts") {
    image::Image img = scene();               // un trait de 100 px (25 mm) et un bloc épais
    fill_rect(img, 20, 20, 32, 23, 5, 5, 15); // fragment isolé : 12 x 3 px (3 mm)
    const auto seg = segmentation::segment(img, {.max_colors = 4, .min_region_px = 4});
    REQUIRE(seg.has_value());
    ContourOptions co;
    co.technique = ContourTechnique::Running;

    LineLayerOptions keep;
    keep.enabled = true;
    LineLayerMetrics mKeep;
    IdGenerator<ObjectId> idsA;
    REQUIRE(auto_digitize_with_lines(*seg, img, idsA, options(), keep, co, &mKeep).has_value());
    CHECK(mKeep.short_fragment_pixels == 0);

    LineLayerOptions drop = keep;
    drop.min_line_length_mm = 5.0; // 20 px
    LineLayerMetrics mDrop;
    IdGenerator<ObjectId> idsB;
    REQUIRE(auto_digitize_with_lines(*seg, img, idsB, options(), drop, co, &mDrop).has_value());
    INFO("retires=" << mDrop.short_fragment_pixels << " cousus=" << mDrop.line_pixels);
    CHECK(mDrop.short_fragment_pixels >= 30); // le fragment : ~36 px
    CHECK(mDrop.short_fragment_pixels <= 60);
    CHECK(mDrop.line_pixels >= 250); // le long trait reste
    CHECK(mDrop.line_pixels + mDrop.short_fragment_pixels <= mKeep.line_pixels + 5);
}

namespace {

// 120 x 80 : fond beige, grande zone rouge, un trait noir horizontal de 3 px DANS la zone, bordé de
// deux rangées de pixels sombres un peu moins foncés (anti-crénelage) : trop clairs pour le seuil
// des traits mais étiquetés dans la région noire par la quantification.
image::Image scene_rope_with_halo() {
    image::Image img;
    img.width = 120;
    img.height = 80;
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 0);
    fill_rect(img, 0, 0, 120, 80, 230, 220, 200);
    fill_rect(img, 10, 10, 110, 70, 200, 60, 40);
    fill_rect(img, 20, 38, 100, 41, 5, 5, 15);   // le trait
    fill_rect(img, 20, 37, 100, 38, 60, 45, 65); // halo anti-crénelé
    fill_rect(img, 20, 41, 100, 42, 60, 45, 65);
    return img;
}

} // namespace

TEST_CASE("couche de traits : objects_only garde un hauban dont le halo est dans la region noire") {
    const auto img = scene_rope_with_halo();
    const auto seg = segmentation::segment(img, {.max_colors = 3, .min_region_px = 4});
    REQUIRE(seg.has_value());
    ContourOptions co;
    co.technique = ContourTechnique::Running;
    LineLayerOptions only;
    only.enabled = true;
    only.objects_only = true;
    LineLayerMetrics m;
    IdGenerator<ObjectId> ids;
    REQUIRE(auto_digitize_with_lines(*seg, img, ids, options(), only, co, &m).has_value());
    INFO("cousus=" << m.line_pixels << " separation=" << m.separation_pixels);
    // Un seul voisin réel (la zone rouge) : c'est un objet, pas une séparation. Avant le correctif,
    // « rouge + région noire » suffisait à l'écarter.
    CHECK(m.line_pixels >= 200);
    CHECK(m.separation_pixels * 4 <= m.line_pixels);
}

namespace {

// Disque plein (couleur c) de rayon r centré en (cx, cy).
void fill_disc(image::Image& img, int cx, int cy, int r, std::uint8_t cr, std::uint8_t cg,
               std::uint8_t cb) {
    for (int y = cy - r; y <= cy + r; ++y) {
        for (int x = cx - r; x <= cx + r; ++x) {
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) {
                fill_rect(img, x, y, x + 1, y + 1, cr, cg, cb);
            }
        }
    }
}

// Fond beige, zone rouge, un anneau marine (rayon 20, épaisseur 5 px) sur la zone rouge, et un
// disque marine plein (pas un anneau) à l'écart.
image::Image ring_scene() {
    image::Image img;
    img.width = 160;
    img.height = 100;
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 0);
    fill_rect(img, 0, 0, 160, 100, 230, 220, 200);
    fill_rect(img, 10, 10, 150, 90, 200, 60, 40);
    fill_disc(img, 50, 50, 20, 5, 5, 15);
    fill_disc(img, 50, 50, 15, 200, 60, 40);
    fill_disc(img, 120, 50, 14, 5, 5, 15);
    return img;
}

std::size_t count_satin(const AutoResult& r) {
    return static_cast<std::size_t>(
        std::count_if(r.embroideries.begin(), r.embroideries.end(), [](const auto& e) {
            return std::holds_alternative<document::AutoSatinParams>(e.params);
        }));
}

} // namespace

TEST_CASE("couche de traits : un anneau sombre devient un auto-satin, un disque plein non") {
    const auto img = ring_scene();
    const auto seg = segmentation::segment(img, {.max_colors = 4, .min_region_px = 4});
    REQUIRE(seg.has_value());
    ContourOptions co;
    co.technique = ContourTechnique::Running;

    IdGenerator<ObjectId> idsOff;
    LineLayerOptions off;
    off.enabled = true;
    LineLayerMetrics mOff;
    const auto without = auto_digitize_with_lines(*seg, img, idsOff, options(), off, co, &mOff);
    REQUIRE(without.has_value());
    CHECK(mOff.ring_objects == 0);

    IdGenerator<ObjectId> ids;
    LineLayerOptions on = off;
    on.rings_satin = true;
    LineLayerMetrics m;
    const auto result = auto_digitize_with_lines(*seg, img, ids, options(), on, co, &m);
    REQUIRE(result.has_value());
    CHECK(m.ring_objects == 1);
    CHECK(count_satin(*result) == count_satin(*without) + 1);
    // L'anneau est un vrai anneau géométrique : un contour extérieur et un trou, de l'ordre de 5 px
    // (1,25 mm) d'épaisseur.
    const auto& vec = result->vectors.back();
    REQUIRE(vec.paths.size() == 1);
    CHECK(vec.paths[0].holes.size() == 1);
    CHECK(near_rgb(vec.rgb, {5, 5, 15}));

    IdGenerator<ObjectId> ids2;
    LineLayerMetrics again;
    const auto second = auto_digitize_with_lines(*seg, img, ids2, options(), on, co, &again);
    REQUIRE(second.has_value());
    CHECK(second->embroideries.size() == result->embroideries.size());
    CHECK(second->vectors.back().paths == result->vectors.back().paths);
}
