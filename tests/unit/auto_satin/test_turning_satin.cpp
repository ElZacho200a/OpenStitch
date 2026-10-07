// SPDX-License-Identifier: Apache-2.0
//
// Tests du satin tournant (HP-STI-018 Phase D,
// specs/plans/hp-sti-018-turning-satin.md §2.3/§4/§5) : pelage d'une region
// SANS trou en anneaux concentriques (`build_turning_satin_sections`, appele
// par `build_satin_columns` quand `evaluate_satinability` classe la region
// `Ambiguous` ou `Unsuitable`+`has_wide_area`), et extraction partagee
// `build_ring_band_sections` (dont `build_annular_sections`, deja en
// production sur le trou unique d'une region annulaire, devient un mince
// wrapper).
//
// `build_ring_band_sections`/`build_turning_satin_sections` ne sont PAS
// exposees hors de satin_column.cpp (anonymous namespace, meme convention
// que `build_annular_sections` avant cette phase) -- tout est donc verifie
// ICI via l'API publique `build_satin_columns`, exactement comme
// test_columns.cpp le fait deja pour "ring".
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/auto_satin/satinability.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/stitch_generation/satin.hpp"

using namespace openstitch;
using namespace openstitch::auto_satin;

namespace {

SatinColumnsResult columns_of(const std::string& shape) {
    const auto region = make_shape(shape);
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100}; // 0,1 mm : rapide
    return build_satin_columns(*region, params);
}

// --- croisement de segments, meme pattern que test_corridor.cpp -----------
// (copie locale volontaire -- ni l'un ni l'autre fichier n'expose la sienne,
// meme convention que `chaikin`/`make_rectangle` deja dupliques localement
// dans test_corridor.cpp).
bool segments_cross_p2(Vec2um a, Vec2um b, Vec2um c, Vec2um d) {
    const auto toD = [](Vec2um p) {
        return std::pair{static_cast<double>(p.x.value), static_cast<double>(p.y.value)};
    };
    const auto [ax, ay] = toD(a);
    const auto [bx, by] = toD(b);
    const auto [cx, cy] = toD(c);
    const auto [dx, dy] = toD(d);
    const auto cross2 = [](double ux, double uy, double vx, double vy) {
        return ux * vy - uy * vx;
    };
    const auto orient = [&](double px, double py, double qx, double qy, double rx, double ry) {
        return cross2(qx - px, qy - py, rx - px, ry - py);
    };
    const double o1 = orient(ax, ay, bx, by, cx, cy);
    const double o2 = orient(ax, ay, bx, by, dx, dy);
    const double o3 = orient(cx, cy, dx, dy, ax, ay);
    const double o4 = orient(cx, cy, dx, dy, bx, by);
    return (o1 > 0) != (o2 > 0) && (o3 > 0) != (o4 > 0) && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0;
}

void check_columns_no_self_crossing(const std::vector<SatinColumnGeometry>& columns) {
    for (const auto& col : columns) {
        const std::size_t n = std::min(col.rail_a.nodes.size(), col.rail_b.nodes.size());
        for (std::size_t i = 1; i < n; ++i) {
            INFO("station #" << i);
            CHECK_FALSE(segments_cross_p2(col.rail_a.nodes[i - 1].pos, col.rail_a.nodes[i].pos,
                                          col.rail_b.nodes[i - 1].pos, col.rail_b.nodes[i].pos));
            CHECK_FALSE(segments_cross_p2(col.rail_a.nodes[i - 1].pos, col.rail_b.nodes[i - 1].pos,
                                          col.rail_a.nodes[i].pos, col.rail_b.nodes[i].pos));
        }
    }
}

bool any_warning_contains(const std::vector<std::string>& warnings, const std::string& needle) {
    return std::any_of(warnings.begin(), warnings.end(),
                       [&](const std::string& w) { return w.find(needle) != std::string::npos; });
}

