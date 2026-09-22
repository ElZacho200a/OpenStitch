// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/region_split.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <tuple>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/cut.hpp"

namespace openstitch::satin_planning {

namespace {

struct Vec2d {
    double x{0.0};
    double y{0.0};
};

Vec2d to_vec2d(Vec2um v) {
    return {static_cast<double>(v.x.value), static_cast<double>(v.y.value)};
}
Vec2d sub(Vec2d a, Vec2d b) {
    return {a.x - b.x, a.y - b.y};
}
Vec2d add(Vec2d a, Vec2d b) {
    return {a.x + b.x, a.y + b.y};
}
Vec2d scale(Vec2d a, double s) {
    return {a.x * s, a.y * s};
}
double norm(Vec2d a) {
    return std::sqrt(a.x * a.x + a.y * a.y);
}

Vec2um to_vec2um(Vec2d v) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(v.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(v.y))}};
}

double cross(Vec2d a, Vec2d b) { return a.x * b.y - a.y * b.x; }

// Distance depuis `origin` jusqu'au bord EXTERIEUR de `piece` en suivant le
// rayon `origin + t*dir` (t>0), par intersection avec chaque arete du
// contour exterieur -- delibrement PAS les trous : un trou est un vide LOCAL
// au sein de la meme masse de matiere, le franchir n'atteint jamais une
// branche sans rapport (contrairement a une sortie vers le vrai exterieur,
// qui peut re-rentrer dans une autre branche plus loin). Retient la premiere
// arete EXTERIEURE croisee -- defaut reel trouve (2026-08-22) sur une lettre
// avec contre-forme (boucle interieure) : sans ce rayon, une approximation
// via le rayon local du squelette sous-estimait la portee des qu'un trou
// etait plus proche que le vrai bord exterieur dans la direction de coupe.
double ray_boundary_distance(const geometry::PathSet& piece, Vec2d origin, Vec2d dir) {
    double best = std::numeric_limits<double>::max();
    const auto& nodes = piece.outer.nodes;
    const std::size_t n = nodes.size();
    if (n < 2) return best;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Vec2d p0 = to_vec2d(nodes[j].pos);
        const Vec2d p1 = to_vec2d(nodes[i].pos);
        const Vec2d seg = sub(p1, p0);
        const double denom = cross(seg, dir);
        if (std::abs(denom) < 1e-9) continue;  // rayon parallele a cette arete
        const Vec2d diff = sub(p0, origin);
        const double s = cross(dir, diff) / denom;
        if (s < -1e-6 || s > 1.0 + 1e-6) continue;  // hors segment
        const double t = cross(seg, diff) / denom;
        if (t > 1e-6 && t < best) best = t;
    }
    return best;
}

double point_segment_distance(Vec2d p, Vec2d a, Vec2d b) {
    const Vec2d ab = sub(b, a);
    const double lenSq = ab.x * ab.x + ab.y * ab.y;
    if (lenSq < 1e-9) return norm(sub(p, a));
    double t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq;
    t = std::clamp(t, 0.0, 1.0);
    return norm(sub(p, add(a, scale(ab, t))));
}

// Distance depuis `point` jusqu'au squelette de toute branche qui n'est PAS
// incidente a `junctionNode` -- une VRAIE voisine, distincte de la confluence
// courante (ex. sur "comb", les 5 AUTRES dents, chacune a sa propre
// jonction). Sert a plafonner `ray_boundary_distance` (cf. `try_distance`) :
// celui-ci, en s'arretant a la premiere arete EXTERIEURE croisee, sous-estime
// la portee necessaire pres d'une confluence large ou le contour n'est pas
// localement convexe (defaut reel trouve sur "t" (2026-08-22) : le bras
// large de la barre, au-dela d'un renflement de la jonction, n'est jamais
// atteint par le rayon, laissant un sommet reflex parasite sur le morceau
// "reste" qui declenche a tort une redecomposition par concavite, cf.
// `concavity_cuts.hpp`). Exclut deliberement les aretes incidentes a LA MEME
// jonction (le "reste"/la continuation du chemin traversant) : celles-ci ne
// sont jamais une branche a eviter, seulement la matiere naturelle de la
// confluence elle-meme -- les atteindre pour nettoyer le renflement est
// souhaite, pas un risque de coupe croisee.
double distance_to_other_branches(const SkeletonGraph& graph, std::uint32_t junctionNode, std::uint32_t currentEdgeId,
                                   Vec2d point) {
    double best = std::numeric_limits<double>::max();
    for (const auto& edge : graph.edges) {
        if (edge.id == currentEdgeId) continue;
        if (edge.from == junctionNode || edge.to == junctionNode) continue;
        for (std::size_t i = 0; i + 1 < edge.centerline.size(); ++i) {
            const double d = point_segment_distance(point, to_vec2d(edge.centerline[i]), to_vec2d(edge.centerline[i + 1]));
            if (d < best) best = d;
        }
    }
    return best;
}

