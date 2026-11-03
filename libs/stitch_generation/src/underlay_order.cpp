// SPDX-License-Identifier: Apache-2.0
#include "underlay_order.hpp"

#include <algorithm>
#include <numeric>
#include <optional>

namespace openstitch::stitch_generation {

namespace {

using stitch::CommandType;
using stitch::StitchCommand;
using stitch::StitchPass;

constexpr std::size_t kMaxExactRuns = 6;

struct Run {
    std::size_t begin;
    std::size_t end; // exclu
};

struct Choice {
    std::size_t run;
    bool reversed;
};

bool is_underlay_run(const std::vector<StitchCommand>& cmds, const Run& r) {
    if (r.end - r.begin < 2) {
        return false;
    }
    for (std::size_t i = r.begin; i < r.end; ++i) {
        if (cmds[i].pass != StitchPass::Underlay) {
            return false;
        }
    }
    return true;
}

// Coût d'un déplacement : sa longueur, plus une pénalité s'il dépasse le seuil de coupe.
double move_cost(Vec2um a, Vec2um b, double threshold) {
    const double d = length_um(b - a);
    return d + (d > threshold ? 4.0 * threshold : 0.0);
}

} // namespace

UnderlayOrderResult reorder_underlay_passes(const std::vector<StitchCommand>& cmds,
                                            const document::SequenceFinishing& finishing) {
    UnderlayOrderResult result;
    result.commands = cmds;
    if (!finishing.hidden_travel.enabled || !finishing.hidden_travel.reorder_underlay) {
        return result;
    }
    const double threshold = static_cast<double>(finishing.trim_threshold.value);

    // Passes (suites de points cousus consécutifs).
    std::vector<Run> runs;
    for (std::size_t i = 0; i < cmds.size();) {
        if (cmds[i].type != CommandType::Stitch) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < cmds.size() && cmds[j].type == CommandType::Stitch) {
            ++j;
        }
        runs.push_back({i, j});
        i = j;
    }

    // Groupes : passes de sous-couche consécutives, même objet, séparées par de purs sauts.
    std::vector<std::pair<std::size_t, std::size_t>> blocks; // indices dans `runs`, fin exclue
    for (std::size_t r = 0; r < runs.size();) {
        if (!is_underlay_run(cmds, runs[r])) {
            ++r;
            continue;
        }
        std::size_t e = r + 1;
        while (e < runs.size() && is_underlay_run(cmds, runs[e]) &&
               cmds[runs[e].begin].source == cmds[runs[r].begin].source) {
            const bool onlyJumps =
                std::all_of(cmds.begin() + static_cast<std::ptrdiff_t>(runs[e - 1].end),
                            cmds.begin() + static_cast<std::ptrdiff_t>(runs[e].begin),
                            [](const StitchCommand& c) { return c.type == CommandType::Jump; });
            if (!onlyJumps) {
                break;
            }
            ++e;
        }
        blocks.emplace_back(r, e);
        r = e;
    }
    if (blocks.empty()) {
        return result;
    }

