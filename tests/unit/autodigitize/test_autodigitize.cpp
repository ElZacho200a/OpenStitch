// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/polyline.hpp"

using namespace openstitch;
using namespace openstitch::autodigitize;

namespace {

void set_px(image::Image& img, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::uint8_t* px =
        img.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                           static_cast<std::size_t>(x)) *
                              4;
    px[0] = r;
    px[1] = g;
    px[2] = b;
    px[3] = 255;
}

image::Image blank(int w, int h) {
    image::Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    return img;
}

AutoOptions opts() {
    AutoOptions o;
    o.mm_per_px = Millimeters{1.0}; // 1 px = 1 mm (formes de test en mm)
    o.min_fill_area_mm2 = 20.0;
    o.satin_max_width = Micrometers{6'000};
    return o;
}

// Même convention que libs/auto_satin/src/shapes.cpp : coordonnées en µm.
geometry::PathNode node(double x, double y) {
    return geometry::PathNode{
        Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
              Micrometers{static_cast<std::int32_t>(std::lround(y))}},
        geometry::NodeType::Corner, std::nullopt, std::nullopt};
}

geometry::Path rect_path_um(double x0, double y0, double x1, double y1) {
    geometry::Path p;
    p.closed = true;
    p.nodes = {node(x0, y0), node(x1, y0), node(x1, y1), node(x0, y1)};
    return p;
}

document::VectorObject make_vector(ObjectId id, geometry::Path outer,
                                   std::array<std::uint8_t, 3> rgb = {220, 30, 30}) {
    document::VectorObject v;
    v.id = id;
    v.name = "Import test " + std::to_string(id.value);
    v.rgb = rgb;
    v.paths.push_back(geometry::PathSet{std::move(outer), {}});
    return v;
}

}  // namespace

TEST_CASE("grande zone pleine -> tatami editable") {
    // Carré plein 30x30 mm rouge.
    image::Image img = blank(30, 30);
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 30; ++x) {
            set_px(img, x, y, 220, 30, 30);
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    const auto result = auto_digitize(*seg, ids, opts());
    REQUIRE(result.has_value());
    REQUIRE(result->vectors.size() == 1);
    REQUIRE(result->embroideries.size() == 1);
    CHECK(result->embroideries[0].is_tatami());
    // Editable : la geometrie vectorielle est presente.
    CHECK_FALSE(result->vectors[0].paths.empty());
    // Lien objet -> vecteur coherent.
    CHECK(result->embroideries[0].source_vector == result->vectors[0].id);
}