// Meme calcul que `geometry::cut_path_set` (diagonale de la boite englobante
// + marge) : portee "generreuse par defaut" utilisee quand aucune branche
// voisine ne plafonne `distance_to_other_branches` (ex. "t", une seule
// jonction -- rien a proteger).
double bounding_box_diagonal_reach(const geometry::PathSet& piece) {
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    const auto scan = [&](const geometry::Path& p) {
        for (const auto& node : p.nodes) {
            minX = std::min(minX, static_cast<double>(node.pos.x.value));
            maxX = std::max(maxX, static_cast<double>(node.pos.x.value));
            minY = std::min(minY, static_cast<double>(node.pos.y.value));
            maxY = std::max(maxY, static_cast<double>(node.pos.y.value));
        }
    };
    scan(piece.outer);
    for (const auto& hole : piece.holes) scan(hole);
    if (minX > maxX) return 1000.0;
    return std::hypot(maxX - minX, maxY - minY) + 1000.0;
}

// Point + tangente locale (unitaire) sur `edge`, a la distance `distanceUm`
// depuis l'extremite touchant la jonction (`atStart` : jonction = noeud
// `from`, parcours dans l'ordre ; sinon parcours inverse). Renvoie
// `valid=false` si la branche est plus courte que `distanceUm`.
struct PointAndTangent {
    Vec2d point{};
    Vec2d tangent{};
    bool valid{false};
};

PointAndTangent point_at_distance(const SkeletonEdge& edge, bool atStart, double distanceUm) {
    PointAndTangent out;
    const std::size_t n = edge.centerline.size();
    if (n < 2)
        return out;
    auto pointAt = [&](std::size_t i) {
        return atStart ? edge.centerline[i] : edge.centerline[n - 1 - i];
    };

    double accumulated = 0.0;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const Vec2d p0 = to_vec2d(pointAt(i));
        const Vec2d p1 = to_vec2d(pointAt(i + 1));
        const Vec2d seg = sub(p1, p0);
        const double segLen = norm(seg);
        if (segLen < 1e-9)
            continue;
        if (accumulated + segLen >= distanceUm) {
            const double t = (distanceUm - accumulated) / segLen;
            out.point = add(p0, scale(seg, t));
            out.tangent = scale(seg, 1.0 / segLen);
            out.valid = true;
            return out;
        }
        accumulated += segLen;
    }
    return out; // branche plus courte que distanceUm
}

// Test point-dans-polygone par ray casting (nombre de croisements), sur les
// sommets `pos` du chemin -- suffisant ici : les chemins manipules par cette
// phase (fixtures, sorties de `geometry::cut_path_set`) sont tous
// polygonaux (nœuds Coin), jamais des courbes de controle.
bool point_in_polygon(const geometry::Path& path, Vec2um p) {
    const auto& nodes = path.nodes;
    const std::size_t n = nodes.size();
    if (n < 3)
        return false;
    const double px = static_cast<double>(p.x.value);
    const double py = static_cast<double>(p.y.value);
    bool inside = false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = static_cast<double>(nodes[i].pos.x.value);
        const double yi = static_cast<double>(nodes[i].pos.y.value);
        const double xj = static_cast<double>(nodes[j].pos.x.value);
        const double yj = static_cast<double>(nodes[j].pos.y.value);
        const bool crosses = (yi > py) != (yj > py);
        if (crosses && px < (xj - xi) * (py - yi) / (yj - yi) + xi) {
            inside = !inside;
        }
    }
    return inside;
}

bool path_set_contains(const geometry::PathSet& set, Vec2um p) {
    if (!point_in_polygon(set.outer, p))
        return false;
    for (const auto& hole : set.holes) {
        if (point_in_polygon(hole, p))
            return false;
    }
    return true;
}

