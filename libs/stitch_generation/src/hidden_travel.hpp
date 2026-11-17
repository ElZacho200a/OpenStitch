// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "openstitch/document/finishing.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_generation {

// HP-SEQ-006.a -- planificateur de ponts cachés (interne à `finish_sequence`).
//
// Une grille d'entiers rastérise les segments de passe `TopStitch` de la séquence ; chaque cellule
// retient l'indice de la DERNIÈRE commande dont le segment la couvre (à `cover_thread_width / 2`
// près). Un écart situé après la commande `g` ne peut donc s'appuyer que sur les cellules dont
// l'indice est > g : « recouvert plus tard ». La preuve vient de la séquence elle-même, pas des
// objets du document (un satin manuel n'a pas de surface vectorielle). Option
// `allow_same_color_surface` (HP-SEQ-006.b) : une cellule déjà cousue plus tôt avec le même fil
// compte aussi comme cachée (le fil de liaison s'y confond). Les appels à `plan` doivent se faire
// dans l'ordre croissant de `g` (sinon la surface est reconstruite depuis le début).
enum class TravelDecision : std::uint8_t { Keep, HiddenJump, Bridge };
enum class TravelRefusal : std::uint8_t { None, TooLong, NoCoverAtEnd, NoPath };

struct TravelPlan {
    TravelDecision decision{TravelDecision::Keep};
    TravelRefusal refusal{TravelRefusal::None};
    // Points intermédiaires du trajet cousu (sans l'origine ni la destination), vide sinon.
    std::vector<Vec2um> via;
};

class HiddenTravelPlanner {
public:
    HiddenTravelPlanner(const std::vector<stitch::StitchCommand>& cmds,
                        const document::SequenceFinishing& finishing);

    // `g` : indice de la dernière commande avant l'écart ; `from`/`to` : dernière piqûre du tracé
    // précédent et première du suivant. Fonction pure (la grille est construite à la première
    // demande), déterministe : voisins visités dans un ordre fixe, départage par indice.
    [[nodiscard]] TravelPlan plan(std::size_t g, Vec2um from, Vec2um to);

    // Vrai quand `plan` ne modifie aucun état une fois la grille construite (sans l'option
    // `allow_same_color_surface`, dont la surface se reconstruit par avancement séquentiel) :
    // après `prepare()`, plusieurs fils peuvent alors appeler `plan` en même temps.
    [[nodiscard]] bool concurrent_plans() const { return !cfg_.allow_same_color_surface; }
    // Construit la grille si ce n'est pas déjà fait (préalable aux appels concurrents).
    void prepare() {
        if (!built_) {
            build();
        }
    }

    struct Grid {
        std::int32_t ox{0};
        std::int32_t oy{0};
        std::int32_t cell{100};
        std::int32_t w{0};
        std::int32_t h{0};
        std::vector<std::int32_t> last; // indice de la dernière commande qui couvre, -1 sinon
    };
    // Grille de recouvrement construite (tests d'équivalence uniquement).
    [[nodiscard]] const Grid& grid_for_test() {
        prepare();
        return grid_;
    }

private:
    void build();
    // Rastérise le segment qui aboutit à la commande `i` (couche supérieure) : dans `last`
    // (indice du dernier recouvrement) ou, si `intoSame`, dans `same_` (surface déjà cousue).
    // `rowLo`..`rowHi` : rangées de la grille à traiter (bornes incluses), pour la construction
    // par bandes de rangées en parallèle.
    void stamp(std::size_t i, bool intoSame, std::int32_t rowLo = 0,
               std::int32_t rowHi = INT32_MAX);
    // Surface déjà cousue AVEC LE MÊME FIL (option `allow_same_color_surface`) jusqu'à la commande
    // `g` : se reconstruit par avancement séquentiel, remise à zéro à chaque changement de fil.
    void advance_same_color(std::size_t g);

    const std::vector<stitch::StitchCommand>& cmds_;
    document::HiddenTravel cfg_;
    std::int32_t minStitch_{0};
    bool built_{false};
    bool usable_{false};
    Grid grid_;
    std::vector<std::uint8_t> same_;
    std::size_t sameNext_{0}; // première commande pas encore rastérisée dans `same_`
};

} // namespace openstitch::stitch_generation
