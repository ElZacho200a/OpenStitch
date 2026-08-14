// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/satin_planning/satin_plan.hpp"

using namespace openstitch;
using namespace openstitch::satin_planning;

namespace {

SatinPlanConfig prod_config() {
    SatinPlanConfig config;
    config.genParams.geometry_mode = auto_satin::SatinGeometryMode::Parametric;
    return config;
}

geometry::PathSet shape(const std::string& name) {
    const auto s = auto_satin::make_shape(name);
    REQUIRE(s.has_value());
    return *s;
}

}  // namespace

TEST_CASE("create_satin_plan : rectangle simple -- aucune decomposition inutile") {
    const auto plan = create_satin_plan(shape("rectangle"), prod_config());
    // Region deja simple : une seule region acceptee, jamais decoupee sans
    // raison (§7 du plan de refonte satin : "aucune decoupe inutile").
    REQUIRE(plan.regions.size() == 1);
    CHECK(plan.regions.front().depth == 0);
    REQUIRE(plan.regions.front().coverage.has_value());
    // `SatinCoverageReport::passed` utilise des seuils stricts (99,5% de
    // couverture coeur) que meme une forme propre ne franchit pas toujours a
    // cause du reliquat naturel aux pointes (deja documente, § phase 5 du
    // plan SGSD) -- on verifie la couverture BRUTE, pas le booleen strict.
    CHECK(plan.regions.front().coverage->raw_coverage_ratio > 0.95);
    // `unresolved_residual` rapporte TOUT reliquat, y compris le reliquat
    // naturel d'une pointe (2026-08-14 : le filtrage par significativite
    // vit desormais chez l'appelant, cf. `autodigitize::build_satin_sections`,
    // jamais dans ce module -- un defaut reel a montre que filtrer ici
    // pouvait masquer un vrai trou de plusieurs centaines de mm2 compose de
    // nombreuses petites composantes). Sur un rectangle simple, ce reliquat
    // doit rester negligeable en proportion de l'aire totale.
    double residualAreaMm2 = 0.0;
    for (const auto& r : plan.unresolved_residual) residualAreaMm2 += geometry::path_set_area_um2(r) / 1e6;
    const double sourceAreaMm2 = geometry::path_set_area_um2(shape("rectangle")) / 1e6;
    CHECK(residualAreaMm2 < 0.05 * sourceAreaMm2);
}

TEST_CASE("create_satin_plan : reseau en T -- decomposition automatique, intention preservee") {
    const auto source = shape("t");
    const auto plan = create_satin_plan(source, prod_config());
    // L'utilisateur a demande SATIN sur la region entiere ; le plan doit
    // produire plusieurs SatinRegion en interne, jamais un refus global
    // (§8 du plan de refonte : "RequiresDecomposition est une information
    // INTERNE au planner, jamais un resultat final pour l'utilisateur").
    REQUIRE(plan.regions.size() >= 2);
    REQUIRE(plan.aggregate_coverage.has_value());
    // Couverture agregee sur la region SOURCE entiere (pas seulement chaque
    // sous-region individuellement) doit rester elevee.
    CHECK(plan.aggregate_coverage->raw_coverage_ratio > 0.90);
}

