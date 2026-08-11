// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/geometry/polyline.hpp"

using namespace openstitch;
using namespace openstitch::auto_satin;

namespace {

SatinColumnsResult columns_of(const std::string& shape) {
    const auto region = make_shape(shape);
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};  // 0,1 mm : rapide
    return build_satin_columns(*region, params);
}

// Point dans polygone (even-odd), coordonnées µm (Y vers le haut).
bool point_in_poly(const geometry::Path& poly, Vec2um p) {
    bool inside = false;
    const auto& n = poly.nodes;
    const std::size_t m = n.size();
    for (std::size_t i = 0, j = m - 1; i < m; j = i++) {
        const auto ax = n[i].pos.x.value, ay = n[i].pos.y.value;
        const auto bx = n[j].pos.x.value, by = n[j].pos.y.value;
        if (((ay > p.y.value) != (by > p.y.value)) &&
            (static_cast<double>(p.x.value) <
             static_cast<double>(bx - ax) * static_cast<double>(p.y.value - ay) /
                     static_cast<double>(by - ay) +
                 static_cast<double>(ax))) {
            inside = !inside;
        }
    }
    return inside;
}

bool in_region(const geometry::PathSet& region, Vec2um p) {
    if (!point_in_poly(region.outer, p)) {
        return false;
    }
    for (const auto& h : region.holes) {
        if (point_in_poly(h, p)) {
            return false;
        }
    }
    return true;
}

Vec2um mid(Vec2um a, Vec2um b) {
    return {Micrometers{(a.x.value + b.x.value) / 2}, Micrometers{(a.y.value + b.y.value) / 2}};
}

geometry::PathNode node(std::int32_t x, std::int32_t y) {
    return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner,
                              std::nullopt, std::nullopt};
}

double length_of(double x, double y) { return std::sqrt(x * x + y * y); }

// Distance minimale (µm) d'un point au segment [a,b].
double distance_to_segment(Vec2um p, Vec2um a, Vec2um b) {
    const double px = static_cast<double>(p.x.value), py = static_cast<double>(p.y.value);
    const double ax = static_cast<double>(a.x.value), ay = static_cast<double>(a.y.value);
    const double bx = static_cast<double>(b.x.value), by = static_cast<double>(b.y.value);
    const double dx = bx - ax, dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    const double t =
        len2 > 1e-9 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / len2, 0.0, 1.0) : 0.0;
    return length_of(px - (ax + dx * t), py - (ay + dy * t));
}

// Distance minimale (µm) d'un point au contour (polyligne fermée) d'une
// région : sert à vérifier qu'un rail suit réellement le bord de la forme
// (§ rails inchangés, construits sur le contour réel).
double distance_to_contour(const geometry::PathSet& region, Vec2um p) {
    double best = std::numeric_limits<double>::max();
    const auto scan = [&](const geometry::Path& poly) {
        const std::size_t n = poly.nodes.size();
        for (std::size_t i = 0; i < n; ++i) {
            best = std::min(best,
                            distance_to_segment(p, poly.nodes[i].pos, poly.nodes[(i + 1) % n].pos));
        }
    };
    scan(region.outer);
    for (const auto& h : region.holes) {
        scan(h);
    }
    return best;
}

// Rassemble toutes les extrémités de rail (A et B) des sections touchant
// `junctionId` et les regroupe par position quasi identique (tolérance 5 µm).
// Invariant vérifié : aucun groupe de PLUS DE DEUX membres — un sommet de
// confluence appartient au maximum à deux rails de sections différentes.
// Un groupe de 3+ signale une collision (deux rails DIFFÉRENTS convergeant
// par erreur vers le même sommet, créant un barreau dégénéré). Un groupe de 1
// est légitime : toutes les jonctions ne sont pas des étoiles symétriques
// (une branche de T peut n'avoir qu'un seul côté touchant une vraie encoche,
// l'autre suit simplement le bord plat de la forme — cf. test dédié "Y" pour
// l'invariant plus fort applicable aux confluences symétriques).
void check_junction_no_collision(const SatinColumnsResult& r, std::uint32_t junctionId) {
    std::vector<Vec2um> endpoints;
    for (const auto& col : r.columns) {
        if (col.start_junction && *col.start_junction == junctionId) {
            REQUIRE_FALSE(col.rail_a.nodes.empty());
            endpoints.push_back(col.rail_a.nodes.front().pos);
            endpoints.push_back(col.rail_b.nodes.front().pos);
        }
        if (col.end_junction && *col.end_junction == junctionId) {
            REQUIRE_FALSE(col.rail_a.nodes.empty());
            endpoints.push_back(col.rail_a.nodes.back().pos);
            endpoints.push_back(col.rail_b.nodes.back().pos);
        }
    }
    std::vector<std::pair<Vec2um, int>> groups;
    for (const auto& p : endpoints) {
        bool found = false;
        for (auto& [gp, count] : groups) {
            if (length_um(gp - p) < 5.0) {
                ++count;
                found = true;
                break;
            }
        }
        if (!found) {
            groups.push_back({p, 1});
        }
    }
    INFO("endpoints = " << endpoints.size() << " groupes = " << groups.size());
    for (const auto& [gp, count] : groups) {
        CHECK(count <= 2);
    }
}

// Largeur maximale mesurée le long d'un rail (distance rail_a[i]<->rail_b[i]) :
// sert à détecter une dérive de section transversale près d'une jonction (le
// défaut corrigé par l'ancrage — cf. test dédié).
double max_station_width(const SatinColumnGeometry& col) {
    double w = 0.0;
    const std::size_t n = std::min(col.rail_a.nodes.size(), col.rail_b.nodes.size());
    for (std::size_t i = 0; i < n; ++i) {
        w = std::max(w, length_um(col.rail_a.nodes[i].pos - col.rail_b.nodes[i].pos));
    }
    return w;
}

// Intersection propre de deux segments (croisement transversal), coordonnées
// µm. Copie locale volontaire du test de `satin_column.cpp` (interne, non
// exposé) : sert uniquement à vérifier qu'aucun barreau ne croise un autre
// dans les tests de non-régression ci-dessous.
bool segments_cross_2d(Vec2um a, Vec2um b, Vec2um c, Vec2um d) {
    const auto cross2 = [](double ax, double ay, double bx, double by) { return ax * by - ay * bx; };
    const auto orient = [&](Vec2um p, Vec2um q, Vec2um r) {
        return cross2(static_cast<double>(q.x.value - p.x.value),
                      static_cast<double>(q.y.value - p.y.value),
                      static_cast<double>(r.x.value - p.x.value),
                      static_cast<double>(r.y.value - p.y.value));
    };
    const double o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
    return (o1 > 0) != (o2 > 0) && (o3 > 0) != (o4 > 0) && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0;
}

}  // namespace

// --- Formes simples : colonnes cohérentes ------------------------------------

TEST_CASE("colonnes : formes simples produisent au moins une colonne") {
    for (const char* s : {"rectangle", "capsule", "ribbon", "s"}) {
        const auto r = columns_of(s);
        INFO("forme = " << s << " statut = " << to_string(r.status)
                        << " refus = " << r.refusal);
        CHECK(r.refusal.empty());
        REQUIRE(r.columns.size() >= 1);
        const auto& c = r.columns.front();
        CHECK(c.rail_a.nodes.size() >= 2);
        CHECK(c.rail_b.nodes.size() == c.rail_a.nodes.size());
        CHECK(c.rungs.size() >= 2);
        CHECK(c.mean_width_um > 0.0);
    }
}

