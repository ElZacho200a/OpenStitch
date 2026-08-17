// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/satin_plan.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <sstream>
#include <unordered_map>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/satin_planning/beam_search.hpp"
#include "openstitch/satin_planning/concavity_cuts.hpp"
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

// Budget de recherche GLOBAL (§18 de la mission de durcissement du contrat,
// 2026-08-17), partage par reference a travers tout l'arbre de recursion
// (initial ET chaque round de reparation de residu) -- distinct de
// `SatinPlanConfig::max_recursion_depth`, qui ne borne que la PROFONDEUR
// d'une branche individuelle, jamais le volume total de travail.
struct PlanningBudgetState {
    int regions_explored{0};   // appels a `plan_recursive`
    int regions_accepted{0};   // feuilles reellement acceptees dans le plan
    int oracle_evaluations{0}; // tentatives de decomposition guidee (beam search / concavite)
    bool exceeded{false};      // vrai des qu'une des limites est franchie
    // Filet de securite wall-clock (§18, cf. doc de `SatinPlanConfig::
    // max_planning_wall_clock_ms`) : horodatage du DEBUT de l'appel a
    // `create_satin_plan`, pas de chaque iteration individuelle.
    std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};
};

bool budget_exceeded(const PlanningBudgetState& budget, const SatinPlanConfig& config) {
    if (budget.regions_explored >= config.max_planning_iterations ||
        budget.regions_accepted >= config.max_total_regions) {
        return true;
    }
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                                  budget.start_time)
                                .count();
    return elapsedMs >= config.max_planning_wall_clock_ms;
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
                                bool fromResidualRepair, PlanningBudgetState& budget);

// §14 du plan de refonte satin, suite (2026-08-14) : quand `region` n'a
// AUCUNE jonction de squelette (donc rien pour les deux familles de coupe
// ancrees sur un evenement de detachement), tente une coupe ancree sur une
// concavite du CONTOUR lui-meme -- `generate_concavity_cut_candidates`,
// purement geometrique, sans graphe. Meme structure de recursion que
// `decompose_and_recurse` (chaque moitie replanifiee recursivement), mais
// exactement DEUX enfants fixes (une seule coupe) plutot qu'une boucle sur
// des evenements de detachement. Limite assumee et documentee : contrairement
// au chemin base sur le squelette, aucune adjacence n'est rapportee entre
// les deux moities issues d'ici (§19/§20 restent un travail futur pour cette
// famille -- pas de `RegionSplitReport::merge_candidates` equivalent).
RecursionOutcome try_concavity_decomposition(const geometry::PathSet& region, const SatinPlanConfig& config, int depth,
                                             bool fromResidualRepair, PlanningBudgetState& budget) {
    RecursionOutcome out;
    const auto candidates = generate_concavity_cut_candidates(region, config.concavityCutParams);
    ++budget.oracle_evaluations;
    const auto chosen = select_best_concavity_cut(candidates, config.genParams, config.coverageConfig, config.density,
                                                   config.concavity_cut_beam_width);
    if (!chosen) {
        out.unresolved.push_back(region);
        return out;
    }
    const auto& winner = candidates[*chosen];
    const auto absorb = [&](const geometry::PathSet& piece) {
        RecursionOutcome child = plan_recursive(piece, config, depth + 1, fromResidualRepair, budget);
        const std::size_t baseIndex = out.accepted.size();
        for (std::size_t k = 0; k < child.adjacency.size(); ++k) {
            out.adjacency.emplace_back(child.adjacency[k].first + baseIndex, child.adjacency[k].second + baseIndex);
            out.overlaps.push_back(std::move(child.overlaps[k]));
        }
        for (auto& leaf : child.accepted) out.accepted.push_back(std::move(leaf));
        for (auto& p : child.unresolved) out.unresolved.push_back(std::move(p));
        for (auto& w : child.warnings) out.warnings.push_back(std::move(w));
    };
    absorb(winner.first_piece);
    absorb(winner.second_piece);
    return out;
}

