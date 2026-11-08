// SPDX-License-Identifier: Apache-2.0
#include "fix_service.hpp"

#include <algorithm>
#include <set>
#include <type_traits>
#include <variant>

#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/finishing_commands.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/stitch_analysis/thread_usage.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::desktop_v2 {

namespace {

using stitch_analysis::FixKind;

// Longueur de point plafonnée selon le type de l'objet (même règle que le panneau V1).
document::StitchParams clamped(const document::StitchParams& source, Micrometers value) {
    document::StitchParams params = source;
    std::visit(
        [&](auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, document::RunningStitchParams> ||
                          std::is_same_v<T, document::TatamiParams> ||
                          std::is_same_v<T, document::DirectionalFillParams>) {
                p.stitch_length = value;
            } else if constexpr (std::is_same_v<T, document::SatinParams>) {
                p.max_stitch_length = value;
            } else if constexpr (std::is_same_v<T, document::AutoSatinParams>) {
                p.split_threshold = value;
            }
        },
        params);
    return params;
}

} // namespace

bool isApplicableFix(FixKind kind) {
    return kind == FixKind::MoveIntoHoop || kind == FixKind::ClampStitchLength ||
           kind == FixKind::SplitLongStitches || kind == FixKind::EnableFinishing;
}

bool isSafeFix(FixKind kind) {
    return isApplicableFix(kind);
}