// Projection de `point` sur la centerline de `edge` (meme convention
// `atStart` que `point_at_distance`) : distance d'arc depuis l'extremite
// jonction jusqu'au point de la centerline le plus proche, et la distance
// perpendiculaire a ce point le plus proche -- utilise par la famille §14
// (JunctionSeparatorInfo) pour convertir un point de CONTOUR (jamais
// exactement sur la centerline) en une distance de coupe candidate.
struct EdgeProjection {
    double distance_along_edge_um{0.0};
    double perpendicular_distance_um{0.0};
    bool valid{false};
};

EdgeProjection project_onto_edge(const SkeletonEdge& edge, bool atStart, Vec2d point) {
    EdgeProjection out;
    const std::size_t n = edge.centerline.size();
    if (n < 2)
        return out;
    auto pointAt = [&](std::size_t i) {
        return atStart ? edge.centerline[i] : edge.centerline[n - 1 - i];
    };

    double accumulated = 0.0;
    double bestPerp = std::numeric_limits<double>::max();
    double bestDistance = 0.0;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const Vec2d p0 = to_vec2d(pointAt(i));
        const Vec2d p1 = to_vec2d(pointAt(i + 1));
        const Vec2d seg = sub(p1, p0);
        const double segLen = norm(seg);
        if (segLen < 1e-9)
            continue;
        const Vec2d toPoint = sub(point, p0);
        double t = (toPoint.x * seg.x + toPoint.y * seg.y) / (segLen * segLen);
        t = std::clamp(t, 0.0, 1.0);
        const Vec2d closest = add(p0, scale(seg, t));
        const double perp = norm(sub(point, closest));
        if (perp < bestPerp) {
            bestPerp = perp;
            bestDistance = accumulated + t * segLen;
        }
        accumulated += segLen;
    }
    if (bestPerp == std::numeric_limits<double>::max())
        return out;
    out.distance_along_edge_um = bestDistance;
    out.perpendicular_distance_um = bestPerp;
    out.valid = true;
    return out;
}

// Point sonde interieur au trace d'un SatinPath, robuste aux coupes qui ont
// pu servir a l'isoler (celles-ci n'affectent que ses deux extremites) : le
// noeud median s'il y en a un de strictement interne, sinon le milieu de
// ses deux extremites.
Vec2um probe_point(const SkeletonGraph& graph, const SatinPath& path) {
    // Priorite absolue : un point pris DIRECTEMENT sur la centerline de
    // l'arc median du chemin -- toujours interieur a la forme par
    // construction (c'est l'axe median lui-meme), quelle que soit la
    // courbure du trace. Defaut trouve (2026-08-13, `notch`) : le repli
    // "milieu de start/end" ci-dessous est une corde DROITE entre les deux
    // extremites -- pour un chemin qui courbe significativement (ex. autour
    // d'une entaille concave profonde), cette corde peut sortir de la forme,
    // faisant echouer l'assignation d'un chemin pourtant parfaitement
    // resolu (aucune coupe necessaire, un seul arc).
    if (!path.edges.empty()) {
        const std::size_t midEdgeIdx = path.edges.size() / 2;
        if (const SkeletonEdge* edge = find_edge(graph, path.edges[midEdgeIdx]);
            edge != nullptr && !edge->centerline.empty()) {
            return edge->centerline[edge->centerline.size() / 2];
        }
    }
    // Repli (ne devrait arriver que si le graphe est incoherent) : noeud
    // median du chemin, sinon le milieu de ses extremites.
    if (path.nodes.size() >= 3) {
        const std::uint32_t midNodeId = path.nodes[path.nodes.size() / 2];
        if (const SkeletonNode* n = find_node(graph, midNodeId))
            return n->position;
    }
    return to_vec2um(scale(add(to_vec2d(path.start), to_vec2d(path.end)), 0.5));
}

} // namespace