TEST_CASE("colonnes : milieu des barreaux interieur a la region") {
    const auto region = make_shape("capsule");
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    const auto r = build_satin_columns(*region, params);
    REQUIRE(r.columns.size() >= 1);
    int outside = 0;
    for (const auto& col : r.columns) {
        for (const auto& rung : col.rungs) {
            if (!in_region(*region, mid(rung.a, rung.b))) {
                ++outside;
            }
        }
    }
    CHECK(outside == 0);
}

TEST_CASE("colonnes : rails et barreaux finis, non degeneres") {
    const auto r = columns_of("ribbon");
    REQUIRE(r.columns.size() >= 1);
    for (const auto& col : r.columns) {
        for (const auto& n : col.rail_a.nodes) {
            CHECK(std::isfinite(static_cast<double>(n.pos.x.value)));
            CHECK(std::isfinite(static_cast<double>(n.pos.y.value)));
        }
        for (const auto& rung : col.rungs) {
            CHECK(length_um(rung.a - rung.b) > 0.0);  // largeur non nulle
        }
    }
}

// --- Extension des bouts ouverts jusqu'au bord réel ---------------------------
//
// Défaut trouvé par revue (mission « auto-satin béton ») : un bout OUVERT du
// squelette (sans jonction) s'arrête, par construction de l'amincissement,
// sensiblement avant le bord réel de la région — un embout arrondi ou pointu
// n'était pas couvert du tout par le satin, et même un bout CARRÉ retracte
// (retrait générique proche de la demi-largeur locale, artefact connu de
// l'amincissement). Démontré visuellement sur « capsule » (rect 40 mm + deux
// demi-cercles de rayon 2,5 mm, longueur totale bout-à-bout 45 mm) : avant
// correction, la colonne s'arrêtait à ~38,8 mm ; les deux embouts (2,5 mm de
// rayon chacun, ~11 % de la longueur totale) restaient entièrement hors
// couture. Confirmé aussi sur un bout plat (« rectangle », retrait ~2,55 mm
// par bout avant correction).