// Tente de decomposer `region` (au moins une jonction requise dans son
// propre squelette) et replanifie RECURSIVEMENT chaque sous-region
// resultante -- c'est cette recursion, absente de l'ancien
// `build_satin_sections` a une seule passe, qui permet a une region fille
// encore mediocre d'etre redecoupee a son tour (§10 du plan de refonte).
RecursionOutcome decompose_and_recurse(const geometry::PathSet& region, const SatinPlanConfig& config, int depth,
                                       bool fromResidualRepair, PlanningBudgetState& budget) {
    RecursionOutcome out;
    const auto analysis = auto_satin::analyze_region(region, config.genParams.analysis);
    if (!analysis || analysis->debug.graph.junction_count() == 0) {
        // Rien a decomposer via le squelette (echec d'analyse, ou squelette
        // sans jonction -- ex. un anneau pur ou une entaille). Avant de
        // renoncer, tente la famille de coupes qui ne depend d'AUCUNE
        // jonction (§14 suite) : concavite du contour -- cible exactement ce
        // cas (une entaille profonde, un sablier). Deux garde-fous trouves
        // par des regressions REELLES (test_autodigitize.cpp) en construisant
        // cette famille :
        //  - JAMAIS pendant la reparation de residu (`fromResidualRepair`) :
        //    un fragment de residu est une geometrie de DECOUPE (Clipper2,
        //    `subtract_polygons` sur la couverture deja produite), souvent
        //    petite et irreguliere par construction -- pas la forme voulue
        //    par l'utilisateur. Un sommet reflex "spurieux" issu de ce bruit
        //    geometrique peut y fabriquer une coupe qui fragmente le residu
        //    en plusieurs eclats au lieu de le laisser honnetement rapporte
        //    (defaut trouve sur le reseau en T : reliquat mesure EN HAUSSE
        //    malgre l'"acceptation" de ces eclats).
        //  - JAMAIS sur une region AVEC UN TROU : un anneau/une region
        //    perforee A EXACTEMENT UN TROU a deja son propre solveur local
        //    dedie et excellent (`build_annular_sections`, appele
        //    inconditionnellement des que `region.holes.size()==1`, quel
        //    que soit le statut de satinabilite) -- strictement meilleur
        //    qu'une coupe rectiligne qui ignore la topologie du trou (cette
        //    famille ne considere QUE le contour EXTERIEUR, jamais les
        //    trous). Defaut trouve sur un anneau reel (segmentation ->
        //    vectorisation, contour legerement irregulier) : une coupe
        //    concavite trouvee sur un sommet reflex marginal du contour
        //    tranchait l'anneau en fragments (9 sections au lieu des 4
        //    sections annulaires propres), inconditionnellement preferee a
        //    la bonne solution locale simplement parce qu'elle produisait
        //    ELLE AUSSI un resultat "accepte" (meme regle deja presente
        //    pour le chemin base sur le squelette : la decomposition
        //    l'emporte sans comparaison de qualite des qu'elle produit quoi
        //    que ce soit).
        //    POUR 2+ TROUS EN REVANCHE (corpus de torture, 2026-08-17,
        //    fixture "two_holes") : AUCUN solveur dedie n'existe (`build_
        //    annular_sections` ne gere qu'EXACTEMENT un trou), donc la
        //    condition "strictement meilleur" ci-dessus ne s'applique plus
        //    -- exclure aussi ce cas laissait une region a 2+ trous SANS
        //    AUCUNE strategie de decomposition (defaut REEL trouve : refus
        //    immediat, aucune tentative). La coupe concavite reste correcte
        //    ici : elle opere uniquement sur le contour EXTERIEUR (jamais
        //    les trous, cf. Clipper2 dans `cut_path_set`/`subtract_polygons`
        //    qui preservent deja les trous intacts de part et d'autre d'une
        //    coupe), donc les trous restent proprement geres sans logique
        //    additionnelle -- juste moins optimal qu'un solveur dedie qui
        //    n'existe simplement pas pour ce cas.
        if (config.use_concavity_cuts && !fromResidualRepair && region.holes.size() != 1) {
            RecursionOutcome concavityOutcome =
                try_concavity_decomposition(region, config, depth, fromResidualRepair, budget);
            if (!concavityOutcome.accepted.empty()) {
                return concavityOutcome;
            }
        }
        // Ni le squelette ni une concavite exploitable : le meilleur effort
        // local a deja ete tente par l'appelant avant ce point ; ici il n'y
        // a plus rien a essayer, seulement a signaler honnetement.
        out.unresolved.push_back(region);
        return out;
    }
    const auto& graph = analysis->debug.graph;
    const auto decomposition = decompose_into_paths(graph);

    satin_planning::BeamSearchParams beamParams;
    beamParams.genParams = config.genParams;
    beamParams.coverageConfig = config.coverageConfig;
    beamParams.density = config.density;
    // §18 de la mission de durcissement du contrat (2026-08-17) : voir la
    // documentation de `SatinPlanConfig::beam_width` -- borne le cout du
    // selecteur par defaut (generations+mesures COMPLETES par evenement de
    // detachement) sur les regions complexes, plutot que de le laisser
    // croitre sans limite. `decomposition.size()` (nombre de CHEMINS, donc
    // d'evenements de detachement effectivement traites par `split_region`)
    // s'est revele un signal plus fiable que `graph.junction_count()`
    // (nombre de NOEUDS de jonction) : une etoile a 5 branches n'a qu'UN
    // seul noeud de jonction mais 5 chemins -- `junction_count()` seul
    // manquait donc ce cas reel (fixture "star5", corpus de torture
    // 2026-08-17), toujours lent apres un premier correctif base
    // uniquement sur le nombre de jonctions.
    const std::size_t complexitySignal =
        std::max(static_cast<std::size_t>(graph.junction_count()), decomposition.paths.size());
    const bool localComplexityHigh = complexitySignal > config.max_junctions_for_full_beam_search;
    const bool globalBudgetSpent = budget.oracle_evaluations >= config.max_oracle_evaluations_at_full_beam_width;
    beamParams.beam_width = (localComplexityHigh || globalBudgetSpent) ? std::size_t{1} : config.beam_width;
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
    ++budget.oracle_evaluations;

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
        RecursionOutcome child = plan_recursive(childRegion, config, depth + 1, fromResidualRepair, budget);
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
                                bool fromResidualRepair, PlanningBudgetState& budget) {
    ++budget.regions_explored;
    LocalAttempt attempt = try_local_satin(region, config);
    // §18 de la mission de durcissement du contrat (2026-08-17) : la
    // profondeur locale n'est qu'UNE des deux facons d'epuiser le budget --
    // `budget_exceeded` borne le volume TOTAL de travail sur tout l'arbre
    // (initial + reparation de residu), pas seulement une branche. Les deux
    // conditions partagent volontairement le meme repli (accepter le
    // meilleur effort local s'il est adequat, sinon rapporter en residu) :
    // la difference entre elles n'est utile que pour le diagnostic
    // (`budget.exceeded`, lu par `create_satin_plan` pour choisir entre
    // `Incomplete` et `Impossible`), jamais pour la decision elle-meme.
    const bool globalBudgetExceeded = budget_exceeded(budget, config);
    if (globalBudgetExceeded) budget.exceeded = true;
    const bool budgetExhausted = depth >= config.max_recursion_depth || globalBudgetExceeded;
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
        ++budget.regions_accepted;
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

    RecursionOutcome decomposed = decompose_and_recurse(region, config, depth, fromResidualRepair, budget);
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

// §20 du plan de refonte satin (2026-08-16) : ferme visuellement
// l'interstice de coupe entre deux regions adjacentes en reconstruisant
// leurs colonnes sur la geometrie de recouvrement DEDIEE (`SatinPlan::
// overlaps`, deja calculee depuis §19/§20 mais jusqu'ici jamais consommee)
// plutot que la geometrie structurelle brute -- jamais l'inverse : la
// mesure de couverture continue TOUJOURS de se faire sur `SatinPlanRegion::
// region` (structurelle, inchangee), la geometrie d'overlap ne sert qu'A LA
// GENERATION. Repli sur les colonnes d'origine si la reconstruction echoue
// ou couvre moins bien la region structurelle que la version d'origine --
// jamais un resultat degrade accepte au nom de fermer un interstice (meme
// regle "jamais de degradation silencieuse" que le reste du planificateur).
// Chaque region participe a au plus UNE paire d'adjacence (§19 : issue de
// l'unique coupe qui l'a creee), donc aucun risque de double reconstruction
// incoherente ici.
void extend_columns_into_known_overlaps(SatinPlan& plan, const SatinPlanConfig& config) {
    for (std::size_t i = 0; i < plan.adjacency.size(); ++i) {
        const auto [a, b] = plan.adjacency[i];
        const auto& overlap = plan.overlaps[i];

        const auto try_extend = [&](std::size_t regionIdx, const geometry::PathSet& extendedGeom) {
            SatinPlanRegion& target = plan.regions[regionIdx];
            if (!target.coverage) return;  // rien de fiable a comparer, ne rien risquer

            auto rebuilt = auto_satin::build_satin_columns(extendedGeom, config.genParams);
            const auto inputs = to_coverage_inputs(rebuilt, config.density);
            if (inputs.empty()) return;  // reconstruction refusee : repli silencieux sur l'original
            const auto newCoverage =
                satin_coverage::analyze_satin_coverage(target.region, inputs, config.coverageConfig);
            if (!newCoverage) return;
            if (newCoverage->raw_coverage_ratio + 1e-9 < target.coverage->raw_coverage_ratio) return;

            target.columns = std::move(rebuilt);
            target.coverage = *newCoverage;
        };

        try_extend(a, overlap.first_extended);
        try_extend(b, overlap.second_extended);
    }
}

}  // namespace