std::vector<CutCandidate> generate_cut_candidates(const geometry::PathSet& piece,
                                                  const SkeletonGraph& graph,
                                                  std::uint32_t junctionNode, std::uint32_t edgeId,
                                                  const CutCandidateParams& params) {
    std::vector<CutCandidate> candidates;
    const SkeletonEdge* edge = find_edge(graph, edgeId);
    if (edge == nullptr)
        return candidates;
    const bool atStart = edge->from == junctionNode;

    const std::uint32_t farNodeId = atStart ? edge->to : edge->from;
    const SkeletonNode* farNode = find_node(graph, farNodeId);
    const bool verifySatinability = params.verify_isolated_endpoint_branches &&
                                    farNode != nullptr &&
                                    farNode->type == SkeletonNodeType::Endpoint;

    // Construit et evalue un candidat a la distance `d` (memes regles de
    // rejet, quelle que soit la famille qui a propose `d`). Renvoie `false`
    // seulement quand la branche est plus courte que `d` -- le seul cas ou
    // continuer le balayage regulier n'a plus de sens (monotone en distance) ;
    // toute autre issue (candidat valide OU rejete pour une autre raison)
    // renvoie `true`.
    const auto try_distance = [&](double d, bool fromSeparator) {
        const PointAndTangent sample = point_at_distance(*edge, atStart, d);
        CutCandidate cand;
        cand.distance_from_junction_um = d;
        cand.from_junction_separator = fromSeparator;
        if (!sample.valid) {
            cand.rejection_reason = "branche plus courte que la distance testee";
            candidates.push_back(cand);
            return false;
        }
        cand.point = to_vec2um(sample.point);
        // Normale locale : rotation 90 deg de la tangente.
        const Vec2d normal{-sample.tangent.y, sample.tangent.x};
        cand.a = to_vec2um(sub(sample.point, scale(normal, 1000.0)));
        cand.b = to_vec2um(add(sample.point, scale(normal, 1000.0)));

        // Portee = genereuse par defaut (meme calcul que l'ancien
        // `geometry::cut_path_set` non borne -- diagonale de la boite
        // englobante), plafonnee UNIQUEMENT par la distance a une VRAIE
        // branche voisine (`distance_to_other_branches`, jamais celles
        // incidentes a CETTE jonction). Defaut reel trouve sur "comb"
        // (2026-08-22) : une coupe NON plafonnee tranche aussi les branches
        // VOISINES des qu'elles occupent la meme plage perpendiculaire (6
        // dents a la meme hauteur). Mais plafonner via un simple rayon local
        // (essaye d'abord) sous-estime pres d'une confluence large ou le
        // contour n'est pas localement convexe -- defaut reel trouve sur "t"
        // (2026-08-22) : le bras de la barre, au-dela du renflement de la
        // jonction, restait hors de portee, laissant un sommet reflex
        // parasite sur le morceau "reste" qui declenchait a tort une
        // redecomposition par concavite (cf. `concavity_cuts.hpp`). Le
        // plafond par squelette resout les deux : sur "t", aucune arete
        // n'est incidente a une AUTRE jonction (une seule jonction dans toute
        // la forme), donc rien ne plafonne -- la portee genereuse s'applique
        // sans risque, exactement comme avant ce correctif. Sur "comb",
        // chaque dent voisine a sa PROPRE jonction, donc la distance
        // plafonne correctement avant d'y atteindre.
        const double siblingDistance = distance_to_other_branches(graph, junctionNode, edgeId, sample.point);
        const double generousReach = bounding_box_diagonal_reach(piece);
        // Portee minimale REELLEMENT necessaire dans cette direction precise
        // (rayon vers le bord exterieur, jamais les trous -- cf.
        // `ray_boundary_distance`) : sans ce plancher, plafonner par la seule
        // distance au squelette voisin sous-estime des qu'un trou est plus
        // proche que le bord exterieur (defaut reel trouve sur une lettre
        // avec contre-forme, 2026-08-22 : plusieurs jonctions redevenaient
        // impossibles a isoler des que `siblingDistance`, plus petit que le
        // rayon exterieur necessaire, plafonnait trop court). `std::max` avec
        // `siblingDistance` laisse malgre tout un peu de marge pres d'une
        // confluence large ("t") quand le rayon exterieur seul serait trop
        // court -- les deux corrections cohabitent sans se supplanter.
        const double outerReach = std::max(ray_boundary_distance(piece, sample.point, normal),
                                            ray_boundary_distance(piece, sample.point, {-normal.x, -normal.y}));
        const double primaryReach = siblingDistance < std::numeric_limits<double>::max()
                                  ? std::min(generousReach, std::max(outerReach, siblingDistance) + params.local_cut_margin_um)
                                  : generousReach;

        // Tente une coupe a une portee DONNEE et remplit `cand` en
        // consequence (`valid`/aires si acceptee, sinon `rejection_reason`).
        // Retourne `true` seulement si la coupe est acceptee -- permet a
        // l'appelant de retenter une AUTRE portee sans dupliquer toute la
        // logique de rejet.
        const auto attempt_reach = [&](double reach) {
            cand.reach_um = reach;
            const auto cutResult = geometry::cut_path_set_bounded(piece, cand.a, cand.b, reach, params.cut_width);
            if (!cutResult.has_value()) {
                cand.rejection_reason = "echec de la decoupe geometrique";
                return false;
            }
            if (cutResult->size() != 2) {
                cand.rejection_reason = "coupe n'a pas produit exactement 2 morceaux (" +
                                         std::to_string(cutResult->size()) + " -- traverse une zone sans rapport)";
                return false;
            }
            const double areaA = geometry::path_set_area_um2((*cutResult)[0]) / 1e6;
            const double areaB = geometry::path_set_area_um2((*cutResult)[1]) / 1e6;
            if (areaA < params.min_piece_area_mm2 || areaB < params.min_piece_area_mm2) {
                cand.rejection_reason = "fragment trop petit";
                return false;
            }

            // Determine lequel des deux morceaux est la branche isolee
            // (contient le noeud distal) pour renseigner les aires ET, le
            // cas echeant, pour la verification de satinabilite ci-dessous.
            std::size_t branchIdx = 0;
            if (farNode != nullptr && path_set_contains((*cutResult)[1], farNode->position)) branchIdx = 1;
            const std::size_t remainderIdx = 1 - branchIdx;

            if (verifySatinability) {
                const auto analysis = auto_satin::analyze_region((*cutResult)[branchIdx], {});
                const bool clean = analysis.has_value() && analysis->report.junction_count == 0;
                if (!clean) {
                    cand.rejection_reason = "morceau isole encore branche apres cette coupe (jonction residuelle, "
                                             "probablement une branche voisine partiellement tranchee)";
                    return false;
                }
            }

            cand.valid = true;
            cand.branch_piece_area_mm2 = geometry::path_set_area_um2((*cutResult)[branchIdx]) / 1e6;
            cand.remainder_piece_area_mm2 = geometry::path_set_area_um2((*cutResult)[remainderIdx]) / 1e6;
            return true;
        };

        if (!attempt_reach(primaryReach)) {
            // Repli : la portee "genereuse" (necessaire pres d'une confluence
            // large comme "t", cf. commentaire ci-dessus) peut a l'inverse
            // trancher une branche SANS RAPPORT qui partage la meme jonction
            // sans en etre pour autant un simple renflement -- defaut reel
            // trouve sur "E" (2026-08-28) : la branche detachee (barre du
            // milieu) partage sa jonction avec DEUX branches qui s'etendent,
            // elles, dans la MEME direction que la portee (le montant vertical
            // du E, de part et d'autre) -- la portee genereuse les tranche
            // aussi (plus de 2 morceaux), la ou une portee bornee au bord
            // exterieur REEL (`outerReach`, sans marge speculative) aurait pu
            // suffire (le trou entre la barre et ses voisines est souvent
            // proche). Retente donc avec `outerReach` seul si strictement
            // plus petit que ce qui vient d'echouer -- jamais l'inverse
            // (naurait aucune chance de reussir la ou la version generreuse a
            // deja echoue), et jamais un troisieme essai (deux tentatives
            // suffisent a distinguer les deux limitations reelles connues).
            if (outerReach + params.local_cut_margin_um < primaryReach) {
                attempt_reach(outerReach + params.local_cut_margin_um);
            }
        }
        candidates.push_back(cand);
        return true;
    };

    // §14 : encoches reelles connues (JunctionSeparatorInfo, moteur Legacy),
    // testees EN PREMIER -- priorite dans le budget du beam search (§16) sur
    // le balayage regulier ci-dessous, qui reste un pur repli geometrique
    // quand aucun separateur n'est fourni ou n'est exploitable ici.
    std::vector<double> separatorDistances;
    for (const auto& sep : params.junction_separators) {
        if (sep.junction_id != junctionNode)
            continue;
        const EdgeProjection proj = project_onto_edge(*edge, atStart, to_vec2d(sep.point));
        if (!proj.valid ||
            proj.perpendicular_distance_um > params.junction_separator_max_perpendicular_um)
            continue;
        if (proj.distance_along_edge_um < params.search_min_um ||
            proj.distance_along_edge_um > params.search_max_um)
            continue;
        separatorDistances.push_back(proj.distance_along_edge_um);
    }
    std::sort(separatorDistances.begin(), separatorDistances.end());
    separatorDistances.erase(std::unique(separatorDistances.begin(), separatorDistances.end(),
                                         [](double a, double b) { return std::abs(a - b) < 1.0; }),
                             separatorDistances.end());
    for (double d : separatorDistances)
        try_distance(d, true);

    for (double d = params.search_min_um; d <= params.search_max_um; d += params.search_step_um) {
        if (!try_distance(d, false))
            break; // aucune distance plus grande ne sera valide non plus
    }
    return candidates;
}