// Disque avec une encoche radiale peu profonde (profil polaire continu,
// aucune arete vive) centree sur l'angle 0 : sert UNIQUEMENT au test
// d'acceptation PARTIELLE ci-dessous -- un disque pur (cf. "disc_15mm"/
// "circle") pele toujours proprement jusqu'a disparition sous erosion
// (Clipper2 degrade "gracieusement" un offset convexe), jamais par un
// croisement/largeur hors plage sur un anneau INTERMEDIAIRE precis. Une
// irregularite non convexe est necessaire pour qu'UN anneau donne echoue sa
// validation alors que les anneaux plus exterieurs restent corrects --
// calibre empiriquement (cf. rapport de livraison) : profondeur 30% du
// rayon, demi-angle 15 degres, rayon 10mm, largeur d'anneau 1,5mm produit
// exactement ce resultat (anneau 2 refuse par croisement local, anneaux 0/1
// conserves).
geometry::PathSet disc_with_notch(double radius, double notchHalfAngleDeg, double notchDepthFrac) {
    geometry::Path p;
    p.closed = true;
    constexpr int n = 128;
    const double notchHalfAngle = notchHalfAngleDeg * std::numbers::pi / 180.0;
    for (int i = 0; i < n; ++i) {
        const double t = 2.0 * std::numbers::pi * i / n;
        double wrapped = t;
        if (wrapped > std::numbers::pi) {
            wrapped -= 2.0 * std::numbers::pi;
        }
        double r = radius;
        if (std::abs(wrapped) < notchHalfAngle) {
            const double shape = 1.0 - std::abs(wrapped) / notchHalfAngle; // 1 au centre, 0 au bord
            r = radius * (1.0 - notchDepthFrac * shape);
        }
        p.nodes.push_back(geometry::PathNode{
            Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(r * std::cos(t)))},
                   Micrometers{static_cast<std::int32_t>(std::lround(r * std::sin(t)))}},
            geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    return geometry::PathSet{p, {}};
}

} // namespace

// ---------------------------------------------------------------------
// § item 1 (rapport de livraison) : extraction `build_ring_band_sections`
// hors de `build_annular_sections` -- refactor PUR, comportement inchange.
// Preuve directe : `git diff` de l'extraction est un deplacement de code
// (signature/corps inchanges, seul `polys`/`fullRegionPolys` differe dans
// le test `in_region` du barreau, cf. satin_column.cpp) ; preuve indirecte
// ICI, en re-asserant exactement les memes invariants structurels que
// test_columns.cpp verifie deja sur "ring" (connectivite cyclique des 4
// sections, barreaux >= 2, section_index/count) PLUS determinisme --
// la suite COMPLETE (test_columns.cpp, test_satin_column_view.cpp) deja
// verte avant et apres l'extraction (meme commit) est la preuve principale.
// ---------------------------------------------------------------------
TEST_CASE("build_ring_band_sections (extrait) : \"ring\" -- determinisme, deux appels identiques") {
    const auto a = columns_of("ring");
    const auto b = columns_of("ring");
    REQUIRE(a.refusal.empty());
    REQUIRE(a.status == SatinabilityStatus::RequiresDecomposition);
    REQUIRE(a.columns.size() == 4);
    REQUIRE(b.columns.size() == 4);
    for (std::size_t i = 0; i < a.columns.size(); ++i) {
        INFO("section " << i);
        REQUIRE(a.columns[i].rail_a.nodes.size() == b.columns[i].rail_a.nodes.size());
        for (std::size_t k = 0; k < a.columns[i].rail_a.nodes.size(); ++k) {
            CHECK(a.columns[i].rail_a.nodes[k].pos == b.columns[i].rail_a.nodes[k].pos);
            CHECK(a.columns[i].rail_b.nodes[k].pos == b.columns[i].rail_b.nodes[k].pos);
        }
        REQUIRE(a.columns[i].rungs.size() == b.columns[i].rungs.size());
        for (std::size_t k = 0; k < a.columns[i].rungs.size(); ++k) {
            CHECK(a.columns[i].rungs[k].a == b.columns[i].rungs[k].a);
            CHECK(a.columns[i].rungs[k].b == b.columns[i].rungs[k].b);
        }
    }
    check_columns_no_self_crossing(a.columns);
}

