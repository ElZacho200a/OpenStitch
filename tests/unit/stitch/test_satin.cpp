// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "openstitch/stitch_generation/lock.hpp"
#include "openstitch/stitch_generation/satin.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

geometry::Path open_path(std::initializer_list<std::pair<std::int32_t, std::int32_t>> pts) {
    geometry::Path p;
    p.closed = false;
    for (const auto& [x, y] : pts) {
        p.nodes.push_back(
            {Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner, {}, {}});
    }
    return p;
}

Vec2um v(std::int32_t x, std::int32_t y) { return {Micrometers{x}, Micrometers{y}}; }
SatinRungSeg rung(std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by) {
    return {v(ax, ay), v(bx, by)};
}
Vec2um mid(Vec2um a, Vec2um b) {
    return {Micrometers{(a.x.value + b.x.value) / 2}, Micrometers{(a.y.value + b.y.value) / 2}};
}

}  // namespace

// --- Lot 2 : générateur satin par barreaux -----------------------------------

TEST_CASE("satin colonnes : espacement median regulier (colonne droite)") {
    const auto railA = open_path({{0, 0}, {20'000, 0}});
    const auto railB = open_path({{0, 4'000}, {20'000, 4'000}});
    const std::vector<SatinRungSeg> rungs{rung(0, 0, 0, 4'000), rung(10'000, 0, 10'000, 4'000),
                                          rung(20'000, 0, 20'000, 4'000)};
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    const auto r = fill_satin_columns(railA, railB, rungs, cfg);
    REQUIRE(r.satin.size() >= 6);
    REQUIRE(r.satin.size() % 2 == 0);

    // Ligne médiane à y = 2000 ; espacement perpendiculaire ~1 mm, régulier.
    double maxGap = 0.0;
    Vec2um prev = mid(r.satin[0], r.satin[1]);
    CHECK(prev.y.value == 2'000);
    for (std::size_t i = 2; i < r.satin.size(); i += 2) {
        const Vec2um m = mid(r.satin[i], r.satin[i + 1]);
        CHECK(m.y.value == 2'000);
        maxGap = std::max(maxGap, length_um(m - prev));
        prev = m;
    }
    CHECK(maxGap <= 1'250.0);  // aucun écart ne dépasse nettement la densité
}

TEST_CASE("satin colonnes : barreaux traverses exactement") {
    const auto railA = open_path({{0, 0}, {20'000, 0}});
    const auto railB = open_path({{0, 4'000}, {20'000, 4'000}});
    const std::vector<SatinRungSeg> rungs{rung(0, 0, 0, 4'000), rung(10'000, 0, 10'000, 4'000),
                                          rung(20'000, 0, 20'000, 4'000)};
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    const auto r = fill_satin_columns(railA, railB, rungs, cfg);
    // Un fil passe EXACTEMENT par le barreau central (10000,0)-(10000,4000).
    bool found = false;
    for (std::size_t i = 0; i + 1 < r.satin.size(); i += 2) {
        if (r.satin[i] == v(10'000, 0) && r.satin[i + 1] == v(10'000, 4'000)) {
            found = true;
        }
    }
    CHECK(found);
}

// --- Saut plutôt qu'un point continu disproportionné (§ audit lettres) ------
//
// Défaut trouvé sur un projet réel (texte en périphérie d'un sceau, lettre en
// T) : au coude intérieur d'une lettre, deux barreaux consécutifs venaient de
// deux bords quasi perpendiculaires (rail A avance ~horizontalement, rail B
// ~verticalement, sur le même intervalle). `fill_satin_columns` forçait un
// point continu d'environ 5 mm, une diagonale visible traversant la lettre.
// Pratique standard en broderie (logiciels commerciaux du métier) : lever le
// fil plutôt que de forcer un point disproportionné.
TEST_CASE("satin colonnes : coin quasi perpendiculaire -> saut plutot qu'un point continu") {
    // Rail A avance surtout en X entre les deux barreaux, rail B surtout en Y
    // (~85 degres d'ecart) : reproduit la geometrie du coin de lettre en T.
    // Un tel virage franc implique nécessairement une largeur de barreau qui
    // varie beaucoup sur l'intervalle (geometrie du ruban, pas un artefact) --
    // l'invariant testé n'est donc pas "aucun point large" mais "aucun point
    // PLUS large qu'aucun des deux barreaux réels qui encadrent l'intervalle"
    // (avant le correctif, l'appariement défaillant produisait un point
    // continu qui excédait même le plus large des deux).
    const auto railA = open_path({{0, 0}, {3'600, -200}});
    const auto railB = open_path({{-200, -50}, {-100, -3'700}});
    const std::vector<SatinRungSeg> rungs{rung(0, 0, -200, -50), rung(3'600, -200, -100, -3'700)};
    const double maxRungWidth =
        std::max(length_um(v(0, 0) - v(-200, -50)), length_um(v(3'600, -200) - v(-100, -3'700)));
    SatinConfig cfg;
    cfg.density = Micrometers{400};
    const auto r = fill_satin_columns(railA, railB, rungs, cfg);

    REQUIRE_FALSE(r.jump_before.empty());
    // Aucun segment cousu (hors saut) ne dépasse la largeur du plus large des
    // deux barreaux réels encadrant l'intervalle.
    std::size_t nextBreak = 0;
    for (std::size_t i = 1; i < r.satin.size(); ++i) {
        const bool isBreak = nextBreak < r.jump_before.size() && r.jump_before[nextBreak] == i;
        if (isBreak) {
            ++nextBreak;
            continue;
        }
        CHECK(length_um(r.satin[i] - r.satin[i - 1]) <= maxRungWidth + 50.0);
    }
}

// Garde-fou anti-faux-positif : une terminaison effilée ORDINAIRE (largeur
// qui décroît jusqu'à un point, rails toujours co-directionnels) ne doit
// JAMAIS déclencher de saut -- seul un changement de DIRECTION franc entre
// deux barreaux consécutifs doit le faire, pas une simple variation de
// largeur.
TEST_CASE("satin colonnes : terminaison effilee ordinaire -> aucun saut") {
    const auto railA = open_path({{0, 0}, {10'000, 0}});
    const auto railB = open_path({{0, 3'000}, {10'000, 0}});  // converge vers le meme point
    const std::vector<SatinRungSeg> rungs{rung(0, 0, 0, 3'000), rung(10'000, 0, 10'000, 0)};
    SatinConfig cfg;
    cfg.density = Micrometers{400};
    const auto r = fill_satin_columns(railA, railB, rungs, cfg);
    CHECK(r.jump_before.empty());
}

// --- § refonte auto-satin paramétrique : rails Bézier épars -----------------
//
// `fill_satin_columns` doit aplatir (`geometry::flatten`) un rail Bézier
// épars (quelques `PathNode` à `tan_in`/`tan_out`) avant d'en tirer les
// points de couture -- sinon la courbure serait ignorée et les poignées de
// contrôle reconnectées par des cordes (§ étape 11 : « avancer selon la
// longueur d'arc... pas selon le paramètre Bézier brut »).
TEST_CASE("satin colonnes : rail Bezier eparse -> points suivent la courbe, pas la corde") {
    // Un seul segment cubique en arc de cercle grossier : les deux rails
    // s'arquent vers le haut (poignées verticales), tout en restant
    // parallèles (même déplacement de poignée), pour une largeur constante.
    geometry::Path railA;
    railA.closed = false;
    {
        geometry::PathNode a;
        a.pos = v(0, 0);
        a.type = geometry::NodeType::Smooth;
        a.tan_out = Vec2um{Micrometers{6'667}, Micrometers{0}};
        geometry::PathNode b;
        b.pos = v(20'000, 0);
        b.type = geometry::NodeType::Smooth;
        b.tan_in = Vec2um{Micrometers{-6'667}, Micrometers{0}};
        railA.nodes = {a, b};
    }
    geometry::Path railB;
    railB.closed = false;
    {
        geometry::PathNode a;
        a.pos = v(0, 4'000);
        a.type = geometry::NodeType::Smooth;
        a.tan_out = Vec2um{Micrometers{6'667}, Micrometers{0}};
        geometry::PathNode b;
        b.pos = v(20'000, 4'000);
        b.type = geometry::NodeType::Smooth;
        b.tan_in = Vec2um{Micrometers{-6'667}, Micrometers{0}};
        railB.nodes = {a, b};
    }
    // Fait bomber la courbe hors de la corde : poignées perpendiculaires au
    // segment direct, donc le point médian de la courbe s'écarte largement
    // de y=0 (rail A) / y=4000 (rail B) si (et seulement si) la courbure est
    // effectivement suivie.
    railA.nodes[0].tan_out = Vec2um{Micrometers{5'000}, Micrometers{8'000}};
    railA.nodes[1].tan_in = Vec2um{Micrometers{-5'000}, Micrometers{8'000}};
    railB.nodes[0].tan_out = Vec2um{Micrometers{5'000}, Micrometers{8'000}};
    railB.nodes[1].tan_in = Vec2um{Micrometers{-5'000}, Micrometers{8'000}};

    const std::vector<SatinRungSeg> rungs{rung(0, 0, 0, 4'000), rung(20'000, 0, 20'000, 4'000)};
    SatinConfig cfg;
    cfg.density = Micrometers{500};
    const auto r = fill_satin_columns(railA, railB, rungs, cfg);
    REQUIRE(r.satin.size() >= 4);

    // Si les rails étaient traités comme leurs seuls nœuds de contrôle
    // (corde directe, l'ancien comportement), tout point de couture aurait
    // y == 0 ou y == 4000 exactement. La courbure du milieu du rail A doit
    // dépasser nettement cette corde -- la géométrie prouve que `flatten` a
    // bien été appelé, pas seulement que le test l'espère.
    double maxYAboveChordA = 0.0;
    for (const auto& p : r.satin) {
        if (p.y.value < 2'000) {  // du côté du rail A (sous le milieu)
            maxYAboveChordA = std::max(maxYAboveChordA, static_cast<double>(p.y.value));
        }
    }
    CHECK(maxYAboveChordA > 500.0);  // largement au-dessus de la corde y=0
}

TEST_CASE("satin colonnes : rails de longueurs differentes") {
    // Rail A courbe (plus long) ; rail B droit (plus court). Doit rester cohérent.
    const auto railA = open_path({{0, 0}, {10'000, 3'000}, {20'000, 0}});
    const auto railB = open_path({{0, 6'000}, {20'000, 6'000}});
    const std::vector<SatinRungSeg> rungs{rung(0, 0, 0, 6'000), rung(10'000, 3'000, 10'000, 6'000),
                                          rung(20'000, 0, 20'000, 6'000)};
    SatinConfig cfg;
    cfg.density = Micrometers{800};
    const auto r = fill_satin_columns(railA, railB, rungs, cfg);
    REQUIRE(r.satin.size() >= 6);
    CHECK(r.max_width_um > 0.0);
    // Espacement médian borné (pas de saut géant malgré la différence de longueur).
    Vec2um prev = mid(r.satin[0], r.satin[1]);
    double maxGap = 0.0;
    for (std::size_t i = 2; i < r.satin.size(); i += 2) {
        const Vec2um m = mid(r.satin[i], r.satin[i + 1]);
        maxGap = std::max(maxGap, length_um(m - prev));
        prev = m;
    }
    CHECK(maxGap <= 1'100.0);
}

TEST_CASE("satin colonnes : moins de deux barreaux -> repli fill_satin") {
    const auto railA = open_path({{0, 0}, {10'000, 0}});
    const auto railB = open_path({{0, 3'000}, {10'000, 3'000}});
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    const std::vector<SatinRungSeg> one{rung(0, 0, 0, 3'000)};
    CHECK(fill_satin_columns(railA, railB, one, cfg).satin == fill_satin(railA, railB, cfg).satin);
}

TEST_CASE("satin colonnes : deterministe") {
    const auto railA = open_path({{0, 0}, {15'000, 1'000}});
    const auto railB = open_path({{0, 5'000}, {15'000, 6'000}});
    const std::vector<SatinRungSeg> rungs{rung(0, 0, 0, 5'000), rung(15'000, 1'000, 15'000, 6'000)};
    SatinConfig cfg;
    cfg.density = Micrometers{600};
    CHECK(fill_satin_columns(railA, railB, rungs, cfg).satin ==
          fill_satin_columns(railA, railB, rungs, cfg).satin);
}

// --- Lot 3 : points courts (virages) -----------------------------------------

namespace {
// Colonne à virage : rail A intérieur (droit, court), rail B extérieur (détour).
struct Column {
    geometry::Path a;
    geometry::Path b;
    std::vector<SatinRungSeg> rungs;
};
Column curved_column() {
    Column c;
    c.a = open_path({{0, 0}, {20'000, 0}});
    c.b = open_path({{0, 6'000}, {5'000, 20'000}, {15'000, 20'000}, {20'000, 6'000}});
    c.rungs = {rung(0, 0, 0, 6'000), rung(10'000, 0, 10'000, 20'000),
               rung(20'000, 0, 20'000, 6'000)};
    return c;
}
SatinConfig short_cfg(ShortStitchMode mode) {
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    cfg.short_stitch = mode;
    cfg.short_stitch_min_gap = Micrometers{900};  // force l'action sur ce virage modéré
    return cfg;
}
}  // namespace

TEST_CASE("short stitches : inset modifie le rail interieur en virage") {
    const Column c = curved_column();
    const auto off = fill_satin_columns(c.a, c.b, c.rungs, short_cfg(ShortStitchMode::Disabled));
    const auto multi = fill_satin_columns(c.a, c.b, c.rungs, short_cfg(ShortStitchMode::MultiLevelInset));
    const auto single = fill_satin_columns(c.a, c.b, c.rungs, short_cfg(ShortStitchMode::SingleInset));
    // Les points courts changent le tracé (rail intérieur rentré) mais gardent
    // le même nombre de pénétrations (inset, pas suppression).
    CHECK(multi.satin != off.satin);
    CHECK(single.satin != off.satin);
    CHECK(multi.satin.size() == off.satin.size());
}

TEST_CASE("short stitches : remove-and-redistribute reduit les penetrations") {
    const Column c = curved_column();
    const auto off = fill_satin_columns(c.a, c.b, c.rungs, short_cfg(ShortStitchMode::Disabled));
    const auto rem = fill_satin_columns(c.a, c.b, c.rungs,
                                        short_cfg(ShortStitchMode::RemoveAndRedistribute));
    CHECK(rem.satin.size() < off.satin.size());
    CHECK(rem.satin.size() >= 4);  // ne vide jamais la colonne
}

TEST_CASE("short stitches : deterministe") {
    const Column c = curved_column();
    for (auto m : {ShortStitchMode::SingleInset, ShortStitchMode::MultiLevelInset,
                   ShortStitchMode::RemoveAndRedistribute}) {
        CHECK(fill_satin_columns(c.a, c.b, c.rungs, short_cfg(m)).satin ==
              fill_satin_columns(c.a, c.b, c.rungs, short_cfg(m)).satin);
    }
}

// --- Lot 3 : split stitches (traversées longues) -----------------------------

namespace {
Column wide_column() {
    Column c;
    c.a = open_path({{0, 0}, {20'000, 0}});
    c.b = open_path({{0, 10'000}, {20'000, 10'000}});  // 10 mm de large
    c.rungs = {rung(0, 0, 0, 10'000), rung(10'000, 0, 10'000, 10'000),
               rung(20'000, 0, 20'000, 10'000)};
    return c;
}
SatinConfig split_cfg(SplitStitchMode mode) {
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    cfg.split_stitch = mode;
    cfg.max_stitch_length = Micrometers{4'000};  // 10 mm > 4 mm -> 2 splits par fil
    return cfg;
}
}  // namespace

TEST_CASE("split : traversee longue subdivisee") {
    const Column c = wide_column();
    const auto off = fill_satin_columns(c.a, c.b, c.rungs, split_cfg(SplitStitchMode::Disabled));
    const auto simple = fill_satin_columns(c.a, c.b, c.rungs, split_cfg(SplitStitchMode::Simple));
    CHECK(simple.satin.size() > off.satin.size());  // pénétrations intermédiaires ajoutées
    // Chaque fil = 4 points (a, s1, s2, b) : des points strictement entre 0 et 10 mm.
    int between = 0;
    for (const auto& p : simple.satin) {
        if (p.y.value > 100 && p.y.value < 9'900) ++between;
    }
    CHECK(between > 0);
}

TEST_CASE("split : staggered decale les points (pas de ligne centrale)") {
    const Column c = wide_column();
    const auto simple = fill_satin_columns(c.a, c.b, c.rungs, split_cfg(SplitStitchMode::Simple));
    const auto stag = fill_satin_columns(c.a, c.b, c.rungs, split_cfg(SplitStitchMode::Staggered));
    // Simple : 1er point de split identique d'un fil à l'autre (ligne centrale).
    CHECK(simple.satin[1].y == simple.satin[5].y);
    // Staggered : décalé.
    CHECK(stag.satin[1].y != stag.satin[5].y);
}

TEST_CASE("split : jitter deterministe et reproductible") {
    const Column c = wide_column();
    const auto j1 = fill_satin_columns(c.a, c.b, c.rungs, split_cfg(SplitStitchMode::DeterministicJitter));
    const auto j2 = fill_satin_columns(c.a, c.b, c.rungs, split_cfg(SplitStitchMode::DeterministicJitter));
    CHECK(j1.satin == j2.satin);
    const auto simple = fill_satin_columns(c.a, c.b, c.rungs, split_cfg(SplitStitchMode::Simple));
    CHECK(j1.satin != simple.satin);  // varie autour des positions régulières
}

// --- Lot 3 : terminaisons -----------------------------------------------------

namespace {
double thread_width(const std::vector<Vec2um>& satin, std::size_t thread) {
    return length_um(satin[2 * thread] - satin[2 * thread + 1]);
}
}  // namespace

TEST_CASE("caps : tapered reduit la largeur au bout sans l'annuler") {
    const auto railA = open_path({{0, 0}, {20'000, 0}});
    const auto railB = open_path({{0, 6'000}, {20'000, 6'000}});
    const std::vector<SatinRungSeg> rungs{rung(0, 0, 0, 6'000), rung(20'000, 0, 20'000, 6'000)};
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    cfg.cap_end = SatinCapType::Tapered;
    cfg.cap_length = 4;
    const auto r = fill_satin_columns(railA, railB, rungs, cfg);
    const std::size_t nThreads = r.satin.size() / 2;
    REQUIRE(nThreads >= 8);
    const double last = thread_width(r.satin, nThreads - 1);
    const double middle = thread_width(r.satin, nThreads / 2);
    CHECK(last < middle * 0.5);  // effilé
    CHECK(last > 0.0);           // jamais un point unique
    CHECK(middle > 5'000.0);     // milieu à pleine largeur (~6 mm)
}

TEST_CASE("caps : flat garde la pleine largeur au bout") {
    const auto railA = open_path({{0, 0}, {20'000, 0}});
    const auto railB = open_path({{0, 6'000}, {20'000, 6'000}});
    const std::vector<SatinRungSeg> rungs{rung(0, 0, 0, 6'000), rung(20'000, 0, 20'000, 6'000)};
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};  // cap_end = Flat par défaut
    const auto r = fill_satin_columns(railA, railB, rungs, cfg);
    const std::size_t nThreads = r.satin.size() / 2;
    CHECK(thread_width(r.satin, nThreads - 1) > 5'000.0);
}

// --- Barreaux par défaut (satin manuel) --------------------------------------
//
// Défaut trouvé par revue : un satin créé sans barreaux explicites (deux
// rails seuls, cf. `rails_from_contour`) retombe sur `fill_satin`, qui
// n'implémente qu'un sous-ensemble de `SatinConfig` (densité, compensation
// symétrique, sous-couche centrale) — tous les autres réglages (terminaisons,
// split, sous-couches de bord/zigzag, compensation push/pull asymétrique)
// restent silencieusement sans effet. `default_rungs` produit des barreaux
// (par la même correspondance ladder que `fill_satin`) pour amener ce cas sur
// le chemin `fill_satin_columns`, qui les implémente tous.

TEST_CASE("barreaux par defaut : au moins deux barreaux sur une colonne simple") {
    const auto railA = open_path({{0, 0}, {20'000, 0}});
    const auto railB = open_path({{0, 5'000}, {20'000, 5'000}});
    const auto rungs = default_rungs(railA, railB, Micrometers{2'000});
    REQUIRE(rungs.size() >= 2);
    // Chaque barreau relie effectivement un point du rail A à un point du rail B.
    for (const auto& r : rungs) {
        CHECK(r.first.y.value == 0);
        CHECK(r.second.y.value == 5'000);
    }
}

TEST_CASE("barreaux par defaut : vide sur des rails degeneres") {
    const auto railA = open_path({{0, 0}});  // un seul point
    const auto railB = open_path({{0, 5'000}, {20'000, 5'000}});
    CHECK(default_rungs(railA, railB, Micrometers{2'000}).empty());
}

TEST_CASE("barreaux par defaut : debloque les reglages ignores par fill_satin (terminaison effilee)") {
    // Même colonne, même config (cap_end = Tapered), UNE FOIS sans barreaux
    // (fill_satin, l'ancien comportement pour un satin manuel -- Tapered SANS
    // EFFET) et une fois avec les barreaux par défaut (fill_satin_columns,
    // Tapered rétrécit réellement le bout) -- démontre que le défaut est
    // réellement corrigé, pas seulement que `default_rungs` produit des
    // barreaux.
    const auto railA = open_path({{0, 0}, {20'000, 0}});
    const auto railB = open_path({{0, 6'000}, {20'000, 6'000}});
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    cfg.cap_end = SatinCapType::Tapered;

    const auto withoutRungs = fill_satin(railA, railB, cfg);
    const std::size_t nBare = withoutRungs.satin.size() / 2;
    REQUIRE(nBare >= 2);
    CHECK(thread_width(withoutRungs.satin, nBare - 1) > 5'500.0);  // pleine largeur : Tapered ignoré

    const auto rungs = default_rungs(railA, railB, cfg.density);
    REQUIRE(rungs.size() >= 2);
    const auto withRungs = fill_satin_columns(railA, railB, rungs, cfg);
    const std::size_t nCols = withRungs.satin.size() / 2;
    REQUIRE(nCols >= 2);
    CHECK(thread_width(withRungs.satin, nCols - 1) < 5'500.0);  // effilé : Tapered appliqué
}

// --- Lot 5 : lock stitches ---------------------------------------------------

TEST_CASE("lock : None -> vide ; sinon ancre au point et de taille bornee") {
    const Vec2um anchor = v(1'000, 1'000);
    const Vec2um toward = v(3'000, 1'000);  // vers +x
    CHECK(lock_stitches(anchor, toward, LockType::None, Micrometers{800}, 2).empty());
    for (auto type : {LockType::BackAndForth, LockType::Triangle, LockType::MicroZigzag}) {
        const auto pts = lock_stitches(anchor, toward, type, Micrometers{800}, 2);
        REQUIRE(pts.size() >= 3);
        CHECK(pts.front() == anchor);  // commence à l'ancre
        // Reste proche de l'ancre (quelques taille de lock).
        for (const auto& p : pts) {
            CHECK(length_um(p - anchor) <= 2'000.0);
        }
        // Le lock progresse dans la direction de couture (+x).
        std::int32_t maxX = anchor.x.value;
        for (const auto& p : pts) maxX = std::max(maxX, p.x.value);
        CHECK(maxX >= anchor.x.value + 600);
    }
}

TEST_CASE("lock : jamais au-dela du point voisin (colonne fine)") {
    // Défaut trouvé par revue : `length` (souvent la valeur par défaut,
    // 800 µm) n'était jamais comparée à la distance réelle vers `toward` (le
    // point du rail OPPOSÉ, donc la largeur locale du barreau) — sur une
    // colonne fine (largeur < `length`, cas courant pour du texte fin),
    // l'aiguille piquait hors de la matière déjà cousue.
    const Vec2um anchor = v(0, 0);
    const Vec2um toward = v(300, 0);  // rail opposé très proche (colonne 0,3 mm)
    for (auto type : {LockType::BackAndForth, LockType::Triangle, LockType::MicroZigzag}) {
        const auto pts = lock_stitches(anchor, toward, type, Micrometers{800}, 2);
        for (const auto& p : pts) {
            CHECK(p.x.value <= 300);  // jamais au-delà de `toward` le long de l'axe
        }
    }
}

TEST_CASE("lock : deterministe") {
    const auto a = lock_stitches(v(0, 0), v(0, 5'000), LockType::MicroZigzag, Micrometers{600}, 3);
    const auto b = lock_stitches(v(0, 0), v(0, 5'000), LockType::MicroZigzag, Micrometers{600}, 3);
    CHECK(a == b);
}

// --- Lot 4 : sous-couches + compensation -------------------------------------

namespace {
Column straight6() {
    Column c;
    c.a = open_path({{0, 0}, {20'000, 0}});
    c.b = open_path({{0, 6'000}, {20'000, 6'000}});
    c.rungs = {rung(0, 0, 0, 6'000), rung(10'000, 0, 10'000, 6'000),
               rung(20'000, 0, 20'000, 6'000)};
    return c;
}
std::int32_t minY(const std::vector<Vec2um>& v) {
    std::int32_t m = INT32_MAX;
    for (auto p : v) m = std::min(m, p.y.value);
    return m;
}
std::int32_t maxY(const std::vector<Vec2um>& v) {
    std::int32_t m = INT32_MIN;
    for (auto p : v) m = std::max(m, p.y.value);
    return m;
}
std::int32_t maxX(const std::vector<Vec2um>& v) {
    std::int32_t m = INT32_MIN;
    for (auto p : v) m = std::max(m, p.x.value);
    return m;
}
std::int32_t minX(const std::vector<Vec2um>& v) {
    std::int32_t m = INT32_MAX;
    for (auto p : v) m = std::min(m, p.x.value);
    return m;
}
}  // namespace

TEST_CASE("underlays : center, edge et zigzag = passes distinctes ordonnees") {
    const Column c = straight6();
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    cfg.center_underlay = true;
    cfg.underlay_edge = true;
    cfg.underlay_zigzag = true;
    const auto r = fill_satin_columns(c.a, c.b, c.rungs, cfg);
    REQUIRE(r.underlays.size() == 4);  // center, edge A, edge B, zigzag
    // Center : sur l'axe (y ~ 3000).
    for (auto p : r.underlays[0].points) CHECK(std::abs(p.y.value - 3'000) < 50);
    // Edge A : rentré du rail A (y=0) de ~0,6 mm.
    for (auto p : r.underlays[1].points) CHECK(std::abs(p.y.value - 600) < 100);
    // Edge B : rentré du rail B (y=6000).
    for (auto p : r.underlays[2].points) CHECK(std::abs(p.y.value - 5'400) < 100);
    CHECK(r.underlays[3].points.size() >= 2);  // zigzag présent
}

TEST_CASE("underlays : desactivees par defaut") {
    const Column c = straight6();
    SatinConfig cfg;  // tout désactivé
    CHECK(fill_satin_columns(c.a, c.b, c.rungs, cfg).underlays.empty());
}

TEST_CASE("compensation pull : elargit un seul cote (asymetrique)") {
    const Column c = straight6();
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    cfg.pull_left = Micrometers{800};  // côté A (rail à y=0) uniquement
    const auto r = fill_satin_columns(c.a, c.b, c.rungs, cfg);
    CHECK(minY(r.satin) <= -750);   // côté A poussé au-delà de y=0
    CHECK(maxY(r.satin) <= 6'050);  // côté B inchangé (~6000)
    CHECK(maxY(r.satin) >= 5'950);
}

TEST_CASE("compensation push : etend la colonne au bout") {
    const Column c = straight6();
    SatinConfig base;
    base.density = Micrometers{1'000};
    SatinConfig ext = base;
    ext.push_end = Micrometers{2'000};
    CHECK(maxX(fill_satin_columns(c.a, c.b, c.rungs, ext).satin) >
          maxX(fill_satin_columns(c.a, c.b, c.rungs, base).satin) + 1'500);
}

TEST_CASE("compensation push : retraction bornee, jamais au-dela de la station voisine") {
    // Défaut trouvé par revue : contrairement à `pull_left`/`pull_right`
    // (bornés par `pull_max`), `push_start`/`push_end` n'avaient aucune borne.
    // Une rétraction excessive (`push_start` très négatif) faisait passer le
    // premier fil de la colonne DE L'AUTRE CÔTÉ du second, inversant leur
    // ordre le long de l'axe (auto-croisement au tout début de la colonne).
    // La rétraction est désormais bornée pour ne jamais dépasser la station
    // voisine (colonne `straight6()` : premier barreau à x=0, voisin ~x=1000).
    const Column c = straight6();
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    cfg.push_start = Micrometers{-50'000};  // rétraction demandée très excessive
    const auto r = fill_satin_columns(c.a, c.b, c.rungs, cfg);
    REQUIRE(r.satin.size() >= 4);
    // Sans borne, le premier fil se retrouverait vers x=-50000 (auto-croisement
    // massif avec tout le reste de la colonne). Avec la borne, il reste proche
    // de x=0, jamais au-delà de la station voisine dans le sens négatif.
    CHECK(minX(r.satin) > -1'000);
}

TEST_CASE("underlays + compensation : deterministe") {
    const Column c = straight6();
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    cfg.underlay_edge = true;
    cfg.pull_left = Micrometers{300};
    cfg.push_end = Micrometers{1'000};
    const auto x = fill_satin_columns(c.a, c.b, c.rungs, cfg);
    const auto y = fill_satin_columns(c.a, c.b, c.rungs, cfg);
    CHECK(x.satin == y.satin);
    REQUIRE(x.underlays.size() == y.underlays.size());
    for (std::size_t i = 0; i < x.underlays.size(); ++i) {
        CHECK(x.underlays[i].points == y.underlays[i].points);
    }
}

TEST_CASE("satin : colonne droite, zigzag entre les deux rails") {
    // Deux rails horizontaux, longueur 20 mm, ecartes de 4 mm.
    const auto railA = open_path({{0, 0}, {20'000, 0}});
    const auto railB = open_path({{0, 4'000}, {20'000, 4'000}});
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};  // 20 mm / 1 mm -> ~20 crossings
    const auto result = fill_satin(railA, railB, cfg);

    CHECK(result.max_width_um == 4'000.0);
    // 21 crossings x 2 points = 42 points de satin.
    CHECK(result.satin.size() == 42);
    // Chaque paire de points relie les deux bords (y=0 et y=4000).
    for (const Vec2um& p : result.satin) {
        CHECK((p.y.value == 0 || p.y.value == 4'000));
    }
}

TEST_CASE("satin : la densite controle le nombre de penetrations") {
    const auto railA = open_path({{0, 0}, {10'000, 0}});
    const auto railB = open_path({{0, 2'000}, {10'000, 2'000}});
    SatinConfig dense;
    dense.density = Micrometers{500};
    SatinConfig coarse;
    coarse.density = Micrometers{2'000};
    CHECK(fill_satin(railA, railB, dense).satin.size() >
          fill_satin(railA, railB, coarse).satin.size());
}

TEST_CASE("satin : compensation de tirage elargit la colonne") {
    const auto railA = open_path({{0, 0}, {10'000, 0}});
    const auto railB = open_path({{0, 4'000}, {10'000, 4'000}});
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    cfg.pull_compensation = Micrometers{500};
    const auto result = fill_satin(railA, railB, cfg);
    // Les points debordent de 0,5 mm de chaque cote (y=-500 et y=4500).
    bool below = false;
    bool above = false;
    for (const Vec2um& p : result.satin) {
        if (p.y.value <= -500) below = true;
        if (p.y.value >= 4'500) above = true;
    }
    CHECK(below);
    CHECK(above);
}

TEST_CASE("satin : sous-couche centrale sur l'axe") {
    const auto railA = open_path({{0, 0}, {12'000, 0}});
    const auto railB = open_path({{0, 4'000}, {12'000, 4'000}});
    SatinConfig cfg;
    cfg.center_underlay = true;
    cfg.underlay_spacing = Micrometers{3'000};
    const auto result = fill_satin(railA, railB, cfg);
    REQUIRE(result.underlays.size() == 1);
    // Axe central : y = 2000.
    for (const Vec2um& p : result.underlays.front().points) {
        CHECK(p.y.value == 2'000);
    }
}

TEST_CASE("satin : rails degeneres -> resultat vide") {
    const auto single = open_path({{0, 0}});
    CHECK(fill_satin(single, single, {}).satin.empty());
}

TEST_CASE("satin : deterministe") {
    const auto railA = open_path({{0, 0}, {15'000, 1'000}});
    const auto railB = open_path({{0, 5'000}, {15'000, 6'000}});
    SatinConfig cfg;
    cfg.density = Micrometers{600};
    CHECK(fill_satin(railA, railB, cfg).satin == fill_satin(railA, railB, cfg).satin);
}

TEST_CASE("rails_from_contour : rectangle allonge -> deux longs rails") {
    // Rectangle 20x4 mm : les bouts sont les cotes courts.
    geometry::Path rect;
    rect.closed = true;
    rect.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{20'000}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{20'000}, Micrometers{4'000}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{0}, Micrometers{4'000}}, geometry::NodeType::Corner, {}, {}},
    };
    const auto rails = rails_from_contour(rect);
    REQUIRE(rails.has_value());
    // Les deux rails doivent produire un satin coherent (largeur ~4 mm).
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    const auto result = fill_satin(rails->first, rails->second, cfg);
    REQUIRE_FALSE(result.satin.empty());
    CHECK(result.max_width_um > 3'000.0);
    CHECK(result.max_width_um < 6'000.0);
}

TEST_CASE("rails_from_contour : contour trop petit -> nullopt") {
    geometry::Path tri;
    tri.closed = true;
    tri.nodes = {{Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}}};
    CHECK_FALSE(rails_from_contour(tri).has_value());
}