RegionSplitReport split_region(const geometry::PathSet& region, const SkeletonGraph& graph,
                               const DecompositionReport& decomposition,
                               const CutCandidateParams& params) {
    RegionSplitReport report;

    struct Event {
        std::uint32_t junction;
        std::uint32_t edge;
    };
    std::vector<Event> events;
    for (const auto& jr : decomposition.junctions) {
        for (auto edgeId : jr.detached)
            events.push_back({jr.node, edgeId});
    }
    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return std::tie(a.junction, a.edge) < std::tie(b.junction, b.edge);
    });

    std::vector<geometry::PathSet> pieces{region};

    // Trace de l'arbre de decoupage pour `merge_candidates` (phase 7) :
    // pour chaque coupe reussie, le slot source (reutilise en place pour le
    // "reste") et le slot de la branche detachee (nouvellement ajoute), plus
    // la geometrie du morceau AVANT cette coupe (capturee ici, jamais
    // reconstruite apres coup).
    struct EventRecord {
        std::size_t sourceSlot;
        std::size_t branchSlot;
        geometry::PathSet preCutPiece;
    };
    std::vector<EventRecord> successfulEvents;

    for (const auto& ev : events) {
        CutAttempt attempt;
        attempt.junction = ev.junction;
        attempt.edge = ev.edge;

        const SkeletonNode* junctionNode = find_node(graph, ev.junction);
        const SkeletonEdge* edge = find_edge(graph, ev.edge);
        if (junctionNode == nullptr || edge == nullptr) {
            report.cuts.push_back(std::move(attempt));
            continue;
        }

        std::optional<std::size_t> pieceIdx;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            if (path_set_contains(pieces[i], junctionNode->position)) {
                pieceIdx = i;
                break;
            }
        }
        if (!pieceIdx) {
            report.cuts.push_back(std::move(attempt));
            continue; // jonction introuvable dans le pool courant : incoherence, on laisse tel quel
        }

        attempt.candidates =
            generate_cut_candidates(pieces[*pieceIdx], graph, ev.junction, ev.edge, params);

        std::optional<std::size_t> chosen;
        if (params.selector) {
            chosen = params.selector(pieces[*pieceIdx], attempt.candidates);
        } else {
            const auto validIt = std::find_if(attempt.candidates.begin(), attempt.candidates.end(),
                                              [](const CutCandidate& c) { return c.valid; });
            if (validIt != attempt.candidates.end())
                chosen =
                    static_cast<std::size_t>(std::distance(attempt.candidates.begin(), validIt));
        }
        if (!chosen || *chosen >= attempt.candidates.size()) {
            report.cuts.push_back(std::move(attempt));
            continue; // aucune coupe valide (ou aucune retenue par le selecteur) : la branche reste
                      // fusionnee
        }
        attempt.selected = *chosen;
        const CutCandidate& winner = attempt.candidates[*chosen];

        const auto cutResult =
            geometry::cut_path_set_bounded(pieces[*pieceIdx], winner.a, winner.b, winner.reach_um, params.cut_width);
        if (!cutResult.has_value() || cutResult->size() != 2) {
            report.cuts.push_back(std::move(attempt));
            continue; // garde-fou : ne devrait pas arriver, deja verifie par
                      // generate_cut_candidates
        }

        const std::uint32_t farNodeId = edge->from == ev.junction ? edge->to : edge->from;
        const SkeletonNode* farNode = find_node(graph, farNodeId);
        std::size_t branchIdx = 0;
        std::size_t restIdx = 1;
        if (farNode != nullptr && path_set_contains((*cutResult)[1], farNode->position)) {
            branchIdx = 1;
            restIdx = 0;
        }

        geometry::PathSet branchPiece = (*cutResult)[branchIdx];
        geometry::PathSet restPiece = (*cutResult)[restIdx];
        const geometry::PathSet preCutPiece = pieces[*pieceIdx];
        const std::size_t branchSlot = pieces.size();
        pieces[*pieceIdx] = std::move(restPiece);
        pieces.push_back(std::move(branchPiece));
        successfulEvents.push_back({*pieceIdx, branchSlot, preCutPiece});

        report.cuts.push_back(std::move(attempt));
    }

    std::vector<bool> pieceUsed(pieces.size(), false);
    std::vector<std::optional<std::size_t>> slotPathIndex(pieces.size());
    for (std::size_t pi = 0; pi < decomposition.paths.size(); ++pi) {
        const Vec2um probe = probe_point(graph, decomposition.paths[pi]);
        std::optional<std::size_t> found;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            if (!pieceUsed[i] && path_set_contains(pieces[i], probe)) {
                found = i;
                break;
            }
        }
        if (!found) {
            report.unresolved_paths.push_back(pi);
            continue;
        }
        pieceUsed[*found] = true;
        slotPathIndex[*found] = pi;
        SatinRegion region_out;
        region_out.path_index = pi;
        region_out.region = pieces[*found];
        region_out.area_mm2 = geometry::path_set_area_um2(region_out.region) / 1e6;
        report.regions.push_back(std::move(region_out));
    }

    // Un slot est une feuille finale ssi aucun evenement ULTERIEUR ne l'a
    // reutilise comme source (le "reste" continue de vivre au meme index a
    // chaque coupe successive sur la meme lignee -- ex. les deux branches
    // detachees d'une meme jonction degre 4, cf. `cross`).
    for (std::size_t i = 0; i < successfulEvents.size(); ++i) {
        const auto reusedLater = [&](std::size_t slot) {
            for (std::size_t j = i + 1; j < successfulEvents.size(); ++j) {
                if (successfulEvents[j].sourceSlot == slot)
                    return true;
            }
            return false;
        };
        if (reusedLater(successfulEvents[i].sourceSlot) ||
            reusedLater(successfulEvents[i].branchSlot))
            continue;

        const auto sourcePath = slotPathIndex[successfulEvents[i].sourceSlot];
        const auto branchPath = slotPathIndex[successfulEvents[i].branchSlot];
        if (!sourcePath || !branchPath)
            continue; // un des deux cotes non resolu (chemin non isole)

        MergeCandidate candidate;
        candidate.first_path_index = *sourcePath;
        candidate.second_path_index = *branchPath;
        candidate.merged_region = successfulEvents[i].preCutPiece;
        candidate.merged_area_mm2 = geometry::path_set_area_um2(candidate.merged_region) / 1e6;
        report.merge_candidates.push_back(std::move(candidate));
    }

    return report;
}

