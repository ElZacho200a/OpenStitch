// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/machine_checks.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <string>

#include "openstitch/document/hoop_library.hpp"
#include "openstitch/document/machine_profile.hpp"
#include "openstitch/stitch_analysis/color_blocks.hpp"

namespace openstitch::stitch_analysis {

namespace {

std::string mm1(std::int64_t um) {
    return format_mm_fr(static_cast<double>(um));
}

} // namespace

std::vector<Finding> check_machine(const document::Project& project,
                                   const stitch::StitchSequence& sequence,
                                   std::string_view export_format) {
    std::vector<Finding> out;
    if (project.machine_id.empty()) {
        return out;
    }
    const document::MachineProfile* machine = document::find_machine(project.machine_id);
    if (machine == nullptr) {
        out.push_back({Severity::Warning,
                       "machine-inconnue",
                       "Profil machine inconnu : « " + project.machine_id + " ».",
                       {},
                       {},
                       "Choisissez une machine dans le dialogue Machine.",
                       {}});
        return out;
    }
    const std::string machineName(machine->name);

    if (!export_format.empty() && !machine->accepts_format(export_format)) {
        out.push_back({Severity::Warning,
                       "machine-format",
                       "Le format « " + std::string(export_format) + " » n'est pas accepté par « " +
                           machineName + " ».",
                       {},
                       {},
                       "Exportez en « " + std::string(machine->preferred_format) +
                           " » (format préféré de cette machine).",
                       {}});
    }

    // Boîte englobante des points cousus, plus long point, coupes.
    bool any = false;
    stitch::BoundsUm box{};
    std::int64_t longest = 0;
    std::size_t longCount = 0;
    std::size_t trims = 0;
    bool hasPrev = false;
    Vec2um prev{};
    Vec2um longAt{};
    const std::int64_t limit = static_cast<std::int64_t>(machine->max_stitch_length_mm) * 1000;
    for (const auto& cmd : sequence.commands) {
        if (cmd.type == stitch::CommandType::Trim) {
            ++trims;
        }
        if (cmd.type == stitch::CommandType::Jump || cmd.type == stitch::CommandType::Trim ||
            cmd.type == stitch::CommandType::ColorChange || cmd.type == stitch::CommandType::Stop) {
            hasPrev = false;
            continue;
        }
        if (cmd.type != stitch::CommandType::Stitch) {
            continue;
        }
        if (!any) {
            box = {cmd.pos, cmd.pos};
            any = true;
        } else {
            box.min.x = std::min(box.min.x, cmd.pos.x);
            box.min.y = std::min(box.min.y, cmd.pos.y);
            box.max.x = std::max(box.max.x, cmd.pos.x);
            box.max.y = std::max(box.max.y, cmd.pos.y);
        }
        if (hasPrev) {
            const std::int64_t dx = static_cast<std::int64_t>(cmd.pos.x.value) - prev.x.value;
            const std::int64_t dy = static_cast<std::int64_t>(cmd.pos.y.value) - prev.y.value;
            // Comparaison au carré : pas de flottant pour décider.
            if (dx * dx + dy * dy > limit * limit) {
                if (longCount == 0) {
                    longAt = cmd.pos;
                }
                ++longCount;
                longest = std::max<std::int64_t>(
                    longest, static_cast<std::int64_t>(length_um(cmd.pos - prev)));
            }
        }
        prev = cmd.pos;
        hasPrev = true;
    }

    if (trims > 0 && !machine->auto_trim) {
        out.push_back({Severity::Info,
                       "machine-coupe",
                       "« " + machineName + " » n'a pas de coupe-fil automatique : " +
                           std::to_string(trims) + " coupe(s) du design seront ignorées.",
                       {},
                       {},
                       "Coupez les fils à la main aux sauts, ou désactivez les coupes.",
                       {}});
    }
    if (longCount > 0) {
        out.push_back({Severity::Warning,
                       "machine-point-long",
                       std::to_string(longCount) + " point(s) plus long(s) que la limite de « " +
                           machineName + " » (" + std::to_string(machine->max_stitch_length_mm) +
                           " mm ; le plus long : " + mm1(longest) + " mm).",
                       longAt,
                       {},
                       "Activez le découpage des points trop longs dans les finitions.",
                       {}});
    }
    if (!any) {
        return out;
    }

    const std::int64_t w = static_cast<std::int64_t>(box.max.x.value) - box.min.x.value;
    const std::int64_t h = static_cast<std::int64_t>(box.max.y.value) - box.min.y.value;
    const std::int64_t fieldW = static_cast<std::int64_t>(machine->max_field_width_mm) * 1000;
    const std::int64_t fieldH = static_cast<std::int64_t>(machine->max_field_height_mm) * 1000;
    // Le design peut être tourné de 90 degrés sur la machine : il passe si l'une des deux
    // orientations tient dans le champ.
    const bool fits = (w <= fieldW && h <= fieldH) || (w <= fieldH && h <= fieldW);
    if (!fits) {
        out.push_back({Severity::Error,
                       "machine-champ",
                       "Le design (" + mm1(w) + " x " + mm1(h) +
                           " mm) dépasse le champ maximal de « " + machineName + " » (" +
                           std::to_string(machine->max_field_width_mm) + " x " +
                           std::to_string(machine->max_field_height_mm) + " mm).",
                       box.min,
                       {},
                       "Réduisez le design ou choisissez une machine à plus grand champ.",
                       {}});
    } else {
        // Cadres de la machine : au moins un doit contenir tous les points (forme comprise).
        bool inSomeHoop = false;
        for (const std::string_view id : machine->hoops()) {
            const document::HoopSpec* hoop = document::find_hoop(id);
            if (hoop == nullptr) {
                continue;
            }
            const document::Canvas canvas =
                document::canvas_from_hoop(*hoop, project.canvas.margin);
            const bool inside = std::all_of(sequence.commands.begin(), sequence.commands.end(),
                                            [&](const auto& cmd) {
                                                return cmd.type != stitch::CommandType::Stitch ||
                                                       canvas.contains_usable(cmd.pos);
                                            });
            if (inside) {
                inSomeHoop = true;
                break;
            }
        }
        if (!inSomeHoop) {
            out.push_back({Severity::Warning,
                           "machine-cadre",
                           "Aucun cadre de « " + machineName +
                               " » ne contient tout le design (marge de sécurité et forme du "
                               "cadre comprises).",
                           box.min,
                           {},
                           "Recentrez le design, réduisez-le ou choisissez un plus grand cadre.",
                           {}});
        }
    }

    const auto blocks = color_blocks(project, sequence);
    std::set<std::array<std::uint8_t, 3>> colors;
    for (const auto& b : blocks) {
        colors.insert(b.rgb);
    }
    if (static_cast<int>(colors.size()) > machine->needles) {
        out.push_back({Severity::Warning,
                       "machine-aiguilles",
                       "Le design utilise " + std::to_string(colors.size()) + " couleurs, « " +
                           machineName + " » a " + std::to_string(machine->needles) +
                           " aiguille(s) : des changements de fil manuels seront nécessaires.",
                       {},
                       {},
                       "Réduisez le nombre de couleurs ou prévoyez des arrêts.",
                       {}});
    }
    return out;
}

} // namespace openstitch::stitch_analysis
