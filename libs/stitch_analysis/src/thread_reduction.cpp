// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/thread_reduction.hpp"

#include <numeric>

#include "openstitch/thread_palette/color_reduction.hpp"

namespace openstitch::stitch_analysis {

namespace {

ThreadMergePlan finish(const std::vector<ThreadUsage>& usage, std::vector<std::size_t> target) {
    ThreadMergePlan plan;
    plan.before = usage.size();
    for (std::size_t i = 0; i < usage.size(); ++i) {
        if (target[i] == i) {
            continue;
        }
        const auto& to = usage[target[i]].identity;
        for (const ObjectId id : usage[i].objects) {
            plan.reassignments.push_back({id, to.key, to.rgb});
        }
    }
    std::size_t kept = 0;
    for (std::size_t i = 0; i < usage.size(); ++i) {
        kept += target[i] == i ? 1 : 0;
    }
    plan.after = kept;
    plan.target = std::move(target);
    return plan;
}

} // namespace

ThreadMergePlan plan_reduce_threads(const std::vector<ThreadUsage>& usage,
                                    std::size_t max_threads) {
    std::vector<std::size_t> identity(usage.size());
    std::iota(identity.begin(), identity.end(), std::size_t{0});
    if (max_threads == 0 || usage.size() <= max_threads) {
        return finish(usage, std::move(identity));
    }
    std::vector<thread_palette::WeightedColor> colors;
    colors.reserve(usage.size());
    for (const auto& u : usage) {
        colors.push_back({u.identity.rgb, static_cast<double>(u.stitch_count)});
    }
    const auto reduction = thread_palette::reduce_colors(colors, max_threads);
    if (reduction.palette.size() >= usage.size()) {
        return finish(usage, std::move(identity));
    }
    // Cible de chaque fil : parmi ceux qui portent la couleur retenue, le plus cousu (le premier en
    // cas d'égalité, pour rester déterministe).
    std::vector<std::size_t> target(usage.size());
    for (std::size_t i = 0; i < usage.size(); ++i) {
        const auto& kept = reduction.palette[reduction.mapping[i]];
        std::size_t best = i;
        bool found = false;
        for (std::size_t k = 0; k < usage.size(); ++k) {
            if (usage[k].identity.rgb == kept &&
                (!found || usage[k].stitch_count > usage[best].stitch_count)) {
                best = k;
                found = true;
            }
        }
        // Un fil déjà à la couleur retenue est conservé tel quel, même s'il n'est pas le plus
        // cousu parmi ses homonymes de couleur : on ne touche qu'aux fils qui changent.
        target[i] = usage[i].identity.rgb == kept ? i : best;
    }
    return finish(usage, std::move(target));
}

ThreadMergePlan plan_merge_threads(const std::vector<ThreadUsage>& usage, std::size_t from,
                                   std::size_t to) {
    std::vector<std::size_t> target(usage.size());
    std::iota(target.begin(), target.end(), std::size_t{0});
    if (from < usage.size() && to < usage.size() && from != to) {
        target[from] = to;
    }
    return finish(usage, std::move(target));
}

} // namespace openstitch::stitch_analysis
