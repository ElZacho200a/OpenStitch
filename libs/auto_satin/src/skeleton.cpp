// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/skeleton.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace openstitch::auto_satin {

namespace {

// Voisins dans l'ordre P2..P9 de Zhang-Suen (haut, haut-droite, droite, …).
constexpr std::array<int, 8> DX{0, 1, 1, 1, 0, -1, -1, -1};
constexpr std::array<int, 8> DY{-1, -1, 0, 1, 1, 1, 0, -1};

// Prédicat de Zhang-Suen pour un pixel allumé dont le voisinage est codé sur
// 8 bits (bit k = voisin P(k+2), dans l'ordre DX/DY ci-dessus).
constexpr bool deletable_code(unsigned code, int step) {
    std::array<int, 8> p{};
    int bsum = 0;
    for (int k = 0; k < 8; ++k) {
        p[static_cast<std::size_t>(k)] = static_cast<int>((code >> k) & 1u);
        bsum += p[static_cast<std::size_t>(k)];
    }
    if (bsum < 2 || bsum > 6) {
        return false;
    }
    int a = 0; // transitions 0->1 dans la séquence p2..p9,p2
    for (int k = 0; k < 8; ++k) {
        if (p[static_cast<std::size_t>(k)] == 0 && p[static_cast<std::size_t>((k + 1) % 8)] == 1) {
            ++a;
        }
    }
    if (a != 1) {
        return false;
    }
    // p[0]=P2(N), p[2]=P4(E), p[4]=P6(S), p[6]=P8(W).
    if (step == 0) {
        return p[0] * p[2] * p[4] == 0 && p[2] * p[4] * p[6] == 0;
    }
    return p[0] * p[2] * p[6] == 0 && p[0] * p[4] * p[6] == 0;
}

constexpr std::array<std::array<bool, 256>, 2> make_tables() {
    std::array<std::array<bool, 256>, 2> t{};
    for (int step = 0; step < 2; ++step) {
        for (unsigned code = 0; code < 256; ++code) {
            t[static_cast<std::size_t>(step)][code] = deletable_code(code, step);
        }
    }
    return t;
}

constexpr auto kDeletable = make_tables();

} // namespace

// Performance (audit 2026-09, docs/performance-audit.md) : la version
// d'origine réévaluait TOUS les pixels de la grille à chaque passe, soit
// O(largeur x hauteur x épaisseur) -- 94,6 s sur les 100,7 s d'analyse de
// l'auto-numérisation de la fixture tentabrode. Désormais :
//  - un pixel dont les 8 voisins sont allumés (bsum = 8, a fortiori > 6)
//    n'est jamais effaçable : seuls les pixels allumés ayant au moins un
//    voisin éteint (le « bord ») sont évalués, et le bord est tenu à jour à
//    partir des seuls pixels effacés ;
//  - la grille est entourée d'une bordure éteinte d'un pixel (même
//    convention que l'ancien accès borné : hors grille = éteint) et le
//    prédicat est précalculé pour les 256 voisinages possibles.
// Chaque passe évalue toujours l'état de la grille AVANT effacement
// (effacement différé), donc l'ensemble effacé -- et le squelette -- est
// identique bit à bit à la version d'origine, indépendamment de l'ordre de
// parcours du bord (vérifié contre l'implémentation de référence naïve dans
// tests/unit/auto_satin/test_skeleton_equivalence.cpp).
RasterMask thin_zhang_suen(const RasterMask& mask) {
    RasterMask out = mask;
    if (mask.width <= 0 || mask.height <= 0) {
        return out;
    }
    const int w = out.width;
    const int h = out.height;
    const int pw = w + 2; // grille avec bordure
    const auto pidx = [pw](int x, int y) {
        return static_cast<std::size_t>(y + 1) * static_cast<std::size_t>(pw) +
               static_cast<std::size_t>(x + 1);
    };
    std::vector<std::uint8_t> g(static_cast<std::size_t>(pw) * static_cast<std::size_t>(h + 2), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            g[pidx(x, y)] = out.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                       static_cast<std::size_t>(x)] != 0
                                ? 1
                                : 0;
        }
    }
    std::array<std::ptrdiff_t, 8> offset{};
    for (int k = 0; k < 8; ++k) {
        offset[static_cast<std::size_t>(k)] =
            static_cast<std::ptrdiff_t>(DY[static_cast<std::size_t>(k)]) * pw +
            DX[static_cast<std::size_t>(k)];
    }
    const auto code_of = [&](std::size_t i) {
        unsigned code = 0;
        for (int k = 0; k < 8; ++k) {
            code |= static_cast<unsigned>(
                        g[static_cast<std::size_t>(static_cast<std::ptrdiff_t>(i) +
                                                   offset[static_cast<std::size_t>(k)])])
                    << k;
        }
        return code;
    };

    // Bord initial : pixels allumés ayant au moins un voisin éteint.
    std::vector<std::uint8_t> inBorder(g.size(), 0);
    std::vector<std::size_t> border;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = pidx(x, y);
            if (g[i] && code_of(i) != 0xFFu) {
                inBorder[i] = 1;
                border.push_back(i);
            }
        }
    }

    std::vector<std::size_t> toClear;
    const auto pass = [&](int step) {
        const auto& table = kDeletable[static_cast<std::size_t>(step)];
        toClear.clear();
        for (const std::size_t i : border) {
            if (table[code_of(i)]) {
                toClear.push_back(i);
            }
        }
        if (toClear.empty()) {
            return false;
        }
        for (const std::size_t i : toClear) {
            g[i] = 0;
            inBorder[i] = 0;
        }
        // Mise à jour du bord : les pixels effacés en sortent, leurs voisins
        // encore allumés y entrent (ils ont désormais un voisin éteint). La
        // bordure est toujours éteinte, donc jamais ajoutée.
        std::size_t kept = 0;
        for (const std::size_t i : border) {
            if (g[i]) {
                border[kept++] = i;
            }
        }
        border.resize(kept);
        for (const std::size_t i : toClear) {
            for (int k = 0; k < 8; ++k) {
                const auto j = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(i) +
                                                        offset[static_cast<std::size_t>(k)]);
                if (g[j] && !inBorder[j]) {
                    inBorder[j] = 1;
                    border.push_back(j);
                }
            }
        }
        return true;
    };

    bool changed = true;
    int guard = 0;
    const int maxIter = mask.width + mask.height + 10;
    while (changed && guard++ < maxIter) {
        const bool c0 = pass(0);
        const bool c1 = pass(1);
        changed = c0 || c1;
    }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!g[pidx(x, y)]) {
                out.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                           static_cast<std::size_t>(x)] = 0;
            }
        }
    }
    return out;
}

} // namespace openstitch::auto_satin