// ---------------------------------------------------------------------
// § item 2/3 (rapport) : classification reelle de "petal" verifiee
// directement (pas supposee) -- le fixture a ete dimensionne pour tomber
// dans `Ambiguous`, confirme ici.
// ---------------------------------------------------------------------
TEST_CASE("petal : classifie Ambiguous par evaluate_satinability (verifie, pas suppose)") {
    const auto region = make_shape("petal");
    REQUIRE(region.has_value());
    AutoSatinParameters analysisParams;
    analysisParams.raster.pixel_size = Micrometers{100};
    const auto analysis = analyze_region(*region, analysisParams);
    REQUIRE(analysis.has_value());
    CHECK(analysis->report.status == SatinabilityStatus::Ambiguous);
    CHECK_FALSE(analysis->report.is_elongated);
}

TEST_CASE("disc_15mm : classifie Ambiguous par evaluate_satinability (verifie, pas suppose)") {
    const auto region = make_shape("disc_15mm");
    REQUIRE(region.has_value());
    AutoSatinParameters analysisParams;
    analysisParams.raster.pixel_size = Micrometers{100};
    const auto analysis = analyze_region(*region, analysisParams);
    REQUIRE(analysis.has_value());
    CHECK(analysis->report.status == SatinabilityStatus::Ambiguous);
}

// ---------------------------------------------------------------------
// § item 3 (rapport) : AVANT HP-STI-018 Phase D, une region `Ambiguous`
// etait TOUJOURS refusee sans aucune colonne (cf. le switch de
// `build_satin_columns`, case `Ambiguous`, inchange -- seul un succes du
// pelage en amont l'evite desormais). "disc_15mm"/"petal" sont de nouveaux
// fixtures (n'existaient pas avant cette phase), donc il n'y a pas de test
// historique a "avant/apres" sur eux directement -- la preuve avant/apres
// vit sur "circle"/"wide" (cf. test_columns.cpp, qui documente
// explicitement le changement de comportement avec le commentaire du
// fixture correspondant). Ici : succes du pelage sur les DEUX nouveaux
// fixtures nommes d'apres la roadmap.
// ---------------------------------------------------------------------
TEST_CASE("disc_15mm : satin tournant -- >= 2 anneaux, bandes valides, multiple de 4, sans "
          "croisement") {
    const auto r = columns_of("disc_15mm");
    CHECK(r.refusal.empty());
    REQUIRE(r.status == SatinabilityStatus::RequiresDecomposition);
    REQUIRE(r.columns.size() % 4 == 0);
    const std::size_t ringCount = r.columns.size() / 4;
    CHECK(ringCount >= 2);
    check_columns_no_self_crossing(r.columns);
    CHECK(any_warning_contains(r.warnings, "satin tournant"));
}

TEST_CASE("petal : satin tournant -- >= 2 anneaux, bandes valides, multiple de 4, sans "
          "croisement") {
    const auto r = columns_of("petal");
    CHECK(r.refusal.empty());
    REQUIRE(r.status == SatinabilityStatus::RequiresDecomposition);
    REQUIRE(r.columns.size() % 4 == 0);
    const std::size_t ringCount = r.columns.size() / 4;
    CHECK(ringCount >= 2);
    check_columns_no_self_crossing(r.columns);
    CHECK(any_warning_contains(r.warnings, "satin tournant"));
}

