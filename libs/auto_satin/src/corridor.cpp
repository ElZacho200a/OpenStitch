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
// ENCORE UTILISÉ du côté demandé de la normale (`wantLeft` -- +N, même
// convention que `compute_column_stations` : "+N = gauche").
std::size_t pick_by_side(const std::vector<BoundaryFoot>& feet, P2 axis_pt, P2 normal,
                         bool wantLeft, const std::vector<bool>& used) {
    for (std::size_t i = 0; i < feet.size(); ++i) {
        if (used[i]) {
            continue;
        }
        const double side = dot(feet[i].point - axis_pt, normal);
        if (wantLeft ? side >= 0.0 : side < 0.0) {
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

// Sélectionne foot_a (+N, gauche) et foot_b (-N, droite) parmi `feet` pour la
// station courante. `prev` est nullptr pour la première station d'une
// branche (aucune continuité possible, repli direct gauche/droite). Renvoie
// false si moins de 2 pieds distincts étaient exploitables (feet trop
// court) -- l'appelant comble alors depuis la station précédente.
bool select_feet(const std::vector<BoundaryFoot>& feet, const std::vector<Poly>& polys, P2 axis_pt,
                 P2 normal, const CorridorStation* prev, BoundaryFoot& out_a, BoundaryFoot& out_b) {
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
        idx_a = pick_by_side(feet, axis_pt, normal, /*wantLeft=*/true, used);
        if (idx_a == feet.size()) {
            idx_a = pick_any_unused(feet, used);
        }
        if (idx_a != feet.size()) {
            used[idx_a] = true;
        }
    }
    if (idx_b == feet.size()) {
        idx_b = pick_by_side(feet, axis_pt, normal, /*wantLeft=*/false, used);
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
    // Phase B : aucun champ de `SatinColumnsParameters` n'est encore lu ici --
    // le paramètre existe pour la stabilité de signature avec la Phase C
    // (`find_stable_corridor_end` consommera des seuils de jonction dédiés,
    // cf. specs/plans/hp-sti-018-turning-satin.md §2.2).
    (void)params;

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
        // differents que medial_field.hpp ne distingue pas lui-meme :
        //   - `wide_feet` (tolerance tres large, 100% -- garde tout candidat
        //     jusqu'a 2x la distance minimale, max_feet=6) sert a la
        //     SELECTION de foot_a/foot_b. Un axe reel (centerline issue du
        //     squelette amincil, pas une ligne mediane mathematique exacte)
        //     n'est quasiment jamais parfaitement equidistant des deux cotes
        //     -- le filtre de tolerance de 2% de `nearest_boundary_feet`
        //     (concu pour detecter une proximite de jonction, cf.
        //     medial_field.hpp) exclurait alors le cote le plus eloigne,
        //     meme quand il reste le bon pied du bon cote a mesurer. Constate
        //     empiriquement (Phase B, corpus `shapes.cpp`) sur "ribbon"/"s"/
        //     "notch" : avec la seule requete stricte, plusieurs stations
        //     consecutives perdaient un cote et la colonne entiere etait
        //     refusee -- un vrai regression sur des formes a branche unique
        //     sans jonction, hors de portee de la Phase B.
        //   - `strict_feet` (FootQuery par defaut : max_feet=3,
        //     tolerance_relative=2%) sert UNIQUEMENT a `foot_multiplicity` --
        //     c'est la, et seulement la, qu'un filtre serre a un sens
        //     (signaler qu'un 3e point de contour distinct est VRAIMENT a
        //     portee, pas juste "un candidat parmi 6").
        const auto wide_feet = nearest_boundary_feet(polys, axis[i], FootQuery{6, 1.0});
        const auto strict_feet = nearest_boundary_feet(polys, axis[i], FootQuery{});

        CorridorStation st;
        st.axis_point = axis[i];
        st.tangent = tan;

        BoundaryFoot a;
        BoundaryFoot b;
        if (select_feet(wide_feet, polys, axis[i], normal, prev, a, b)) {
            st.foot_a = a;
            st.foot_b = b;
            st.width_um = norm(a.point - b.point);
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

} // namespace openstitch::auto_satin::detail
