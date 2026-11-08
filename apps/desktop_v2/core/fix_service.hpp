// SPDX-License-Identifier: Apache-2.0
#pragma once
// Desktop V2 / Studio (CX09) -- propositions de correction « Corriger » et réduction des fils.
// Sans Qt. Une proposition est de la donnée pure calculée sur un instantané immuable, marquée de
// la révision analysée : l'interface la montre en aperçu, l'utilisateur l'approuve, puis UNE
// commande composite annulable est construite contre le projet courant. Une proposition dont la
// révision ne correspond plus est refusée par l'appelant (voir `FixController`).
//
// Aucun second moteur d'analyse : les constats viennent de `stitch_analysis::analyze_project` (le
// même que le panneau V1, `project quality` et l'export) et le plan de fusion de
// `stitch_analysis::plan_reduce_threads`.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_analysis/thread_reduction.hpp"

namespace openstitch::desktop_v2 {

struct FixProposal {
    stitch_analysis::FixKind kind{stitch_analysis::FixKind::None};
    ObjectId object{};
    Vec2um delta{};
    Micrometers value{};
    std::string category; // slug du constat d'origine
    std::string label;    // libellé du bouton, ex. « Ramener dans le cadre »
    std::string message;  // phrase du constat
    bool safe{false};     // applicable par « Corriger tout ce qui est sûr »
};

struct FixPlan {
    std::vector<FixProposal> proposals;
    std::size_t safeCount{0};
    std::uint64_t revision{0}; // révision du document analysée
};

// Corrections que le moteur sait appliquer sans éditeur supplémentaire. L'agrandissement d'un
// texte passe par l'éditeur de texte : il n'est jamais proposé ici.
[[nodiscard]] bool isApplicableFix(stitch_analysis::FixKind kind);
// Corrections « sûres » : déplacer dans le cadre, plafonner une longueur de point, scinder les
// points longs, activer les finitions.
[[nodiscard]] bool isSafeFix(stitch_analysis::FixKind kind);

[[nodiscard]] Result<FixPlan> plan_fixes(std::shared_ptr<const document::Project> project,
                                         std::uint64_t revision, const TaskContext& task);

struct FixBuild {
    std::unique_ptr<commands::ICommand> command; // nul si rien d'applicable
    std::size_t applied{0};                      // corrections réellement incluses
    std::size_t skipped{0};                      // proposées mais plus applicables / en double
};

// Construit UNE commande composite pour `indices` (indices dans `plan.proposals`) contre
// `project`. Un objet déplacé ou plafonné une seule fois ; les finitions sont fusionnées en une
// seule commande. `indices` vide : toutes les corrections sûres.
[[nodiscard]] FixBuild build_fix_command(const document::Project& project, const FixPlan& plan,
                                         const std::vector<std::size_t>& indices,
                                         const std::string& label);

// Aperçu chiffré d'une correction (avant/après), sans rien modifier.
[[nodiscard]] std::string describe_fix(const document::Project& project,
                                       const FixProposal& proposal);

// --- Réduction du nombre de fils -------------------------------------------------------------

struct ThreadReductionPreview {
    stitch_analysis::ThreadMergePlan plan;
    std::uint64_t revision{0};
};

// `maxThreads == 0` ou design déjà sous la limite : aperçu au plan vide.
[[nodiscard]] Result<ThreadReductionPreview>
plan_thread_reduction(std::shared_ptr<const document::Project> project, std::uint64_t revision,
                      std::size_t maxThreads, const TaskContext& task);

// Commande unique recolorant tous les objets concernés ; nulle si le plan est vide.
[[nodiscard]] std::unique_ptr<commands::ICommand>
build_thread_reduction_command(const ThreadReductionPreview& preview, const std::string& label);

} // namespace openstitch::desktop_v2
