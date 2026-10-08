// SPDX-License-Identifier: Apache-2.0
#include "corridor.hpp"

#include <algorithm>
#include <cstddef>

namespace openstitch::auto_satin::detail {

namespace {

P2 unit(P2 a) {
    const double n = norm(a);
    return n > 1e-9 ? P2{a.x / n, a.y / n} : P2{0.0, 0.0};
}

// Tolérance de coïncidence (µm) utilisée pour compter les pieds DISTINCTS
// (§ foot_multiplicity) : un sommet ordinaire de polygone contribue 2 entrées
// au MÊME point (edge_index différent, cf. medial_field.hpp) -- une valeur
// très inférieure à toute largeur de satin plausible (min_satin_width est en
// centaines de µm) suffit à les fusionner sans jamais fusionner deux pieds
// réellement distincts d'une vraie jonction (cf. test_medial_field.cpp,
// "jonction reelle a 3 branches distinctes" : séparation mesurée très
// supérieure, de l'ordre du rayon de branche).
constexpr double kFootCoincidenceEpsilonUm = 1.0;

// Nombre de pieds DISTINCTS (en position) parmi `feet` -- jamais `feet.size()`
// brut, cf. l'avertissement de medial_field.hpp repris dans corridor.hpp.
int count_distinct_feet(const std::vector<BoundaryFoot>& feet) {
    std::vector<P2> distinct;
    distinct.reserve(feet.size());
    for (const auto& f : feet) {
        const bool already = std::any_of(distinct.begin(), distinct.end(), [&f](const P2& p) {
            return norm(p - f.point) < kFootCoincidenceEpsilonUm;
        });
        if (!already) {
            distinct.push_back(f.point);
        }
    }
    return static_cast<int>(distinct.size());
}

// Distance circulaire entre deux indices d'arête d'un même polygone à `ring`
// arêtes (0 si `ring == 0`, polygone dégénéré déjà filtré en amont).
std::size_t edge_ring_distance(std::size_t a, std::size_t b, std::size_t ring) {
    if (ring == 0) {
        return 0;
    }
    const std::size_t diff = a > b ? a - b : b - a;
    return std::min(diff, ring - diff);
}

// Même ordre total que le comparateur privé `foot_order` de medial_field.cpp
// (dupliqué ici délibérément -- même style déjà choisi par ce fichier pour
// ses petits helpers géométriques plutôt que de partager un comparateur via
// un en-tête supplémentaire pour une fonction aussi réduite). Sert
// UNIQUEMENT à fusionner les deux listes déjà triées d'un
// `OrientedBoundaryFeet` (`merge_oriented`, ci-dessous) en une seule liste
// GLOBALEMENT triée par distance croissante.
bool foot_less(const BoundaryFoot& a, const BoundaryFoot& b) {
    if (a.distance_um != b.distance_um) {
        return a.distance_um < b.distance_um;
    }
    if (a.poly_index != b.poly_index) {
        return a.poly_index < b.poly_index;
    }
    if (a.edge_index != b.edge_index) {
        return a.edge_index < b.edge_index;
    }
    return a.edge_t < b.edge_t;
}

// Fusionne `left`/`right` (chacune déjà triée par `foot_less`, sortie de
// `nearest_boundary_feet_oriented`) en une seule liste globalement triée par
// distance croissante, plus un vecteur parallèle `out_is_left` portant le
// côté d'origine de chaque entrée -- PAS une simple concaténation
// gauche-puis-droite : `pick_continuous` ci-dessous renvoie le PREMIER
// candidat trouvé qui satisfait son critère de continuité, en supposant que
// "premier dans la liste" == "le plus proche qui satisfait le critère". Une
// concaténation brute casserait cette hypothèse (un candidat du côté droit,
// plus proche mais placé APRÈS tout le côté gauche dans une concaténation,
// serait injustement court-circuité par un candidat gauche plus éloigné mais
// trouvé en premier) -- régression constatée empiriquement (Phase B.5,
// fixtures "rectangle"/"ribbon"/"notch"/"t" : la station de continuité
// choisissait l'arête topologiquement adjacente la plus proche DANS L'ORDRE
// DE CONCATÉNATION plutôt que la plus proche en distance réelle). Un merge
// deux-pointeurs classique (équivalent à `std::merge`, réécrit à la main
// pour produire DEUX vecteurs parallèles -- pied et côté d'origine -- ce que
// `std::merge` seul, une sortie un type, ne permet pas directement sans
// structure intermédiaire).
void merge_oriented(const std::vector<BoundaryFoot>& left, const std::vector<BoundaryFoot>& right,
                    std::vector<BoundaryFoot>& out_feet, std::vector<bool>& out_is_left) {
    out_feet.clear();
    out_is_left.clear();
    out_feet.reserve(left.size() + right.size());
    out_is_left.reserve(left.size() + right.size());
    std::size_t li = 0;
    std::size_t ri = 0;
    while (li < left.size() && ri < right.size()) {
        if (foot_less(left[li], right[ri])) {
            out_feet.push_back(left[li]);
            out_is_left.push_back(true);
            ++li;
        } else {
            out_feet.push_back(right[ri]);
            out_is_left.push_back(false);
            ++ri;
        }
    }
    while (li < left.size()) {
        out_feet.push_back(left[li]);
        out_is_left.push_back(true);
        ++li;
    }
    while (ri < right.size()) {
        out_feet.push_back(right[ri]);
        out_is_left.push_back(false);
        ++ri;
    }
}

// Sentinelle "aucun candidat" pour les fonctions de sélection ci-dessous :
// toujours `feet.size()` (un indice valide est dans [0, feet.size())).

// Cherche, parmi `feet` (triés par distance croissante par
// `nearest_boundary_feet`) le candidat NON ENCORE UTILISÉ continu avec
// `prev` : même `poly_index`, arête identique ou immédiatement adjacente.
// Le premier trouvé est le plus proche remplissant ce critère (tri déjà
// garanti par l'appelé). `polys` sert uniquement à retrouver la taille de
// l'anneau d'arêtes du polygone de `prev`.
std::size_t pick_continuous(const std::vector<BoundaryFoot>& feet, const std::vector<Poly>& polys,
                            const BoundaryFoot& prev, const std::vector<bool>& used) {
    if (prev.poly_index >= polys.size()) {
        return feet.size();
    }
    const std::size_t ring = polys[prev.poly_index].size();
    for (std::size_t i = 0; i < feet.size(); ++i) {
        if (used[i] || feet[i].poly_index != prev.poly_index) {
            continue;
        }
        if (edge_ring_distance(feet[i].edge_index, prev.edge_index, ring) <= 1) {
            return i;
        }
    }
    return feet.size();
}

// Repli "première station" / "continuité rompue" : plus proche candidat NON
// ENCORE UTILISÉ du côté demandé (`wantLeft` -- +N, même convention que
// `compute_column_stations` : "+N = gauche").
//
// § HP-STI-018 Phase B.5 : le côté n'est plus recalculé ici par un produit
// scalaire sur `feet[i].point` -- `is_left[i]` porte déjà la classification
// faite AU MOMENT DE LA COLLECTE par `nearest_boundary_feet_oriented`
// (medial_field.hpp), qui est la source de vérité (c'est elle qui garantit
// qu'un candidat du côté demandé est présent si un tel candidat existe à
// portée, cf. justification de `trace_corridor` ci-dessous). Recalculer le
// signe ici recréerait la même classification qu'à la collecte -- aucune
// raison de le refaire, et une divergence accidentelle entre les deux
// calculs (ex. normale légèrement différente) serait une source de bug
// silencieux à éviter.
std::size_t pick_by_side_flag(const std::vector<BoundaryFoot>& feet,
                              const std::vector<bool>& is_left, bool wantLeft,
                              const std::vector<bool>& used) {
    for (std::size_t i = 0; i < feet.size(); ++i) {
        if (used[i]) {
            continue;
        }
        if (is_left[i] == wantLeft) {
            return i;
        }
    }
    return feet.size();
}

// Dernier repli : premier candidat libre, quel que soit son côté -- n'est
// atteint que dans un cas déjà dégénéré (ex. tous les candidats restants du
// même côté), pour ne jamais laisser un pied vide plutôt que de produire une
// géométrie strictement meilleure.
std::size_t pick_any_unused(const std::vector<BoundaryFoot>& feet, const std::vector<bool>& used) {
    for (std::size_t i = 0; i < feet.size(); ++i) {
        if (!used[i]) {
            return i;
        }
    }
    return feet.size();
}

// Sélectionne foot_a (+N, gauche) et foot_b (-N, droite) parmi `feet`
// (concaténation gauche++droite d'un `OrientedBoundaryFeet`, `is_left` portant
// la classification d'origine de chaque entrée) pour la station courante.
// `prev` est nullptr pour la première station d'une branche (aucune
// continuité possible, repli direct gauche/droite). Renvoie false si moins de
// 2 pieds distincts étaient exploitables (feet trop court) -- l'appelant
// comble alors depuis la station précédente.
//
// § HP-STI-018 Phase B.5 : la recherche de continuité (`pick_continuous`)
// reste volontairement AVEUGLE au côté -- elle cherche parmi TOUS les
// candidats (gauche ET droite confondus), exactement comme avant cette phase.
// Ce n'est pas un oubli : un pied physiquement continu peut, à une station où
// la normale tourne vite (virage serré), basculer de classification gauche/
// droite d'une station à l'autre sans qu'il s'agisse d'un vrai changement de
// branche -- seul le repli gauche/droite (`pick_by_side_flag`), qui ne
// s'applique qu'en l'ABSENCE de continuité, doit se fier à la classification
// de collecte.
bool select_feet(const std::vector<BoundaryFoot>& feet, const std::vector<bool>& is_left,
                 const std::vector<Poly>& polys, const CorridorStation* prev, BoundaryFoot& out_a,
                 BoundaryFoot& out_b) {
    if (feet.size() < 2) {
        return false;
    }
    std::vector<bool> used(feet.size(), false);
    std::size_t idx_a = feet.size();
    std::size_t idx_b = feet.size();

    if (prev != nullptr) {
        idx_a = pick_continuous(feet, polys, prev->foot_a, used);
        if (idx_a != feet.size()) {
            used[idx_a] = true;
        }
        idx_b = pick_continuous(feet, polys, prev->foot_b, used);
        if (idx_b != feet.size()) {
            used[idx_b] = true;
        }
    }
    if (idx_a == feet.size()) {
        idx_a = pick_by_side_flag(feet, is_left, /*wantLeft=*/true, used);
        if (idx_a == feet.size()) {
            idx_a = pick_any_unused(feet, used);
        }
        if (idx_a != feet.size()) {
            used[idx_a] = true;
        }
    }
    if (idx_b == feet.size()) {
        idx_b = pick_by_side_flag(feet, is_left, /*wantLeft=*/false, used);
        if (idx_b == feet.size()) {
            idx_b = pick_any_unused(feet, used);
        }
        if (idx_b != feet.size()) {
            used[idx_b] = true;
        }
    }
    if (idx_a == feet.size() || idx_b == feet.size()) {
        return false;
    }
    out_a = feet[idx_a];
    out_b = feet[idx_b];
    return true;
}

} // namespace

std::vector<CorridorStation> trace_corridor(const std::vector<P2>& axis,
                                            const std::vector<Poly>& polys,
                                            const SatinColumnsParameters& params) {
    const double softMaxWidth =
        static_cast<double>(params.analysis.thresholds.max_satin_width.value);

    std::vector<CorridorStation> stations;
    stations.reserve(axis.size());
    const CorridorStation* prev = nullptr;

    for (std::size_t i = 0; i < axis.size(); ++i) {
        P2 tan;
        if (axis.size() < 2) {
            tan = P2{0.0, 0.0};
        } else if (i == 0) {
            tan = axis[1] - axis[0];
        } else if (i + 1 == axis.size()) {
            tan = axis[i] - axis[i - 1];
        } else {
            tan = axis[i + 1] - axis[i - 1];
        }
        tan = unit(tan);
        const P2 normal{-tan.y, tan.x}; // +90 degres, +N = gauche (meme convention que
                                        // compute_column_stations).

        // Deux requetes distinctes, pas une seule -- decouple deux besoins
        // differents que medial_field.hpp ne distingue pas dans une seule
        // fonction :
        //   - `oriented_feet` (`nearest_boundary_feet_oriented`, §Phase B.5)
        //     sert a la SELECTION de foot_a/foot_b : DEUX files de candidats
        //     INDEPENDANTES, une par cote de `normal`, chacune jusqu'a 4
        //     candidats a moins de 2x sa PROPRE distance minimale. Remplace
        //     l'ancienne requete "large" unique (FootQuery{6, 1.0} sur
        //     nearest_boundary_feet, aveugle a la direction) qui, a forte
        //     courbure ("s") ou a une transition de largeur serree
        //     ("multi_neck", les branches aigues de "y"/"trident"), pouvait
        //     laisser les quelques candidats globalement les plus proches
        //     tomber TOUS du meme cote physique, affamant l'autre cote meme
        //     quand un vrai point exploitable s'y trouvait, juste plus loin
        //     que le top-6 global (§ HP-STI-018 Phase B.5,
        //     specs/plans/hp-sti-018-turning-satin.md). La separation par
        //     cote est faite A LA COLLECTE (medial_field.hpp), pas apres
        //     coup : un cote ne peut donc plus etre exclu par la proximite
        //     de l'autre.
        //   - `strict_feet` (FootQuery par defaut : max_feet=3,
        //     tolerance_relative=2%) sert UNIQUEMENT a `foot_multiplicity` --
        //     c'est la, et seulement la, qu'un filtre serre a un sens
        //     (signaler qu'un 3e point de contour distinct est VRAIMENT a
        //     portee, pas juste "un candidat parmi 6"). Inchange par la
        //     Phase B.5 : ce besoin n'a jamais ete celui qui regressait.
        const auto oriented =
            nearest_boundary_feet_oriented(polys, axis[i], OrientedFootQuery{normal, 4, 1.0});
        std::vector<BoundaryFoot> combined;
        std::vector<bool> is_left;
        merge_oriented(oriented.left, oriented.right, combined, is_left);
        const auto strict_feet = nearest_boundary_feet(polys, axis[i], FootQuery{});

        CorridorStation st;
        st.axis_point = axis[i];
        st.tangent = tan;

        BoundaryFoot a;
        BoundaryFoot b;
        if (select_feet(combined, is_left, polys, prev, a, b)) {
            st.foot_a = a;
            st.foot_b = b;
            st.width_um = norm(a.point - b.point);
            st.wide = st.width_um > softMaxWidth;
            st.foot_multiplicity = count_distinct_feet(strict_feet);
            st.interpolated = false;
        } else if (prev != nullptr) {
            // Station degeneree (moins de 2 pieds exploitables) : comblee
            // depuis la precedente plutot que laissee vide -- cf. invariant
            // "trace_corridor(...).size() == axis.size()" documente dans
            // corridor.hpp.
            st.foot_a = prev->foot_a;
            st.foot_b = prev->foot_b;
            st.width_um = prev->width_um;
            st.wide = prev->wide;
            st.foot_multiplicity = prev->foot_multiplicity;
            st.interpolated = true;
        } else {
            // Toute premiere station deja degeneree (region vide/invalide) :
            // rien a combler depuis -- pieds par defaut (origine), largeur
            // nulle, signale interpolated pour que l'appelant sache ne pas
            // s'y fier.
            st.foot_a = BoundaryFoot{};
            st.foot_b = BoundaryFoot{};
            st.width_um = 0.0;
            st.foot_multiplicity = 0;
            st.interpolated = true;
        }

        stations.push_back(st);
        prev = &stations.back(); // stable : `stations` est pre-reserve a axis.size(),
                                 // jamais de reallocation pendant cette boucle.
    }
    return stations;
}

std::size_t find_stable_corridor_end_index(const std::vector<CorridorStation>& stations, bool atEnd,
                                           const SatinColumnsParameters& params) {
    const std::size_t n = stations.size();
    if (n == 0) {
        return 0;
    }
    // Negatif ou nul traite comme "aucune marge exigee" (seule la station
    // elle-meme compte) -- jamais une exception, cf. le meme traitement
    // defensif que le reste du fichier (ex. `tip_min_width`).
    const std::size_t margin =
        params.junction_stability_margin_stations > 0
            ? static_cast<std::size_t>(params.junction_stability_margin_stations)
            : 0;
    for (std::size_t step = 0; step < n; ++step) {
        const std::size_t idx = atEnd ? (n - 1 - step) : step;
        if (stations[idx].foot_multiplicity != 2) {
            continue;
        }
        bool stable = true;
        for (std::size_t m = 1; m <= margin; ++m) {
            if (atEnd) {
                if (idx < m || stations[idx - m].foot_multiplicity != 2) {
                    stable = false;
                    break;
                }
            } else {
                if (idx + m >= n || stations[idx + m].foot_multiplicity != 2) {
                    stable = false;
                    break;
                }
            }
        }
        if (stable) {
            return idx;
        }
    }
    // Degenere (branche entierement contaminee, ou trop courte pour meme
    // tester la marge) : ne rien retrancher plutot que de vider la branche --
    // meme garde-fou conservateur que l'ancien `trim_unstable_junction_tail`
    // (`st.size() >= 3`).
    return atEnd ? (n - 1) : 0;
}

CorridorEnd find_stable_corridor_end(const std::vector<CorridorStation>& stations, bool atEnd,
                                     const SatinColumnsParameters& params) {
    CorridorEnd result;
    result.at_end = atEnd;
    if (stations.empty()) {
        return result; // station par defaut, edge_id 0 -- cf. corridor.hpp.
    }
    result.station = stations[find_stable_corridor_end_index(stations, atEnd, params)];
    return result;
}

} // namespace openstitch::auto_satin::detail