// ---------------------------------------------------------------------
// § item 4 (rapport) : disque "tight inner ring" -- un seul anneau tient
// avant que la passe suivante ne fasse disparaitre la forme (cf.
// shapes.cpp : rayon 5mm, largeur d'anneau par defaut 3mm). Sert aussi a
// probe le risque documente §3 du plan (courbure serree pres du centre,
// `kJumpDegPerMm` dans `fill_satin_columns`).
// ---------------------------------------------------------------------
TEST_CASE("disc_tight_inner_ring : exactement 1 anneau avant arret (cap/collapse)") {
    const auto r = columns_of("disc_tight_inner_ring");
    CHECK(r.refusal.empty());
    REQUIRE(r.status == SatinabilityStatus::RequiresDecomposition);
    REQUIRE(r.columns.size() == 4); // 1 seul anneau = 4 sections
    check_columns_no_self_crossing(r.columns);
    CHECK(any_warning_contains(r.warnings, "1 anneau"));
}

TEST_CASE("disc_tight_inner_ring : fill_satin_columns ne produit ni croisement ni saut "
          "disproportionne pres du centre (risque kJumpDegPerMm, section 3 du plan)") {
    // Ne corrige rien si un probleme est trouve (hors perimetre de cette
    // phase, cf. consigne) -- rapporte honnetement via WARN plutot que
    // d'echouer silencieusement un CHECK sur une valeur non calibree.
    const auto r = columns_of("disc_tight_inner_ring");
    REQUIRE(r.columns.size() == 4);
    std::size_t totalJumps = 0;
    double maxSegmentUm = 0.0;
    for (const auto& col : r.columns) {
        std::vector<stitch_generation::SatinRungSeg> rungs;
        rungs.reserve(col.rungs.size());
        for (const auto& rg : col.rungs) {
            rungs.emplace_back(rg.a, rg.b);
        }
        stitch_generation::SatinConfig cfg;
        cfg.density = Micrometers{400};
        const auto fill = stitch_generation::fill_satin_columns(col.rail_a, col.rail_b, rungs, cfg);
        totalJumps += fill.jump_before.size();
        for (std::size_t i = 1; i < fill.satin.size(); ++i) {
            const double dx =
                static_cast<double>(fill.satin[i].x.value - fill.satin[i - 1].x.value);
            const double dy =
                static_cast<double>(fill.satin[i].y.value - fill.satin[i - 1].y.value);
            maxSegmentUm = std::max(maxSegmentUm, std::sqrt(dx * dx + dy * dy));
        }
    }
    WARN("disc_tight_inner_ring -- fill_satin_columns : jump_before total="
         << totalJumps << " segment max=" << maxSegmentUm << " um");
    // Pas d'assertion dure sur une valeur non calibree (§3 du plan : risque
    // documente, pas un defaut deja prouve) -- seule l'absence de crash/
    // valeur aberrante grossiere (plus large que la colonne entiere) est
    // verifiee.
    CHECK(maxSegmentUm <
          20'000.0); // 20 mm : tout saut au-dela serait une aberration, pas un detail
}

// ---------------------------------------------------------------------
// § item 5 (rapport) : acceptation PARTIELLE -- un anneau interieur echoue
// sa validation (croisement local) tandis que les anneaux exterieurs
// restent conserves, au lieu d'un refus total (comportement DELIBEREMENT
// different de `build_annular_sections`, cf. commentaire de
// `build_turning_satin_sections` dans satin_column.cpp).
// ---------------------------------------------------------------------
TEST_CASE("satin tournant : acceptation partielle -- anneau interieur refuse, anneaux "
          "exterieurs conserves") {
    const auto region = disc_with_notch(10'000.0, 15.0, 0.3);
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    params.turning_satin_ring_width = Micrometers{1'500};
    const auto r = build_satin_columns(region, params);

    REQUIRE(r.refusal.empty()); // le PREMIER anneau a reussi -> pas de refus total
    REQUIRE(r.status == SatinabilityStatus::RequiresDecomposition);
    // 2 anneaux conserves (8 colonnes) avant qu'un 3e echoue sa validation --
    // valeur calibree empiriquement sur ce fixture precis (cf. rapport de
    // livraison) : une vraie acceptation PARTIELLE, ni 0 (refus total) ni le
    // nombre d'anneaux qu'on obtiendrait si tout avait reussi.
    REQUIRE(r.columns.size() == 8);
    check_columns_no_self_crossing(r.columns);
    CHECK(any_warning_contains(r.warnings, "refuse"));
    CHECK(any_warning_contains(r.warnings, "conserve"));
}

// ---------------------------------------------------------------------
// § item 6 (rapport) : determinisme (deux executions, sortie identique a
// l'octet) sur les deux fixtures principaux du pelage.
// ---------------------------------------------------------------------
TEST_CASE("satin tournant : determinisme (disc_15mm, petal)") {
    for (const std::string& shape : {std::string("disc_15mm"), std::string("petal")}) {
        INFO("forme = " << shape);
        const auto a = columns_of(shape);
        const auto b = columns_of(shape);
        REQUIRE(a.columns.size() == b.columns.size());
        for (std::size_t i = 0; i < a.columns.size(); ++i) {
            REQUIRE(a.columns[i].rail_a.nodes.size() == b.columns[i].rail_a.nodes.size());
            for (std::size_t k = 0; k < a.columns[i].rail_a.nodes.size(); ++k) {
                CHECK(a.columns[i].rail_a.nodes[k].pos == b.columns[i].rail_a.nodes[k].pos);
                CHECK(a.columns[i].rail_b.nodes[k].pos == b.columns[i].rail_b.nodes[k].pos);
            }
        }
    }
}

// ---------------------------------------------------------------------
// § item 7 (rapport) : note Phase B.5 sur "multi_neck" -- CORRECTIF (revue
// Phase D, verifie independamment via `openstitch-cli sgsd-debug
// --shape multi_neck`) : cette affirmation NE TIENT PAS pour la vraie
// fixture. Le squelette complet de "multi_neck" (shapes.cpp) a 0 jonction,
// 1 seul arc -- la decomposition SGSD (satin_planning) ne coupe qu'aux
// nœuds de jonction, donc elle ne decoupe JAMAIS "multi_neck" en masses
// isolees : la forme entiere (358,03 mm²) reste une seule region, construite
// directement ET via SGSD, a une couverture identique (40,69 %, FAIL) AVANT
// et APRES ce lot -- inchange par Phase D. Le test ci-dessous reste utile
// (il prouve que LE PELAGE lui-meme fonctionne sur une masse circulaire
// isolee, construite a la main), mais ce n'est PAS ce que SGSD produit
// reellement pour "multi_neck" -- ne pas lire ce test comme une preuve que
// le dispatch Phase D resout "multi_neck" en pratique. Cette fixture reste
// un gap ouvert, toujours territoire Phase D au sens large (axe degenere a
// un hub), mais non resolu par ce lot.
// ---------------------------------------------------------------------
TEST_CASE("satin tournant sur une masse circulaire isolee (rayon 6mm, memes proportions que la "
          "masse de shapes.cpp::multi_neck -- ne prouve PAS que multi_neck lui-meme est resolu, "
          "cf. commentaire ci-dessus)") {
    geometry::Path hub;
    hub.closed = true;
    constexpr int n = 64;
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * std::numbers::pi * i / n;
        hub.nodes.push_back(geometry::PathNode{
            Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(6'000.0 * std::cos(a)))},
                   Micrometers{static_cast<std::int32_t>(std::lround(6'000.0 * std::sin(a)))}},
            geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    geometry::PathSet region{hub, {}};
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    const auto r = build_satin_columns(region, params);
    CHECK(r.refusal.empty());
    CHECK(r.status == SatinabilityStatus::RequiresDecomposition);
    CHECK_FALSE(r.columns.empty());
}
