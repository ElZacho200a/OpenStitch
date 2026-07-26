// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/skeleton.hpp"

#include <array>

namespace openstitch::auto_satin {

namespace {

// Voisins dans l'ordre P2..P9 de Zhang-Suen (haut, haut-droite, droite, …).
constexpr std::array<int, 8> DX{0, 1, 1, 1, 0, -1, -1, -1};
constexpr std::array<int, 8> DY{-1, -1, 0, 1, 1, 1, 0, -1};

int get(const std::vector<std::uint8_t>& g, int w, int h, int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h) {
        return 0;
    }
    return g[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
             static_cast<std::size_t>(x)];
}

// Une passe Zhang-Suen (step 0 ou 1). Renvoie true si au moins un pixel changé.
bool pass(std::vector<std::uint8_t>& g, int w, int h, int step) {
    std::vector<std::pair<int, int>> to_clear;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!get(g, w, h, x, y)) {
                continue;
            }
            std::array<int, 8> p{};
            for (int k = 0; k < 8; ++k) {
                p[static_cast<std::size_t>(k)] = get(g, w, h, x + DX[static_cast<std::size_t>(k)],
                                                     y + DY[static_cast<std::size_t>(k)]);
            }
            int bsum = 0;
            for (int v : p) {
                bsum += v;
            }
            if (bsum < 2 || bsum > 6) {
                continue;
            }
            int a = 0;  // transitions 0->1 dans la séquence p2..p9,p2
            for (int k = 0; k < 8; ++k) {
                if (p[static_cast<std::size_t>(k)] == 0 &&
                    p[static_cast<std::size_t>((k + 1) % 8)] == 1) {
                    ++a;
                }
            }
            if (a != 1) {
                continue;
            }
            // p[0]=P2(N), p[2]=P4(E), p[4]=P6(S), p[6]=P8(W).
            if (step == 0) {
                if (p[0] * p[2] * p[4] != 0) continue;
                if (p[2] * p[4] * p[6] != 0) continue;
            } else {
                if (p[0] * p[2] * p[6] != 0) continue;
                if (p[0] * p[4] * p[6] != 0) continue;
            }
            to_clear.emplace_back(x, y);
        }
    }
    for (const auto& [x, y] : to_clear) {
        g[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
          static_cast<std::size_t>(x)] = 0;
    }
    return !to_clear.empty();
}

}  // namespace

RasterMask thin_zhang_suen(const RasterMask& mask) {
    RasterMask out = mask;
    if (mask.width <= 0 || mask.height <= 0) {
        return out;
    }
    bool changed = true;
    int guard = 0;
    const int maxIter = mask.width + mask.height + 10;
    while (changed && guard++ < maxIter) {
        const bool c0 = pass(out.pixels, out.width, out.height, 0);
        const bool c1 = pass(out.pixels, out.width, out.height, 1);
        changed = c0 || c1;
    }
    return out;
}

}  // namespace openstitch::auto_satin
