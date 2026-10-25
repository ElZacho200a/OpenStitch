// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "openstitch/document/hoop_library.hpp"

namespace openstitch::document {

// HP-HOOP-002 : profil de machine à broder GÉNÉRIQUE (classes de machines, pas de base de
// données de marques). Valeurs indicatives : le profil sert à AVERTIR avant l'export
// (HP-HOOP-003), il ne modifie jamais le design. Le projet ne garde que l'identifiant
// (`Project::machine_id`) ; vide = aucune machine, aucune vérification.
struct MachineProfile {
    std::string_view id;
    std::string_view name;
    std::string_view preferred_format;       // identifiant de `formats::FormatInfo`
    std::array<std::string_view, 4> formats; // formats acceptés (préféré en tête)
    int format_count;
    std::array<std::string_view, 6> hoop_ids; // cadres disponibles (`HoopSpec::id`)
    int hoop_count;
    int max_field_width_mm; // champ de broderie maximal (boîte englobante)
    int max_field_height_mm;
    int max_stitch_length_mm; // plus long point que la machine coud sans le couper
    bool auto_trim;           // coupe-fil automatique
    int needles;              // nombre d'aiguilles (= couleurs sans changement manuel)
    int max_speed_spm;        // vitesse maximale, points par minute

    [[nodiscard]] std::span<const std::string_view> format_ids() const {
        return {formats.data(), static_cast<std::size_t>(format_count)};
    }
    [[nodiscard]] std::span<const std::string_view> hoops() const {
        return {hoop_ids.data(), static_cast<std::size_t>(hoop_count)};
    }
    [[nodiscard]] bool accepts_format(std::string_view format_id) const {
        for (const std::string_view f : format_ids()) {
            if (f == format_id) {
                return true;
            }
        }
        return false;
    }
};

inline constexpr std::array<MachineProfile, 6> kMachineProfiles{{
    {"generic-single-100",
     "Mono-aiguille compacte (champ 100 x 100)",
     "pes",
     {"pes", "jef", "exp", "dst"},
     4,
     {"rect-50x50", "rect-100x100", "round-60", "round-100"},
     4,
     100,
     100,
     7,
     false,
     1,
     650},
    {"generic-single-130x180",
     "Mono-aiguille domestique (champ 130 x 180)",
     "pes",
     {"pes", "jef", "exp", "dst"},
     4,
     {"rect-100x100", "rect-130x180", "rounded-130x180", "round-100"},
     4,
     130,
     180,
     7,
     true,
     1,
     850},
    {"generic-single-200x300",
     "Mono-aiguille grand champ (champ 200 x 300)",
     "pes",
     {"pes", "jef", "exp", "dst"},
     4,
     {"rect-130x180", "rect-160x260", "rect-200x300", "round-150", "oval-200x260"},
     5,
     200,
     300,
     12,
     true,
     1,
     1000},
    {"generic-multi-6",
     "6 aiguilles (champ 200 x 300)",
     "dst",
     {"dst", "exp", "pes"},
     3,
     {"rect-100x100", "rect-130x180", "rect-200x300", "round-150", "cap-150x60"},
     5,
     200,
     300,
     12,
     true,
     6,
     1000},
    {"generic-multi-12",
     "12 aiguilles (champ 300 x 400)",
     "dst",
     {"dst", "exp"},
     2,
     {"rect-130x180", "rect-200x300", "rect-300x400", "round-200", "cap-150x60"},
     5,
     300,
     400,
     12,
     true,
     12,
     1000},
    {"generic-industrial-15",
     "Industrielle 15 aiguilles (champ 360 x 450)",
     "dst",
     {"dst", "exp"},
     2,
     {"rect-200x300", "rect-300x400", "rect-360x450", "oval-200x260", "cap-150x60"},
     5,
     360,
     450,
     12,
     true,
     15,
     1200},
}};

[[nodiscard]] inline std::span<const MachineProfile> machine_profiles() {
    return std::span<const MachineProfile>(kMachineProfiles);
}

// Profil par identifiant ; nullptr si inconnu (identifiant vide compris).
[[nodiscard]] inline const MachineProfile* find_machine(std::string_view id) {
    for (const MachineProfile& m : kMachineProfiles) {
        if (m.id == id) {
            return &m;
        }
    }
    return nullptr;
}

} // namespace openstitch::document