TEST_CASE("colonnes : bout ouvert (capsule) atteint le bord reel") {
    const auto r = columns_of("capsule");
    REQUIRE(r.refusal.empty());
    REQUIRE(r.columns.size() == 1);
    const auto& c = r.columns.front();
    // Longueur bout-a-bout reelle = 45 mm (40 mm de corps + 2 x 2,5 mm de
    // demi-cercle). Avant correction : ~38,8 mm (embouts non couverts).
    CHECK(std::abs(c.length_um - 45'000.0) < 1'000.0);
    // Les rails atteignent la pointe reelle de chaque demi-cercle : x = -2500
    // (gauche) et x = 42500 (droite), rayon W = 2500 depuis les centres
    // x=0/x=40000 (cf. shapes.cpp). Marge large : rasterisation 0,1 mm +
    // pas d'echantillonnage 0,5 mm.
    const auto firstX = c.rail_a.nodes.front().pos.x.value;
    const auto lastX = c.rail_a.nodes.back().pos.x.value;
    const auto [minX, maxX] = std::minmax(firstX, lastX);
    CHECK(minX < -1'800);
    CHECK(maxX > 41'800);
}

TEST_CASE("colonnes : extend_open_ends=false conserve l'ancien comportement") {
    const auto region = make_shape("capsule");
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    params.extend_open_ends = false;
    const auto r = build_satin_columns(*region, params);
    REQUIRE(r.columns.size() == 1);
    // Sans extension, la colonne s'arrete nettement avant le bord reel
    // (~38,8 mm avant correction) : verifie que le bascule desactive bien le
    // nouveau comportement plutot que de toujours l'appliquer.
    CHECK(r.columns.front().length_um < 40'000.0);
}

TEST_CASE("colonnes : bout carre (rectangle) atteint aussi le bord reel") {
    // Un bout CARRE (perpendiculaire, sans arrondi) souffre du meme defaut :
    // l'amincissement retracte generiquement le squelette d'un bout plat
    // avant le coin reel (d'environ la demi-largeur locale). Mesure sur
    // "rectangle" (40 mm, bouts plats) : sans extension, la colonne s'arrete a
    // ~34,9 mm (retrait ~2,55 mm par bout, proche de la demi-largeur 2,5 mm) ;
    // l'extension corrige aussi ce cas, pas seulement les embouts arrondis.
    const auto region = make_shape("rectangle");
    REQUIRE(region.has_value());
    SatinColumnsParameters withExt;
    withExt.analysis.raster.pixel_size = Micrometers{100};
    SatinColumnsParameters noExt = withExt;
    noExt.extend_open_ends = false;
    const auto a = build_satin_columns(*region, withExt);
    const auto b = build_satin_columns(*region, noExt);
    REQUIRE(a.columns.size() == 1);
    REQUIRE(b.columns.size() == 1);
    // Longueur reelle bout-a-bout = 40 mm (x de 0 a 40000, cf. shapes.cpp).
    CHECK(std::abs(a.columns.front().length_um - 40'000.0) < 1'000.0);
    // Sans extension, retrait mesurable (regression du defaut d'origine).
    CHECK(a.columns.front().length_um - b.columns.front().length_um > 2'000.0);
}

TEST_CASE("colonnes : bouts de jonction (Y) jamais etendus, arretes ouverts allonges") {
    SatinColumnsParameters withExt;
    withExt.analysis.raster.pixel_size = Micrometers{100};
    SatinColumnsParameters noExt = withExt;
    noExt.extend_open_ends = false;
    const auto region = make_shape("y");
    REQUIRE(region.has_value());
    const auto a = build_satin_columns(*region, withExt);
    const auto b = build_satin_columns(*region, noExt);
    REQUIRE(a.refusal.empty());
    REQUIRE(b.refusal.empty());
    REQUIRE(a.columns.size() == b.columns.size());
    REQUIRE(a.columns.size() >= 2);
    // La jonction partagee reste geometriquement identique (non affectee par
    // l'extension, qui ne touche que le bout SANS jonction de chaque bras).
    std::set<std::uint32_t> junctionsA, junctionsB;
    for (const auto& c : a.columns) {
        if (c.start_junction) junctionsA.insert(*c.start_junction);
        if (c.end_junction) junctionsA.insert(*c.end_junction);
    }
    for (const auto& c : b.columns) {
        if (c.start_junction) junctionsB.insert(*c.start_junction);
        if (c.end_junction) junctionsB.insert(*c.end_junction);
    }
    CHECK(junctionsA.size() == 1);
    CHECK(junctionsB.size() == 1);
    // Chaque bras s'est allonge (bout ouvert etendu jusqu'au bord reel).
    for (std::size_t i = 0; i < a.columns.size(); ++i) {
        CHECK(a.columns[i].length_um > b.columns[i].length_um);
    }
}

TEST_CASE("colonnes : extension deterministe") {
    const auto region = make_shape("capsule");
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    const auto a = build_satin_columns(*region, params);
    const auto b = build_satin_columns(*region, params);
    REQUIRE(a.columns.size() == b.columns.size());
    for (std::size_t i = 0; i < a.columns.size(); ++i) {
        CHECK(a.columns[i].rail_a == b.columns[i].rail_a);
        CHECK(a.columns[i].rail_b == b.columns[i].rail_b);
        REQUIRE(a.columns[i].rungs.size() == b.columns[i].rungs.size());
        for (std::size_t j = 0; j < a.columns[i].rungs.size(); ++j) {
            CHECK(a.columns[i].rungs[j].a == b.columns[i].rungs[j].a);
            CHECK(a.columns[i].rungs[j].b == b.columns[i].rungs[j].b);
        }
    }
}

TEST_CASE("colonnes : aucun barreau degenere apres extension (capsule)") {
    const auto r = columns_of("capsule");
    REQUIRE(r.columns.size() == 1);
    for (const auto& rung : r.columns.front().rungs) {
        CHECK(length_um(rung.a - rung.b) > 0.0);
    }
}

// --- Décomposition et refus --------------------------------------------------

TEST_CASE("colonnes : Y produit plusieurs colonnes (decomposition)") {
    const auto r = columns_of("y");
    INFO("statut = " << to_string(r.status) << " colonnes = " << r.columns.size());
    // RequiresDecomposition : une colonne par branche, ou refus clair.
    CHECK((r.columns.size() >= 2 || !r.refusal.empty()));
}

TEST_CASE("colonnes : un reseau Y identifie ses sections et sa jonction") {
    const auto r = columns_of("y");
    REQUIRE(r.refusal.empty());
    REQUIRE(r.columns.size() >= 2);
    std::set<std::uint32_t> junctions;
    for (std::size_t i = 0; i < r.columns.size(); ++i) {
        const auto& column = r.columns[i];
        CHECK(column.section_index == i);
        CHECK(column.section_count == r.columns.size());
        CHECK(column.start_junction.has_value() != column.end_junction.has_value());
        if (column.start_junction) junctions.insert(*column.start_junction);
        if (column.end_junction) junctions.insert(*column.end_junction);
    }
    CHECK(junctions.size() == 1);
}

// --- Bridges de jonction, sans ancrage force (remplace l'ancien ancrage) -----
//
// Défaut d'origine trouvé par revue (mission « auto-satin béton », piste
// jonctions du brevet Pulse Microsystems) : la section transversale d'une
// branche dérive en approchant du nœud du squelette (balaie le bourrelet de
// la confluence). La correction d'ALORS forçait chaque paire de rails
// adjacents à coïncider EXACTEMENT sur une ancre partagée (sommet reflex, ou
// un rayon de secours) — ce qui déplaçait le dernier nœud de chaque rail en
// ligne droite vers ce point, produisant de grandes diagonales sur des
// confluences asymétriques (fixture "trident", cf. tests dédiés plus bas).
// Le modèle actuel (`JunctionBridge`, § remplacement de l'ancrage) ne déplace
// plus jamais un rail : chaque branche garde sa PROPRE dernière section
// transversale stable, verrouillée telle quelle. Sur une confluence
// symétrique, les trois bridges restent proches (petit noyau résiduel,
// exposé via `junction_cores`) mais ne sont plus forcés à coïncider au micron
// près.
TEST_CASE("colonnes : jonction Y symetrique - bridges locaux, sans ancre partagee forcee") {
    // La rasterisation par defaut des autres tests (0,1 mm) elague parfois une
    // des trois branches du Y (defaut PREEXISTANT de sensibilite du squelette
    // a la resolution, sans rapport avec les bridges). On verifie donc a une
    // resolution ou les 3 branches survivent (confirme empiriquement).
    const auto region = make_shape("y");
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{50};
    const auto r = build_satin_columns(*region, params);
    REQUIRE(r.refusal.empty());
    REQUIRE(r.columns.size() == 3);
    std::set<std::uint32_t> junctions;
    for (const auto& c : r.columns) {
        if (c.start_junction) junctions.insert(*c.start_junction);
        if (c.end_junction) junctions.insert(*c.end_junction);
    }
    REQUIRE(junctions.size() == 1);
    const std::uint32_t junctionId = *junctions.begin();
    check_junction_no_collision(r, junctionId);

    const auto junctionNode =
        std::find_if(r.debug.graph.nodes.begin(), r.debug.graph.nodes.end(),
                     [&](const auto& n) { return n.id == junctionId; });
    REQUIRE(junctionNode != r.debug.graph.nodes.end());

    std::vector<Vec2um> endpoints;
    for (const auto& col : r.columns) {
        if (col.start_junction) {
            endpoints.push_back(col.rail_a.nodes.front().pos);
            endpoints.push_back(col.rail_b.nodes.front().pos);
        }
        if (col.end_junction) {
            endpoints.push_back(col.rail_a.nodes.back().pos);
            endpoints.push_back(col.rail_b.nodes.back().pos);
        }
    }
    REQUIRE(endpoints.size() == 6);
    // Chaque bridge reste LOCAL a la confluence (aucun rail egare loin du
    // noeud du squelette).
    const double localityBound = static_cast<double>(params.junction_anchor_radius.value) * 2.0;
    for (const auto& p : endpoints) {
        CHECK(length_um(p - junctionNode->position) < localityBound);
    }

    // Le noyau central residuel (§7) est expose en diagnostic et reste petit
    // sur une confluence symetrique (les trois bridges se rejoignent presque).
    const auto core = std::find_if(r.junction_cores.begin(), r.junction_cores.end(),
                                   [&](const auto& c) { return c.junction_id == junctionId; });
    if (core != r.junction_cores.end()) {
        CHECK(core->area_um2 < 50.0 * 1'000'000.0);  // < 50 mm^2 (bandes de 5 mm de large)
    }
}

// --- Arête Jonction-Jonction (pont d'un "H") ---------------------------------
//
// Défaut trouvé par revue : `build_satin_columns` ne convertissait en colonne
// que les arêtes du squelette touchant au moins une EXTRÉMITÉ (bras du Y/T).
// Une arête reliant deux jonctions (le pont horizontal d'un "H", topologie
// absente des fixtures précédentes) n'était donc jamais essayée : `r.columns`
// restait non vide (les bras des deux barres verticales étaient bien produits)
// et `r.refusal` restait vide (aucun signal d'erreur), mais le pont lui-même —
// une portion entière, visible, de la région — ne recevait aucun point de
// broderie. Corrigé en traitant TOUTES les arêtes du graphe élagué, sans
// distinction sur le type de leurs extrémités (`try_edge` gère déjà chaque
// bout indépendamment selon son type).

TEST_CASE("colonnes : pont Jonction-Jonction d'un H converti en colonne") {
    const auto r = columns_of("h");
    REQUIRE(r.refusal.empty());
    // 5 arêtes de squelette attendues : 2 demi-barres par branche verticale
    // (haut/bas de chaque jonction) + le pont horizontal lui-même.
    REQUIRE(r.columns.size() == 5);

    const auto bridge = std::find_if(r.columns.begin(), r.columns.end(), [](const auto& c) {
        return c.start_junction.has_value() && c.end_junction.has_value();
    });
    REQUIRE(bridge != r.columns.end());
    CHECK(*bridge->start_junction != *bridge->end_junction);
    CHECK_FALSE(bridge->rungs.empty());
    for (const auto& rung : bridge->rungs) {
        CHECK(length_um(rung.a - rung.b) > 0.0);
    }

    std::set<std::uint32_t> junctions;
    for (const auto& c : r.columns) {
        if (c.start_junction) junctions.insert(*c.start_junction);
        if (c.end_junction) junctions.insert(*c.end_junction);
    }
    REQUIRE(junctions.size() == 2);
    for (auto j : junctions) {
        check_junction_no_collision(r, j);
    }
}

// --- Jonction a 4 branches (croix) -------------------------------------------
//
// L'exclusion de sommet reflex (`nearestExcluding`) n'est garantie que DANS un
// même appel de `trim_and_anchor_junction_end` (les deux rails d'une même
// station), jamais explicitement ENTRE les colonnes de branches différentes
// convergeant vers la même jonction à haut degré. Non corrigé (latent, jamais
// démontré défaillant) : verrouille le comportement actuel par un test dédié,
// utile en particulier maintenant que le correctif du squelette (voir
// skeleton_graph.cpp) rend la jonction de "croix" enfin correctement détectée
// à degré 4 (elle retombait auparavant à degré 2 par un bug distinct).

TEST_CASE("colonnes : jonction a 4 branches (croix) - aucune collision") {
    const auto r = columns_of("cross");
    REQUIRE(r.refusal.empty());
    REQUIRE(r.columns.size() == 4);
    std::set<std::uint32_t> junctions;
    for (const auto& c : r.columns) {
        if (c.start_junction) junctions.insert(*c.start_junction);
        if (c.end_junction) junctions.insert(*c.end_junction);
    }
    REQUIRE(junctions.size() == 1);
    check_junction_no_collision(r, *junctions.begin());
    for (const auto& col : r.columns) {
        for (const auto& rung : col.rungs) {
            CHECK(length_um(rung.a - rung.b) > 0.0);
        }
    }
}

TEST_CASE("colonnes : jonction T - aucune collision (barreau degenere)") {
    // Le "T" (cf. shapes.cpp) n'a que DEUX encoches reelles pour trois
    // branches : le sommet de la branche verticale est exactement au ras du
    // sommet de la barre horizontale (pas de troisieme encoche « en haut »
    // comme sur un Y symetrique). Chaque moitie de barre ne touche donc
    // qu'UNE seule encoche par son cote interne ; son cote externe n'a
    // legitimement aucune ancre a proximite. Verifie l'absence de collision
    // (deux rails DIFFERENTS convergeant a tort vers le meme sommet), le vrai
    // defaut trouve : avant le correctif d'exclusion, les DEUX rails d'une
    // meme demi-barre convergeaient vers l'unique encoche voisine, creant un
    // barreau de largeur nulle a cette station.
    const auto r = columns_of("t");
    REQUIRE(r.refusal.empty());
    std::set<std::uint32_t> junctions;
    for (const auto& c : r.columns) {
        if (c.start_junction) junctions.insert(*c.start_junction);
        if (c.end_junction) junctions.insert(*c.end_junction);
    }
    REQUIRE(junctions.size() == 1);
    check_junction_no_collision(r, *junctions.begin());
    for (const auto& col : r.columns) {
        for (const auto& rung : col.rungs) {
            CHECK(length_um(rung.a - rung.b) > 0.0);
        }
    }
}

// --- Résolution globale des ancres sur une jonction concave asymétrique ------
//
// Défaut trouvé par revue (capture fournie) : sur une confluence branchée/
// concave, la colonne venant d'une branche et celle venant d'une autre
// branche voisine ne se raccordaient pas exactement — chaque bout de
// jonction cherchait indépendamment son sommet reflex le plus proche
// (`trim_and_anchor_junction_end`), sans coordination entre branches
// ANGULAIREMENT ADJACENTES. Symptômes observés : rails terminaux qui ne
// partagent pas exactement le même point, derniers barreaux en éventail,
// zone triangulaire mal couverte près du centre. La fixture "trident"
// (cf. shapes.cpp) reproduit précisément le cas de la capture : une grande
// branche verticale épaisse, une branche interne POINTUE (triangle effilé,
// pas une bande à largeur constante) partant à angle aigu, et une branche
// latérale étroite quasi perpendiculaire — une confluence délibérément non
// étoilée, sans symétrie qui aiderait un ancrage indépendant à deviner la
// bonne encoche partagée par accident.
TEST_CASE("colonnes : jonction concave asymetrique (trident) - raccord coherent, pas d'eventail") {
    const auto region = make_shape("trident");
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    // La confluence à 3 branches très inégales (6 mm / 1,2 mm / pointe) crée
    // un bourrelet de recouvrement plus large qu'une jonction Y/T symétrique
    // à branches égales : seuils desserrés pour isoler ici la question
    // testée (cohérence du raccord), déjà couverte séparément par les tests
    // de seuils sur `notch`/`pinch`.
    params.analysis.thresholds.max_satin_width = Micrometers{30'000};
    params.max_adjacent_width_jump_ratio = 5.0;
    params.min_axis_coverage_ratio = 0.7;
    const auto r = build_satin_columns(*region, params);
    INFO("refus = " << r.refusal);
    REQUIRE(r.refusal.empty());
    REQUIRE(r.columns.size() == 3);

    std::set<std::uint32_t> junctions;
    for (const auto& c : r.columns) {
        if (c.start_junction) junctions.insert(*c.start_junction);
        if (c.end_junction) junctions.insert(*c.end_junction);
    }
    REQUIRE(junctions.size() == 1);
    const std::uint32_t junctionId = *junctions.begin();

    // Aucune collision à 3+ rails sur un même point (éventail / ancres
    // indépendantes divergentes) et aucun barreau dégénéré (largeur nulle),
    // y compris sur le côté qui n'a naturellement aucune encoche à portée.
    check_junction_no_collision(r, junctionId);
    for (const auto& col : r.columns) {
        for (const auto& rung : col.rungs) {
            CHECK(length_um(rung.a - rung.b) > 0.0);
        }
    }

    // Aucun trou triangulaire : chaque barreau (y compris les terminaux)
    // reste dans la région, et les barreaux terminaux des trois branches ne
    // se croisent pas entre eux (raccord en éventail détecté).
    std::vector<SatinRung> terminalRungs;
    for (const auto& col : r.columns) {
        REQUIRE_FALSE(col.rungs.empty());
        for (const auto& rung : col.rungs) {
            CHECK(in_region(*region, mid(rung.a, rung.b)));
        }
        if (col.start_junction && *col.start_junction == junctionId) {
            terminalRungs.push_back(col.rungs.front());
        }
        if (col.end_junction && *col.end_junction == junctionId) {
            terminalRungs.push_back(col.rungs.back());
        }
    }
    REQUIRE(terminalRungs.size() == 3);
    for (std::size_t i = 0; i + 1 < terminalRungs.size(); ++i) {
        for (std::size_t j = i + 1; j < terminalRungs.size(); ++j) {
            CHECK_FALSE(segments_cross_2d(terminalRungs[i].a, terminalRungs[i].b, terminalRungs[j].a,
                                          terminalRungs[j].b));
        }
    }
}

TEST_CASE("colonnes : jonction concave asymetrique (trident) - deterministe") {
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    params.analysis.thresholds.max_satin_width = Micrometers{30'000};
    params.max_adjacent_width_jump_ratio = 5.0;
    params.min_axis_coverage_ratio = 0.7;
    const auto region = make_shape("trident");
    REQUIRE(region.has_value());
    const auto a = build_satin_columns(*region, params);
    const auto b = build_satin_columns(*region, params);
    REQUIRE(a.columns.size() == b.columns.size());
    for (std::size_t i = 0; i < a.columns.size(); ++i) {
        CHECK(a.columns[i].rail_a == b.columns[i].rail_a);
        CHECK(a.columns[i].rail_b == b.columns[i].rail_b);
    }
}

// --- Géométrie des rails près d'une jonction (remplace la diagonale d'ancrage) ---
//
// L'invariant central de ce correctif : un bout de jonction ne doit plus
// JAMAIS produire un grand segment rectiligne artificiel (l'ancien
// `set_terminal_point` déplaçait le dernier nœud vers une ancre, créant une
// corde entre la dernière station stable et un point potentiellement
// éloigné). Puisque les stations consécutives d'un rail viennent toujours
// d'échantillons d'axe adjacents (pas `station_spacing`), un segment terminal
// anormalement long est la signature directe de ce défaut.
TEST_CASE("colonnes : jonction concave asymetrique (trident) - pas de segment "
         "rectiligne artificiel pres de la jonction") {
    const auto region = make_shape("trident");
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    params.analysis.thresholds.max_satin_width = Micrometers{30'000};
    params.max_adjacent_width_jump_ratio = 5.0;
    params.min_axis_coverage_ratio = 0.7;
    const auto r = build_satin_columns(*region, params);
    INFO("refus = " << r.refusal);
    REQUIRE(r.refusal.empty());  // aucune zone non couturee ne bloque la generation
    REQUIRE(r.columns.size() == 3);

    // Rails inchanges : deterministes, et chaque noeud reste sur le contour
    // reel de la region (tolerance generreuse pour le leger recul volontaire
    // de fermeture de `extend_tip`, tres inferieure a un defaut de plusieurs mm).
    const auto again = build_satin_columns(*region, params);
    REQUIRE(again.columns.size() == r.columns.size());
    constexpr double kOnContourToleranceUm = 200.0;  // 0,2 mm
    for (std::size_t ci = 0; ci < r.columns.size(); ++ci) {
        CHECK(again.columns[ci].rail_a == r.columns[ci].rail_a);
        CHECK(again.columns[ci].rail_b == r.columns[ci].rail_b);
        for (const auto& n : r.columns[ci].rail_a.nodes) {
            CHECK(distance_to_contour(*region, n.pos) < kOnContourToleranceUm);
        }
        for (const auto& n : r.columns[ci].rail_b.nodes) {
            CHECK(distance_to_contour(*region, n.pos) < kOnContourToleranceUm);
        }
    }

    // Segment terminal borné à quelques stations d'axe (jamais un saut de
    // plusieurs millimetres traversant la confluence).
    const double bound = static_cast<double>(params.station_spacing.value) * 4.0;
    for (const auto& col : r.columns) {
        const std::size_t n = col.rail_a.nodes.size();
        REQUIRE(n == col.rail_b.nodes.size());
        REQUIRE(n >= 2);
        if (col.start_junction) {
            CHECK(length_um(col.rail_a.nodes[0].pos - col.rail_a.nodes[1].pos) < bound);
            CHECK(length_um(col.rail_b.nodes[0].pos - col.rail_b.nodes[1].pos) < bound);
        }
        if (col.end_junction) {
            CHECK(length_um(col.rail_a.nodes[n - 1].pos - col.rail_a.nodes[n - 2].pos) < bound);
            CHECK(length_um(col.rail_b.nodes[n - 1].pos - col.rail_b.nodes[n - 2].pos) < bound);
        }
        // Progression curviligne monotone : chaque station d'axe successive
        // avance (jamais un rail qui rebrousse chemin sur lui-meme).
        for (std::size_t i = 1; i + 1 < n; ++i) {
            const double prevStep = length_um(col.rail_a.nodes[i].pos - col.rail_a.nodes[i - 1].pos);
            CHECK(prevStep > 0.0);
        }
    }

    // Identifie la jonction unique J1 de "trident".
    std::set<std::uint32_t> junctionIds;
    for (const auto& c : r.columns) {
        if (c.start_junction) junctionIds.insert(*c.start_junction);
        if (c.end_junction) junctionIds.insert(*c.end_junction);
    }
    REQUIRE(junctionIds.size() == 1);
    const std::uint32_t j1 = *junctionIds.begin();
    const auto j1Node = std::find_if(r.debug.graph.nodes.begin(), r.debug.graph.nodes.end(),
                                     [&](const auto& n) { return n.id == j1; });
    REQUIRE(j1Node != r.debug.graph.nodes.end());

    // 3 secteurs de jonction (§ StableBranchEnd/JunctionSeparator), un par branche.
    std::vector<JunctionSectorInfo> sectorsJ1;
    for (const auto& s : r.junction_sectors) {
        if (s.junction_id == j1) sectorsJ1.push_back(s);
    }
    REQUIRE(sectorsJ1.size() == 3);
    for (const auto& s : sectorsJ1) {
        REQUIRE(s.boundary.size() >= 4);  // separateur, ..., trailing, leading, ..., separateur
    }

    // 3 JunctionSeparator (un par espace angulaire entre 2 des 3 branches),
    // chacun PARTAGE EXACTEMENT par les deux secteurs qu'il borde : son point
    // doit apparaitre comme premier OU dernier sommet des deux secteurs
    // concernes (§4 -- construction de `build_sector`).
    std::vector<JunctionSeparatorInfo> separatorsJ1;
    for (const auto& s : r.junction_separators) {
        if (s.junction_id == j1) separatorsJ1.push_back(s);
    }
    REQUIRE(separatorsJ1.size() == 3);
    const auto sectorForColumn = [&](std::size_t columnIndex) -> const JunctionSectorInfo* {
        for (const auto& s : sectorsJ1) {
            if (s.column_index == columnIndex) return &s;
        }
        return nullptr;
    };
    for (const auto& sep : separatorsJ1) {
        const auto* before = sectorForColumn(sep.column_index_before);
        const auto* after = sectorForColumn(sep.column_index_after);
        REQUIRE(before != nullptr);
        REQUIRE(after != nullptr);
        CHECK((before->boundary.front() == sep.point || before->boundary.back() == sep.point));
        CHECK((after->boundary.front() == sep.point || after->boundary.back() == sep.point));
    }

    // Le JunctionCore résiduel : local à J1 (aucun point au-delà du rayon
    // local RÉELLEMENT calculé -- jamais un rayon fixe de 8 mm), petit,
    // et surtout NETTEMENT plus petit que l'ancien résultat invalide
    // (22,1266 mm² -- un polygone auto-croisé issu du modèle "dernière
    // section stable connectée directement à sa voisine", désormais
    // remplacé par la partition StableBranchEnd/JunctionSeparator/secteurs).
    bool sawJ1Core = false;
    for (const auto& core : r.junction_cores) {
        if (core.junction_id != j1) {
            continue;
        }
        sawJ1Core = true;
        INFO("aire mm2 = " << core.area_um2 / 1'000'000.0);
        INFO("local_radius mm = " << core.local_radius_um / 1000.0);
        INFO("actual_max_radius mm = " << core.actual_max_radius_um / 1000.0);
        CHECK(core.area_um2 >= 0.0);
        CHECK(core.area_um2 < 22'126'600.0 / 4.0);  // nettement < 22,1266 mm^2 (ancien resultat invalide)
        CHECK(core.local_radius_um <= core.configured_radius_um + 1.0);
        CHECK(core.actual_max_radius_um <= core.local_radius_um + 1.0);
        for (const auto& p : core.boundary) {
            CHECK(length_um(p - j1Node->position) <= core.local_radius_um + 1.0);
        }
    }
    CHECK(sawJ1Core);
}

TEST_CASE("colonnes : aucune derive de largeur pres d'une jonction (Y)") {
    const auto r = columns_of("y");
    REQUIRE(r.refusal.empty());
    for (const auto& col : r.columns) {
        // Largeur mediane comme reference (robuste aux quelques stations de
        // bord) ; aucune station ne doit depasser 1,2x cette reference apres
        // correction (avant correction : jusqu'a ~1,85x sur "y").
        std::vector<double> widths;
        const std::size_t n = std::min(col.rail_a.nodes.size(), col.rail_b.nodes.size());
        widths.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            widths.push_back(length_um(col.rail_a.nodes[i].pos - col.rail_b.nodes[i].pos));
        }
        REQUIRE_FALSE(widths.empty());
        std::sort(widths.begin(), widths.end());
        const double median = widths[widths.size() / 2];
        CHECK(max_station_width(col) <= median * 1.2);
    }
}

TEST_CASE("colonnes : anchor_junction_ends=false desactive l'ancrage") {
    const auto region = make_shape("y");
    REQUIRE(region.has_value());
    SatinColumnsParameters withAnchor;
    withAnchor.analysis.raster.pixel_size = Micrometers{100};
    SatinColumnsParameters noAnchor = withAnchor;
    noAnchor.anchor_junction_ends = false;
    const auto a = build_satin_columns(*region, withAnchor);
    const auto b = build_satin_columns(*region, noAnchor);
    REQUIRE(a.refusal.empty());
    REQUIRE(b.refusal.empty());
    REQUIRE(a.columns.size() == b.columns.size());
    // Bascule desactivee : la derive de largeur pres de la jonction reapparait
    // (aucune borne 1,2x garantie) sur au moins une colonne.
    bool anyDrift = false;
    for (const auto& col : b.columns) {
        std::vector<double> widths;
        const std::size_t n = std::min(col.rail_a.nodes.size(), col.rail_b.nodes.size());
        for (std::size_t i = 0; i < n; ++i) {
            widths.push_back(length_um(col.rail_a.nodes[i].pos - col.rail_b.nodes[i].pos));
        }
        std::sort(widths.begin(), widths.end());
        const double median = widths[widths.size() / 2];
        if (max_station_width(col) > median * 1.2) {
            anyDrift = true;
        }
    }
    CHECK(anyDrift);
}

TEST_CASE("colonnes : ancrage des jonctions deterministe") {
    const auto region = make_shape("y");
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    const auto a = build_satin_columns(*region, params);
    const auto b = build_satin_columns(*region, params);
    REQUIRE(a.columns.size() == b.columns.size());
    for (std::size_t i = 0; i < a.columns.size(); ++i) {
        CHECK(a.columns[i].rail_a == b.columns[i].rail_a);
        CHECK(a.columns[i].rail_b == b.columns[i].rail_b);
    }
}

TEST_CASE("colonnes : cercle refuse (direction ambigue)") {
    const auto r = columns_of("circle");
    CHECK(r.columns.empty());
    CHECK_FALSE(r.refusal.empty());
}

TEST_CASE("colonnes : anneau decompose en quatre sections ouvertes raccordees") {
    const auto r = columns_of("ring");
    REQUIRE(r.refusal.empty());
    REQUIRE(r.status == SatinabilityStatus::RequiresDecomposition);
    REQUIRE(r.columns.size() == 4);
    for (std::size_t i = 0; i < r.columns.size(); ++i) {
        const auto& current = r.columns[i];
        const auto& next = r.columns[(i + 1) % r.columns.size()];
        REQUIRE_FALSE(current.rail_a.closed);
        REQUIRE_FALSE(current.rail_b.closed);
        REQUIRE(current.rungs.size() >= 2);
        CHECK(current.section_index == i);
        CHECK(current.section_count == 4);
        REQUIRE(current.start_junction.has_value());
        REQUIRE(current.end_junction.has_value());
        CHECK(*current.start_junction == i);
        CHECK(*current.end_junction == (i + 1) % 4);
        CHECK(current.rail_a.nodes.back().pos == next.rail_a.nodes.front().pos);
        CHECK(current.rail_b.nodes.back().pos == next.rail_b.nodes.front().pos);
    }
}

TEST_CASE("colonnes : forme large refusee") {
    const auto r = columns_of("wide");
    CHECK(r.columns.empty());
    CHECK_FALSE(r.refusal.empty());
}

// Défaut trouvé sur un projet réel (logo circulaire "GISTRE" numérisé
// automatiquement) : géométrie EXACTE de la région en cause (contour à 54
// sommets, arc fin d'environ 130 mm de long -- vraisemblablement le trait de
// bordure du sceau capté par la segmentation), recentrée sur l'origine.
// Avant correctif : railA/railB s'effondraient l'un sur l'autre sur toute la
// longueur (aucun barreau), `cross_section` n'ayant aucun plancher de
// largeur -- une station de largeur quasi nulle était acceptée comme
// valide au lieu de déclencher le refus/l'interpolation déjà en place pour
// les autres types d'échec (§ audit génération partielle).
geometry::PathSet real_region_gistre_bordure() {
    geometry::Path p;
    p.closed = true;
    p.nodes = {
        node(-18606, -58556), node(-27442, -50342), node(-32296, -44243), node(-35407, -39390),
        node(-38145, -34162), node(-41630, -24953), node(-43746, -15121), node(-44244, -9520),
        node(-44244, -4044),  node(-43123, 5663),    node(-40634, 14749),  node(-38519, 19976),
        node(-35158, 26447),  node(-31674, 31799),   node(-28189, 36155),  node(-22464, 41880),
        node(-18979, 44742),  node(-12134, 49223),   node(-6409, 52210),   node(-1929, 54077),
        node(5539, 56317),    node(12259, 57561),    node(18109, 58059),   node(25078, 57935),
        node(30554, 57312),   node(38022, 55694),    node(44120, 53579),   node(44867, 53828),
        node(43746, 54574),   node(39764, 55943),    node(33914, 57437),   node(27567, 58433),
        node(16989, 58681),   node(7779, 57561),     node(-1306, 55072),   node(-5662, 53330),
        node(-11387, 50467),  node(-19228, 45240),   node(-25824, 39391),  node(-29931, 34910),
        node(-33665, 30057),  node(-38394, 21967),   node(-42003, 12882),  node(-43621, 6659),
        node(-44741, -684),   node(-44866, -10267),  node(-44119, -17610), node(-41630, -27317),
        node(-39639, -32544), node(-37150, -37647),  node(-32669, -44741), node(-27069, -51462),
        node(-22837, -55569), node(-18979, -58680),
    };
    return {p, {}};
}

TEST_CASE("colonnes : bordure fine d'un logo circulaire (projet reel) - jamais de rails "
         "confondus") {
    const auto region = real_region_gistre_bordure();
    SatinColumnsParameters params;  // parametres par defaut, comme autodigitize.cpp (pixel 50 um)
    params.analysis.thresholds.max_satin_width = Micrometers{6'000};  // options.satin_max_width par defaut
    const auto r = build_satin_columns(region, params);
    std::fprintf(stderr, "DIAG status=%d columns=%zu parametric=%zu refusal=%s\n",
                static_cast<int>(r.status), r.columns.size(), r.parametric_columns.size(),
                r.refusal.c_str());
    for (const auto& w : r.warnings) {
        std::fprintf(stderr, "DIAG warn: %s\n", w.c_str());
    }
    for (const auto& col : r.columns) {
        const auto flatA = geometry::flatten(col.rail_a, Micrometers{30});
        const auto flatB = geometry::flatten(col.rail_b, Micrometers{30});
        REQUIRE(flatA.points.size() == flatB.points.size());
        for (std::size_t i = 0; i < flatA.points.size(); ++i) {
            CHECK(length_um(flatA.points[i] - flatB.points[i]) >= 800.0);
        }
    }
    for (const auto& col : r.parametric_columns) {
        const auto flatA = geometry::flatten(col.rail_a, Micrometers{30});
        const auto flatB = geometry::flatten(col.rail_b, Micrometers{30});
        REQUIRE(flatA.points.size() == flatB.points.size());
        for (std::size_t i = 0; i < flatA.points.size(); ++i) {
            CHECK(length_um(flatA.points[i] - flatB.points[i]) >= 800.0);
        }
    }
}

// --- Génération partielle sur formes concaves (audit) ------------------------
//
// Défaut trouvé par revue : dans `build_column`, un échec isolé de
// `cross_section` (ou une station retirée par le nettoyage anti-croisement)
// était ignoré par un simple `continue` — la station disparaissait en silence
// et les deux stations valides encadrant le trou se retrouvaient reconnectées
// directement. Sur une forme concave, ceci produisait de larges zones non
// couvertes, des rails discontinus, des éventails/pointes artificielles, ou
// des morceaux partiels au lieu d'un refus propre. Après correctif : soit la
// colonne est refusée explicitement (aucune colonne produite, `refusal` non
// vide), soit elle est complète et géométriquement saine (aucun grand trou
// entre stations consécutives, tous les barreaux dans la région, aucun
// croisement entre barreaux) — jamais un résultat partiel.

namespace {

// Invariant central de l'audit : jamais de résultat "partiel". Vérifie que
// SOIT la colonne est proprement refusée (aucune colonne, refus non vide),
// SOIT elle est complète : aucun grand trou le long des rails, tous les
// barreaux dans la région, aucun croisement entre barreaux.
void check_no_partial_generation(const SatinColumnsResult& r, const geometry::PathSet& region) {
    INFO("refus = " << r.refusal << " colonnes = " << r.columns.size());
    if (r.columns.empty()) {
        CHECK_FALSE(r.refusal.empty());
        return;
    }
    CHECK(r.refusal.empty());
    // Seuil large (6x le pas d'echantillonnage par defaut, 500 um) : couvre
    // a la fois le coeur de l'axe et les stations ajoutees par extend_tip
    // (pas = station_spacing), tout en restant strictement en dessous d'une
    // reconnexion a travers un trou reel (des dizaines de stations).
    constexpr double kMaxGapUm = 3'000.0;
    for (const auto& col : r.columns) {
        REQUIRE(col.rail_a.nodes.size() == col.rail_b.nodes.size());
        REQUIRE(col.rail_a.nodes.size() >= 2);
        for (std::size_t i = 1; i < col.rail_a.nodes.size(); ++i) {
            const double gapA = length_um(col.rail_a.nodes[i].pos - col.rail_a.nodes[i - 1].pos);
            const double gapB = length_um(col.rail_b.nodes[i].pos - col.rail_b.nodes[i - 1].pos);
            CHECK(gapA < kMaxGapUm);
            CHECK(gapB < kMaxGapUm);
        }
        for (const auto& rung : col.rungs) {
            CHECK(length_um(rung.a - rung.b) > 0.0);
            CHECK(in_region(region, mid(rung.a, rung.b)));
        }
        for (std::size_t i = 0; i + 1 < col.rungs.size(); ++i) {
            for (std::size_t j = i + 1; j < col.rungs.size(); ++j) {
                const bool crosses = segments_cross_2d(col.rungs[i].a, col.rungs[i].b,
                                                       col.rungs[j].a, col.rungs[j].b);
                CHECK_FALSE(crosses);
            }
        }
    }
}

}  // namespace

TEST_CASE("colonnes : forme concave (encoche profonde) - jamais de generation partielle") {
    for (const char* s : {"notch", "pinch"}) {
        const auto region = make_shape(s);
        REQUIRE(region.has_value());
        SatinColumnsParameters params;
        params.analysis.raster.pixel_size = Micrometers{100};
        const auto r = build_satin_columns(*region, params);
        INFO("forme = " << s);
        check_no_partial_generation(r, *region);
    }
}

TEST_CASE("colonnes : forme concave (encoche profonde) - deterministe") {
    for (const char* s : {"notch", "pinch"}) {
        const auto a = columns_of(s);
        const auto b = columns_of(s);
        REQUIRE(a.columns.size() == b.columns.size());
        for (std::size_t i = 0; i < a.columns.size(); ++i) {
            CHECK(a.columns[i].rail_a == b.columns[i].rail_a);
            CHECK(a.columns[i].rail_b == b.columns[i].rail_b);
        }
    }
}

// --- Déterminisme ------------------------------------------------------------

TEST_CASE("colonnes : deterministe (memes rails a chaque execution)") {
    const auto a = columns_of("s");
    const auto b = columns_of("s");
    REQUIRE(a.columns.size() == b.columns.size());
    REQUIRE(a.columns.size() >= 1);
    CHECK(a.columns.front().rail_a == b.columns.front().rail_a);
    CHECK(a.columns.front().rail_b == b.columns.front().rail_b);
}

// --- § refonte auto-satin paramétrique (mode `Parametric`) ------------------
//
// Remplace les rails polyligne denses (une station = un nœud) par quelques
// paires structurantes couplant deux rails Bézier épars, des lignes d'angle
// explicites, et un recouvrement local borné aux jonctions -- jamais une
// ancre centrale partagée. Voir `docs/source/satin.md`.

namespace {

SatinColumnsResult parametric_columns_of(const std::string& shape) {
    const auto region = make_shape(shape);
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    params.geometry_mode = SatinGeometryMode::Parametric;
    return build_satin_columns(*region, params);
}

// Aplatit chaque objet en une polyligne par rail, pour les vérifications
// géométriques (croisement, distance au contour) qui doivent porter sur la
// courbe RÉELLE, jamais sur les seuls nœuds de contrôle Bézier.
std::pair<std::vector<Vec2um>, std::vector<Vec2um>> flatten_object(const ParametricSatinObject& obj) {
    const auto a = geometry::flatten(obj.rail_a, Micrometers{30});
    const auto b = geometry::flatten(obj.rail_b, Micrometers{30});
    return {a.points, b.points};
}

bool segments_cross(Vec2um a, Vec2um b, Vec2um c, Vec2um d) {
    const auto cross = [](double ax, double ay, double bx, double by) { return ax * by - ay * bx; };
    const auto o = [&](Vec2um p, Vec2um q, Vec2um r) {
        return cross(static_cast<double>(q.x.value - p.x.value),
                     static_cast<double>(q.y.value - p.y.value),
                     static_cast<double>(r.x.value - p.x.value),
                     static_cast<double>(r.y.value - p.y.value));
    };
    const double o1 = o(a, b, c), o2 = o(a, b, d), o3 = o(c, d, a), o4 = o(c, d, b);
    return (o1 > 0) != (o2 > 0) && (o3 > 0) != (o4 > 0) && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0;
}

bool polylines_cross(const std::vector<Vec2um>& a, const std::vector<Vec2um>& b) {
    for (std::size_t i = 0; i + 1 < a.size(); ++i) {
        for (std::size_t j = 0; j + 1 < b.size(); ++j) {
            if (segments_cross(a[i], a[i + 1], b[j], b[j + 1])) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

TEST_CASE("parametrique : rectangle -> un objet, rails Bezier epars (pas une station = un noeud)") {
    const auto r = parametric_columns_of("rectangle");
    REQUIRE(r.refusal.empty());
    REQUIRE(r.parametric_columns.size() == 1);
    const auto& obj = r.parametric_columns.front();
    REQUIRE(obj.rail_a.nodes.size() >= 2);
    REQUIRE(obj.rail_b.nodes.size() >= 2);
    // La simplification doit être réelle : très en dessous du nombre de
    // stations denses observées (pas un segment dégénéré par station).
    CHECK(obj.rail_a.nodes.size() < obj.raw_station_count / 3);
    CHECK(obj.control_pairs.size() == obj.rail_a.nodes.size());
    CHECK(obj.angle_guides.size() == obj.control_pairs.size());
    // Au moins un nœud interne porte une tangente (Bézier réel, pas une
    // simple polyligne rebaptisée).
    bool anyTangent = false;
    for (const auto& n : obj.rail_a.nodes) {
        anyTangent = anyTangent || n.tan_in.has_value() || n.tan_out.has_value();
    }
    CHECK(anyTangent);
}

TEST_CASE("parametrique : capsule -> rails Bezier restent dans la region, embouts arrondis") {
    const auto r = parametric_columns_of("capsule");
    const auto region = make_shape("capsule");
    REQUIRE(region.has_value());
    REQUIRE(r.refusal.empty());
    REQUIRE(r.parametric_columns.size() == 1);
    const auto [flatA, flatB] = flatten_object(r.parametric_columns.front());
    constexpr double kToleranceUm = 250.0;  // generreuse : tolerance de fit + aplatissement
    for (const auto& p : flatA) {
        CHECK(distance_to_contour(*region, p) < kToleranceUm);
    }
    for (const auto& p : flatB) {
        CHECK(distance_to_contour(*region, p) < kToleranceUm);
    }
}

TEST_CASE("parametrique : trident -> exactement 3 objets independants, un par branche") {
    const auto r = parametric_columns_of("trident");
    REQUIRE(r.refusal.empty());
    REQUIRE(r.parametric_columns.size() == 3);

    // Chaque objet garde peu de paires structurantes (simplification réelle).
    for (const auto& obj : r.parametric_columns) {
        CHECK(obj.control_pairs.size() >= 2);
        CHECK(obj.control_pairs.size() <= 16);
    }

    // Aucun rail (courbe RÉELLE, aplatie) ne croise le rail d'une AUTRE
    // branche -- un léger recouvrement de LARGEUR près du nœud est attendu et
    // toléré (§ étape 9), mais jamais un rail qui traverse une branche
    // voisine de part en part.
    std::vector<std::pair<std::vector<Vec2um>, std::vector<Vec2um>>> flat;
    for (const auto& obj : r.parametric_columns) {
        flat.push_back(flatten_object(obj));
    }
    for (std::size_t i = 0; i < flat.size(); ++i) {
        for (std::size_t j = i + 1; j < flat.size(); ++j) {
            CHECK_FALSE(polylines_cross(flat[i].first, flat[j].first));
            CHECK_FALSE(polylines_cross(flat[i].first, flat[j].second));
            CHECK_FALSE(polylines_cross(flat[i].second, flat[j].first));
            CHECK_FALSE(polylines_cross(flat[i].second, flat[j].second));
        }
    }

    // Un seul plan de jonction, couvrant les 3 branches exactement une fois.
    REQUIRE(r.junction_plans.size() == 1);
    CHECK(r.junction_plans.front().stitch_order.size() == 3);
    std::set<std::uint32_t> distinctOrder(r.junction_plans.front().stitch_order.begin(),
                                          r.junction_plans.front().stitch_order.end());
    CHECK(distinctOrder.size() == 3);

    // Déterminisme.
    const auto again = parametric_columns_of("trident");
    REQUIRE(again.parametric_columns.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(r.parametric_columns[i].rail_a == again.parametric_columns[i].rail_a);
        CHECK(r.parametric_columns[i].rail_b == again.parametric_columns[i].rail_b);
    }
}

TEST_CASE("parametrique : recouvrement de jonction borne, jamais loin dans la branche voisine") {
    // "t"/"cross" : branches perpendiculaires de MÊME largeur -- rien n'est
    // amputé par `trim_unstable_junction_tail` (aucun bourrelet à détecter),
    // donc chaque rail atteint déjà le bord de la confluence par son propre
    // corps. Y prolonger davantage échouerait de toute façon : une section
    // transversale prise DANS le carré de confluence, perpendiculaire à CETTE
    // branche, balaie alors le long de la branche perpendiculaire et dépasse
    // `max_satin_width` dès le premier pas -- recouvrement nul, légitimement.
    // "y"/"trident" : branches à angle, largeurs différentes -- une queue
    // instable est amputée, donc un recouvrement réel doit apparaître.
    for (const char* shape : {"y", "t", "cross", "trident"}) {
        const auto r = parametric_columns_of(shape);
        INFO("forme = " << shape);
        REQUIRE(r.refusal.empty());
        SatinColumnsParameters defaults;
        for (const auto& obj : r.parametric_columns) {
            CHECK(obj.start_overlap_um >= 0.0);
            CHECK(obj.end_overlap_um >= 0.0);
            CHECK(obj.start_overlap_um <=
                 static_cast<double>(defaults.junction_overlap_max.value) + 1.0);
            CHECK(obj.end_overlap_um <=
                 static_cast<double>(defaults.junction_overlap_max.value) + 1.0);
        }
        const std::string s = shape;
        if (s == "y" || s == "trident") {
            bool anyOverlap = false;
            for (const auto& obj : r.parametric_columns) {
                anyOverlap = anyOverlap || obj.start_overlap_um > 0.0 || obj.end_overlap_um > 0.0;
            }
            CHECK(anyOverlap);
        }
    }
}

TEST_CASE("parametrique : lignes d'angle -- correspondance strictement monotone sur les deux rails") {
    for (const char* shape : {"rectangle", "capsule", "y", "t", "cross", "trident"}) {
        const auto r = parametric_columns_of(shape);
        INFO("forme = " << shape);
        REQUIRE(r.refusal.empty());
        for (const auto& obj : r.parametric_columns) {
            for (std::size_t i = 0; i + 1 < obj.angle_guides.size(); ++i) {
                CHECK(obj.angle_guides[i + 1].rail_a_arc_um > obj.angle_guides[i].rail_a_arc_um);
                CHECK(obj.angle_guides[i + 1].rail_b_arc_um > obj.angle_guides[i].rail_b_arc_um);
            }
        }
    }
}

TEST_CASE("parametrique : aucune ligne d'angle croisee, largeur toujours positive") {
    for (const char* shape : {"y", "t", "cross", "trident"}) {
        const auto r = parametric_columns_of(shape);
        INFO("forme = " << shape);
        REQUIRE(r.refusal.empty());
        for (const auto& obj : r.parametric_columns) {
            for (const auto& g : obj.angle_guides) {
                CHECK(length_um(g.rail_a_point - g.rail_b_point) > 0.0);
            }
            for (std::size_t i = 0; i + 1 < obj.angle_guides.size(); ++i) {
                CHECK_FALSE(segments_cross(obj.angle_guides[i].rail_a_point,
                                          obj.angle_guides[i].rail_b_point,
                                          obj.angle_guides[i + 1].rail_a_point,
                                          obj.angle_guides[i + 1].rail_b_point));
            }
        }
    }
}
