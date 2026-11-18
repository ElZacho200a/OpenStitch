// SPDX-License-Identifier: Apache-2.0
#include "openstitch/segmentation/blend_zones.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <queue>
#include <tuple>
#include <vector>

#include "openstitch/core/parallel.hpp"

namespace openstitch::segmentation {
namespace {

using V3 = std::array<double, 3>;

// Répartit les lignes [0 ; rows) en bandes contiguës, traitées par `parallel_for`. Chaque bande
// n'écrit que ses lignes et ne lit que des données constantes : le résultat ne dépend ni du
// nombre de bandes ni du nombre de fils.
template <typename Fn> void parallel_bands(int rows, int minRows, Fn&& fn) {
    if (rows <= 0) {
        return;
    }
    const int bands = std::max(
        1, std::min(static_cast<int>(parallel_workers()) * 4, rows / std::max(1, minRows)));
    parallel_for(static_cast<std::size_t>(bands), [&](std::size_t b) {
        const auto lo = static_cast<long long>(rows) * static_cast<long long>(b) / bands;
        const auto hi = static_cast<long long>(rows) * static_cast<long long>(b + 1) / bands;
        fn(static_cast<int>(lo), static_cast<int>(hi));
    });
}

double srgb_to_linear(double c) {
    c /= 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

// sRVB (0..255) -> CIELAB (D65), comme skimage.color.rgb2lab.
V3 to_lab(const V3& rgb) {
    const double r = srgb_to_linear(rgb[0]);
    const double g = srgb_to_linear(rgb[1]);
    const double b = srgb_to_linear(rgb[2]);
    const double x = (0.412453 * r + 0.357580 * g + 0.180423 * b) / 0.95047;
    const double y = (0.212671 * r + 0.715160 * g + 0.072169 * b);
    const double z = (0.019334 * r + 0.119193 * g + 0.950227 * b) / 1.08883;
    const auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
    const double fx = f(x);
    const double fy = f(y);
    const double fz = f(z);
    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

int reflect(int i, int n) {
    if (n == 1) {
        return 0;
    }
    while (i < 0 || i >= n) {
        i = i < 0 ? -i - 1 : 2 * n - i - 1;
    }
    return i;
}

// Flou gaussien séparable, bord en miroir (comme scipy.ndimage, mode « reflect »).
void gaussian_blur(std::vector<V3>& img, int w, int h, double sigma) {
    if (sigma <= 0.0) {
        return;
    }
    const int radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
    std::vector<double> kernel(static_cast<std::size_t>(2 * radius + 1));
    double sum = 0.0;
    for (int k = -radius; k <= radius; ++k) {
        const double v = std::exp(-0.5 * k * k / (sigma * sigma));
        kernel[static_cast<std::size_t>(k + radius)] = v;
        sum += v;
    }
    for (auto& v : kernel) {
        v /= sum;
    }
    std::vector<V3> tmp(img.size());
    const std::vector<V3>& src = img;
    parallel_bands(h, 16, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                V3 acc{};
                for (int k = -radius; k <= radius; ++k) {
                    const auto& p = src[static_cast<std::size_t>(y * w + reflect(x + k, w))];
                    const double kv = kernel[static_cast<std::size_t>(k + radius)];
                    for (int c = 0; c < 3; ++c) {
                        acc[c] += kv * p[c];
                    }
                }
                tmp[static_cast<std::size_t>(y * w + x)] = acc;
            }
        }
    });
    const std::vector<V3>& horizontal = tmp;
    parallel_bands(h, 16, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                V3 acc{};
                for (int k = -radius; k <= radius; ++k) {
                    const auto& p = horizontal[static_cast<std::size_t>(reflect(y + k, h) * w + x)];
                    const double kv = kernel[static_cast<std::size_t>(k + radius)];
                    for (int c = 0; c < 3; ++c) {
                        acc[c] += kv * p[c];
                    }
                }
                img[static_cast<std::size_t>(y * w + x)] = acc;
            }
        }
    });
}

struct Edge {
    double weight{0.0};
    double count{0.0};
};

struct UnionFind {
    std::vector<std::uint32_t> parent;
    explicit UnionFind(std::size_t n) : parent(n) {
        for (std::size_t i = 0; i < n; ++i) {
            parent[i] = static_cast<std::uint32_t>(i);
        }
    }
    std::uint32_t find(std::uint32_t a) {
        while (parent[a] != a) {
            parent[a] = parent[parent[a]];
            a = parent[a];
        }
        return a;
    }
};

} // namespace