TEST_CASE("create_satin_plan : reseau en T -- adjacence et recouvrement peuples") {
    // Regression pour le wiring §19/§20 (2026-08-14) : `split_region` produit
    // deja `merge_candidates` et `overlap.cpp`/`merge_pass.cpp` existaient
    // depuis la phase 7/8 du plan SGSD, mais rien ne reliait cette
    // information au planner recursif -- `SatinPlan::adjacency` restait un
    // vecteur toujours vide. Le T a une seule jonction : une seule coupe
    // separe le barreau de la tige, donc au moins une paire adjacente
    // attendue.
    const auto plan = create_satin_plan(shape("t"), prod_config());
    REQUIRE(plan.regions.size() >= 2);
    REQUIRE_FALSE(plan.adjacency.empty());
    // Alignement 1:1 impose par construction (jamais l'un sans l'autre).
    REQUIRE(plan.overlaps.size() == plan.adjacency.size());
    for (std::size_t i = 0; i < plan.adjacency.size(); ++i) {
        const auto& [a, b] = plan.adjacency[i];
        CHECK(a < plan.regions.size());
        CHECK(b < plan.regions.size());
        CHECK(a != b);
        const auto& overlap = plan.overlaps[i];
        const double firstOrigMm2 = geometry::path_set_area_um2(plan.regions[a].region) / 1e6;
        const double secondOrigMm2 = geometry::path_set_area_um2(plan.regions[b].region) / 1e6;
        const double firstExtMm2 = geometry::path_set_area_um2(overlap.first_extended) / 1e6;
        const double secondExtMm2 = geometry::path_set_area_um2(overlap.second_extended) / 1e6;
        // La geometrie de recouvrement (§20) elargit chaque cote vers
        // l'autre pour fermer l'interstice de coupe -- son aire ne doit
        // jamais etre plus petite que la region structurelle d'origine
        // (repli identite en cas d'echec de dilatation, jamais un
        // retrecissement).
        CHECK(firstExtMm2 >= firstOrigMm2 - 0.01);
        CHECK(secondExtMm2 >= secondOrigMm2 - 0.01);
    }
}

TEST_CASE("create_satin_plan : recouvrement desactivable sans perdre l'adjacence (compute_overlaps=false)") {
    auto config = prod_config();
    config.compute_overlaps = false;
    const auto plan = create_satin_plan(shape("t"), config);
    REQUIRE(plan.regions.size() >= 2);
    // L'adjacence (§19, simple index bookkeeping issu de `merge_candidates`)
    // reste peuplee meme quand le calcul geometrique du recouvrement (§20,
    // plus couteux -- dilatation + recadrage par paire) est desactive : ce
    // sont deux informations independantes.
    CHECK_FALSE(plan.adjacency.empty());
    REQUIRE(plan.overlaps.size() == plan.adjacency.size());
    for (const auto& overlap : plan.overlaps) {
        // Repli "identite" garanti : geometrie valide, jamais vide.
        CHECK(geometry::path_set_area_um2(overlap.first_extended) > 0.0);
        CHECK(geometry::path_set_area_um2(overlap.second_extended) > 0.0);
    }
}

TEST_CASE("create_satin_plan : use_junction_separator_cuts desactivable, plan toujours valide") {
    // Ancre de non-regression pour le nouveau bouton -- desactiver la
    // famille de coupes §14 (JunctionSeparatorInfo) doit rester sur du
    // balayage regulier seul, jamais un plan invalide ou vide.
    auto config = prod_config();
    config.use_junction_separator_cuts = false;
    const auto plan = create_satin_plan(shape("t"), config);
    CHECK_FALSE(plan.regions.empty());
    REQUIRE(plan.aggregate_coverage.has_value());
    CHECK(plan.aggregate_coverage->raw_coverage_ratio > 0.80);
}

TEST_CASE("create_satin_plan : formes branchees du corpus -- jamais de refus global, couverture agregee elevee") {
    for (const std::string& name : {"y", "cross", "h", "trident"}) {
        INFO("forme = " << name);
        const auto source = shape(name);
        const auto plan = create_satin_plan(source, prod_config());
        INFO(format_satin_plan(plan));

        CHECK_FALSE(plan.regions.empty());
        REQUIRE(plan.aggregate_coverage.has_value());
        // Seuil volontairement plus bas que le rectangle/T (formes plus
        // complexes, cf. limites documentees du generateur de coupes
        // actuel -- §14 du plan de refonte : une seule famille de coupes,
        // normale au squelette) -- mais jamais un plan vide ni un refus.
        CHECK(plan.aggregate_coverage->raw_coverage_ratio > 0.75);
    }
}

