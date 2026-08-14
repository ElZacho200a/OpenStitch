// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/satin_plan.hpp"

#include <algorithm>
#include <functional>
#include <sstream>
#include <unordered_map>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/satin_planning/beam_search.hpp"
#include "openstitch/satin_planning/merge_pass.hpp"

namespace openstitch::satin_planning {

namespace {

template <typename Column>
satin_coverage::SatinColumnInput to_coverage_input(const Column& col, Micrometers density) {
    satin_coverage::SatinColumnInput in;
    in.rail_a = col.rail_a;
    in.rail_b = col.rail_b;
    in.rungs.reserve(col.rungs.size());
    for (const auto& r : col.rungs) in.rungs.emplace_back(r.a, r.b);
    in.density = density;
    return in;
}

std::vector<satin_coverage::SatinColumnInput> to_coverage_inputs(const auto_satin::SatinColumnsResult& built,
                                                                  Micrometers density) {
    std::vector<satin_coverage::SatinColumnInput> inputs;
    if (!built.parametric_columns.empty()) {
        inputs.reserve(built.parametric_columns.size());
        for (const auto& col : built.parametric_columns) inputs.push_back(to_coverage_input(col, density));
    } else {
        inputs.reserve(built.columns.size());
        for (const auto& col : built.columns) inputs.push_back(to_coverage_input(col, density));
    }
    return inputs;
}

// Resultat d'une tentative du solveur local sur UNE region : les colonnes
// reellement construites (peuvent etre vides si refuse) et, si au moins une
// colonne existe, le verdict de couverture reel qui decide si cette region
// est exploitable telle quelle (§5/§7 du plan de refonte satin : "la preuve
// finale est empirique").
struct LocalAttempt {
    auto_satin::SatinColumnsResult built;
    std::optional<satin_coverage::SatinCoverageReport> coverage;
    bool passed{false};
};

LocalAttempt try_local_satin(const geometry::PathSet& region, const SatinPlanConfig& config) {
    LocalAttempt attempt;
    attempt.built = auto_satin::build_satin_columns(region, config.genParams);
    if (attempt.built.columns.empty() && attempt.built.parametric_columns.empty()) {
        return attempt;  // refuse : ni couverture ni verdict, `passed` reste false
    }
    const auto coverage =
        satin_coverage::analyze_satin_coverage(region, to_coverage_inputs(attempt.built, config.density),
                                                config.coverageConfig);
    if (coverage) {
        attempt.coverage = *coverage;
        attempt.passed = coverage->passed;
    }
    return attempt;
}

struct RecursionOutcome {
    std::vector<SatinPlanRegion> accepted;
    // Morceaux dont ni le solveur local ni une decomposition ulterieure n'ont
    // pu etre tentes avec succes -- geometrie brute, jamais transformee en
    // satin ou tatami par ce module.
    std::vector<geometry::PathSet> unresolved;
    // Avertissements du solveur local, accumules a travers toute la
    // recursion -- jamais silencieux.
    std::vector<std::string> warnings;
    // Paires d'index DANS `accepted` (§19) et leur recouvrement associe
    // (§20) -- TOUJOURS de meme taille, alignes 1:1 par construction (jamais
    // pousses independamment l'un de l'autre).
    std::vector<std::pair<std::size_t, std::size_t>> adjacency;
    std::vector<SatinPlanOverlap> overlaps;
};

RecursionOutcome plan_recursive(const geometry::PathSet& region, const SatinPlanConfig& config, int depth,
                                bool fromResidualRepair);

// Tente de decomposer `region` (au moins une jonction requise dans son
// propre squelette) et replanifie RECURSIVEMENT chaque sous-region
// resultante -- c'est cette recursion, absente de l'ancien
// `build_satin_sections` a une seule passe, qui permet a une region fille
// encore mediocre d'etre redecoupee a son tour (§10 du plan de refonte).
RecursionOutcome decompose_and_recurse(const geometry::PathSet& region, const SatinPlanConfig& config, int depth,
                                       bool fromResidualRepair) {
    RecursionOutcome out;
    const auto analysis = auto_satin::analyze_region(region, config.genParams.analysis);
    if (!analysis || analysis->debug.graph.junction_count() == 0) {
        // Rien a decomposer (echec d'analyse, ou squelette sans jonction --
        // ex. un anneau pur ou une entaille : famille de coupes actuelle,
        // uniquement normale au squelette a une distance d'une jonction,
        // n'a rien a quoi s'accrocher). Le meilleur effort local a deja ete
        // tente par l'appelant avant ce point ; ici il n'y a plus rien a
        // essayer, seulement a signaler honnetement.
        out.unresolved.push_back(region);
        return out;
    }
    const auto& graph = analysis->debug.graph;
    const auto decomposition = decompose_into_paths(graph);

    satin_planning::BeamSearchParams beamParams;
    beamParams.genParams = config.genParams;
    beamParams.coverageConfig = config.coverageConfig;
    beamParams.density = config.density;
    satin_planning::OracleGuidedSelector selector(beamParams);

    CutCandidateParams cutParams = config.cutParams;
    if (!cutParams.selector) {
        cutParams.selector = std::ref(selector);
    }

    // §14 : recolte les JunctionSeparatorInfo DEJA calcules par le moteur
    // Legacy (sommet reflex du contour a chaque confluence -- cf.
    // `auto_satin::satin_column.cpp`, `resolve_junction`) pour enrichir
    // `generate_cut_candidates` d'une famille de coupes ancree sur une
    // encoche REELLE, plutot que sur une distance devinee par le seul
    // balayage regulier. Mode Legacy FORCE ici (independamment de
    // `config.genParams.geometry_mode`) -- seul mode qui resout
    // StableBranchEnd/JunctionSeparator -- pour cette extraction
    // diagnostique uniquement : n'affecte jamais la geometrie satin
    // reellement produite par `try_local_satin` (le solveur local reste
    // inchange). Repli silencieux sur une liste vide si le moteur Legacy
    // refuse (ex. jonction incoherente) : la famille supplementaire est
    // simplement absente pour cette region, jamais une erreur -- le
    // balayage regulier reste disponible.
    if (config.use_junction_separator_cuts && cutParams.junction_separators.empty()) {
        auto_satin::SatinColumnsParameters legacyParams = config.genParams;
        legacyParams.geometry_mode = auto_satin::SatinGeometryMode::Legacy;
        const auto legacyResult = auto_satin::build_satin_columns(region, legacyParams);
        cutParams.junction_separators = legacyResult.junction_separators;
    }

    const auto split = split_region(region, graph, decomposition, cutParams);

    // §18 (phase 7 SGSD, merge pass, 2026-08-14) : reconsidere CHAQUE coupe
    // reussie A POSTERIORI -- si les deux regions separees ne font pas mieux
    // (au-dela de `merge_pass_coverage_tolerance`) que leur union, prefere
    // le SEUL segment fusionne ("le plus petit nombre de segments", §18)
    // plutot que la premiere partition valide trouvee par le beam search.
    // `evaluate_merge_pass` existait deja, testee et fonctionnelle, mais
    // n'etait jamais appelee par le planner recursif -- simple reutilisation,
    // aucune nouvelle regle de decision reimplementee ici. Chaque region de
    // `split.regions` participe a AU PLUS un `merge_candidates` (celui de la
    // coupe qui l'a creee) : les deux cotes fusionnes sont donc exclus de la
    // recursion individuelle ci-dessous, remplaces par UNE recursion sur la
    // geometrie fusionnee.
    std::unordered_map<std::size_t, std::size_t> pathIndexToRegionSlot;
    for (std::size_t i = 0; i < split.regions.size(); ++i) pathIndexToRegionSlot[split.regions[i].path_index] = i;

    std::vector<bool> mergedAway(split.regions.size(), false);
    std::vector<geometry::PathSet> mergedGroups;
    if (config.use_merge_pass && !split.merge_candidates.empty()) {
        MergePassParams mergeParams;
        mergeParams.genParams = config.genParams;
        mergeParams.coverageConfig = config.coverageConfig;
        mergeParams.density = config.density;
        mergeParams.coverage_tolerance = config.merge_pass_coverage_tolerance;
        const auto mergeReport = evaluate_merge_pass(split, mergeParams);
        for (std::size_t i = 0; i < mergeReport.decisions.size() && i < split.merge_candidates.size(); ++i) {
            if (!mergeReport.decisions[i].merge_recommended) continue;
            const auto& candidate = split.merge_candidates[i];
            const auto firstIt = pathIndexToRegionSlot.find(candidate.first_path_index);
            const auto secondIt = pathIndexToRegionSlot.find(candidate.second_path_index);
            if (firstIt == pathIndexToRegionSlot.end() || secondIt == pathIndexToRegionSlot.end()) continue;
            mergedAway[firstIt->second] = true;
            mergedAway[secondIt->second] = true;
            mergedGroups.push_back(candidate.merged_region);
        }
    }

    // §19 : n'associe l'index de chemin de decomposition (`SatinRegion::
    // path_index`, meme espace que `MergeCandidate::first/second_path_index`)
    // a l'index final dans `out.accepted` QUE lorsque la sous-region a ete
    // acceptee TELLE QUELLE, sans redecoupage ulterieur (une seule feuille en
    // sortie de recursion) -- sinon son "cote" de l'adjacence n'est plus une
    // geometrie unique et bien definie. Une region fusionnee ci-dessus n'a
    // volontairement PAS d'entree ici : elle n'est plus l'un ou l'autre
    // `path_index` d'origine, donc plus de paire adjacente a rapporter pour
    // ce `merge_candidates` -- la boucle qui suit `continue` naturellement
    // pour elle (aucune entree trouvee).
    std::unordered_map<std::size_t, std::size_t> leafIndexByPathIndex;
    const auto absorb_child = [&](const geometry::PathSet& childRegion, std::optional<std::size_t> pathIndexForMapping) {
        RecursionOutcome child = plan_recursive(childRegion, config, depth + 1, fromResidualRepair);
        const std::size_t baseIndex = out.accepted.size();
        if (pathIndexForMapping && child.accepted.size() == 1) {
            leafIndexByPathIndex[*pathIndexForMapping] = baseIndex;
        }
        for (std::size_t k = 0; k < child.adjacency.size(); ++k) {
            out.adjacency.emplace_back(child.adjacency[k].first + baseIndex, child.adjacency[k].second + baseIndex);
            out.overlaps.push_back(std::move(child.overlaps[k]));
        }
        for (auto& leaf : child.accepted) out.accepted.push_back(std::move(leaf));
        for (auto& piece : child.unresolved) out.unresolved.push_back(std::move(piece));
        for (auto& w : child.warnings) out.warnings.push_back(std::move(w));
    };
    for (std::size_t i = 0; i < split.regions.size(); ++i) {
        if (mergedAway[i]) continue;
        absorb_child(split.regions[i].region, split.regions[i].path_index);
    }
    for (const auto& mergedRegion : mergedGroups) {
        absorb_child(mergedRegion, std::nullopt);
    }

    // Recouvrement (§20, phase 8 SGSD deja implementee -- `generate_overlaps`,
    // simplement jamais appelee depuis le planner recursif avant ce point) :
    // calcule une seule fois par niveau de recursion, sur les regions
    // structurelles issues de CE decoupage precis (avant toute recursion
    // ulterieure d'un cote ou de l'autre).
    const OverlapReport overlapReport =
        config.compute_overlaps ? generate_overlaps(split, OverlapParams{config.overlap_distance}) : OverlapReport{};

    for (const auto& mc : split.merge_candidates) {
        const auto itA = leafIndexByPathIndex.find(mc.first_path_index);
        const auto itB = leafIndexByPathIndex.find(mc.second_path_index);
        if (itA == leafIndexByPathIndex.end() || itB == leafIndexByPathIndex.end()) continue;

        SatinPlanOverlap planOverlap;
        planOverlap.first_extended = out.accepted[itA->second].region;
        planOverlap.second_extended = out.accepted[itB->second].region;
        for (const auto& ov : overlapReport.overlaps) {
            if (ov.first_path_index == mc.first_path_index && ov.second_path_index == mc.second_path_index) {
                planOverlap.first_extended = ov.first_extended;
                planOverlap.second_extended = ov.second_extended;
                break;
            }
        }
        out.adjacency.emplace_back(itA->second, itB->second);
        out.overlaps.push_back(std::move(planOverlap));
    }

    // Les chemins jamais isoles par `split_region` (`split.unresolved_paths`)
    // n'ont pas de geometrie individuelle recuperable ici : leur surface
    // reste fondue dans un morceau voisin deja traite par la boucle
    // ci-dessus (donc deja comptee), ou apparaitra dans le reliquat mesure
    // globalement par la reparation de residu au niveau racine si elle
    // echappe reellement a toute colonne produite.
    return out;
}

RecursionOutcome plan_recursive(const geometry::PathSet& region, const SatinPlanConfig& config, int depth,
                                bool fromResidualRepair) {
    LocalAttempt attempt = try_local_satin(region, config);
    const bool budgetExhausted = depth >= config.max_recursion_depth;
    const bool hasColumns = !attempt.built.columns.empty() || !attempt.built.parametric_columns.empty();
    // `analyze_satin_coverage` échoue (erreur, jamais un simple "couverture
    // insuffisante") UNIQUEMENT sur une région cible dégénérée (moins de 3
    // sommets) -- défaut réel trouvé le 2026-08-14 sur une forme utilisateur
    // (lettre T réelle) : une région issue d'un découpage ou d'une
    // réparation de résidu peut occasionnellement produire un tel artefact
    // dégénéré, sur laquelle `build_satin_columns` construit parfois quand
    // même des colonnes (souvent quasi vides) -- SANS que leur couverture
    // n'ait jamais pu être vérifiée. Accepter un tel résultat sans le
    // distinguer d'une vraie réussite mesurée aurait fabriqué une "réussite"
    // invérifiable en silence (agrégat mesuré à 1,9% sur 392 mm² dans le cas
    // trouvé, alors que le plan se déclarait "réussi").
    const bool coverageUnverifiable = hasColumns && !attempt.coverage;
    // Un "meilleur effort local" n'est digne d'etre accepte SANS autre
    // tentative que si sa couverture reelle depasse un seuil minimal --
    // sinon, mieux vaut le rapporter honnetement comme residu (§ defaut
    // reel trouve sur tentabrode.png, cf. SatinPlanConfig::
    // min_fallback_coverage_ratio) que fabriquer une "reussite" a 1-2%.
    const bool adequateFallback =
        hasColumns && !coverageUnverifiable && attempt.coverage->raw_coverage_ratio >= config.min_fallback_coverage_ratio;

    const auto accept_as_leaf = [&]() {
        RecursionOutcome out;
        for (const auto& w : attempt.built.warnings) out.warnings.push_back(w);
        SatinPlanRegion leaf;
        leaf.region = region;
        leaf.columns = std::move(attempt.built);
        leaf.depth = depth;
        leaf.from_residual_repair = fromResidualRepair;
        if (attempt.coverage) leaf.coverage = *attempt.coverage;
        out.accepted.push_back(std::move(leaf));
        return out;
    };
    const auto report_as_residual = [&]() {
        RecursionOutcome out;
        for (const auto& w : attempt.built.warnings) out.warnings.push_back(w);
        out.unresolved.push_back(region);
        return out;
    };

    if (attempt.passed) {
        return accept_as_leaf();
    }
    if (budgetExhausted) {
        // Dernier recours : la récursion doit se terminer quelque part --
        // mais seulement si le résultat franchit au moins le seuil minimal,
        // jamais un raccourci inconditionnel (cf. le défaut réel ci-dessus).
        return adequateFallback ? accept_as_leaf() : report_as_residual();
    }

    RecursionOutcome decomposed = decompose_and_recurse(region, config, depth, fromResidualRepair);
    if (!decomposed.accepted.empty()) {
        return decomposed;
    }
    // La décomposition n'a rien produit de mieux (aucune jonction, ou aucune
    // coupe valide). Si la couverture locale a été RÉELLEMENT mesurée ET
    // franchit le seuil minimal, c'est un meilleur effort légitime --
    // accepté tel quel plutôt que de perdre la région. Sinon (invérifiable,
    // aucune colonne, OU couverture mesurée mais clairement inadéquate), ne
    // JAMAIS fabriquer une réussite silencieuse : signaler honnêtement la
    // géométrie comme résidu, comme n'importe quel autre échec.
    return adequateFallback ? accept_as_leaf() : report_as_residual();
}

}  // namespace

SatinPlan create_satin_plan(const geometry::PathSet& source, const SatinPlanConfig& config) {
    SatinPlan plan;

    RecursionOutcome top = plan_recursive(source, config, 0, false);
    plan.regions = std::move(top.accepted);
    plan.warnings = std::move(top.warnings);
    plan.adjacency = std::move(top.adjacency);
    plan.overlaps = std::move(top.overlaps);
    std::vector<geometry::PathSet> pendingResidual = std::move(top.unresolved);

    const double sourceAreaMm2 = geometry::path_set_area_um2(source) / 1e6;
    const double repairAreaThresholdMm2 =
        std::max(config.residual_repair_min_area_mm2, config.residual_repair_min_area_ratio * sourceAreaMm2);

    // Reparation de residu (§17) : mesure la couverture AGREGEE sur la
    // region SOURCE entiere (jamais une sous-region) et tente de
    // replanifier chaque composante manquante significative comme une
    // nouvelle region a part entiere -- une vraie tentative recursive,
    // jamais un comblement automatique.
    for (int round = 0; round < config.max_residual_repair_rounds; ++round) {
        std::vector<satin_coverage::SatinColumnInput> allInputs;
        for (const auto& r : plan.regions) {
            auto inputs = to_coverage_inputs(r.columns, config.density);
            for (auto& in : inputs) allInputs.push_back(std::move(in));
        }
        const auto aggregate = satin_coverage::analyze_satin_coverage(source, allInputs, config.coverageConfig);
        if (!aggregate) {
            // Echec de mesure (region source degeneree) : s'arreter
            // proprement plutot que de deviner un etat de couverture.
            break;
        }
        plan.aggregate_coverage = *aggregate;

        bool anyRepaired = false;
        std::vector<geometry::PathSet> stillMissing;
        for (const auto& missing : aggregate->missing_regions) {
            if (missing.area_mm2 < repairAreaThresholdMm2 ||
                missing.max_gap_radius_mm < config.residual_repair_min_gap_radius_mm) {
                // Trop petit pour justifier une TENTATIVE de replanification
                // individuelle (reliquat probablement naturel, pointe/
                // jonction) -- mais reste neanmoins signale dans le residu
                // final. Defaut reel trouve et corrige ici (2026-08-14) :
                // ignorer purement et simplement ces composantes en
                // supposait a tort qu'elles etaient negligeables une par
                // une, alors que DE NOMBREUSES petites composantes peuvent
                // s'additionner en un reliquat total tres significatif sur
                // une forme complexe (verifie sur une vraie lettre de logo,
                // § docs/source/satin.md -- plusieurs centaines de mm2
                // cumules, jamais visibles individuellement au-dessus du
                // seuil). Le filtre ne decide donc plus JAMAIS ce qui est
                // rapporte, seulement ce qui merite une tentative couteuse.
                stillMissing.push_back(missing.region);
                continue;
            }
            RecursionOutcome repaired = plan_recursive(missing.region, config, 0, true);
            for (auto& w : repaired.warnings) plan.warnings.push_back(std::move(w));
            if (!repaired.accepted.empty()) {
                const std::size_t baseIndex = plan.regions.size();
                for (std::size_t k = 0; k < repaired.adjacency.size(); ++k) {
                    plan.adjacency.emplace_back(repaired.adjacency[k].first + baseIndex,
                                                 repaired.adjacency[k].second + baseIndex);
                    plan.overlaps.push_back(std::move(repaired.overlaps[k]));
                }
                for (auto& leaf : repaired.accepted) plan.regions.push_back(std::move(leaf));
                anyRepaired = true;
            } else {
                for (auto& piece : repaired.unresolved) stillMissing.push_back(std::move(piece));
            }
        }
        pendingResidual = std::move(stillMissing);
        if (!anyRepaired) {
            break;  // plus aucun progres possible, inutile de reboucler
        }
    }

    // Mesure finale (apres toute reparation) pour que `aggregate_coverage`
    // reflete l'etat REELLEMENT emis, pas un etat intermediaire d'une
    // iteration de reparation precedente.
    {
        std::vector<satin_coverage::SatinColumnInput> allInputs;
        for (const auto& r : plan.regions) {
            auto inputs = to_coverage_inputs(r.columns, config.density);
            for (auto& in : inputs) allInputs.push_back(std::move(in));
        }
        if (const auto aggregate = satin_coverage::analyze_satin_coverage(source, allInputs, config.coverageConfig)) {
            plan.aggregate_coverage = *aggregate;
        }
    }

    plan.unresolved_residual = std::move(pendingResidual);

    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);
    out << "[SatinPlan] " << plan.regions.size() << " region(s) acceptee(s)";
    if (!plan.adjacency.empty()) {
        out << ", " << plan.adjacency.size() << " adjacence(s)/recouvrement(s) connu(s)";
    }
    if (plan.aggregate_coverage) {
        out << ", couverture agregee = " << (plan.aggregate_coverage->raw_coverage_ratio * 100.0) << "%";
    }
    if (!plan.unresolved_residual.empty()) {
        out << " -- " << plan.unresolved_residual.size() << " zone(s) non resolue(s)";
    }
    plan.diagnostic = out.str();

    return plan;
}

std::string format_satin_plan(const SatinPlan& plan) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);
    out << "[SatinPlan]\n";
    for (std::size_t i = 0; i < plan.regions.size(); ++i) {
        const auto& r = plan.regions[i];
        out << "  region " << i << " (profondeur=" << r.depth
            << (r.from_residual_repair ? ", reparation" : "") << ") : ";
        if (r.coverage) {
            out << (r.coverage->raw_coverage_ratio * 100.0) << "% brut, " << (r.coverage->core_coverage_ratio * 100.0)
                << "% coeur";
        } else {
            out << "sans mesure de couverture";
        }
        out << "\n";
    }
    if (plan.aggregate_coverage) {
        out << "Couverture agregee (region source entiere) : " << (plan.aggregate_coverage->raw_coverage_ratio * 100.0)
            << "% brut, " << (plan.aggregate_coverage->core_coverage_ratio * 100.0) << "% coeur\n";
    }
    out << "Adjacence(s) connue(s) : " << plan.adjacency.size() << " paire(s)\n";
    out << "Residu non resolu : " << plan.unresolved_residual.size() << " zone(s)\n";
    out << plan.diagnostic << "\n";
    return out.str();
}

}  // namespace openstitch::satin_planning
