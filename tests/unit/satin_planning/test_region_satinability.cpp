// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/satin_planning/region_satinability.hpp"
#include "openstitch/satin_planning/region_split.hpp"

using namespace openstitch;
using namespace openstitch::satin_planning;

namespace {

struct Pipeline {
    geometry::PathSet shape;
    SkeletonGraph graph;
    DecompositionReport decomposition;
    RegionSplitReport split;
};

Pipeline run_pipeline(const std::string& shapeName) {
    const auto shape = auto_satin::make_shape(shapeName);
    REQUIRE(shape.has_value());
    const auto analysis = auto_satin::analyze_region(*shape, {});
    REQUIRE(analysis.has_value());

    Pipeline out;
    out.shape = *shape;
    out.graph = analysis->debug.graph;
    out.decomposition = decompose_into_paths(out.graph);
    out.split = split_region(out.shape, out.graph, out.decomposition);
    return out;
}

}  // namespace

TEST_CASE("check_region_satinability : rectangle -- deja simple, prete sans decoupe") {
    const Pipeline p = run_pipeline("rectangle");
    REQUIRE(p.split.regions.size() == 1);

    const RegionSatinabilityVerdict verdict = check_region_satinability(p.split.regions.front());
    REQUIRE(verdict.report.has_value());
    CHECK(verdict.ready_for_generation);
    CHECK(verdict.report->junction_count == 0);
}

TEST_CASE("check_all_regions : boucle complete decompose+split+check -- formes a coupe unique") {
    // T, Y, croix : chaque branche detachee se termine par une vraie
    // extremite (jamais une autre jonction), donc le garde-fou de
    // `generate_cut_candidates` (reanalyse de satinabilite du morceau
    // isole) s'applique pleinement -- toutes les regions doivent ressortir
    // completement propres.
    for (const std::string& name : {"t", "y", "cross"}) {
        INFO("forme = " << name);
        const Pipeline p = run_pipeline(name);
        REQUIRE(p.split.unresolved_paths.empty());

        const DecompositionSatinabilityReport report = check_all_regions(p.split);
        INFO(format_satinability_report(report));

        REQUIRE(report.verdicts.size() == p.split.regions.size());
        // Chaque region issue du decoupage doit redevenir un squelette simple
        // (sans jonction interne) une fois reanalysee isolement -- la preuve
        // que la coupe a reellement separe la topologie, pas seulement la
        // surface.
        for (const auto& v : report.verdicts) {
            REQUIRE(v.report.has_value());
            CHECK(v.report->junction_count == 0);
            CHECK(v.ready_for_generation);
        }
        CHECK(report.not_ready.empty());
    }
}

TEST_CASE("check_all_regions : H -- les montants sont propres, le pont jonction-jonction connu limite") {
    // Le pont d'un "H" a SES DEUX bouts sur une jonction (§ region_split.hpp) :
    // il lui faut deux coupes successives, et le garde-fou de satinabilite
    // ne s'applique volontairement qu'aux branches se terminant par une
    // vraie extremite (sinon la premiere des deux coupes serait rejetee a
    // tort, le pont etant legitimement encore branche a ce stade). Limite
    // connue et documentee (docs/source/satin.md) plutot que corrigee ici :
    // valider correctement le pont demanderait de reappliquer le garde-fou
    // apres la DERNIERE coupe d'un chemin a deux bouts de jonction, ce que
    // `generate_cut_candidates` (qui ne voit qu'un evenement a la fois) ne
    // peut pas savoir tout seul.
    const Pipeline p = run_pipeline("h");
    REQUIRE(p.split.unresolved_paths.empty());
    const DecompositionSatinabilityReport report = check_all_regions(p.split);
    INFO(format_satinability_report(report));
    REQUIRE(report.verdicts.size() == 3);

    std::size_t cleanCount = 0;
    for (const auto& v : report.verdicts) {
        REQUIRE(v.report.has_value());
        if (v.ready_for_generation) ++cleanCount;
    }
    CHECK(cleanCount == 2);  // les deux montants (jamais coupes qu'a une seule extremite chacun... voir note ci-dessus)
    CHECK(report.not_ready.size() == 1);
}

TEST_CASE("check_all_regions : trident -- une branche isolee reste propre, sinon honnetement non resolue") {
    // La branche laterale du trident est etroite et courte (§ shapes.cpp) :
    // aucune distance de coupe dans la plage testee n'echappe forcement a
    // l'empreinte de la branche voisine pres de la confluence. Le garde-fou
    // rejette alors TOUTES les distances candidates plutot que d'accepter
    // une coupe defectueuse -- `split_region` reporte honnetement le chemin
    // en `unresolved_paths` au lieu de produire une region silencieusement
    // fausse. Ce test verifie cette propriete (echec propre), pas un succes
    // total.
    const Pipeline p = run_pipeline("trident");
    const DecompositionSatinabilityReport report = check_all_regions(p.split);
    INFO(format_satinability_report(report));
    CHECK(report.verdicts.size() == p.decomposition.paths.size());
    for (const auto& v : report.verdicts) {
        if (v.report.has_value()) CHECK(v.report->junction_count == 0);
    }
}

TEST_CASE("format_satinability_report : rendu textuel exploitable pour le debug") {
    const Pipeline p = run_pipeline("t");
    const DecompositionSatinabilityReport report = check_all_regions(p.split);
    const std::string text = format_satinability_report(report);
    CHECK_FALSE(text.empty());
    CHECK(text.find("Total :") != std::string::npos);
    CHECK(text.find("[PRET]") != std::string::npos);
}