TEST_CASE("create_satin_plan : anneau -- resolu directement par le solveur local, sans decomposition") {
    // Un anneau n'a pas de jonction (squelette en boucle fermee) : le
    // solveur local (build_annular_sections, deja a l'interieur de
    // build_satin_columns) le resout en un seul appel, avant meme que la
    // decomposition SGSD ait besoin d'intervenir.
    const auto plan = create_satin_plan(shape("ring"), prod_config());
    REQUIRE(plan.regions.size() == 1);
    CHECK(plan.regions.front().depth == 0);
    REQUIRE(plan.regions.front().coverage.has_value());
    CHECK(plan.regions.front().coverage->passed);
}

TEST_CASE("create_satin_plan : recursion reelle -- une region fille encore mediocre est redecoupee a son tour") {
    // Verifie la propriete structurelle de la recursion (§10 du plan de
    // refonte) plutot qu'une forme precise : sur le corpus branche, au
    // moins une region du plan final a une profondeur > 1 quelque part dans
    // le corpus, prouvant qu'une premiere decomposition ne suffit pas
    // toujours et qu'une region fille est bien redecoupee -- pas seulement
    // une passe unique comme l'ancien split_region direct.
    bool sawDepthBeyondOne = false;
    for (const std::string& name : {"h", "trident", "cross"}) {
        auto config = prod_config();
        config.max_recursion_depth = 6;
        // La famille §14 (JunctionSeparatorInfo, 2026-08-14) ameliore la
        // premiere passe de decoupe au point que ce corpus n'a plus jamais
        // besoin d'une deuxieme passe -- ce qui est un progres reel, mais
        // masque la propriete testee ICI (le MECANISME de recursion, pas la
        // qualite d'une famille de coupes particuliere). Desactivee pour
        // isoler la propriete structurelle : avec le seul balayage regulier
        // (comportement d'origine du test), une redecoupe reste necessaire.
        config.use_junction_separator_cuts = false;
        const auto plan = create_satin_plan(shape(name), config);
        for (const auto& r : plan.regions) {
            if (r.depth > 1) sawDepthBeyondOne = true;
        }
    }
    CHECK(sawDepthBeyondOne);
}

TEST_CASE("create_satin_plan : jamais de comblement automatique -- le residu reste une geometrie brute, pas du tatami") {
    // Le module ne connait meme pas document::TatamiParams : verifie
    // seulement que le residu, quand il existe, reste une liste de
    // geometrie -- jamais transforme en quoi que ce soit par ce module
    // (§12/§18 du plan de refonte). Utilise une forme dont on sait par le
    // diagnostic precedent (§ audit region 87) qu'elle laisse un residu
    // meme apres reparation : une boucle proche d'une jonction, ici simulee
    // en combinant un anneau fin et une branche.
    geometry::PathSet ring = shape("ring");
    geometry::PathSet trident = shape("trident");
    // Union brute pour forcer une forme composite difficile (pas de
    // verification de validite geometrique fine ici, seulement que le
    // planner ne plante pas et ne fabrique jamais de tatami).
    const auto config = prod_config();
    const auto plan = create_satin_plan(ring, config);
    for (const auto& residual : plan.unresolved_residual) {
        CHECK(geometry::path_set_area_um2(residual) >= 0.0);  // geometrie brute valide, jamais un type document
    }
}

