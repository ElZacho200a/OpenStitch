// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::formats {

// HP-FMT-001 : contraintes machine/format qui pilotent `normalize_for_machine`
// -- résolution, pas max par enregistrement, représentation des coupes,
// fusion Stop/ColorChange, nombre de couleurs max. Les valeurs par défaut
// sont celles du DST (seul format implémenté en S2a) ; un futur format
// (PES/JEF/EXP, S2b/S2c) construit son propre jeu de valeurs.
struct MachineConstraints {
    // µm par unité native du format (DST : 0,1 mm = 100 µm).
    std::int32_t resolution_um{100};
    // |dx| et |dy| max, en unités natives, par enregistrement de mouvement
    // (DST : ±12,1 mm = 121 unités, encodage ternaire équilibré sur 5 chiffres).
    int max_record_delta{121};

    // Comment une coupe LOGIQUE (`stitch::CommandType::Trim`) se représente
    // dans le flux d'enregistrements normalisé :
    //  - Native : un enregistrement dédié (type `MachineRecordType::Trim`).
    //  - RepeatedZeroJumps : convention machine la plus répandue (DST, JEF,
    //    TAP...) -- N enregistrements `Jump` de delta nul déclenchent la
    //    coupe à la relecture.
    enum class TrimEncoding : std::uint8_t { Native, RepeatedZeroJumps };
    TrimEncoding trim_encoding{TrimEncoding::RepeatedZeroJumps};
    // Nombre de sauts nuls (RepeatedZeroJumps uniquement) qui valent une
    // coupe. DST : convention à 3 (cf. `DstWriteOptions::trim_jumps`,
    // paramétrable à l'écriture ; la LECTURE reste fixée à 3, c'est la
    // convention du FORMAT, pas de l'outil qui a écrit le fichier).
    int trim_zero_jump_count{3};

    // Un format qui ne distingue pas un arrêt machine (`Stop`) d'un
    // changement de fil (`ColorChange`) -- DST, notamment -- fond les deux
    // dans un seul type d'enregistrement `ColorChange`.
    bool merge_stop_into_color_change{true};

    // Nombre de couleurs/aiguilles max du format, si limité (absent = pas de
    // limite connue). Non utilisé par la normalisation DST (aucune vraie
    // couleur) ; réservé pour PES/JEF (S2b/S2c).
    std::optional<int> max_colors{};
};

// Enregistrement machine NORMALISÉ : delta en unités natives (déjà quantifié
// sans dérive, déjà découpé sous `max_record_delta`), et le type réel à
// sérialiser. Un encodeur de format n'a plus qu'à transformer chaque
// enregistrement en octets (table de bits propre au format) -- toute la
// logique de découpage/quantification/représentation des coupes est déjà
// faite.
enum class MachineRecordType : std::uint8_t { Stitch, Jump, ColorChange, Stop, Trim };

struct MachineRecord {
    int dx{0};
    int dy{0};
    MachineRecordType type{MachineRecordType::Stitch};

    bool operator==(const MachineRecord&) const = default;
};

// Séquence NORMALISÉE pour un jeu de contraintes donné : les deltas sont
// relatifs à une origine implicite (0, 0) = la première position de la
// séquence d'entrée, après quantification. Aucune information sur l'origine
// réelle n'est nécessaire en aval : un encodeur calcule ses propres bornes
// d'en-tête par somme cumulée des deltas (cf. `docs/source/dst-format.md`
// pour la preuve que ce calcul coïncide avec l'ancien calcul par commande
// d'entrée).
struct MachineSequence {
    std::vector<MachineRecord> records;
};

// HP-FMT-001 : étape PURE qui sépare la normalisation machine des codecs.
// Découpe les déplacements/points au-delà de `max_record_delta`, quantifie
// à `resolution_um` SANS dérive cumulative (quantification de chaque
// position absolue, jamais un delta accumulé en virgule flottante),
// représente les `Trim` selon `trim_encoding`, fond `Stop` dans
// `ColorChange` si `merge_stop_into_color_change`. Un `Jump` « organique »
// (commande d'entrée) de delta nul consécutif à un autre `Jump` de delta nul
// déjà émis est omis (sans ça, plusieurs sauts sous la résolution du format
// d'affilée seraient relus comme une coupe fantôme avec `RepeatedZeroJumps`).
// Les commandes `End` de l'entrée sont ignorées (un `End` n'est jamais un
// enregistrement machine, c'est un marqueur de fin propre à chaque format).
[[nodiscard]] Result<MachineSequence> normalize_for_machine(const stitch::StitchSequence& sequence,
                                                            const MachineConstraints& constraints);

// Réciproque de `normalize_for_machine` côté décodage : reconstruit une
// `StitchSequence` à positions ABSOLUES µm depuis des enregistrements déjà
// extraits bit à bit par un décodeur de format (un décodeur ne fait QUE
// l'extraction (dx, dy, type) + la détection de son marqueur de fin ; cette
// fonction fait tout le reste, y compris reconnaître un run de
// `trim_zero_jump_count` `Jump` de delta nul consécutifs comme un `Trim`
// logique unique). Ajoute la commande `End` finale. `source` de chaque
// commande reconstruite est `ObjectId{}` (0 = manuel/importé, cf.
// `stitch/sequence.hpp`).
[[nodiscard]] stitch::StitchSequence
sequence_from_machine_records(std::span<const MachineRecord> records,
                              const MachineConstraints& constraints);

} // namespace openstitch::formats