    // Réécriture : pour chaque groupe retenu, la nouvelle liste de (passe, sens).
    struct Plan {
        std::size_t firstRun;
        std::size_t lastRun; // exclu
        std::vector<Choice> order;
    };
    std::vector<Plan> plans;
    for (const auto& [rb, re] : blocks) {
        const std::size_t n = re - rb;
        const Run& first = runs[rb];
        const Run& last = runs[re - 1];
        // Ancres : dernier point cousu avant le groupe, premier après, sauf si une coupe,
        // un changement de fil ou un arrêt s'intercale (le déplacement n'est alors pas le nôtre).
        std::optional<Vec2um> before;
        {
            const std::size_t prevEnd = rb > 0 ? runs[rb - 1].end : 0;
            bool clean = rb > 0;
            for (std::size_t i = prevEnd; i < first.begin && clean; ++i) {
                clean = cmds[i].type == CommandType::Jump;
            }
            if (clean) {
                before = cmds[runs[rb - 1].end - 1].pos;
            }
        }
        std::optional<Vec2um> after;
        {
            const std::size_t nextBegin = re < runs.size() ? runs[re].begin : cmds.size();
            bool clean = re < runs.size();
            for (std::size_t i = last.end; i < nextBegin && clean; ++i) {
                clean = cmds[i].type == CommandType::Jump;
            }
            if (clean) {
                after = cmds[nextBegin].pos;
            }
        }
        const auto startOf = [&](const Choice& c) {
            const Run& r = runs[rb + c.run];
            return c.reversed ? cmds[r.end - 1].pos : cmds[r.begin].pos;
        };
        const auto endOf = [&](const Choice& c) {
            const Run& r = runs[rb + c.run];
            return c.reversed ? cmds[r.begin].pos : cmds[r.end - 1].pos;
        };
        const auto costOf = [&](const std::vector<Choice>& order) {
            double cost = 0.0;
            if (before) {
                cost += move_cost(*before, startOf(order.front()), threshold);
            }
            for (std::size_t k = 1; k < order.size(); ++k) {
                cost += move_cost(endOf(order[k - 1]), startOf(order[k]), threshold);
            }
            if (after) {
                cost += move_cost(endOf(order.back()), *after, threshold);
            }
            return cost;
        };
        const auto lengthOf = [&](const std::vector<Choice>& order) {
            double len = 0.0;
            if (before) {
                len += length_um(startOf(order.front()) - *before);
            }
            for (std::size_t k = 1; k < order.size(); ++k) {
                len += length_um(startOf(order[k]) - endOf(order[k - 1]));
            }
            if (after) {
                len += length_um(*after - endOf(order.back()));
            }
            return len;
        };

        std::vector<Choice> original;
        for (std::size_t k = 0; k < n; ++k) {
            original.push_back({k, false});
        }
        const double originalCost = costOf(original);
        std::vector<Choice> best = original;
        double bestCost = originalCost;

        if (n <= kMaxExactRuns) {
            std::vector<std::size_t> perm(n);
            std::iota(perm.begin(), perm.end(), std::size_t{0});
            do {
                for (std::size_t mask = 0; mask < (std::size_t{1} << n); ++mask) {
                    std::vector<Choice> cand;
                    cand.reserve(n);
                    for (std::size_t k = 0; k < n; ++k) {
                        cand.push_back({perm[k], ((mask >> k) & 1U) != 0});
                    }
                    const double c = costOf(cand);
                    if (c < bestCost - 1e-9) { // strict : l'ordre d'origine gagne les égalités
                        bestCost = c;
                        best = std::move(cand);
                    }
                }
            } while (std::next_permutation(perm.begin(), perm.end()));
        } else {
            // Plus proche voisin orienté, à partir de l'ancre (ou de la première passe).
            std::vector<bool> used(n, false);
            std::vector<Choice> cand;
            std::optional<Vec2um> at = before;
            for (std::size_t step = 0; step < n; ++step) {
                std::size_t bestRun = n;
                bool bestRev = false;
                double bestD = 0.0;
                for (std::size_t k = 0; k < n; ++k) {
                    if (used[k]) {
                        continue;
                    }
                    for (const bool rev : {false, true}) {
                        const Choice c{k, rev};
                        const double d = at ? move_cost(*at, startOf(c), threshold) : 0.0;
                        if (bestRun == n || d < bestD - 1e-9) {
                            bestRun = k;
                            bestRev = rev;
                            bestD = d;
                        }
                    }
                }
                used[bestRun] = true;
                cand.push_back({bestRun, bestRev});
                at = endOf(cand.back());
            }
            // Au-delà de six passes, le plus proche voisin peut rester bloqué dans un
            // parcours coûteux. Recherche locale bornée, sur l'ordre ET le sens des passes :
            // retournement seul, 2-opt (segment inversé), puis Or-opt (déplacement).
            // On part du meilleur de l'ordre initial et du parcours glouton. Un gain strict
            // est exigé : résultat déterministe et jamais plus mauvais que l'ancien mode.
            std::vector<Choice> local =
                costOf(cand) < originalCost - 1e-9 ? std::move(cand) : original;
            double localCost = costOf(local);
            std::size_t budget = 4'096;
            const auto accept = [&](std::vector<Choice> candidate) {
                if (budget == 0) {
                    return false;
                }
                --budget;
                const double score = costOf(candidate);
                if (score < localCost - 1e-9) {
                    localCost = score;
                    local = std::move(candidate);
                    return true;
                }
                return false;
            };
            for (std::size_t pass = 0; pass < 8 && budget > 0; ++pass) {
                bool improved = false;
                for (std::size_t i = 0; i < n && budget > 0; ++i) {
                    auto candidate = local;
                    candidate[i].reversed = !candidate[i].reversed;
                    if (accept(std::move(candidate))) {
                        improved = true;
                        break;
                    }
                }
                if (improved) {
                    continue;
                }
                for (std::size_t i = 0; i + 1 < n && !improved && budget > 0; ++i) {
                    for (std::size_t j = i + 1; j < n && budget > 0; ++j) {
                        auto candidate = local;
                        std::reverse(candidate.begin() + static_cast<std::ptrdiff_t>(i),
                                     candidate.begin() + static_cast<std::ptrdiff_t>(j + 1));
                        for (std::size_t k = i; k <= j; ++k) {
                            candidate[k].reversed = !candidate[k].reversed;
                        }
                        if (accept(std::move(candidate))) {
                            improved = true;
                            break;
                        }
                    }
                }
                if (improved) {
                    continue;
                }
                for (std::size_t from = 0; from < n && !improved && budget > 0; ++from) {
                    for (std::size_t to = 0; to < n && budget > 0; ++to) {
                        if (from == to) {
                            continue;
                        }
                        auto candidate = local;
                        const Choice moved = candidate[from];
                        candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(from));
                        candidate.insert(candidate.begin() + static_cast<std::ptrdiff_t>(to),
                                         moved);
                        if (accept(std::move(candidate))) {
                            improved = true;
                            break;
                        }
                    }
                }
                if (!improved) {
                    break;
                }
            }
            if (localCost < bestCost - 1e-9) {
                bestCost = localCost;
                best = std::move(local);
            }
        }
        if (bestCost < originalCost - 1e-9) {
            result.blocks += 1;
            result.saved_um += lengthOf(original) - lengthOf(best);
            plans.push_back({rb, re, std::move(best)});
        }
    }
    if (plans.empty()) {
        return result;
    }

    // Reconstruction de la séquence.
    std::vector<StitchCommand> out;
    out.reserve(cmds.size());
    std::size_t cursor = 0;
    for (const Plan& plan : plans) {
        const Run& first = runs[plan.firstRun];
        const Run& last = runs[plan.lastRun - 1];
        out.insert(out.end(), cmds.begin() + static_cast<std::ptrdiff_t>(cursor),
                   cmds.begin() + static_cast<std::ptrdiff_t>(first.begin));
        for (std::size_t k = 0; k < plan.order.size(); ++k) {
            const Choice& c = plan.order[k];
            const Run& r = runs[plan.firstRun + c.run];
            std::vector<StitchCommand> body(cmds.begin() + static_cast<std::ptrdiff_t>(r.begin),
                                            cmds.begin() + static_cast<std::ptrdiff_t>(r.end));
            if (c.reversed) {
                std::reverse(body.begin(), body.end());
            }
            if (k == 0) {
                // Le saut qui arrive au groupe vise maintenant le début de la première passe.
                if (first.begin > cursor && cmds[first.begin - 1].type == CommandType::Jump) {
                    out.back().pos = body.front().pos;
                }
            } else {
                out.push_back(
                    {body.front().pos, CommandType::Jump, body.front().source, StitchPass::Travel});
            }
            out.insert(out.end(), body.begin(), body.end());
        }
        cursor = last.end;
    }
    out.insert(out.end(), cmds.begin() + static_cast<std::ptrdiff_t>(cursor), cmds.end());
    result.commands = std::move(out);
    return result;
}

} // namespace openstitch::stitch_generation
