// SPDX-License-Identifier: Apache-2.0
#include "openstitch/thread_palette/color_reduction.hpp"

#include <algorithm>
#include <limits>
#include <map>

#include "openstitch/thread_palette/color_distance.hpp"

namespace openstitch::thread_palette {

namespace {

struct Cluster {
    std::array<std::uint8_t, 3> rgb{};
    CieLab lab{};
    double weight{0.0};
    double top_weight{0.0}; // poids du membre le plus lourd (donne la teinte)
    std::size_t first{0};   // plus petit indice d'entrée
    std::vector<std::size_t> members;
};

} // namespace

ColorReduction reduce_colors(std::span<const WeightedColor> colors, std::size_t max_colors,
                             double max_distance) {
    std::vector<Cluster> clusters;
    clusters.reserve(colors.size());
    for (std::size_t i = 0; i < colors.size(); ++i) {
        Cluster c;
        c.rgb = colors[i].rgb;
        c.lab = to_cielab(colors[i].rgb);
        c.weight = colors[i].weight;
        c.top_weight = colors[i].weight;
        c.first = i;
        c.members = {i};
        clusters.push_back(std::move(c));
    }

    // Les grappes gardent leur emplacement d'origine ; l'ordre des emplacements vivants est
    // exactement l'ordre de l'ancien vecteur (l'effacement conservait l'ordre relatif), donc le
    // départage « première paire (i, j) rencontrée » est inchangé (HP-PERF-028).
    std::vector<char> alive(clusters.size(), 1);
    std::size_t aliveCount = clusters.size();

    const auto merge = [&clusters, &alive, &aliveCount](std::size_t keep, std::size_t drop) {
        Cluster& a = clusters[keep];
        Cluster& b = clusters[drop];
        bool labChanged = false;
        if (b.top_weight > a.top_weight) {
            a.rgb = b.rgb;
            a.lab = b.lab;
            a.top_weight = b.top_weight;
            labChanged = true;
        }
        a.weight += b.weight;
        a.first = std::min(a.first, b.first);
        a.members.insert(a.members.end(), b.members.begin(), b.members.end());
        b.members.clear();
        alive[drop] = 0;
        --aliveCount;
        return labChanged;
    };

    // Doublons exacts d'abord (toujours, même sans limite) : table ordonnée sur la couleur,
    // fusion dans l'ordre croissant des indices dans la première occurrence.
    {
        std::map<std::array<std::uint8_t, 3>, std::size_t> firstOf;
        for (std::size_t j = 0; j < clusters.size(); ++j) {
            const auto [it, inserted] = firstOf.emplace(clusters[j].rgb, j);
            if (!inserted) {
                merge(it->second, j);
            }
        }
    }

    // Voisin le plus proche par ligne : plus petit j > i de distance minimale (strict).
    constexpr double kInf = std::numeric_limits<double>::infinity();
    const std::size_t n = clusters.size();
    constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();
    std::vector<double> nnDist(n, kInf);
    std::vector<std::size_t> nnIdx(n, kNone);
    const auto recomputeRow = [&](std::size_t i) {
        nnDist[i] = kInf;
        nnIdx[i] = kNone;
        for (std::size_t j = i + 1; j < n; ++j) {
            if (alive[j] == 0) {
                continue;
            }
            const double d = ciede2000(clusters[i].lab, clusters[j].lab);
            if (d < nnDist[i]) {
                nnDist[i] = d;
                nnIdx[i] = j;
            }
        }
    };
    if (aliveCount > 1 && (max_colors > 0 || max_distance > 0.0)) {
        for (std::size_t i = 0; i < n; ++i) {
            if (alive[i] != 0) {
                recomputeRow(i);
            }
        }
    }

    while (aliveCount > 1) {
        const bool tooMany = max_colors > 0 && aliveCount > max_colors;
        if (!tooMany && max_distance <= 0.0) {
            break;
        }
        double best = kInf;
        std::size_t bi = kNone;
        for (std::size_t i = 0; i < n; ++i) {
            if (alive[i] != 0 && nnIdx[i] != kNone && nnDist[i] < best) {
                best = nnDist[i];
                bi = i;
            }
        }
        if (bi == kNone) {
            break; // distances toutes non finies : ancien comportement indéfini, on s'arrête
        }
        const std::size_t bj = nnIdx[bi];
        if (!tooMany && best > max_distance) {
            break; // plus de paire assez proche
        }
        const bool labChanged = merge(bi, bj);
        for (std::size_t i = 0; i < bj; ++i) {
            if (alive[i] == 0) {
                continue;
            }
            if (i == bi || nnIdx[i] == bi || nnIdx[i] == bj) {
                recomputeRow(i);
            } else if (labChanged && i < bi) {
                const double d = ciede2000(clusters[i].lab, clusters[bi].lab);
                if (d < nnDist[i] || (d == nnDist[i] && bi < nnIdx[i])) {
                    nnDist[i] = d;
                    nnIdx[i] = bi;
                }
            }
        }
    }

    // Compactage des grappes vivantes (ordre conservé).
    {
        std::vector<Cluster> kept;
        kept.reserve(aliveCount);
        for (std::size_t i = 0; i < n; ++i) {
            if (alive[i] != 0) {
                kept.push_back(std::move(clusters[i]));
            }
        }
        clusters = std::move(kept);
    }
    // Ordre de sortie : première apparition dans l'entrée.
    std::vector<std::size_t> order(clusters.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&clusters](std::size_t a, std::size_t b) {
        return clusters[a].first < clusters[b].first;
    });

    ColorReduction out;
    out.mapping.assign(colors.size(), 0);
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        const Cluster& c = clusters[order[rank]];
        out.palette.push_back(c.rgb);
        for (const std::size_t m : c.members) {
            out.mapping[m] = rank;
        }
    }
    return out;
}

} // namespace openstitch::thread_palette
