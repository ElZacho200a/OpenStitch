// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/skeleton_graph.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <numeric>

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

// Un pixel-nœud candidat, avant consolidation des amas de jonction (§ audit
// jonctions branchées/concaves) : Zhang-Suen laisse souvent, sur une
// confluence à 3+ branches (surtout asymétrique), un petit amas de plusieurs
// pixels adjacents ayant CHACUN un nombre de croisement >= 3, plutôt qu'un
// unique pixel de jonction net. Traiter chacun comme un `SkeletonNode`
// séparé (comportement d'origine) crée plusieurs nœuds de jonction quasi
// confondus, reliés par des micro-arêtes internes à l'amas — chaque branche
// incidente se retrouve alors ancrée sur un nœud légèrement différent, d'où
// des rails terminaux qui ne coïncident pas exactement au raccord.
struct NodeCandidate {
    int x{0};
    int y{0};
    SkeletonNodeType type{SkeletonNodeType::Endpoint};
    double radius{0.0};
};

// Union-Find minimal (compression de chemin, union par index) pour regrouper
// les pixels de jonction 8-connexes appartenant au même amas.
struct DisjointSet {
    std::vector<int> parent;
    explicit DisjointSet(std::size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0); }
    int find(int a) {
        while (parent[static_cast<std::size_t>(a)] != a) {
            parent[static_cast<std::size_t>(a)] = parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(a)])];
            a = parent[static_cast<std::size_t>(a)];
        }
        return a;
    }
    void unite(int a, int b) {
        a = find(a);
        b = find(b);
        if (a != b) {
            parent[static_cast<std::size_t>(std::max(a, b))] = std::min(a, b);
        }
    }
};

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

    // 1a) Repère les pixels-nœuds candidats (degré != 2) sans encore créer de
    // `SkeletonNode` : un pixel de jonction fait d'abord l'objet d'une
    // consolidation d'amas (1b) avant qu'un nœud logique ne lui soit assigné.
    const auto pixelIdx = [&](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    std::vector<int> candidate_at(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), -1);
    std::vector<NodeCandidate> candidates;
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
            NodeCandidate c;
            c.x = x;
            c.y = y;
            c.type = nb == 0 ? SkeletonNodeType::Isolated
                     : cn == 1 ? SkeletonNodeType::Endpoint
                               : SkeletonNodeType::Junction;
            c.radius = radius_at(d, x, y);
            candidate_at[pixelIdx(x, y)] = static_cast<int>(candidates.size());
            candidates.push_back(c);
        }
    }

    // 1b) Regroupe les pixels de jonction 8-connexes (amas Zhang-Suen d'une
    // même confluence) en un seul amas logique. Seuls des candidats de type
    // Junction se rejoignent : un pixel d'extrémité ou isolé reste toujours
    // son propre nœud (ces types ne s'agglutinent pas en pratique).
    DisjointSet dsu(candidates.size());
    for (std::size_t ci = 0; ci < candidates.size(); ++ci) {
        if (candidates[ci].type != SkeletonNodeType::Junction) {
            continue;
        }
        const NodeCandidate& c = candidates[ci];
        for (int k = 0; k < 8; ++k) {
            const int nx = c.x + DX[static_cast<std::size_t>(k)];
            const int ny = c.y + DY[static_cast<std::size_t>(k)];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                continue;
            }
            const int ni = candidate_at[pixelIdx(nx, ny)];
            if (ni >= 0 && candidates[static_cast<std::size_t>(ni)].type == SkeletonNodeType::Junction) {
                dsu.unite(static_cast<int>(ci), ni);
            }
        }
    }

    // 1c) Un nœud logique par amas (racine DSU). Position stable = le pixel
    // de plus grand rayon dans le `DistanceField` au sein de l'amas (le
    // « cœur » géométrique de la confluence), départagé par (y, x) croissants
    // pour le déterminisme ; c'est toujours un pixel réel de l'amas, jamais
    // un barycentre flottant qui pourrait retomber hors squelette.
    struct Cluster {
        int repX{0};
        int repY{0};
        double repRadius{-1.0};
        SkeletonNodeType type{SkeletonNodeType::Endpoint};
        std::vector<std::size_t> members;
    };
    std::map<int, Cluster> clustersByRoot;
    for (std::size_t ci = 0; ci < candidates.size(); ++ci) {
        const NodeCandidate& c = candidates[ci];
        const int root = c.type == SkeletonNodeType::Junction ? dsu.find(static_cast<int>(ci))
                                                               : static_cast<int>(ci);
        Cluster& cluster = clustersByRoot[root];
        cluster.type = c.type;
        cluster.members.push_back(ci);
        if (c.radius > cluster.repRadius ||
            (c.radius == cluster.repRadius &&
             (c.y < cluster.repY || (c.y == cluster.repY && c.x < cluster.repX)))) {
            cluster.repRadius = c.radius;
            cluster.repX = c.x;
            cluster.repY = c.y;
        }
    }

    // 1d) Ordre déterministe des nœuds finaux (position du représentant,
    // (y, x) croissants — cohérent avec l'ordre de balayage d'origine, dont
    // un amas à un seul pixel est un cas particulier). `node_at` fait
    // pointer TOUS les pixels d'un amas vers le même identifiant de nœud : la
    // trace d'arête (2) atteint donc le même nœud logique quel que soit le
    // pixel de l'amas par lequel elle y entre.
    std::vector<Cluster*> ordered;
    ordered.reserve(clustersByRoot.size());
    for (auto& [root, cluster] : clustersByRoot) {
        ordered.push_back(&cluster);
    }
    std::sort(ordered.begin(), ordered.end(), [](const Cluster* a, const Cluster* b) {
        if (a->repY != b->repY) return a->repY < b->repY;
        return a->repX < b->repX;
    });
    std::vector<std::uint32_t> node_at(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    for (const Cluster* cluster : ordered) {
        SkeletonNode n;
        n.id = static_cast<std::uint32_t>(g.nodes.size());
        n.position = s.transform.to_um(cluster->repX, cluster->repY);
        n.type = cluster->type;
        n.local_radius_um = radius_at(d, cluster->repX, cluster->repY);
        for (const std::size_t ci : cluster->members) {
            node_at[pixelIdx(candidates[ci].x, candidates[ci].y)] = n.id + 1;
        }
        g.nodes.push_back(n);
    }

    // 2) Trace les arêtes : depuis chaque pixel-nœud, suivre chaque voisin
    // squelette le long des pixels de degré 2 jusqu'au prochain nœud.
    // `used` évite de retracer une arête deux fois (par ses deux extrémités).
    std::vector<std::uint8_t> used(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    const auto& idx = pixelIdx;

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
                    // Un nœud voisin est TOUJOURS prioritaire sur un pixel de
                    // degré 2, quel que soit l'ordre de balayage des 8 directions :
                    // un pixel juste avant une jonction peut avoir, en plus de la
                    // jonction elle-même, un pixel de degré 2 d'une AUTRE branche
                    // dans son 8-voisinage (branches proches à la jonction). Ne
                    // s'arrêter sur le premier candidat rencontré (ancien
                    // comportement) pouvait donc sauter la jonction et fusionner
                    // deux branches en une seule arête si ce pixel d'une autre
                    // branche apparaissait plus tôt dans l'ordre fixe des
                    // directions — défaut trouvé par revue (jonction de "croix"
                    // ramenée à un degré 2 au lieu de 4).
                    //
                    // Le pixel d'ORIGINE de la trace (x, y, pas seulement le
                    // précédent px, py) est exclu de tout candidat : près d'une
                    // jonction, l'amincissement (Zhang-Suen) laisse souvent un
                    // petit amas de plusieurs pixels allumés autour du pixel-nœud
                    // réel (un « hub » de 2-3 px de large), dont certains
                    // touchent directement le pixel d'origine par un chemin de 2
                    // pas différent de celui emprunté au départ. Sans cette
                    // exclusion, la trace pouvait boucler sur son propre nœud de
                    // départ (arête parasite `from == to`, quelques centaines de
                    // µm) en consommant au passage le seul pixel d'accès vers une
                    // branche réelle plus loin, qui disparaissait alors du graphe
                    // sans aucune arête ni diagnostic — défaut trouvé par revue
                    // (branche sud entière perdue sur un réseau "y").
                    int nx = -1, ny = -1;
                    for (int m = 0; m < 8; ++m) {
                        const int tx = cx + DX[static_cast<std::size_t>(m)];
                        const int ty = cy + DY[static_cast<std::size_t>(m)];
                        if ((tx == px && ty == py) || (tx == x && ty == y) || !s.at(tx, ty)) {
                            continue;
                        }
                        if (node_at[idx(tx, ty)] != 0) {
                            nx = tx;
                            ny = ty;
                            break;  // atteint un nœud : priorité absolue.
                        }
                    }
                    if (nx < 0) {
                        for (int m = 0; m < 8; ++m) {
                            const int tx = cx + DX[static_cast<std::size_t>(m)];
                            const int ty = cy + DY[static_cast<std::size_t>(m)];
                            if ((tx == px && ty == py) || (tx == x && ty == y) || !s.at(tx, ty)) {
                                continue;
                            }
                            if (!used[idx(tx, ty)]) {
                                nx = tx;
                                ny = ty;
                                break;
                            }
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
                // Micro-arête interne à un amas de jonction consolidé (1b/1c) :
                // la trace est partie d'un pixel membre du hub `fromNode` et a
                // atteint un AUTRE pixel membre du MÊME hub logique (pas
                // nécessairement le pixel d'origine littéral, déjà exclu plus
                // haut) — ce n'est pas une branche réelle mais une simple
                // reconnexion interne à l'amas. Rejetée sans condition : ne
                // jamais produire d'arête `from == to`.
                if (toNode == fromNode) {
                    continue;
                }
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
