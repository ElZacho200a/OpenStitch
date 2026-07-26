// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/skeleton_graph.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>

namespace openstitch::auto_satin {

namespace {

constexpr std::array<int, 8> DX{-1, 0, 1, -1, 1, -1, 0, 1};
constexpr std::array<int, 8> DY{-1, -1, -1, 0, 0, 1, 1, 1};

// Ordre CIRCULAIRE des 8 voisins (E, NE, N, NW, W, SW, S, SE), pour le nombre
// de croisement.
constexpr std::array<int, 8> RX{1, 1, 0, -1, -1, -1, 0, 1};
constexpr std::array<int, 8> RY{0, -1, -1, -1, 0, 1, 1, 1};

int neighbor_count(const RasterMask& s, int x, int y) {
    int c = 0;
    for (int k = 0; k < 8; ++k) {
        c += s.at(x + DX[static_cast<std::size_t>(k)], y + DY[static_cast<std::size_t>(k)]) ? 1 : 0;
    }
    return c;
}

// Nombre de croisement = nombre de composantes de voisins autour du pixel :
// = 1 extrémité, = 2 continuation, >= 3 jonction. Robuste aux « escaliers » des
// diagonales, contrairement au comptage brut de voisins.
int crossing_number(const RasterMask& s, int x, int y) {
    int sum = 0;
    for (int k = 0; k < 8; ++k) {
        const int a = s.at(x + RX[static_cast<std::size_t>(k)], y + RY[static_cast<std::size_t>(k)])
                          ? 1
                          : 0;
        const int b =
            s.at(x + RX[static_cast<std::size_t>((k + 1) % 8)],
                 y + RY[static_cast<std::size_t>((k + 1) % 8)])
                ? 1
                : 0;
        sum += std::abs(a - b);
    }
    return sum / 2;
}

double radius_at(const DistanceField& d, int x, int y) {
    return static_cast<double>(d.at(x, y));
}

}  // namespace

std::size_t SkeletonGraph::endpoint_count() const {
    return static_cast<std::size_t>(
        std::count_if(nodes.begin(), nodes.end(),
                      [](const SkeletonNode& n) { return n.type == SkeletonNodeType::Endpoint; }));
}

std::size_t SkeletonGraph::junction_count() const {
    return static_cast<std::size_t>(
        std::count_if(nodes.begin(), nodes.end(),
                      [](const SkeletonNode& n) { return n.type == SkeletonNodeType::Junction; }));
}

SkeletonGraph build_skeleton_graph(const RasterMask& s, const DistanceField& d) {
    SkeletonGraph g;
    const int w = s.width;
    const int h = s.height;
    if (w <= 0 || h <= 0) {
        return g;
    }

    // 1) Repère les pixels-nœuds (degré != 2), triés (row, col) pour l'ordre.
    // node_id_at[y*w+x] = id+1 si nœud, sinon 0.
    std::vector<std::uint32_t> node_at(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!s.at(x, y)) {
                continue;
            }
            const int nb = neighbor_count(s, x, y);
            const int cn = crossing_number(s, x, y);
            // Continuation (cn == 2, ou cas dégénéré cn == 0 avec voisins) :
            // pas un nœud. Seuls extrémités (cn == 1) et jonctions (cn >= 3)
            // et pixels isolés le sont.
            if (nb != 0 && cn != 1 && cn < 3) {
                continue;
            }
            SkeletonNode n;
            n.id = static_cast<std::uint32_t>(g.nodes.size());
            n.position = s.transform.to_um(x, y);
            n.type = nb == 0      ? SkeletonNodeType::Isolated
                     : cn == 1    ? SkeletonNodeType::Endpoint
                                  : SkeletonNodeType::Junction;
            n.local_radius_um = radius_at(d, x, y);
            node_at[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                    static_cast<std::size_t>(x)] = n.id + 1;
            g.nodes.push_back(n);
        }
    }

    // 2) Trace les arêtes : depuis chaque pixel-nœud, suivre chaque voisin
    // squelette le long des pixels de degré 2 jusqu'au prochain nœud.
    // `used` évite de retracer une arête deux fois (par ses deux extrémités).
    std::vector<std::uint8_t> used(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    const auto idx = [&](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
               static_cast<std::size_t>(x);
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::uint32_t nid = node_at[idx(x, y)];
            if (nid == 0) {
                continue;
            }
            const std::uint32_t fromNode = nid - 1;
            for (int k = 0; k < 8; ++k) {
                int cx = x + DX[static_cast<std::size_t>(k)];
                int cy = y + DY[static_cast<std::size_t>(k)];
                if (!s.at(cx, cy) || used[idx(cx, cy)]) {
                    continue;
                }
                // Ne pas partir directement vers un autre nœud déjà relié par ce
                // pixel : on marque le pixel de départ comme utilisé.
                std::vector<Vec2um> line;
                std::vector<double> radii;
                line.push_back(s.transform.to_um(x, y));
                radii.push_back(radius_at(d, x, y));
                int px = x, py = y;
                bool ok = true;
                while (node_at[idx(cx, cy)] == 0) {
                    used[idx(cx, cy)] = 1;
                    line.push_back(s.transform.to_um(cx, cy));
                    radii.push_back(radius_at(d, cx, cy));
                    // Prochain voisin de degré 2 différent de celui d'où l'on vient.
                    int nx = -1, ny = -1;
                    for (int m = 0; m < 8; ++m) {
                        const int tx = cx + DX[static_cast<std::size_t>(m)];
                        const int ty = cy + DY[static_cast<std::size_t>(m)];
                        if ((tx == px && ty == py) || !s.at(tx, ty)) {
                            continue;
                        }
                        if (node_at[idx(tx, ty)] != 0) {
                            nx = tx;
                            ny = ty;
                            break;  // atteint un nœud : on s'arrête
                        }
                        if (!used[idx(tx, ty)]) {
                            nx = tx;
                            ny = ty;
                            break;
                        }
                    }
                    if (nx < 0) {
                        ok = false;
                        break;
                    }
                    px = cx;
                    py = cy;
                    cx = nx;
                    cy = ny;
                }
                if (!ok || node_at[idx(cx, cy)] == 0) {
                    continue;
                }
                const std::uint32_t toNode = node_at[idx(cx, cy)] - 1;
                line.push_back(s.transform.to_um(cx, cy));
                radii.push_back(radius_at(d, cx, cy));

                SkeletonEdge e;
                e.id = static_cast<std::uint32_t>(g.edges.size());
                e.from = fromNode;
                e.to = toNode;
                e.centerline = std::move(line);
                e.local_radii_um = std::move(radii);
                for (std::size_t i = 1; i < e.centerline.size(); ++i) {
                    e.length_um += length_um(e.centerline[i] - e.centerline[i - 1]);
                }
                g.edges.push_back(std::move(e));
            }
        }
    }

    // 3) Ordre déterministe stable des arêtes.
    std::sort(g.edges.begin(), g.edges.end(), [](const SkeletonEdge& a, const SkeletonEdge& b) {
        if (a.from != b.from) return a.from < b.from;
        if (a.to != b.to) return a.to < b.to;
        return a.length_um < b.length_um;
    });
    return g;
}

}  // namespace openstitch::auto_satin