std::string to_string(SatinPlanStatus status) {
    switch (status) {
    case SatinPlanStatus::Complete:
        return "Complete";
    case SatinPlanStatus::Incomplete:
        return "Incomplete";
    case SatinPlanStatus::Impossible:
        return "Impossible";
    }
    return "?";
}

SatinPlan create_satin_plan(const geometry::PathSet& source, const SatinPlanConfig& config) {
    SatinPlan plan;
    PlanningBudgetState budget;

    RecursionOutcome top = plan_recursive(source, config, 0, false, budget);
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
        // §18 : le budget global peut deja avoir ete atteint pendant la
        // decomposition initiale -- inutile de lancer une nouvelle passe de
        // reparation qui ne ferait que re-epuiser le meme budget sans
        // progres reel (`plan_recursive` refuserait de toute facon toute
        // decomposition ulterieure).
        if (budget.exceeded) break;
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
            // §Phase 7 (2026-08-17) : une premiere version de cette boucle
            // recoupait `missing.region` contre les regions deja acceptees
            // (difference booleenne) avant replanification, pour eliminer
            // le double comptage d'aire structurelle mesure par le corpus
            // de torture (Invariant 2/6, jusqu'a 9% de surface source en
            // trop). CE CORRECTIF A ETE REVERTE : verifie empiriquement
            // regressif sur `test_autodigitize` (reseau en T reel, pas une
            // fixture synthetique) -- recouper produit des lambeaux fins le
            // long des frontieres entre regions, bien plus difficiles a
            // point-piquer correctement que la geometrie "manquante"
            // brute ; resultat mesure : 33,8 mm2 de tissu reellement non
            // point-pique (repli tatami compris) contre <0,5 mm2 avant.
            // Un vrai residu de couverture (tissu non point-pique) est un
            // defaut BIEN PLUS grave qu'un chevauchement de comptabilite
            // interne entre le polygone structurel d'une region et celui
            // de son patch de reparation -- §33/§37 : verifier avant de
            // conclure qu'un correctif est une amelioration, pas seulement
            // qu'il fait taire UN test. Le chevauchement redevient donc
            // toleré ici (documenté comme un chevauchement ATTENDU entre
            // une region et son patch de reparation, pas une regression),
            // et le test d'invariant correspondant a ete ajuste en
            // consequence (cf. `SatinPlanRegion::from_residual_repair`,
            // deja porte par chaque leaf issue de cette boucle, utilise par
            // le test pour distinguer les deux cas).
            RecursionOutcome repaired = plan_recursive(missing.region, config, 0, true, budget);
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
                for (auto& p : repaired.unresolved) stillMissing.push_back(std::move(p));
            }
        }
        pendingResidual = std::move(stillMissing);
        if (!anyRepaired) {
            break;  // plus aucun progres possible, inutile de reboucler
        }
    }

    // §20 : ferme les interstices de coupe connus AVANT la mesure finale,
    // pour qu'`aggregate_coverage` reflete l'etat REELLEMENT emis (colonnes
    // eventuellement etendues), pas un etat intermediaire.
    if (config.extend_columns_into_overlap) {
        extend_columns_into_known_overlaps(plan, config);
    }

    // Mesure finale (apres toute reparation) pour que `aggregate_coverage`
    // reflete l'etat REELLEMENT emis, pas un etat intermediaire d'une
    // iteration de reparation precedente.
    //
    // Defaut reel trouve le 2026-08-17 (`test_autodigitize`, reseau en T
    // reel -- 33,8 mm2 de tissu non point-pique jamais signale) : quand le
    // budget s'epuise PENDANT une tentative de reparation (`plan_recursive`
    // appele avec `budgetExhausted` deja vrai), le mecanisme de repli
    // "meilleur effort local" (`adequateFallback`, delibere -- §7 defaut du
    // 2026-08-14, ne JAMAIS perdre une piece) accepte une region dont la
    // couverture INTERNE est a peine au-dessus de `min_fallback_coverage_
    // ratio` -- correct pour ne pas perdre la piece, mais cette acceptation
    // faisait ALORS disparaitre la piece de `pendingResidual` (plus
    // "manquante" pour la boucle), sans qu'aucun tour suivant n'ait la
    // chance de re-mesurer et re-signaler ce qui reste reellement non
    // couvert a l'INTERIEUR de cette region a peine acceptee -- la boucle
    // suivante s'arrete immediatement (`if (budget.exceeded) break;`) avant
    // de pouvoir le faire. Resultat : un residu bien reel disparaissait
    // silencieusement de `unresolved_residual`. Corrige en derivant
    // `unresolved_residual` de CETTE mesure finale (authoritative, calculee
    // sur l'etat REELLEMENT emis, jamais un bookkeeping intermediaire par
    // tour qui peut devenir perime exactement dans ce cas de bord) plutot
    // que du `pendingResidual` accumule pendant la boucle.
    std::vector<geometry::PathSet> finalResidual;
    {
        std::vector<satin_coverage::SatinColumnInput> allInputs;
        for (const auto& r : plan.regions) {
            auto inputs = to_coverage_inputs(r.columns, config.density);
            for (auto& in : inputs) allInputs.push_back(std::move(in));
        }
        if (const auto aggregate = satin_coverage::analyze_satin_coverage(source, allInputs, config.coverageConfig)) {
            plan.aggregate_coverage = *aggregate;
            finalResidual.reserve(aggregate->missing_regions.size());
            for (const auto& missing : aggregate->missing_regions) finalResidual.push_back(missing.region);
        } else {
            finalResidual = std::move(pendingResidual);
        }
    }

    plan.unresolved_residual = std::move(finalResidual);
    plan.regions_explored = budget.regions_explored;
    plan.oracle_evaluations = budget.oracle_evaluations;

    // §7 de la mission de durcissement du contrat (2026-08-17) : verdict
    // EXPLICITE, calcule une seule fois ici, jamais laisse a deduire par
    // l'appelant. Criteres A-E :
    //  A (aucun residu significatif) : reutilise EXACTEMENT le seuil deja
    //     utilise pour decider si une composante manquante merite une
    //     tentative de reparation (`repairAreaThresholdMm2` ci-dessus) --
    //     un residu qui n'a meme pas franchi ce seuil est du bruit naturel
    //     deja tolere ailleurs dans ce module, pas une raison de refuser le
    //     statut Complete.
    //  C (couverture globale suffisante) : couverture BRUTE (pas coeur,
    //     deliberement -- cf. doc de `complete_min_raw_coverage`) mesuree
    //     sur l'union reelle des colonnes EMISES contre la region SOURCE
    //     entiere, jamais une moyenne par region.
    //  D (aucun trou local disproportionne) : `max_gap_radius_mm` de la
    //     couverture agregee contre le seuil de l'analyseur de couverture
    //     (`coverageConfig.max_gap_radius_mm`) -- ce sous-critere n'avait PAS
    //     besoin d'un seuil dedie (contrairement a C), verifie empiriquement
    //     ne pas se declencher a tort sur le corpus existant.
    //  B (chaque feuille produit reellement des colonnes) est deja garanti
    //     PAR CONSTRUCTION (`accept_as_leaf` n'accepte jamais une region
    //     sans colonnes, cf. `hasColumns` dans `plan_recursive`) -- verifie
    //     ici comme filet de securite explicite plutot que suppose en
    //     silence : une violation signalerait un vrai defaut du planner.
    //  E (aucune geometrie invalide) : reutilise `degenerate_interval_count`
    //     deja calcule par l'analyseur de couverture independant (rails
    //     croises detectes lors de la mesure), plutot que reimplementer une
    //     detection d'auto-intersection separee.
    double significantResidualAreaMm2 = 0.0;
    for (const auto& piece : plan.unresolved_residual) {
        const double areaMm2 = geometry::path_set_area_um2(piece) / 1e6;
        if (areaMm2 >= repairAreaThresholdMm2) significantResidualAreaMm2 += areaMm2;
    }
    const bool noSignificantResidual = significantResidualAreaMm2 <= 1e-9;
    if (!noSignificantResidual) {
        std::ostringstream reason;
        reason.setf(std::ios::fixed);
        reason.precision(2);
        reason << "Residu significatif restant : " << significantResidualAreaMm2
               << " mm2 (seuil de significativite=" << repairAreaThresholdMm2 << " mm2).";
        plan.diagnostics.push_back({"SignificantResidualRemaining", reason.str()});
    }

    const bool coverageSufficient =
        plan.aggregate_coverage.has_value() && plan.aggregate_coverage->raw_coverage_ratio >= config.complete_min_raw_coverage;
    const bool noLargeLocalGap =
        plan.aggregate_coverage.has_value() && plan.aggregate_coverage->max_gap_radius_mm <= config.coverageConfig.max_gap_radius_mm;
    if (plan.aggregate_coverage.has_value() && (!coverageSufficient || !noLargeLocalGap)) {
        std::ostringstream reason;
        reason.setf(std::ios::fixed);
        reason.precision(2);
        reason << "Couverture brute agregee=" << (plan.aggregate_coverage->raw_coverage_ratio * 100.0)
               << "% (seuil=" << (config.complete_min_raw_coverage * 100.0)
               << "%), rayon de trou max=" << plan.aggregate_coverage->max_gap_radius_mm
               << "mm (seuil=" << config.coverageConfig.max_gap_radius_mm << "mm).";
        plan.diagnostics.push_back({"InsufficientAggregateCoverage", reason.str()});
    }

    bool allLeavesHaveColumns = true;
    for (const auto& r : plan.regions) {
        if (r.columns.columns.empty() && r.columns.parametric_columns.empty()) {
            allLeavesHaveColumns = false;
            break;
        }
    }
    if (!allLeavesHaveColumns) {
        plan.diagnostics.push_back(
            {"RegionWithoutColumns",
             "Une region acceptee ne porte aucune colonne construite -- violation du contrat interne, "
             "signale explicitement plutot que silencieusement traite comme un succes."});
    }

    bool geometryValid = true;
    for (const auto& r : plan.regions) {
        if (r.coverage && r.coverage->degenerate_interval_count > 0) {
            geometryValid = false;
            plan.diagnostics.push_back(
                {"InvalidGeometryDetected",
                 "Au moins une region porte des intervalles degeneres (rails croises) detectes par "
                 "l'analyseur de couverture independant."});
            break;
        }
    }

    if (budget.exceeded) {
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - budget.start_time)
                                    .count();
        std::ostringstream reason;
        reason.setf(std::ios::fixed);
        reason.precision(0);
        reason << "Budget d'exploration atteint (regions_explored=" << budget.regions_explored << "/"
               << config.max_planning_iterations << ", regions_accepted=" << budget.regions_accepted << "/"
               << config.max_total_regions << ", ecoule=" << elapsedMs << "ms/" << config.max_planning_wall_clock_ms
               << "ms) avant la fin de la planification.";
        plan.diagnostics.push_back({"SearchBudgetExceeded", reason.str()});
    }

    // Barre delibrement BASSE (§6 : "Impossible" doit rester extremement
    // rare et signifier "budget epuise ET territoire significatif encore
    // non representable", jamais "le solveur local direct a echoue une
    // fois" -- un budget epuise avec une couverture DEJA raisonnable reste
    // simplement `Incomplete`, pas `Impossible`).
    constexpr double kImpossibleCoverageCeiling = 0.5;
    const bool territoryLargelyUnaddressed =
        !plan.aggregate_coverage.has_value() || plan.aggregate_coverage->raw_coverage_ratio < kImpossibleCoverageCeiling;

    if (noSignificantResidual && coverageSufficient && noLargeLocalGap && allLeavesHaveColumns && geometryValid &&
        !budget.exceeded) {
        plan.status = SatinPlanStatus::Complete;
    } else if (budget.exceeded && territoryLargelyUnaddressed) {
        plan.status = SatinPlanStatus::Impossible;
    } else {
        plan.status = SatinPlanStatus::Incomplete;
    }

    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);
    out << "[SatinPlan] statut=" << to_string(plan.status) << ", " << plan.regions.size() << " region(s) acceptee(s)";
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
    out << "[SatinPlan] statut=" << to_string(plan.status) << "\n";
    if (!plan.diagnostics.empty()) {
        out << "Diagnostics :\n";
        for (const auto& d : plan.diagnostics) {
            out << "  [" << d.code << "] " << d.message << "\n";
        }
    }
    out << "Exploration : " << plan.regions_explored << " region(s) tentee(s), " << plan.oracle_evaluations
        << " evaluation(s) de decomposition guidee\n";
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