Result<Segmentation> segment_blend_zones(const image::Image& img, Millimeters mm_per_px,
                                         const BlendZoneOptions& options) {
    if (img.empty() || img.width <= 0 || img.height <= 0 || !(mm_per_px.value > 0.0)) {
        return fail(ErrorCategory::UserInput, "Aucune image à découper en zones de fondu");
    }
    const int W0 = img.width;
    const int H0 = img.height;
    // --- Image de travail : réduction par moyenne (boîte) jusqu'au pas demandé.
    const double factor = std::max(1.0, options.work_mm_per_px / mm_per_px.value);
    const int w = std::max(1, static_cast<int>(std::lround(W0 / factor)));
    const int h = std::max(1, static_cast<int>(std::lround(H0 / factor)));
    const double workMm = mm_per_px.value * static_cast<double>(W0) / w;
    std::vector<V3> work(static_cast<std::size_t>(w * h), V3{});
    std::vector<char> opaque(static_cast<std::size_t>(w * h), 0);
    {
        std::vector<double> n(static_cast<std::size_t>(w * h), 0.0);
        std::vector<double> nAll(static_cast<std::size_t>(w * h), 0.0);
        // Colonne et ligne de travail de chaque colonne / ligne source (mêmes formules que le
        // balayage d'origine). `wy` est croissant : la ligne de travail `r` reçoit exactement les
        // lignes sources [firstY[r] ; firstY[r + 1]), dans l'ordre -> mêmes sommes, bande par
        // bande.
        std::vector<int> wxOf(static_cast<std::size_t>(W0));
        for (int x = 0; x < W0; ++x) {
            wxOf[static_cast<std::size_t>(x)] =
                std::min(w - 1, static_cast<int>(x * static_cast<double>(w) / W0));
        }
        std::vector<int> wyOf(static_cast<std::size_t>(H0));
        std::vector<int> firstY(static_cast<std::size_t>(h) + 1, H0);
        for (int y = H0 - 1; y >= 0; --y) {
            const int wy = std::min(h - 1, static_cast<int>(y * static_cast<double>(h) / H0));
            wyOf[static_cast<std::size_t>(y)] = wy;
            firstY[static_cast<std::size_t>(wy)] = y;
        }
        for (int r = h - 1; r >= 0; --r) { // lignes de travail sans ligne source : bornes vides
            firstY[static_cast<std::size_t>(r)] = std::min(firstY[static_cast<std::size_t>(r)],
                                                           firstY[static_cast<std::size_t>(r) + 1]);
        }
        parallel_bands(h, 4, [&](int r0, int r1) {
            for (int y = firstY[static_cast<std::size_t>(r0)];
                 y < firstY[static_cast<std::size_t>(r1)]; ++y) {
                const int wy = wyOf[static_cast<std::size_t>(y)];
                const auto* row =
                    &img.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(W0) * 4];
                for (int x = 0; x < W0; ++x) {
                    const auto i =
                        static_cast<std::size_t>(wy * w + wxOf[static_cast<std::size_t>(x)]);
                    const auto* p = row + static_cast<std::size_t>(x) * 4;
                    nAll[i] += 1.0;
                    if (p[3] < 128) {
                        continue;
                    }
                    n[i] += 1.0;
                    for (int c = 0; c < 3; ++c) {
                        work[i][c] += p[c];
                    }
                }
            }
        });
        for (std::size_t i = 0; i < work.size(); ++i) {
            if (n[i] > 0.0 && 2.0 * n[i] >= nAll[i]) {
                opaque[i] = 1;
            }
            if (n[i] > 0.0) {
                for (int c = 0; c < 3; ++c) {
                    work[i][c] /= n[i];
                }
            }
        }
    }
    if (std::none_of(opaque.begin(), opaque.end(), [](char o) { return o != 0; })) {
        return fail(ErrorCategory::UserInput, "L'image est entièrement transparente");
    }
    gaussian_blur(work, w, h, options.smoothing_mm / workMm);
    std::vector<V3> lab(work.size());
    parallel_bands(h, 16, [&](int y0, int y1) {
        for (std::size_t i = static_cast<std::size_t>(y0) * static_cast<std::size_t>(w);
             i < static_cast<std::size_t>(y1) * static_cast<std::size_t>(w); ++i) {
            lab[i] = to_lab(work[i]);
        }
    });
    // --- Gradient : Sobel par canal Lab, normalisé comme skimage.filters.sobel
    // (noyau / 4, module / √2), ramené à un pas de 0,5 mm.
    const double gradScale = options.work_mm_per_px / workMm;
    std::vector<double> grad(work.size(), 0.0);
    parallel_bands(h, 16, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                double total = 0.0;
                for (int c = 0; c < 3; ++c) {
                    const auto at = [&](int dx, int dy) {
                        return lab[static_cast<std::size_t>(reflect(y + dy, h) * w +
                                                            reflect(x + dx, w))][c];
                    };
                    const double gx = (at(1, -1) + 2.0 * at(1, 0) + at(1, 1)) -
                                      (at(-1, -1) + 2.0 * at(-1, 0) + at(-1, 1));
                    const double gy = (at(-1, 1) + 2.0 * at(0, 1) + at(1, 1)) -
                                      (at(-1, -1) + 2.0 * at(0, -1) + at(1, -1));
                    total += (gx * gx + gy * gy) / 32.0;
                }
                grad[static_cast<std::size_t>(y * w + x)] = std::sqrt(total) * gradScale;
            }
        }
    });
    // --- Partage des eaux par inondation (Meyer) depuis une grille de germes.
    const int spacing =
        std::max(4, std::min(options.seed_spacing_px, static_cast<int>(std::sqrt(w * h / 200.0))));
    std::vector<std::uint32_t> label(work.size(), 0);
    using Item = std::tuple<double, std::uint64_t, std::uint32_t, std::uint32_t>; // prio, ordre
    std::priority_queue<Item, std::vector<Item>, std::greater<>> flood;
    std::uint64_t order = 0;
    std::uint32_t seeds = 0;
    for (int y = spacing / 2; y < h; y += spacing) {
        for (int x = spacing / 2; x < w; x += spacing) {
            const auto i = static_cast<std::size_t>(y * w + x);
            if (opaque[i] && label[i] == 0) {
                label[i] = ++seeds;
                flood.emplace(grad[i], order++, static_cast<std::uint32_t>(i), seeds);
            }
        }
    }
    // Composantes opaques sans germe (petites, isolées) : un germe à leur premier pixel.
    const auto floodAll = [&] {
        while (!flood.empty()) {
            const auto [prio, ord, idx, lab_] = flood.top();
            (void)prio;
            (void)ord;
            flood.pop();
            const int x = static_cast<int>(idx % static_cast<std::uint32_t>(w));
            const int y = static_cast<int>(idx / static_cast<std::uint32_t>(w));
            const std::array<std::pair<int, int>, 4> nb{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
            for (const auto& [dx, dy] : nb) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                    continue;
                }
                const auto j = static_cast<std::size_t>(ny * w + nx);
                if (!opaque[j] || label[j] != 0) {
                    continue;
                }
                label[j] = lab_;
                flood.emplace(grad[j], order++, static_cast<std::uint32_t>(j), lab_);
            }
        }
    };
    floodAll();
    for (std::size_t i = 0; i < label.size(); ++i) {
        if (opaque[i] && label[i] == 0) {
            label[i] = ++seeds;
            flood.emplace(grad[i], order++, static_cast<std::uint32_t>(i), seeds);
            floodAll();
        }
    }
    const std::size_t nodes = static_cast<std::size_t>(seeds) + 1;
    // --- Graphe d'adjacence : force de bord = gradient moyen le long du bord commun.
    std::vector<std::map<std::uint32_t, Edge>> adj(nodes);
    const auto addBoundary = [&](std::size_t p, std::size_t q) {
        const std::uint32_t a = label[p];
        const std::uint32_t b = label[q];
        if (a == 0 || b == 0 || a == b) {
            return;
        }
        const double v = 0.5 * (grad[p] + grad[q]);
        for (const auto& [u, t] : {std::pair{a, b}, std::pair{b, a}}) {
            auto& e = adj[u][t];
            e.weight = (e.weight * e.count + v) / (e.count + 1.0);
            e.count += 1.0;
        }
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto i = static_cast<std::size_t>(y * w + x);
            if (x + 1 < w) {
                addBoundary(i, i + 1);
            }
            if (y + 1 < h) {
                addBoundary(i, i + static_cast<std::size_t>(w));
            }
        }
    }
    UnionFind uf(nodes);
    const auto mergeInto = [&](std::uint32_t src, std::uint32_t dst) {
        // Bords de `src` reportés sur `dst` : moyenne pondérée par la longueur.
        for (const auto& [n, e] : adj[src]) {
            if (n == dst) {
                continue;
            }
            auto& d = adj[dst][n];
            const double c = d.count + e.count;
            d.weight = (d.weight * d.count + e.weight * e.count) / c;
            d.count = c;
            adj[n].erase(src);
            adj[n][dst] = d;
        }
        adj[dst].erase(src);
        adj[src].clear();
        uf.parent[src] = dst;
    };
    {
        // Fusion hiérarchique : toujours le bord le plus faible, tant qu'il reste sous le seuil.
        using Cand = std::tuple<double, std::uint32_t, std::uint32_t>;
        std::priority_queue<Cand, std::vector<Cand>, std::greater<>> heap;
        for (std::uint32_t a = 1; a < nodes; ++a) {
            for (const auto& [b, e] : adj[a]) {
                if (a < b) {
                    heap.emplace(e.weight, a, b);
                }
            }
        }
        while (!heap.empty()) {
            const auto [weight, a, b] = heap.top();
            heap.pop();
            if (weight >= options.edge_threshold) {
                break;
            }
            if (uf.parent[a] != a || uf.parent[b] != b) {
                continue;
            }
            const auto it = adj[a].find(b);
            if (it == adj[a].end() || it->second.weight != weight) {
                continue; // entrée périmée
            }
            mergeInto(b, a);
            for (const auto& [n, e] : adj[a]) {
                heap.emplace(e.weight, std::min(a, n), std::max(a, n));
            }
        }
    }
    // --- Zones voisines de même couleur moyenne (ΔE < kSameColour) : fusionnées. Le partage des
    // eaux laisse des bassins étroits LE LONG d'un bord net ; leur bord avec le reste de l'aplat
    // touche les pixels flous du bord net et sa force moyenne dépasse le seuil, alors que rien ne
    // les distingue de leur voisine.
    {
        constexpr double kSameColour = 4.0;
        std::vector<V3> labSum(nodes, V3{});
        std::vector<double> labCount(nodes, 0.0);
        for (std::size_t i = 0; i < label.size(); ++i) {
            if (label[i] == 0) {
                continue;
            }
            const auto r = uf.find(label[i]);
            for (int c = 0; c < 3; ++c) {
                labSum[r][c] += lab[i][c];
            }
            labCount[r] += 1.0;
        }
        const auto meanDelta = [&](std::uint32_t a, std::uint32_t b) {
            double d = 0.0;
            for (int c = 0; c < 3; ++c) {
                const double v = labSum[a][c] / labCount[a] - labSum[b][c] / labCount[b];
                d += v * v;
            }
            return std::sqrt(d);
        };
        for (;;) {
            double best = kSameColour;
            std::uint32_t ba = 0;
            std::uint32_t bb = 0;
            for (std::uint32_t a = 1; a < nodes; ++a) {
                if (uf.parent[a] != a || labCount[a] <= 0.0) {
                    continue;
                }
                for (const auto& [b, e] : adj[a]) {
                    (void)e;
                    if (a < b && labCount[b] > 0.0) {
                        const double d = meanDelta(a, b);
                        if (d < best) {
                            best = d;
                            ba = a;
                            bb = b;
                        }
                    }
                }
            }
            if (ba == 0) {
                break;
            }
            for (int c = 0; c < 3; ++c) {
                labSum[ba][c] += labSum[bb][c];
            }
            labCount[ba] += labCount[bb];
            labCount[bb] = 0.0;
            mergeInto(bb, ba);
        }
    }
    // --- Petites zones absorbées par la voisine au plus long bord commun (3 passes).
    const double minPx = options.min_zone_mm2 / (workMm * workMm);
    for (int pass = 0; pass < 3; ++pass) {
        std::vector<double> area(nodes, 0.0);
        for (const auto l : label) {
            if (l != 0) {
                area[uf.find(l)] += 1.0;
            }
        }
        bool changed = false;
        for (std::uint32_t a = 1; a < nodes; ++a) {
            if (uf.parent[a] != a || area[a] <= 0.0 || area[a] >= minPx || adj[a].empty()) {
                continue;
            }
            std::uint32_t best = 0;
            double bestCount = -1.0;
            for (const auto& [n, e] : adj[a]) {
                if (e.count > bestCount) {
                    bestCount = e.count;
                    best = n;
                }
            }
            area[best] += area[a];
            area[a] = 0.0;
            mergeInto(a, best);
            changed = true;
        }
        if (!changed) {
            break;
        }
    }
    // --- Étiquettes à la taille de l'image, numérotées dans l'ordre de balayage.
    Segmentation seg;
    seg.width = W0;
    seg.height = H0;
    seg.labels.assign(static_cast<std::size_t>(W0 * H0), 0);
    std::vector<std::uint32_t> slotOf(nodes, 0);
    std::vector<std::array<double, 3>> sums;
    std::vector<std::size_t> counts;
    {
        // Racine de chaque étiquette de travail (`uf.find` modifie l'union-find : en séquence).
        std::vector<std::uint32_t> rootOf(nodes, 0);
        for (std::uint32_t a = 1; a < nodes; ++a) {
            rootOf[a] = uf.find(a);
        }
        std::vector<int> wxOf(static_cast<std::size_t>(W0));
        for (int x = 0; x < W0; ++x) {
            wxOf[static_cast<std::size_t>(x)] =
                std::min(w - 1, static_cast<int>(x * static_cast<double>(w) / W0));
        }
        // Passe 1 (parallèle, par bandes de lignes sources) : racine de chaque pixel, sommes et
        // effectifs par racine, ordre de première apparition dans la bande. Les sommes sont des
        // entiers exacts en double : leur ordre d'addition ne change rien.
        struct BandStats {
            std::vector<std::array<double, 3>> sums;
            std::vector<std::size_t> counts;
            std::vector<std::uint32_t> order;
        };
        const int bands = std::max(1, std::min(static_cast<int>(parallel_workers()) * 2, H0 / 16));
        std::vector<BandStats> stats(static_cast<std::size_t>(bands));
        parallel_for(static_cast<std::size_t>(bands), [&](std::size_t b) {
            const int y0 =
                static_cast<int>(static_cast<long long>(H0) * static_cast<long long>(b) / bands);
            const int y1 = static_cast<int>(static_cast<long long>(H0) *
                                            static_cast<long long>(b + 1) / bands);
            BandStats& st = stats[b];
            st.sums.assign(nodes, std::array<double, 3>{});
            st.counts.assign(nodes, 0);
            for (int y = y0; y < y1; ++y) {
                const int wy = std::min(h - 1, static_cast<int>(y * static_cast<double>(h) / H0));
                const auto* row =
                    &img.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(W0) * 4];
                for (int x = 0; x < W0; ++x) {
                    const auto* p = row + static_cast<std::size_t>(x) * 4;
                    if (p[3] < 128) {
                        continue;
                    }
                    const std::uint32_t l =
                        label[static_cast<std::size_t>(wy * w + wxOf[static_cast<std::size_t>(x)])];
                    if (l == 0) {
                        continue;
                    }
                    const std::uint32_t root = rootOf[l];
                    if (st.counts[root] == 0) {
                        st.order.push_back(root);
                    }
                    seg.labels[static_cast<std::size_t>(y) * static_cast<std::size_t>(W0) +
                               static_cast<std::size_t>(x)] = root;
                    for (int c = 0; c < 3; ++c) {
                        st.sums[root][static_cast<std::size_t>(c)] += p[c];
                    }
                    ++st.counts[root];
                }
            }
        });
        // Fusion (séquentielle) : les emplacements suivent l'ordre de première apparition dans
        // le balayage de l'image, soit les bandes dans l'ordre puis l'ordre de chaque bande.
        for (const BandStats& st : stats) {
            for (const std::uint32_t root : st.order) {
                if (slotOf[root] == 0) {
                    sums.push_back({});
                    counts.push_back(0);
                    slotOf[root] = static_cast<std::uint32_t>(sums.size());
                }
                const std::uint32_t s = slotOf[root];
                for (int c = 0; c < 3; ++c) {
                    sums[s - 1][static_cast<std::size_t>(c)] +=
                        st.sums[root][static_cast<std::size_t>(c)];
                }
                counts[s - 1] += st.counts[root];
            }
        }
        // Passe 2 (parallèle) : racines -> emplacements.
        parallel_bands(H0, 16, [&](int y0, int y1) {
            for (std::size_t i = static_cast<std::size_t>(y0) * static_cast<std::size_t>(W0);
                 i < static_cast<std::size_t>(y1) * static_cast<std::size_t>(W0); ++i) {
                if (seg.labels[i] != 0) {
                    seg.labels[i] = slotOf[seg.labels[i]];
                }
            }
        });
    }
    for (std::size_t s = 0; s < sums.size(); ++s) {
        Region r;
        r.id = RegionId{s + 1};
        for (std::size_t c = 0; c < 3; ++c) {
            r.rgb[c] = static_cast<std::uint8_t>(
                std::clamp(std::lround(sums[s][c] / static_cast<double>(counts[s])), 0L, 255L));
        }
        r.pixel_count = counts[s];
        seg.region_slots.emplace_back(r);
    }
    if (seg.region_slots.empty()) {
        return fail(ErrorCategory::UserInput, "Aucune zone opaque dans l'image");
    }
    return seg;
}

} // namespace openstitch::segmentation
