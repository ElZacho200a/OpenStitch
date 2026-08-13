// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

#include "openstitch/satin_planning/region_oracle.hpp"
#include "openstitch/satin_planning/region_split.hpp"

namespace openstitch::satin_planning {

// Parametres de la recherche guidee par l'oracle (phase 6 du plan SGSD).
struct BeamSearchParams {
    // Nombre de candidats VALIDES evalues (construits + mesures) par
    // evenement de detachement avant de retenir le meilleur -- le
    // "beamWidth" de la spec (§16), applique ici a une seule decision de
    // coupe plutot qu'a une recherche multi-jonctions combinatoire (§16
    // avertit explicitement contre l'explosion combinatoire ; une recherche
    // arborescente sur l'ensemble des jonctions simultanement est reportee
    // a une iteration ulterieure). `0` fait echouer systematiquement la
    // selection (aucun candidat evalue) ; `1` evalue uniquement le premier
    // candidat valide (comme le comportement par defaut, mais en exigeant
    // EN PLUS que sa construction reussisse reellement).
    std::size_t beam_width{3};
    // Mode Parametric recommande pour reproduire le comportement de
    // production d'`autodigitize.cpp` -- laisse a `Legacy` par defaut
    // (comme `SatinColumnsParameters`) pour que le cout de la recherche soit
    // explicite au niveau de l'appelant, pas cache dans une valeur par
    // defaut surprenante.
    auto_satin::SatinColumnsParameters genParams{};
    satin_coverage::SatinCoverageConfig coverageConfig{};
    Micrometers density{400};
    Micrometers cut_width{20};  // meme defaut que geometry::cut_path_set / CutCandidateParams
};

// Score d'un candidat evalue par la recherche : sa couverture brute une fois
// reellement construit, pour comparaison directe entre candidats.
struct BeamCandidateScore {
    std::size_t candidate_index{0};
    bool build_succeeded{false};
    double raw_coverage_ratio{0.0};
};

// Selecteur (`CutCandidateSelector`, cf. region_split.hpp) guide par
// l'oracle phase 5 : pour chaque evenement de detachement, materialise
// jusqu'a `beam_width` candidats valides (les plus proches de la jonction
// d'abord), construit reellement des colonnes satin sur chacun
// (`auto_satin::build_satin_columns`) et mesure sa couverture
// (`satin_coverage::analyze_satin_coverage`), puis retient celui dont la
// couverture brute est la meilleure -- au lieu du premier valide
// (comportement par defaut de `generate_cut_candidates`). Journalise chaque
// decision (`decisions()`) pour le debug : quels candidats ont ete evalues,
// avec quel score, et lequel a gagné.
//
// Cout : jusqu'a `beam_width` generations+mesures completes PAR evenement de
// detachement (en plus des analyses de satinabilite deja faites par
// `generate_cut_candidates` elle-meme) -- reserve a un usage hors ligne
// (CLI de debug, tests), jamais au chemin interactif.
class OracleGuidedSelector {
public:
    explicit OracleGuidedSelector(BeamSearchParams params = {});

    // Compatible avec `CutCandidateSelector` : `std::ref(*this)` ou une
    // lambda de capture peuvent l'injecter dans `CutCandidateParams::selector`.
    [[nodiscard]] std::optional<std::size_t> operator()(const geometry::PathSet& piece,
                                                         const std::vector<CutCandidate>& candidates);

    // Une entree par evenement de detachement traite jusqu'ici (meme ordre
    // que les appels), chacune la liste des candidats reellement evalues
    // avec leur score.
    [[nodiscard]] const std::vector<std::vector<BeamCandidateScore>>& decisions() const { return decisions_; }

private:
    BeamSearchParams params_;
    std::vector<std::vector<BeamCandidateScore>> decisions_;
};

}  // namespace openstitch::satin_planning