std::string format_region_split_report(const RegionSplitReport& report,
                                       const DecompositionReport& decomposition) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);

    out << "[SGSD phase 3 -- decoupage de polygone]\n\n";
    for (const auto& attempt : report.cuts) {
        out << "Jonction " << attempt.junction << " / arc " << attempt.edge << " :\n";
        for (std::size_t i = 0; i < attempt.candidates.size(); ++i) {
            const auto& c = attempt.candidates[i];
            const bool selected = attempt.selected.has_value() && *attempt.selected == i;
            out << "    d=" << c.distance_from_junction_um << "um" << (c.from_junction_separator ? " [SEP]" : "")
                << " reach=" << c.reach_um << "um : ";
            if (c.valid) {
                out << "valide (branche=" << c.branch_piece_area_mm2
                    << "mm2, reste=" << c.remainder_piece_area_mm2 << "mm2)";
            } else {
                out << "rejetee (" << c.rejection_reason << ")";
            }
            if (selected)
                out << "  [SELECTED]";
            out << "\n";
        }
        if (!attempt.selected.has_value())
            out << "    -> aucune coupe valide, branche non isolee\n";
        out << "\n";
    }

    out.precision(2);
    out << "Regions :\n";
    for (const auto& r : report.regions) {
        out << "    chemin " << r.path_index << " -> region de " << r.area_mm2 << "mm2\n";
    }
    if (!report.unresolved_paths.empty()) {
        out << "Chemins non isoles :";
        for (auto pi : report.unresolved_paths)
            out << " " << pi;
        out << "\n";
    }
    out << "\nTotal : " << report.regions.size() << " region(s) sur " << decomposition.paths.size()
        << " chemin(s)\n";

    return out.str();
}

} // namespace openstitch::satin_planning
