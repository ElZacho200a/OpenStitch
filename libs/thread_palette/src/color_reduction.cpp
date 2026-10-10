// SPDX-License-Identifier: Apache-2.0
#include "openstitch/thread_palette/color_reduction.hpp"

#include <algorithm>
#include <limits>

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

ColorReduction reduce_colors(std::span<const WeightedColor> colors, std::size_t max_colors) {
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

    const auto merge = [&clusters](std::size_t keep, std::size_t drop) {
        Cluster& a = clusters[keep];
        const Cluster b = clusters[drop];
        if (b.top_weight > a.top_weight) {
            a.rgb = b.rgb;
            a.lab = b.lab;
            a.top_weight = b.top_weight;
        }
        a.weight += b.weight;
        a.first = std::min(a.first, b.first);
        a.members.insert(a.members.end(), b.members.begin(), b.members.end());
        clusters.erase(clusters.begin() + static_cast<std::ptrdiff_t>(drop));
    };

    // Doublons exacts d'abord (toujours, même sans limite).
    for (std::size_t i = 0; i < clusters.size(); ++i) {
        for (std::size_t j = i + 1; j < clusters.size();) {
            if (clusters[j].rgb == clusters[i].rgb) {
                merge(i, j);
            } else {
                ++j;
            }
        }
    }

    while (max_colors > 0 && clusters.size() > max_colors && clusters.size() > 1) {
        double best = std::numeric_limits<double>::infinity();
        std::size_t bi = 0;
        std::size_t bj = 1;
        for (std::size_t i = 0; i < clusters.size(); ++i) {
            for (std::size_t j = i + 1; j < clusters.size(); ++j) {
                const double d = ciede2000(clusters[i].lab, clusters[j].lab);
                if (d < best) { // strict : égalité -> première paire (i, j) rencontrée
                    best = d;
                    bi = i;
                    bj = j;
                }
            }
        }
        merge(bi, bj);
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