Result<FixPlan> plan_fixes(std::shared_ptr<const document::Project> project, std::uint64_t revision,
                           const TaskContext& task) {
    if (!project) {
        return fail(ErrorCategory::Internal, "Correction demandée sans projet");
    }
    if (project->embroidery_objects.empty()) {
        return fail(ErrorCategory::OperationImpossible, "Rien à corriger",
                    "Le document ne contient aucun objet brodé.");
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    auto sequence = stitch_generation::effective_sequence(*project, task);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    const auto report = stitch_analysis::analyze_project(*project, *sequence);
    if (task.cancelled()) {
        return cancelled_error(); // analyse terminée mais périmée : jamais livrée
    }
    FixPlan plan;
    plan.revision = revision;
    for (const auto& finding : report.findings) {
        if (!isApplicableFix(finding.fix.kind)) {
            continue;
        }
        FixProposal p;
        p.kind = finding.fix.kind;
        p.object = finding.object;
        p.delta = finding.fix.delta;
        p.value = finding.fix.value;
        p.category = finding.category;
        p.label = finding.fix.label;
        p.message = finding.message;
        p.safe = isSafeFix(finding.fix.kind);
        if (p.safe) {
            ++plan.safeCount;
        }
        plan.proposals.push_back(std::move(p));
    }
    return plan;
}

FixBuild build_fix_command(const document::Project& project, const FixPlan& plan,
                           const std::vector<std::size_t>& indices, const std::string& label) {
    std::vector<std::size_t> wanted = indices;
    if (wanted.empty()) {
        for (std::size_t i = 0; i < plan.proposals.size(); ++i) {
            if (plan.proposals[i].safe) {
                wanted.push_back(i);
            }
        }
    }
    auto group = std::make_unique<commands::CompositeCommand>(label);
    std::set<std::uint64_t> movedSources;
    std::set<std::uint64_t> clampedObjects;
    std::optional<document::SequenceFinishing> finishing;
    FixBuild out;
    for (const std::size_t index : wanted) {
        if (index >= plan.proposals.size()) {
            ++out.skipped;
            continue;
        }
        const FixProposal& p = plan.proposals[index];
        switch (p.kind) {
        case FixKind::MoveIntoHoop: {
            const auto* emb = project.findEmbroidery(p.object);
            if (emb == nullptr || project.findObject(emb->source_vector) == nullptr ||
                !movedSources.insert(emb->source_vector.value).second) {
                ++out.skipped;
                break;
            }
            group->add(std::make_unique<commands::TranslateVectorObjectCommand>(emb->source_vector,
                                                                                p.delta));
            ++out.applied;
            break;
        }
        case FixKind::ClampStitchLength: {
            const auto* emb = project.findEmbroidery(p.object);
            if (emb == nullptr || !clampedObjects.insert(p.object.value).second) {
                ++out.skipped;
                break;
            }
            group->add(std::make_unique<commands::SetStitchParamsCommand>(
                p.object, clamped(emb->params, p.value), "Longueur de point"));
            ++out.applied;
            break;
        }
        case FixKind::SplitLongStitches:
        case FixKind::EnableFinishing: {
            if (!finishing) {
                finishing = project.finishing;
            }
            if (p.kind == FixKind::SplitLongStitches) {
                finishing->split_long_stitches = true;
            } else {
                finishing->enabled = true;
            }
            break;
        }
        default:
            ++out.skipped;
            break;
        }
    }
    if (finishing && !(*finishing == project.finishing)) {
        group->add(std::make_unique<commands::SetFinishingCommand>(*finishing));
        ++out.applied;
    }
    if (out.applied > 0) {
        out.command = std::move(group);
    }
    return out;
}

std::string describe_fix(const document::Project& project, const FixProposal& proposal) {
    const auto mm = [](double um) { return stitch_analysis::format_mm_fr(um); };
    switch (proposal.kind) {
    case FixKind::MoveIntoHoop:
        return "Déplace l'objet de " + mm(proposal.delta.x.value) + " mm en X et de " +
               mm(proposal.delta.y.value) + " mm en Y pour qu'il rentre dans le cadre.";
    case FixKind::ClampStitchLength: {
        std::string before;
        if (const auto* emb = project.findEmbroidery(proposal.object)) {
            std::visit(
                [&](const auto& p) {
                    using T = std::decay_t<decltype(p)>;
                    if constexpr (std::is_same_v<T, document::RunningStitchParams> ||
                                  std::is_same_v<T, document::TatamiParams> ||
                                  std::is_same_v<T, document::DirectionalFillParams>) {
                        before = mm(p.stitch_length.value);
                    } else if constexpr (std::is_same_v<T, document::SatinParams>) {
                        before = mm(p.max_stitch_length.value);
                    } else if constexpr (std::is_same_v<T, document::AutoSatinParams>) {
                        before = mm(p.split_threshold.value);
                    }
                },
                emb->params);
        }
        return "Longueur de point " + (before.empty() ? std::string("actuelle") : before + " mm") +
               " ramenée à " + mm(proposal.value.value) + " mm.";
    }
    case FixKind::SplitLongStitches:
        return project.finishing.split_long_stitches
                   ? "Les points longs sont déjà scindés."
                   : "Active la scission des points trop longs pour tout le projet.";
    case FixKind::EnableFinishing:
        return project.finishing.enabled ? "Les finitions sont déjà actives."
                                         : "Active les finitions (coupes, scission) pour tout le "
                                           "projet.";
    default:
        return {};
    }
}

Result<ThreadReductionPreview>
plan_thread_reduction(std::shared_ptr<const document::Project> project, std::uint64_t revision,
                      std::size_t maxThreads, const TaskContext& task) {
    if (!project) {
        return fail(ErrorCategory::Internal, "Réduction demandée sans projet");
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    auto sequence = stitch_generation::effective_sequence(*project, task);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    const auto usage = stitch_analysis::thread_usage(*project, *sequence);
    if (task.cancelled()) {
        return cancelled_error();
    }
    ThreadReductionPreview preview;
    preview.revision = revision;
    preview.plan = stitch_analysis::plan_reduce_threads(usage, maxThreads);
    return preview;
}

std::unique_ptr<commands::ICommand>
build_thread_reduction_command(const ThreadReductionPreview& preview, const std::string& label) {
    if (preview.plan.empty()) {
        return nullptr;
    }
    std::vector<commands::ObjectThreadAssignment> assignments;
    assignments.reserve(preview.plan.reassignments.size());
    for (const auto& r : preview.plan.reassignments) {
        assignments.push_back({r.object, r.thread, r.rgb});
    }
    return std::make_unique<commands::SetObjectThreadCommand>(std::move(assignments), label);
}

} // namespace openstitch::desktop_v2
