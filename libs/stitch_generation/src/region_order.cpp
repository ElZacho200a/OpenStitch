// SPDX-License-Identifier: Apache-2.0
#include "region_order.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>
#include <optional>

namespace openstitch::stitch_generation {

namespace {

using stitch::CommandType;
using stitch::StitchCommand;
using stitch::StitchPass;

constexpr std::int32_t kCell = 250; // µm : grille d'emprise (grossière, volontairement)
constexpr std::size_t kMaxPasses = 20;

struct Chunk {
    std::size_t begin; // premier index de commande (inclus)
    std::size_t end;   // dernier + 1
    std::uint64_t source;
    bool pinned{false};
    bool barrier{false};
    std::optional<Vec2um> first; // première / dernière piqûre
    std::optional<Vec2um> last;
    // Emprise : bitmap sur la boîte englobante en cellules.
    std::int32_t cx0{0}, cy0{0}, cw{0}, ch{0};
    std::vector<std::uint8_t> bits;
};

bool is_barrier(const StitchCommand& c) {
    return c.type == CommandType::ColorChange || c.type == CommandType::Stop ||
           c.type == CommandType::End;
}

std::int32_t floor_div(std::int64_t a, std::int32_t b) {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return static_cast<std::int32_t>(q);
}

void build_footprint(const std::vector<StitchCommand>& cmds, Chunk& c, double radius) {
    std::int32_t minx = INT32_MAX, miny = INT32_MAX, maxx = INT32_MIN, maxy = INT32_MIN;
    bool any = false;
    for (std::size_t i = c.begin; i < c.end; ++i) {
        if (cmds[i].type != CommandType::Stitch) {
            continue;
        }
        any = true;
        minx = std::min(minx, cmds[i].pos.x.value);
        maxx = std::max(maxx, cmds[i].pos.x.value);
        miny = std::min(miny, cmds[i].pos.y.value);
        maxy = std::max(maxy, cmds[i].pos.y.value);
    }
    if (!any) {
        return;
    }
    const auto pad = static_cast<std::int32_t>(std::ceil(radius));
    c.cx0 = floor_div(static_cast<std::int64_t>(minx) - pad, kCell);
    c.cy0 = floor_div(static_cast<std::int64_t>(miny) - pad, kCell);
    c.cw = floor_div(static_cast<std::int64_t>(maxx) + pad, kCell) - c.cx0 + 1;
    c.ch = floor_div(static_cast<std::int64_t>(maxy) + pad, kCell) - c.cy0 + 1;
    c.bits.assign(static_cast<std::size_t>(c.cw) * static_cast<std::size_t>(c.ch), 0);
    const std::int32_t r = static_cast<std::int32_t>(std::ceil(radius / kCell));
    const auto mark = [&](double x, double y) {
        const std::int32_t ix = floor_div(static_cast<std::int64_t>(std::floor(x)), kCell) - c.cx0;
        const std::int32_t iy = floor_div(static_cast<std::int64_t>(std::floor(y)), kCell) - c.cy0;
        for (std::int32_t dy = -r; dy <= r; ++dy) {
            for (std::int32_t dx = -r; dx <= r; ++dx) {
                const std::int32_t x2 = ix + dx;
                const std::int32_t y2 = iy + dy;
                if (x2 >= 0 && y2 >= 0 && x2 < c.cw && y2 < c.ch) {
                    c.bits[static_cast<std::size_t>(y2) * c.cw + x2] = 1;
                }
            }
        }
    };
    for (std::size_t i = c.begin; i < c.end; ++i) {
        if (cmds[i].type != CommandType::Stitch) {
            continue;
        }
        const double bx = cmds[i].pos.x.value;
        const double by = cmds[i].pos.y.value;
        if (i > c.begin && cmds[i - 1].type == CommandType::Stitch) {
            const double ax = cmds[i - 1].pos.x.value;
            const double ay = cmds[i - 1].pos.y.value;
            const double len = std::hypot(bx - ax, by - ay);
            const int steps = std::max(1, static_cast<int>(std::ceil(len / (kCell / 2.0))));
            for (int s = 0; s <= steps; ++s) {
                const double t = static_cast<double>(s) / steps;
                mark(ax + (bx - ax) * t, ay + (by - ay) * t);
            }
        } else {
            mark(bx, by);
        }
    }
}

bool footprints_touch(const Chunk& a, const Chunk& b) {
    if (a.bits.empty() || b.bits.empty()) {
        return false;
    }
    const std::int32_t x0 = std::max(a.cx0, b.cx0);
    const std::int32_t y0 = std::max(a.cy0, b.cy0);
    const std::int32_t x1 = std::min(a.cx0 + a.cw, b.cx0 + b.cw);
    const std::int32_t y1 = std::min(a.cy0 + a.ch, b.cy0 + b.ch);
    for (std::int32_t y = y0; y < y1; ++y) {
        for (std::int32_t x = x0; x < x1; ++x) {
            if (a.bits[static_cast<std::size_t>(y - a.cy0) * a.cw + (x - a.cx0)] != 0 &&
                b.bits[static_cast<std::size_t>(y - b.cy0) * b.cw + (x - b.cx0)] != 0) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

RegionOrderResult reorder_regions_same_color(const std::vector<StitchCommand>& cmds,
                                             const document::Project& project,
                                             const document::SequenceFinishing& finishing) {
    RegionOrderResult result;
    result.commands = cmds;
    const auto& cfg = finishing.hidden_travel;
    if (!cfg.enabled || !cfg.reorder_regions) {
        return result;
    }
    // Les coutures de finitions coupent entre deux sources distinctes, même à courte
    // distance ; au sein d'une source, elles coupent si le saut dépasse le seuil.
    // 50 mm est un poids heuristique (PAS une durée machine mesurée) : une coupe
    // évitée vaut en priorité un détour modéré. Ne change pas le réglage de coupe.
    const double trimThreshold = static_cast<double>(finishing.trim_threshold.value);
    constexpr double kTrimPenaltyUm = 50'000.0;
    const double radius =
        std::max(1, cfg.cover_thread_width.value) / 2.0 + std::max(0, cfg.cover_margin.value);

    // Régions à ne pas déplacer : verrouillées, connecteur choisi à la main.
    std::map<std::uint64_t, bool> pinnedSource;
    for (const auto& obj : project.embroidery_objects) {
        pinnedSource[obj.id.value] =
            obj.locked || obj.connector != document::ConnectorMode::Inherit;
    }

    // Découpe : barrières (changement de fil, arrêt, fin) puis régions = suites de commandes de
    // même objet source. Une région commence à son saut d'entrée, qui est absolu.
    std::vector<Chunk> chunks;
    {
        std::size_t i = 0;
        while (i < cmds.size()) {
            if (is_barrier(cmds[i])) {
                // La barrière devient une région épinglée d'une commande, pour garder sa place.
                Chunk b;
                b.begin = i;
                b.end = i + 1;
                b.source = cmds[i].source.value;
                b.pinned = true;
                b.barrier = true;
                chunks.push_back(std::move(b));
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < cmds.size() && !is_barrier(cmds[j]) &&
                   cmds[j].source.value == cmds[i].source.value) {
                ++j;
            }
            Chunk c;
            c.begin = i;
            c.end = j;
            c.source = cmds[i].source.value;
            const auto it = pinnedSource.find(c.source);
            c.pinned = c.source == 0 || (it != pinnedSource.end() && it->second);
            for (std::size_t k = i; k < j; ++k) {
                // Un raccord saisi manuellement n'est pas déplaçable par l'optimiseur.
                if (cmds[k].type == CommandType::Trim || cmds[k].pass == StitchPass::Manual) {
                    c.pinned = true;
                }
                if (cmds[k].type == CommandType::Stitch) {
                    if (!c.first) {
                        c.first = cmds[k].pos;
                    }
                    c.last = cmds[k].pos;
                }
            }
            // Une région sans piqûre n'a rien à ordonner : elle reste en place.
            c.pinned = c.pinned || !c.first;
            chunks.push_back(std::move(c));
            i = j;
        }
    }

    // Emprises des seules régions mobiles.
    for (Chunk& c : chunks) {
        if (!c.pinned) {
            build_footprint(cmds, c, radius);
        }
    }

    const auto cost = [&](Vec2um a, Vec2um b, std::uint64_t fromSource, std::uint64_t toSource) {
        const double d = length_um(b - a);
        const bool wouldTrim = d > 0.0 && (fromSource != toSource || d > trimThreshold);
        return d + (wouldTrim ? kTrimPenaltyUm : 0.0);
    };

    // Ordre final des régions de toute la séquence (indices dans `chunks`).
    std::vector<std::size_t> finalOrder;
    finalOrder.reserve(chunks.size());
    std::vector<bool> moved(chunks.size(), false);

    for (std::size_t cursor = 0; cursor < chunks.size();) {
        if (chunks[cursor].barrier) {
            finalOrder.push_back(cursor); // changement de fil, arrêt ou fin : reste en place
            ++cursor;
            continue;
        }
        std::size_t se = cursor;
        while (se < chunks.size() && !chunks[se].barrier) {
            ++se;
        }
        const std::size_t sb = cursor;
        cursor = se;
        // Groupes de régions mobiles consécutives dans cet intervalle.
        std::size_t k = sb;
        while (k < se) {
            if (chunks[k].pinned) {
                finalOrder.push_back(k);
                ++k;
                continue;
            }
            std::size_t e = k;
            while (e < se && !chunks[e].pinned) {
                ++e;
            }
            const std::size_t n = e - k;
            // Ancres : dernière piqûre avant le groupe, première après.
            std::optional<Vec2um> before;
            if (k > 0 && chunks[k - 1].last) {
                before = chunks[k - 1].last;
            }
            std::optional<Vec2um> after;
            if (e < chunks.size() && chunks[e].first) {
                after = chunks[e].first;
            }
            if (n < 2) {
                finalOrder.push_back(k);
                k = e;
                continue;
            }
            // Contraintes : i doit rester avant j (i < j) si leurs emprises se touchent.
            std::vector<std::vector<std::size_t>> preds(n);
            for (std::size_t a = 0; a < n; ++a) {
                for (std::size_t b = a + 1; b < n; ++b) {
                    if (footprints_touch(chunks[k + a], chunks[k + b])) {
                        preds[b].push_back(a);
                    }
                }
            }
            const auto total = [&](const std::vector<std::size_t>& order) {
                double c = 0.0;
                if (before) {
                    c += cost(*before, *chunks[k + order.front()].first, chunks[k - 1].source,
                              chunks[k + order.front()].source);
                }
                for (std::size_t q = 1; q < order.size(); ++q) {
                    c += cost(*chunks[k + order[q - 1]].last, *chunks[k + order[q]].first,
                              chunks[k + order[q - 1]].source, chunks[k + order[q]].source);
                }
                if (after) {
                    c += cost(*chunks[k + order.back()].last, *after,
                              chunks[k + order.back()].source, chunks[e].source);
                }
                return c;
            };
            const auto length = [&](const std::vector<std::size_t>& order) {
                double c = 0.0;
                if (before) {
                    c += length_um(*chunks[k + order.front()].first - *before);
                }
                for (std::size_t q = 1; q < order.size(); ++q) {
                    c += length_um(*chunks[k + order[q]].first - *chunks[k + order[q - 1]].last);
                }
                if (after) {
                    c += length_um(*after - *chunks[k + order.back()].last);
                }
                return c;
            };
            std::vector<std::size_t> original(n);
            std::iota(original.begin(), original.end(), std::size_t{0});

            // Plus proche voisin sous contrainte d'ordre.
            std::vector<std::size_t> order;
            order.reserve(n);
            std::vector<bool> placed(n, false);
            std::optional<Vec2um> at = before;
            for (std::size_t step = 0; step < n; ++step) {
                std::size_t best = n;
                double bestD = 0.0;
                for (std::size_t cand = 0; cand < n; ++cand) {
                    if (placed[cand]) {
                        continue;
                    }
                    bool ready = true;
                    for (const std::size_t p : preds[cand]) {
                        ready = ready && placed[p];
                    }
                    if (!ready) {
                        continue;
                    }
                    const double d = at ? cost(*at, *chunks[k + cand].first) : 0.0;
                    if (best == n || d < bestD - 1e-9) {
                        best = cand;
                        bestD = d;
                    }
                }
                placed[best] = true;
                order.push_back(best);
                at = chunks[k + best].last;
            }

            // Déplacements d'une région vers une autre position, tant qu'ils font baisser le coût
            // et respectent les contraintes.
            const auto valid = [&](const std::vector<std::size_t>& cand) {
                std::vector<std::size_t> pos(n);
                for (std::size_t q = 0; q < n; ++q) {
                    pos[cand[q]] = q;
                }
                for (std::size_t b = 0; b < n; ++b) {
                    for (const std::size_t a : preds[b]) {
                        if (pos[a] > pos[b]) {
                            return false;
                        }
                    }
                }
                return true;
            };
            double bestCost = total(order);
            for (std::size_t pass = 0; pass < kMaxPasses; ++pass) {
                bool improved = false;
                for (std::size_t from = 0; from < n; ++from) {
                    for (std::size_t to = 0; to < n; ++to) {
                        if (to == from) {
                            continue;
                        }
                        std::vector<std::size_t> cand = order;
                        const std::size_t v = cand[from];
                        cand.erase(cand.begin() + static_cast<std::ptrdiff_t>(from));
                        cand.insert(cand.begin() + static_cast<std::ptrdiff_t>(to), v);
                        const double c = total(cand);
                        if (c < bestCost - 1e-9 && valid(cand)) {
                            bestCost = c;
                            order = std::move(cand);
                            improved = true;
                        }
                    }
                }
                if (!improved) {
                    break;
                }
            }
            if (bestCost < total(original) - 1e-9) {
                ++result.groups;
                result.saved_um += length(original) - length(order);
                for (std::size_t q = 0; q < n; ++q) {
                    moved[k + order[q]] = moved[k + order[q]] || order[q] != q;
                    finalOrder.push_back(k + order[q]);
                }
            } else {
                for (std::size_t q = 0; q < n; ++q) {
                    finalOrder.push_back(k + q);
                }
            }
            k = e;
        }
    }
    if (result.groups == 0) {
        return result;
    }
    result.regions_moved = static_cast<std::size_t>(std::count(moved.begin(), moved.end(), true));

    // Reconstruction : régions dans le nouvel ordre. Un saut d'entrée est absolu ; une région qui
    // devient la première après une barrière sans saut d'entrée en reçoit un.
    std::vector<StitchCommand> out;
    out.reserve(cmds.size() + 8);
    for (const std::size_t idx : finalOrder) {
        const Chunk& c = chunks[idx];
        const bool startsWithJump = cmds[c.begin].type == CommandType::Jump;
        if (!startsWithJump && c.first && !c.pinned && !out.empty() &&
            out.back().type != CommandType::ColorChange && out.back().type != CommandType::Stop) {
            ObjectId owner;
            owner.value = c.source;
            out.push_back({*c.first, CommandType::Jump, owner, StitchPass::Travel});
        }
        out.insert(out.end(), cmds.begin() + static_cast<std::ptrdiff_t>(c.begin),
                   cmds.begin() + static_cast<std::ptrdiff_t>(c.end));
    }
    result.commands = std::move(out);
    return result;
}

} // namespace openstitch::stitch_generation