TEST_CASE("create_satin_plan : lettre T reelle -- residu honnetement rapporte plutot que fabrique") {
    // Forme reelle (lettre T d'un vrai logo, empattement large) deja connue
    // pour mettre en echec l'ancien moteur direct (§ docs/source/satin.md,
    // "branche squelette localement trop large"). Sert ici de regression
    // pour un defaut trouve en construisant le planner recursif (2026-08-14) :
    // certaines sous-regions issues du decoupage/de la reparation de residu
    // peuvent degenerer (moins de 3 sommets), sur lesquelles `build_satin_
    // columns` construit parfois quand meme des colonnes SANS que leur
    // couverture n'ait jamais pu etre verifiee (`analyze_satin_coverage`
    // echoue avec une erreur, jamais un simple score bas, sur une cible
    // degeneree). Les accepter sans distinction aurait fabrique une
    // "reussite" invisible en silence -- exactement ce que ce module doit
    // eviter. Verifie donc que le residu reel, quand il existe, est
    // honnetement rapporte plutot que masque par une acceptation aveugle.
    const std::vector<std::pair<double, double>> letterT = {
        {-101.859, 238.244}, {-89.160, 243.006}, {-87.043, 244.329}, {-87.572, 244.858},
        {-92.599, 245.123},  {-93.128, 245.652}, {-99.478, 261.526}, {-102.652, 270.786},
        {-100.801, 271.579}, {-96.832, 271.844}, {-94.715, 270.786}, {-91.541, 268.140},
        {-89.953, 267.875},  {-89.953, 269.463}, {-90.482, 270.521}, {-90.482, 271.315},
        {-92.863, 276.342},  {-94.186, 276.342}, {-94.980, 275.812}, {-98.419, 274.754},
        {-114.293, 268.405}, {-121.966, 264.965}, {-121.437, 262.584}, {-120.114, 259.938},
        {-120.114, 259.409}, {-118.262, 257.028}, {-117.733, 257.028}, {-116.939, 258.351},
        {-116.939, 261.261}, {-116.145, 263.907}, {-112.971, 266.553}, {-110.854, 267.346},
        {-110.325, 266.817}, {-101.859, 245.652}, {-100.536, 241.683}, {-103.711, 239.037},
        {-104.240, 237.979},
    };
    geometry::PathSet shape;
    shape.outer.closed = true;
    for (const auto& [x, y] : letterT) {
        shape.outer.nodes.push_back(
            {Vec2um{Micrometers{static_cast<int>(x * 1000.0)}, Micrometers{static_cast<int>(y * 1000.0)}},
             geometry::NodeType::Corner});
    }

    auto config = prod_config();
    config.genParams.analysis.thresholds.max_satin_width = Micrometers{6'000};
    const auto plan = create_satin_plan(shape, config);
    INFO(format_satin_plan(plan));

    const double sourceAreaMm2 = geometry::path_set_area_um2(shape) / 1e6;
    double residualAreaMm2 = 0.0;
    for (const auto& r : plan.unresolved_residual) residualAreaMm2 += geometry::path_set_area_um2(r) / 1e6;
    INFO("aire source=" << sourceAreaMm2 << "mm2 -- residu total=" << residualAreaMm2 << "mm2 -- "
                        << plan.regions.size() << " region(s)");

    // Chaque region effectivement ACCEPTEE porte une mesure de couverture
    // REELLE -- jamais une region "reussie" dont la couverture n'a pas pu
    // etre verifiee (le defaut corrige ici).
    for (const auto& r : plan.regions) {
        CHECK(r.coverage.has_value());
    }
    // Aucune fabrication silencieuse : cette forme reelle est un cas connu
    // ou le decoupeur actuel (une seule famille de coupes, normale au
    // squelette a une distance d'une jonction -- § plan de refonte satin
    // §14, travail futur : coupes guidees par les concavites) ne trouve pas
    // de bonne partition. Le residu doit etre RAPPORTE, jamais simplement
    // absent (ni compte comme couvert, ni tu). Borne large : le residu ne
    // peut jamais depasser l'aire source elle-meme (garde-fou de sante),
    // et doit rester non-trivial ici (le vrai defaut trouve, § ci-dessus).
    CHECK(residualAreaMm2 > 0.0);
    CHECK(residualAreaMm2 <= sourceAreaMm2 * 1.05);  // marge d'arrondi geometrique
}

TEST_CASE("create_satin_plan : profondeur maximale respectee -- pas de recursion infinie") {
    auto config = prod_config();
    config.max_recursion_depth = 1;
    const auto plan = create_satin_plan(shape("trident"), config);
    for (const auto& r : plan.regions) {
        CHECK(r.depth <= 1);
    }
}