TEST_CASE("bande fine -> satin topologique par defaut") {
    // Bande 40x3 mm. Le moteur topologique doit construire deux rails et des
    // barreaux editables sans recourir au decoupage naif du contour.
    image::Image img = blank(44, 8);
    for (int y = 2; y < 5; ++y) {
        for (int x = 2; x < 42; ++x) {
            set_px(img, x, y, 30, 30, 220);
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    const auto result = auto_digitize(*seg, ids, opts());
    REQUIRE(result.has_value());
    bool anySatin = false;
    for (const auto& e : result->embroideries) {
        anySatin = anySatin || e.is_satin();
        if (e.is_satin()) {
            const auto& satin = std::get<document::SatinParams>(e.params);
            CHECK(satin.rungs.size() >= 2);
        }
    }
    CHECK(anySatin);
}

TEST_CASE("bande fine -> satin quand use_naive_satin est active") {
    // Meme bande, mais on reactive explicitement le satin naif.
    image::Image img = blank(44, 8);
    for (int y = 2; y < 5; ++y) {
        for (int x = 2; x < 42; ++x) {
            set_px(img, x, y, 30, 30, 220);
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    AutoOptions o = opts();
    o.use_auto_satin = false;
    o.use_naive_satin = true;
    const auto result = auto_digitize(*seg, ids, o);
    REQUIRE(result.has_value());
    bool anySatin = false;
    for (const auto& e : result->embroideries) {
        anySatin = anySatin || e.is_satin();
    }
    CHECK(anySatin);
}

TEST_CASE("bande fine -> tatami si les deux moteurs satin sont desactives") {
    image::Image img = blank(44, 8);
    for (int y = 2; y < 5; ++y) {
        for (int x = 2; x < 42; ++x) {
            set_px(img, x, y, 30, 30, 220);
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    AutoOptions o = opts();
    o.use_auto_satin = false;
    o.use_naive_satin = false;
    const auto result = auto_digitize(*seg, ids, o);
    REQUIRE(result.has_value());
    REQUIRE(result->embroideries.size() == 1);
    CHECK(result->embroideries.front().is_tatami());
}

// Le comportement du moteur DIRECT seul (decomposition par arete a
// l'interieur d'un seul appel build_satin_columns, topologie de jonction
// partagee entre sections) reste couvert directement dans
// tests/unit/auto_satin/test_columns.cpp -- SGSD (§ ci-dessous) est desormais
// le seul chemin pour une region branchee au niveau autodigitize, sans
// echappatoire cote AutoOptions.
TEST_CASE("reseau en T -> decomposition en regions independantes via le planner recursif") {
    // Region branchee (reseau en T) : decomposee d'abord (§ libs/satin_planning)
    // -- la branche horizontale se fusionne en un seul chemin continu (angle
    // ~180 deg, meilleure continuation), le pied vertical devient une region
    // independante. Resultat : 2 sections, chacune une colonne INDEPENDANTE
    // (`topology` absent, § document::SatinParams -- deux regions reanalysees
    // separement n'ont plus de jonction comparable entre elles).
    //
    // Avec le planner recursif (`satin_planning::create_satin_plan`,
    // 2026-08-14, § plan de refonte satin), le reliquat NATUREL pres de
    // l'ancienne jonction (couverture SGSD deja mesuree a 97,7%, pas 100%,
    // § docs/source/satin.md sgsd-debug) peut etre absorbe directement par
    // le planner OU rester un tres petit repli tatami cote autodigitize --
    // les deux sont corrects (§ garantie de couverture ci-dessous, jamais un
    // compte exact d'objets, sensible aux details de calibration internes).
    image::Image img = blank(64, 64);
    for (int y = 8; y < 58; ++y) {
        for (int x = 29; x < 35; ++x)
            set_px(img, x, y, 25, 180, 110);
    }
    for (int y = 8; y < 14; ++y) {
        for (int x = 8; x < 56; ++x)
            set_px(img, x, y, 25, 180, 110);
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    AutoOptions o = opts();
    o.satin_max_width = Micrometers{8'000};
    const auto result = auto_digitize(*seg, ids, o);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->vectors.empty());
    const auto& sourceVec = result->vectors.front();
    CHECK(sourceVec.paths.size() == 1);

    std::vector<const document::SatinParams*> satinSections;
    std::vector<geometry::Path> covering;
    for (const auto& e : result->embroideries) {
        if (e.is_satin()) {
            CHECK(e.source_vector == sourceVec.id);
            const auto& satin = std::get<document::SatinParams>(e.params);
            satinSections.push_back(&satin);
            CHECK_FALSE(satin.rail_a.closed);
            CHECK_FALSE(satin.rail_b.closed);
            CHECK(satin.rungs.size() >= 2);
            const auto flatA = geometry::flatten(satin.rail_a, Micrometers{30});
            const auto flatB = geometry::flatten(satin.rail_b, Micrometers{30});
            geometry::Path strip;
            strip.closed = true;
            for (const auto& pt : flatA.points)
                strip.nodes.push_back({pt, geometry::NodeType::Corner});
            for (auto it = flatB.points.rbegin(); it != flatB.points.rend(); ++it) {
                strip.nodes.push_back({*it, geometry::NodeType::Corner});
            }
            covering.push_back(std::move(strip));
        } else if (e.is_tatami() && e.name.find("repli") != std::string::npos) {
            const auto fallbackVec = std::find_if(
                result->vectors.begin(), result->vectors.end(),
                [&](const document::VectorObject& v) { return v.id == e.source_vector; });
            REQUIRE(fallbackVec != result->vectors.end());
            for (const auto& piece : fallbackVec->paths)
                covering.push_back(piece.outer);
        }
    }
    CHECK(satinSections.size() >= 2);

    // Garantie qui compte vraiment : la région source est intégralement
    // couverte (satin seul, ou satin + repli tatami), jamais un compte
    // précis d'objets qui dépend de détails de calibration internes.
    const auto leftover = geometry::subtract_polygons(sourceVec.paths.front(), covering);
    REQUIRE(leftover.has_value());
    double leftoverAreaMm2 = 0.0;
    for (const auto& piece : *leftover)
        leftoverAreaMm2 += std::abs(geometry::signed_area_um2(piece.outer)) / 1e6;
    CHECK(leftoverAreaMm2 < 0.5);
}

TEST_CASE("anneau fin -> quatre sections satin et trou preserve") {
    image::Image img = blank(64, 64);
    constexpr int center = 32;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const int dx = x - center;
            const int dy = y - center;
            const int radiusSquared = dx * dx + dy * dy;
            if (radiusSquared <= 24 * 24 && radiusSquared >= 18 * 18) {
                set_px(img, x, y, 230, 205, 90);
            }
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    AutoOptions o = opts();
    o.satin_max_width = Micrometers{12'000};
    const auto result = auto_digitize(*seg, ids, o);
    REQUIRE(result.has_value());
    // La région principale reste toujours le premier vecteur émis ; un
    // éventuel petit repli tatami (§ planner récursif, sensible aux détails
    // de calibration internes, pas une garantie structurelle de ce test)
    // apparaîtrait ensuite, jamais avant.
    REQUIRE_FALSE(result->vectors.empty());
    REQUIRE(result->vectors.front().paths.size() == 1);
    auto_satin::SatinColumnsParameters satinOptions;
    satinOptions.analysis.thresholds.max_satin_width = o.satin_max_width;
    const auto direct =
        auto_satin::build_satin_columns(result->vectors.front().paths.front(), satinOptions);
    INFO("refus anneau: " << direct.refusal);
    INFO("trous: " << result->vectors.front().paths.front().holes.size());
    REQUIRE(direct.columns.size() == 4);

    std::vector<const document::SatinParams*> satinSections;
    for (const auto& e : result->embroideries) {
        if (e.is_satin()) {
            satinSections.push_back(&std::get<document::SatinParams>(e.params));
        }
    }
    REQUIRE(satinSections.size() == 4);
    for (std::size_t i = 0; i < satinSections.size(); ++i) {
        const auto& topology = satinSections[i]->topology;
        REQUIRE(topology.has_value());
        CHECK(topology->section_index == i);
        CHECK(topology->section_count == 4);
        CHECK(topology->start_junction ==
              std::optional<std::uint32_t>{static_cast<std::uint32_t>(i)});
        CHECK(topology->end_junction ==
              std::optional<std::uint32_t>{static_cast<std::uint32_t>((i + 1) % 4)});
    }
}

TEST_CASE("petite region -> contour (point triple)") {
    // Petit carre 3x3 mm : aire 9 < 20 mm² -> contour.
    image::Image img = blank(9, 9);
    for (int y = 3; y < 6; ++y) {
        for (int x = 3; x < 6; ++x) {
            set_px(img, x, y, 30, 200, 30);
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    const auto result = auto_digitize(*seg, ids, opts());
    REQUIRE(result.has_value());
    bool anyRunning = false;
    for (const auto& e : result->embroideries) {
        if (std::holds_alternative<document::RunningStitchParams>(e.params)) {
            anyRunning = true;
        }
    }
    CHECK(anyRunning);
}

// Défaut trouvé sur un projet réel (logo circulaire sans canal alpha) : le
// fond se fragmentait en plusieurs régions blanches DISJOINTES (les zones
// hors d'un motif rond inscrit dans une image carrée). skip_largest_region
// n'excluait à l'origine que le plus gros MORCEAU -- les autres fragments
// du même fond se faisaient numériser comme de vrais objets (87,9 % des
// pixels du projet réel, répartis sur 3 régions blanches, contre 40,9 %
// pour la plus grosse seule). Corrigé : exclut toute région de la même
// couleur exacte, pas seulement le plus gros morceau.
TEST_CASE(
    "fond fragmente en plusieurs regions disjointes -> toutes exclues par skip_largest_region") {
    // Croix rouge FINE (2 px) sur fond blanc, image 40x40 : les 4 coins
    // blancs (~19x19 px chacun) sont mutuellement disjoints (la croix les
    // sépare entièrement) et individuellement PLUS GRANDS que la croix
    // elle-même (~156 px) -- essentiel pour que le plus gros morceau soit
    // bien un fragment du fond, pas la croix.
    image::Image img = blank(40, 40);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            set_px(img, x, y, 255, 255, 255);
        }
    }
    for (int y = 19; y < 21; ++y) {
        for (int x = 0; x < 40; ++x) {
            set_px(img, x, y, 220, 30, 30); // barre horizontale
        }
    }
    for (int x = 19; x < 21; ++x) {
        for (int y = 0; y < 40; ++y) {
            set_px(img, x, y, 220, 30, 30); // barre verticale
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());

    // Vérifie l'hypothèse du test : plusieurs régions blanches disjointes,
    // pas une seule (sinon l'ancien comportement suffisait déjà).
    constexpr std::array<std::uint8_t, 3> kWhite{255, 255, 255};
    std::size_t whiteRegions = 0;
    for (const auto& slot : seg->region_slots) {
        if (slot && slot->rgb == kWhite) {
            ++whiteRegions;
        }
    }
    REQUIRE(whiteRegions > 1);

    IdGenerator<ObjectId> ids;
    AutoOptions o = opts();
    o.skip_largest_region = true;
    const auto result = auto_digitize(*seg, ids, o);
    REQUIRE(result.has_value());

    // Aucun objet blanc ne doit survivre, quel que soit le fragment d'origine.
    for (const auto& v : result->vectors) {
        CHECK(v.rgb != kWhite);
    }
    // La croix rouge, elle, reste un vrai objet numérisé.
    CHECK_FALSE(result->vectors.empty());
}

TEST_CASE("identifiants uniques, objets editables, deterministe") {
    image::Image img = blank(30, 30);
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 30; ++x) {
            set_px(img, x, y, x < 15 ? 220 : 30, 30, x < 15 ? 30 : 220);
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());

    IdGenerator<ObjectId> ids1;
    const auto a = auto_digitize(*seg, ids1, opts());
    IdGenerator<ObjectId> ids2;
    const auto b = auto_digitize(*seg, ids2, opts());
    REQUIRE((a.has_value() && b.has_value()));
    CHECK(a->vectors.size() == b->vectors.size());
    CHECK(a->embroideries.size() == b->embroideries.size());

    // Tous les ids d'objets sont distincts.
    std::vector<std::uint64_t> allIds;
    for (const auto& v : a->vectors)
        allIds.push_back(v.id.value);
    for (const auto& e : a->embroideries)
        allIds.push_back(e.id.value);
    const auto uniqueEnd = std::unique(allIds.begin(), allIds.end());
    CHECK(uniqueEnd == allIds.end()); // deja tous distincts (pas de doublon adjacent)
}

TEST_CASE("segmentation vide -> erreur propre") {
    segmentation::Segmentation seg;
    IdGenerator<ObjectId> ids;
    CHECK_FALSE(auto_digitize(seg, ids, opts()).has_value());
}

// Point dans polygone (pair-impair), coordonnées en mm.
bool point_in_poly_mm(const std::vector<std::pair<double, double>>& poly, double px, double py) {
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const auto& [ax, ay] = poly[i];
        const auto& [bx, by] = poly[j];
        if (((ay > py) != (by > py)) && (px < (bx - ax) * (py - ay) / (by - ay) + ax)) {
            inside = !inside;
        }
    }
    return inside;
}

// Défaut trouvé sur un projet réel (lettre en T d'un logo, ~28 mm de
// squelette rejetés en silence) : une branche de squelette individuellement
// trop large pour du satin (largeur locale > satin_max_width, ici ~8,1 mm)
// est purement et simplement IGNORÉE par build_satin_columns -- la zone
// qu'elle couvre ne reçoit alors AUCUN point (ni satin, ni tatami), sans que
// rien ne le signale, même quand la largeur/aire globale de la région reste
// dans les clous (ce qui la fait entrer dans le chemin auto-satin en premier
// lieu, avec statut RequiresDecomposition et 4 des 5 branches du squelette
// qui réussissent). Géométrie EXACTE de cette lettre (37 sommets, cf.
// tests/unit/auto_satin/test_columns.cpp § coin intérieur d'une lettre en
// T), rasterisée ici pour passer par la VRAIE chaîne segmentation ->
// vectorisation -> auto-satin, comme en usage réel (pas juste
// build_satin_columns en isolation).
TEST_CASE("branche squelette localement trop large -> avertissement (jamais silencieux)") {
    const std::vector<std::pair<double, double>> letterT = {
        {-101.859, 238.244}, {-89.160, 243.006},  {-87.043, 244.329},  {-87.572, 244.858},
        {-92.599, 245.123},  {-93.128, 245.652},  {-99.478, 261.526},  {-102.652, 270.786},
        {-100.801, 271.579}, {-96.832, 271.844},  {-94.715, 270.786},  {-91.541, 268.140},
        {-89.953, 267.875},  {-89.953, 269.463},  {-90.482, 270.521},  {-90.482, 271.315},
        {-92.863, 276.342},  {-94.186, 276.342},  {-94.980, 275.812},  {-98.419, 274.754},
        {-114.293, 268.405}, {-121.966, 264.965}, {-121.437, 262.584}, {-120.114, 259.938},
        {-120.114, 259.409}, {-118.262, 257.028}, {-117.733, 257.028}, {-116.939, 258.351},
        {-116.939, 261.261}, {-116.145, 263.907}, {-112.971, 266.553}, {-110.854, 267.346},
        {-110.325, 266.817}, {-101.859, 245.652}, {-100.536, 241.683}, {-103.711, 239.037},
        {-104.240, 237.979},
    };
    double minX = letterT[0].first, maxX = letterT[0].first;
    double minY = letterT[0].second, maxY = letterT[0].second;
    for (const auto& [x, y] : letterT) {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }
    constexpr double kMmPerPx = 0.5;
    constexpr int kMargin = 4;
    const int w = static_cast<int>((maxX - minX) / kMmPerPx) + kMargin * 2;
    const int h = static_cast<int>((maxY - minY) / kMmPerPx) + kMargin * 2;
    image::Image img = blank(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            set_px(img, x, y, 255, 255, 255); // fond blanc
        }
    }
    for (int py = 0; py < h; ++py) {
        // Modèle Y vers le haut, image Y vers le bas : ligne 0 = maxY.
        const double my = maxY - (py - kMargin) * kMmPerPx;
        for (int px = 0; px < w; ++px) {
            const double mx = minX + (px - kMargin) * kMmPerPx;
            if (point_in_poly_mm(letterT, mx, my)) {
                set_px(img, px, py, 220, 30, 30);
            }
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    AutoOptions o = opts();
    o.mm_per_px = Millimeters{kMmPerPx};
    o.skip_largest_region = true; // exclut le fond blanc
    const auto result = auto_digitize(*seg, ids, o);
    REQUIRE(result.has_value());

    REQUIRE_FALSE(result->warnings.empty());
    const bool mentionsRejection =
        std::any_of(result->warnings.begin(), result->warnings.end(), [](const std::string& w) {
            return w.find("colonne refusee") != std::string::npos;
        });
    // Le planner récursif (§10 du plan de refonte satin, 2026-08-14) tente
    // plusieurs décompositions successives avant d'abandonner une branche :
    // un message "colonne refusee" peut donc apparaître pour une TENTATIVE
    // intermédiaire qui échoue, même si la récursion finit par résoudre
    // entièrement la région par un découpage différent -- ce test vérifie
    // que ces tentatives intermédiaires restent TRACÉES (jamais un échec
    // silencieux, même transitoire), pas que la région entière échoue.
    CHECK(mentionsRejection);

    // Le nombre exact de sections satin n'est plus une garantie testée ici
    // (§ défaut réel trouvé sur tentabrode.png, 2026-08-14 : un seuil
    // minimal de couverture, `SatinPlanConfig::min_fallback_coverage_ratio`,
    // rejette désormais un "meilleur effort" clairement insuffisant plutôt
    // que de l'accepter silencieusement — sur cette forme précise, cela peut
    // légitimement mener à 0 section satin acceptée si aucune sous-région
    // n'atteint une couverture correcte). La garantie qui compte reste la
    // couverture de bout en bout ci-dessous, satin et/ou tatami confondus.

    // Tout objet tatami (repli ciblé sur un résidu SGSD, OU repli générique
    // sur la région entière si aucune section satin n'a été acceptée,
    // `autodigitize.cpp` § `if (!madeSatin)`) compte pour la couverture.
    const auto isFallbackTatami = [](const document::EmbroideryObject& e) { return e.is_tatami(); };

    // Vérifie la couverture géométrique de bout en bout : région source
    // (le premier objet vectoriel, "Région <id>", pas les objets de repli)
    // moins (bandes satin des sections réussies + zones de repli tatami) ne
    // doit rien laisser -- au-delà d'une tolérance d'arrondi de
    // rasterisation/vectorisation, pas une simple absence d'erreur.
    const auto sourceVec = std::find_if(
        result->vectors.begin(), result->vectors.end(), [](const document::VectorObject& v) {
            return v.name.find("zone non couverte") == std::string::npos;
        });
    REQUIRE(sourceVec != result->vectors.end());
    REQUIRE_FALSE(sourceVec->paths.empty());
    // Le plus grand morceau par aire nette, PAS le premier : à la résolution
    // de rasterisation de ce test (0,5 mm/px), un rétrécissement sous 1 px
    // (la lettre descend à 0,3 mm par endroits) peut fragmenter la région en
    // plusieurs morceaux disjoints -- exactement le `main` que auto_digitize
    // choisit en interne (`autodigitize.cpp`, std::max_element sur net_area_um2).
    const auto& sourceRegion = *std::max_element(
        sourceVec->paths.begin(), sourceVec->paths.end(), [](const auto& a, const auto& b) {
            const auto netArea = [](const geometry::PathSet& s) {
                double area = std::abs(geometry::signed_area_um2(s.outer));
                for (const auto& h : s.holes)
                    area -= std::abs(geometry::signed_area_um2(h));
                return area;
            };
            return netArea(a) < netArea(b);
        });

    std::vector<geometry::Path> covering;
    for (const auto& e : result->embroideries) {
        if (const auto* sp = std::get_if<document::SatinParams>(&e.params)) {
            const auto flatA = geometry::flatten(sp->rail_a, Micrometers{30});
            const auto flatB = geometry::flatten(sp->rail_b, Micrometers{30});
            geometry::Path strip;
            strip.closed = true;
            for (const auto& pt : flatA.points) {
                strip.nodes.push_back({pt, geometry::NodeType::Corner, std::nullopt, std::nullopt});
            }
            for (auto it = flatB.points.rbegin(); it != flatB.points.rend(); ++it) {
                strip.nodes.push_back(
                    {*it, geometry::NodeType::Corner, std::nullopt, std::nullopt});
            }
            covering.push_back(std::move(strip));
        } else if (isFallbackTatami(e)) {
            const auto fallbackVec = std::find_if(
                result->vectors.begin(), result->vectors.end(),
                [&](const document::VectorObject& v) { return v.id == e.source_vector; });
            REQUIRE(fallbackVec != result->vectors.end());
            for (const auto& piece : fallbackVec->paths) {
                covering.push_back(piece.outer);
            }
        }
    }
    const auto leftover = subtract_polygons(sourceRegion, covering);
    REQUIRE(leftover.has_value());
    double leftoverAreaMm2 = 0.0;
    for (const auto& piece : *leftover) {
        leftoverAreaMm2 += std::abs(geometry::signed_area_um2(piece.outer)) / 1e6;
    }
    // Tolérance = quelques pixels de rasterisation (0,5 mm/px), pas un vrai
    // trou (~28 mm² de squelette rejeté, avant le correctif ; ~10 mm² de fins
    // interstices le long de chaque couture satin/tatami avec le premier
    // correctif, incomplet -- cf. `shrink_strips_for_cutout`).
    CHECK(leftoverAreaMm2 < 0.5);
}

// `auto_digitize_vectors` : même classification AutoChoice qu'`auto_digitize`,
// mais à partir d'objets vectoriels DÉJÀ existants -- le cas d'un import SVG
// direct (formats::svg_import), qui construit des VectorObject sans jamais
// passer par image/segmentation/vectorisation (§ demande utilisateur
// "éviter la segmentation"). Réutilise le corpus de formes déjà couvert
// ci-dessus pour la voie segmentation, en µm directs plutôt qu'en pixels.
TEST_CASE("auto_digitize_vectors : grande zone pleine -> tatami editable, sans nouveau vecteur") {
    IdGenerator<ObjectId> ids;
    document::VectorObject v = make_vector(ids.next(), rect_path_um(0, 0, 30'000, 30'000));
    const ObjectId vecId = v.id;
    const auto result = auto_digitize_vectors({v}, ids, opts());
    REQUIRE(result.has_value());
    // L'objet vectoriel d'entrée existe déjà chez l'appelant (§ openSvg) --
    // aucun nouveau vecteur ne doit être créé pour lui, seulement pour un
    // éventuel reliquat satin (aucun ici, c'est du tatami).
    CHECK(result->vectors.empty());
    REQUIRE(result->embroideries.size() == 1);
    CHECK(result->embroideries[0].is_tatami());
    CHECK(result->embroideries[0].source_vector == vecId);
}

TEST_CASE("auto_digitize_vectors : bande fine -> satin topologique, meme moteur que la segmentation") {
    IdGenerator<ObjectId> ids;
    // Bande 40x3 mm, meme proportions que le test segmentation equivalent.
    document::VectorObject v = make_vector(ids.next(), rect_path_um(0, 0, 40'000, 3'000));
    const auto result = auto_digitize_vectors({v}, ids, opts());
    REQUIRE(result.has_value());
    bool anySatin = false;
    for (const auto& e : result->embroideries) {
        anySatin = anySatin || e.is_satin();
        if (e.is_satin()) {
            const auto& satin = std::get<document::SatinParams>(e.params);
            CHECK(satin.rungs.size() >= 2);
        }
    }
    CHECK(anySatin);
}

TEST_CASE("auto_digitize_vectors : petit objet -> contour, comme la voie segmentation") {
    IdGenerator<ObjectId> ids;
    // Carre 3x3 mm : aire 9 < 20 mm^2 (opts().min_fill_area_mm2) -> contour.
    document::VectorObject v = make_vector(ids.next(), rect_path_um(0, 0, 3'000, 3'000));
    const auto result = auto_digitize_vectors({v}, ids, opts());
    REQUIRE(result.has_value());
    REQUIRE(result->embroideries.size() == 1);
    CHECK(std::holds_alternative<document::RunningStitchParams>(result->embroideries[0].params));
}

TEST_CASE("auto_digitize_vectors : plusieurs objets -> chacun classe independamment, ids distincts") {
    IdGenerator<ObjectId> ids;
    document::VectorObject big = make_vector(ids.next(), rect_path_um(0, 0, 30'000, 30'000));
    document::VectorObject strip = make_vector(ids.next(), rect_path_um(0, 0, 40'000, 3'000));
    document::VectorObject tiny = make_vector(ids.next(), rect_path_um(0, 0, 3'000, 3'000));
    const auto result = auto_digitize_vectors({big, strip, tiny}, ids, opts());
    REQUIRE(result.has_value());
    std::set<ObjectId> sources;
    for (const auto& e : result->embroideries) {
        sources.insert(e.source_vector);
    }
    // Un objet source par forme d'entree au minimum (la bande peut en
    // produire plusieurs si le planner topologique la decoupe en sections).
    CHECK(sources.count(big.id) == 1);
    CHECK(sources.count(strip.id) == 1);
    CHECK(sources.count(tiny.id) == 1);

    std::vector<std::uint64_t> allIds;
    for (const auto& v : result->vectors) allIds.push_back(v.id.value);
    for (const auto& e : result->embroideries) allIds.push_back(e.id.value);
    std::sort(allIds.begin(), allIds.end());
    CHECK(std::adjacent_find(allIds.begin(), allIds.end()) == allIds.end());  // tous distincts
}

TEST_CASE("auto_digitize_vectors : aucun objet vectoriel -> erreur propre") {
    IdGenerator<ObjectId> ids;
    CHECK_FALSE(auto_digitize_vectors({}, ids, opts()).has_value());
}

TEST_CASE("auto_digitize_vectors : objet sans geometrie exploitable -> ignore sans crash, erreur globale si seul") {
    IdGenerator<ObjectId> ids;
    document::VectorObject empty;
    empty.id = ids.next();
    empty.name = "vide";
    // paths volontairement vide : simule un objet vectoriel degenere.
    CHECK_FALSE(auto_digitize_vectors({empty}, ids, opts()).has_value());
}

// --- Lot B (audit marine plein cadre) : orientation et sous-couche ----------
// Avant : tous les tatami de l'auto-numérisation étaient à 0°, sans
// sous-couche (`TatamiParams{}`), quelle que soit la forme.

namespace {

// Image opaque w x h (1 px = 1 mm avec opts()) : une liste de rectangles
// colorés [x0,x1)x[y0,y1) peints dans l'ordre sur un fond transparent.
struct Rect {
    int x0, y0, x1, y1;
    std::array<std::uint8_t, 3> rgb;
};
image::Image paint(int w, int h, const std::vector<Rect>& rects) {
    image::Image img = blank(w, h);
    for (const auto& r : rects) {
        for (int y = r.y0; y < r.y1; ++y) {
            for (int x = r.x0; x < r.x1; ++x) {
                set_px(img, x, y, r.rgb[0], r.rgb[1], r.rgb[2]);
            }
        }
    }
    return img;
}

AutoOptions tatami_only_opts() {
    AutoOptions o = opts();
    o.use_auto_satin = false; // formes de test « tatami » quelle que soit leur largeur
    o.min_fill_area_mm2 = 4.0;
    return o;
}

double deg(const document::TatamiParams& p) {
    return p.angle.radians * 180.0 / std::numbers::pi;
}

// Écart angulaire entre deux orientations de rangées (modulo 180°), en degrés.
double row_gap_deg(double a, double b) {
    const double d = std::fmod(std::abs(a - b), 180.0);
    return std::min(d, 180.0 - d);
}

std::vector<document::TatamiParams> tatamis_by_color(const AutoResult& r,
                                                     std::array<std::uint8_t, 3> rgb) {
    std::vector<document::TatamiParams> out;
    // La segmentation rend le centre k-means (reconverti Lab -> RGB), pas la
    // couleur exacte peinte : comparaison à tolérance.
    const auto close = [&](const std::array<std::uint8_t, 3>& c) {
        int d = 0;
        for (int i = 0; i < 3; ++i) {
            d += std::abs(int{c[static_cast<std::size_t>(i)]} -
                          int{rgb[static_cast<std::size_t>(i)]});
        }
        return d <= 24;
    };
    for (const auto& e : r.embroideries) {
        if (close(e.rgb) && e.is_tatami()) {
            out.push_back(std::get<document::TatamiParams>(e.params));
        }
    }
    return out;
}

constexpr std::array<std::uint8_t, 3> kRed{220, 30, 30};
constexpr std::array<std::uint8_t, 3> kBlue{30, 30, 220};

} // namespace

TEST_CASE("Lot B : angle du tatami selon l'axe principal de la region") {
    SECTION("bande horizontale -> rangees a 0 degre") {
        const auto seg = segmentation::segment(paint(70, 20, {{5, 4, 65, 16, kRed}}),
                                               {.max_colors = 2, .min_region_px = 1});
        REQUIRE(seg.has_value());
        IdGenerator<ObjectId> ids;
        const auto r = auto_digitize(*seg, ids, tatami_only_opts());
        REQUIRE(r.has_value());
        const auto t = tatamis_by_color(*r, kRed);
        REQUIRE(t.size() == 1);
        CHECK(row_gap_deg(deg(t[0]), 0.0) < 2.0);
    }
    SECTION("bande verticale -> rangees a 90 degres") {
        const auto seg = segmentation::segment(paint(20, 70, {{4, 5, 16, 65, kRed}}),
                                               {.max_colors = 2, .min_region_px = 1});
        REQUIRE(seg.has_value());
        IdGenerator<ObjectId> ids;
        const auto r = auto_digitize(*seg, ids, tatami_only_opts());
        REQUIRE(r.has_value());
        const auto t = tatamis_by_color(*r, kRed);
        REQUIRE(t.size() == 1);
        CHECK(row_gap_deg(deg(t[0]), 90.0) < 2.0);
    }
    SECTION("carre quasi isotrope -> 45 degres") {
        const auto seg = segmentation::segment(paint(40, 40, {{5, 5, 35, 35, kRed}}),
                                               {.max_colors = 2, .min_region_px = 1});
        REQUIRE(seg.has_value());
        IdGenerator<ObjectId> ids;
        const auto r = auto_digitize(*seg, ids, tatami_only_opts());
        REQUIRE(r.has_value());
        const auto t = tatamis_by_color(*r, kRed);
        REQUIRE(t.size() == 1);
        CHECK(row_gap_deg(deg(t[0]), 45.0) < 0.5);
    }
}

TEST_CASE("Lot B : deux tatami voisins jamais a moins de 20 degres, la plus petite cede") {
    // Deux bandes horizontales empilées (même axe naturel 0°) : la grande
    // (rouge, 60x20) garde son angle, la petite (bleue, 60x12) est décalée.
    const auto seg =
        segmentation::segment(paint(70, 40, {{5, 4, 65, 24, kRed}, {5, 24, 65, 36, kBlue}}),
                              {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    const auto r = auto_digitize(*seg, ids, tatami_only_opts());
    REQUIRE(r.has_value());
    const auto red = tatamis_by_color(*r, kRed);
    const auto blue = tatamis_by_color(*r, kBlue);
    REQUIRE((red.size() == 1 && blue.size() == 1));
    CHECK(row_gap_deg(deg(red[0]), 0.0) < 2.0);
    CHECK(row_gap_deg(deg(red[0]), deg(blue[0])) >= 20.0 - 1e-6);

    // Les régions non voisines ne se contraignent pas : deux bandes séparées
    // par du vide gardent toutes deux leur axe naturel.
    const auto seg2 =
        segmentation::segment(paint(70, 50, {{5, 4, 65, 24, kRed}, {5, 30, 65, 42, kBlue}}),
                              {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg2.has_value());
    IdGenerator<ObjectId> ids2;
    const auto r2 = auto_digitize(*seg2, ids2, tatami_only_opts());
    REQUIRE(r2.has_value());
    CHECK(row_gap_deg(deg(tatamis_by_color(*r2, kBlue).at(0)), 0.0) < 2.0);
}

TEST_CASE("Lot B : sous-couche selon l'aire (aucune, contour, contour + rangees)") {
    // Trois carrés isolés : 4x4 = 16 mm², 7x7 = 49 mm², 20x20 = 400 mm².
    const std::array<std::uint8_t, 3> kGreen{30, 200, 30};
    const auto seg = segmentation::segment(
        paint(60, 30, {{2, 2, 6, 6, kRed}, {10, 2, 17, 9, kBlue}, {30, 2, 50, 22, kGreen}}),
        {.max_colors = 3, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    const auto r = auto_digitize(*seg, ids, tatami_only_opts());
    REQUIRE(r.has_value());
    const auto small = tatamis_by_color(*r, kRed);
    const auto medium = tatamis_by_color(*r, kBlue);
    const auto large = tatamis_by_color(*r, kGreen);
    REQUIRE((small.size() == 1 && medium.size() == 1 && large.size() == 1));
    CHECK_FALSE(small[0].underlay_edge);
    CHECK_FALSE(small[0].underlay_parallel);
    CHECK(medium[0].underlay_edge);
    CHECK_FALSE(medium[0].underlay_parallel);
    CHECK(large[0].underlay_edge);
    CHECK(large[0].underlay_parallel);

    // Réglages désactivables : comportement historique (TatamiParams{}).
    AutoOptions off = tatami_only_opts();
    off.auto_fill_angle = false;
    off.auto_fill_underlay = false;
    IdGenerator<ObjectId> ids2;
    const auto r2 = auto_digitize(*seg, ids2, off);
    REQUIRE(r2.has_value());
    const auto big2 = tatamis_by_color(*r2, kGreen);
    REQUIRE(big2.size() == 1);
    CHECK(big2[0].angle.radians == 0.0);
    CHECK_FALSE(big2[0].underlay_edge);
}

TEST_CASE("Lot B : auto_digitize_vectors oriente aussi ses tatami (sans voisinage)") {
    IdGenerator<ObjectId> ids;
    std::vector<document::VectorObject> vecs{
        make_vector(ids.next(), rect_path_um(0, 0, 12'000, 60'000))};
    AutoOptions o = tatami_only_opts();
    const auto r = auto_digitize_vectors(vecs, ids, o);
    REQUIRE(r.has_value());
    REQUIRE(r->embroideries.size() == 1);
    REQUIRE(r->embroideries[0].is_tatami());
    const auto& p = std::get<document::TatamiParams>(r->embroideries[0].params);
    CHECK(row_gap_deg(deg(p), 90.0) < 0.5);
    CHECK(p.underlay_edge);
    CHECK(p.underlay_parallel);
}

// --- Lot C (audit marine plein cadre) : chevauchement et ordre --------------
// Avant : chaque tatami était rentré de `inset` (0,2 mm) sur TOUS ses bords,
// y compris ceux partagés avec un voisin brodé -- interstices visibles.

namespace {

struct BoundsMm {
    double x0, y0, x1, y1;
};

BoundsMm bounds_mm(const document::VectorObject& v) {
    BoundsMm b{1e18, 1e18, -1e18, -1e18};
    for (const auto& set : v.paths) {
        for (const auto& n : set.outer.nodes) {
            b.x0 = std::min(b.x0, n.pos.x.value / 1000.0);
            b.y0 = std::min(b.y0, n.pos.y.value / 1000.0);
            b.x1 = std::max(b.x1, n.pos.x.value / 1000.0);
            b.y1 = std::max(b.y1, n.pos.y.value / 1000.0);
        }
    }
    return b;
}

// Objet vectoriel suivi par le (seul) tatami de couleur proche de `rgb`.
const document::VectorObject* tatami_vector(const AutoResult& r, std::array<std::uint8_t, 3> rgb,
                                            const document::TatamiParams** params = nullptr) {
    for (const auto& e : r.embroideries) {
        int d = 0;
        for (std::size_t i = 0; i < 3; ++i) {
            d += std::abs(int{e.rgb[i]} - int{rgb[i]});
        }
        if (d > 24 || !e.is_tatami()) {
            continue;
        }
        if (params != nullptr) {
            *params = &std::get<document::TatamiParams>(e.params);
        }
        for (const auto& v : r.vectors) {
            if (v.id == e.source_vector) {
                return &v;
            }
        }
    }
    return nullptr;
}

std::size_t first_index_of(const AutoResult& r, std::array<std::uint8_t, 3> rgb) {
    for (std::size_t i = 0; i < r.embroideries.size(); ++i) {
        int d = 0;
        for (std::size_t k = 0; k < 3; ++k) {
            d += std::abs(int{r.embroideries[i].rgb[k]} - int{rgb[k]});
        }
        if (d <= 24) {
            return i;
        }
    }
    return r.embroideries.size();
}

} // namespace

TEST_CASE("Lot C : tatami voisins -> debord de 0,3 mm sur le bord partage, retrait ailleurs") {
    // Rouge [5,25) et bleu [25,45) en x, 1 px = 1 mm, fond transparent autour.
    const auto seg =
        segmentation::segment(paint(50, 30, {{5, 5, 25, 25, kRed}, {25, 5, 45, 25, kBlue}}),
                              {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    const auto r = auto_digitize(*seg, ids, tatami_only_opts());
    REQUIRE(r.has_value());
    const document::TatamiParams* redParams = nullptr;
    const auto* red = tatami_vector(*r, kRed, &redParams);
    REQUIRE(red != nullptr);
    // Repère vectoriel centré sur l'image (50 px -> x de -25 à 25 mm), contours
    // passant par les centres des pixels de bord (demi-pixel à l'intérieur).
    const auto b = bounds_mm(*red);
    // Frontière partagée (pixel x = 25 -> 0 mm) : débord de 0,3 mm sur le bleu.
    CHECK(b.x1 >= 0.25);
    CHECK(b.x1 <= 0.35);
    // Bord extérieur (pixel x = 5 -> -20 mm ; contour à -19,5) : retrait de
    // 0,2 mm conservé, désormais porté par la géométrie -- le paramètre inset
    // passe à 0 pour ne pas le doubler.
    CHECK(b.x0 >= -19.35);
    CHECK(b.x0 <= -19.25);
    CHECK(redParams->inset.value == 0);
    const auto* blue = tatami_vector(*r, kBlue);
    REQUIRE(blue != nullptr);
    CHECK(bounds_mm(*blue).x0 <= -0.25); // le bleu déborde aussi sur le rouge

    // Réglage désactivable : comportement historique (géométrie brute + inset).
    AutoOptions off = tatami_only_opts();
    off.fill_overlap = Micrometers{0};
    IdGenerator<ObjectId> ids2;
    const auto r2 = auto_digitize(*seg, ids2, off);
    REQUIRE(r2.has_value());
    const document::TatamiParams* p2 = nullptr;
    const auto* red2 = tatami_vector(*r2, kRed, &p2);
    REQUIRE(red2 != nullptr);
    CHECK(bounds_mm(*red2).x1 == -0.5); // demi-pixel en retrait de la frontière
    CHECK(p2->inset.value == 200);
}

TEST_CASE("Lot C : pas de debord vers le fond ignore") {
    // Fond blanc ignoré (skip_largest_region) autour d'un carré rouge : le
    // fond n'est pas brodé, le carré garde son retrait sur tous ses bords.
    const std::array<std::uint8_t, 3> kWhite{250, 250, 250};
    const auto seg =
        segmentation::segment(paint(60, 60, {{0, 0, 60, 60, kWhite}, {20, 20, 40, 40, kRed}}),
                              {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    AutoOptions o = tatami_only_opts();
    o.skip_largest_region = true;
    IdGenerator<ObjectId> ids;
    const auto r = auto_digitize(*seg, ids, o);
    REQUIRE(r.has_value());
    const document::TatamiParams* params = nullptr;
    const auto* red = tatami_vector(*r, kRed, &params);
    REQUIRE(red != nullptr);
    // Aucun voisin brodé : géométrie inchangée, retrait `inset` conservé.
    CHECK(params->inset.value == 200);
    // Carré pixels [20,40) dans 60 px -> [-10, 10] mm, contour à ±9,5.
    const auto b = bounds_mm(*red);
    CHECK(b.x0 >= -9.5);
    CHECK(b.x1 <= 9.5);
    CHECK(b.y0 >= -9.5);
    CHECK(b.y1 <= 9.5);
}

TEST_CASE("Lot C : la grande region de fond est cousue avant le detail pose dessus") {
    // Petit bateau rouge (6x6) posé sur une grande mer bleue (40x40). Les
    // régions de segmentation sont numérotées par couleur k-means : l'ordre
    // naturel ne garantit rien -- l'auto-numérisation doit ordonner en couches.
    const auto seg =
        segmentation::segment(paint(50, 50, {{5, 5, 45, 45, kBlue}, {20, 20, 26, 26, kRed}}),
                              {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    const auto r = auto_digitize(*seg, ids, tatami_only_opts());
    REQUIRE(r.has_value());
    CHECK(first_index_of(*r, kBlue) < first_index_of(*r, kRed));

    // Même résultat quel que soit l'ordre des couleurs dans la segmentation :
    // inverser les couleurs inverse les identifiants de région, pas l'ordre
    // de couture (la grande zone reste d'abord).
    const auto seg2 =
        segmentation::segment(paint(50, 50, {{5, 5, 45, 45, kRed}, {20, 20, 26, 26, kBlue}}),
                              {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg2.has_value());
    IdGenerator<ObjectId> ids2;
    const auto r2 = auto_digitize(*seg2, ids2, tatami_only_opts());
    REQUIRE(r2.has_value());
    CHECK(first_index_of(*r2, kRed) < first_index_of(*r2, kBlue));
}

// --- Lot D (audit marine plein cadre) : fragments ---------------------------
// Avant : toute région sous min_fill_area_mm2 devenait un contour point
// triple, même au milieu d'autres régions brodées (683 morceaux de moins de
// 15 points pour le seul reflet de la marine).

TEST_CASE("Lot D : un petit fragment entre deux regions est fusionne, pas cousu en contour") {
    const std::array<std::uint8_t, 3> kGreen{30, 200, 30};
    // Rouge | bleu à 0,8 mm/px ; fragment vert 2x2 px (2,56 mm²) dans le rouge.
    const auto seg = segmentation::segment(
        paint(40, 20, {{0, 0, 20, 20, kRed}, {20, 0, 40, 20, kBlue}, {10, 8, 12, 10, kGreen}}),
        {.max_colors = 3, .min_region_px = 1});
    REQUIRE(seg.has_value());
    REQUIRE(seg->region_count() == 3);
    AutoOptions o = tatami_only_opts();
    o.mm_per_px = Millimeters{0.8};
    IdGenerator<ObjectId> ids;
    const auto r = auto_digitize(*seg, ids, o);
    REQUIRE(r.has_value());
    for (const auto& e : r->embroideries) {
        CHECK_FALSE(std::holds_alternative<document::RunningStitchParams>(e.params));
    }
    CHECK(r->vectors.size() == 2);
    // La segmentation d'entrée n'est jamais modifiée (copie de travail).
    CHECK(seg->region_count() == 3);

    // Seuil nul : comportement historique (le fragment devient un contour).
    AutoOptions off = o;
    off.min_region_area_mm2 = 0.0;
    off.min_feature_width_mm = 0.0;
    IdGenerator<ObjectId> ids2;
    const auto r2 = auto_digitize(*seg, ids2, off);
    REQUIRE(r2.has_value());
    CHECK(r2->vectors.size() == 3);
}

TEST_CASE("Lot D : seuil de fragment exprime en mm2, independant de la resolution") {
    // Même dessin à une autre résolution : 2x2 px à 3 mm/px (36 mm²) est une
    // vraie région, gardée (à 0,8 mm/px elle est fusionnée, cf. ci-dessus).
    const std::array<std::uint8_t, 3> kGreen{30, 200, 30};
    const auto seg = segmentation::segment(
        paint(40, 20, {{0, 0, 20, 20, kRed}, {20, 0, 40, 20, kBlue}, {10, 8, 12, 10, kGreen}}),
        {.max_colors = 3, .min_region_px = 1});
    REQUIRE(seg.has_value());
    AutoOptions coarse = tatami_only_opts();
    coarse.mm_per_px = Millimeters{3.0};
    coarse.min_feature_width_mm = 0.0; // isole le critère d'aire
    IdGenerator<ObjectId> ids;
    const auto r = auto_digitize(*seg, ids, coarse);
    REQUIRE(r.has_value());
    CHECK(r->vectors.size() == 3);
}

TEST_CASE("Lot D : fragment isole au milieu du fond ignore -> contour point triple") {
    const std::array<std::uint8_t, 3> kWhite{250, 250, 250};
    const std::array<std::uint8_t, 3> kGreen{30, 200, 30};
    const auto seg = segmentation::segment(
        paint(60, 40, {{0, 0, 60, 40, kWhite}, {5, 5, 25, 25, kRed}, {45, 20, 47, 22, kGreen}}),
        {.max_colors = 3, .min_region_px = 1});
    REQUIRE(seg.has_value());
    AutoOptions o = tatami_only_opts();
    o.skip_largest_region = true;
    o.mm_per_px = Millimeters{0.8};
    IdGenerator<ObjectId> ids;
    const auto r = auto_digitize(*seg, ids, o);
    REQUIRE(r.has_value());
    bool greenContour = false;
    for (const auto& e : r->embroideries) {
        if (e.rgb[1] > 150 && e.rgb[0] < 100) {
            greenContour = std::holds_alternative<document::RunningStitchParams>(e.params);
        }
    }
    CHECK(greenContour);
}
