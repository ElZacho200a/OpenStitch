// SPDX-License-Identifier: Apache-2.0
#include "production_sheet.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::desktop_v2 {

namespace {

double mm(Micrometers v) {
    return to_millimeters(v).value;
}

} // namespace

Result<ProductionSheet> buildProductionSheet(const document::Project& project,
                                             const TaskContext& task) {
    ProductionSheet sheet;
    sheet.hoopWidthMm = mm(project.canvas.width);
    sheet.hoopHeightMm = mm(project.canvas.height);
    sheet.objects = project.embroidery_objects.size();
    const bool anyVisible = std::any_of(
        project.embroidery_objects.begin(), project.embroidery_objects.end(),
        [&](const document::EmbroideryObject& o) { return o.visible && project.calqueVisible(o); });
    if (!anyVisible) {
        return sheet; // document vide : fiche sans plan de fils
    }
    auto sequence = stitch_generation::effective_sequence(project, task);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    const auto stats = stitch::compute_stats(*sequence);
    sheet.stitches = stats.stitches;
    sheet.jumps = stats.jumps;
    sheet.trims = stats.trims;
    sheet.colorChanges = stats.color_changes;
    sheet.threadMetres = stats.thread_length_um / 1.0e6;
    sheet.minutesAt800spm = static_cast<double>(stats.stitches) / 800.0 +
                            static_cast<double>(stats.color_changes) * 0.5 +
                            static_cast<double>(stats.trims) * (4.0 / 60.0);
    if (stats.stitches > 0) {
        sheet.designWidthMm = mm(Micrometers{stats.bounds.max.x.value - stats.bounds.min.x.value});
        sheet.designHeightMm = mm(Micrometers{stats.bounds.max.y.value - stats.bounds.min.y.value});
    }
    const double usableW = sheet.hoopWidthMm - 2 * mm(project.canvas.margin);
    const double usableH = sheet.hoopHeightMm - 2 * mm(project.canvas.margin);
    sheet.fitsHoop =
        sheet.designWidthMm <= usableW + 1e-6 && sheet.designHeightMm <= usableH + 1e-6;

    // Plan de fils : un bloc par intervalle entre deux changements de couleur.
    ThreadBlock current;
    std::set<std::string> names;
    std::size_t nameCount = 0;
    bool open = false;
    bool havePrev = false;
    Vec2um prev{};
    const auto flush = [&] {
        if (open && current.stitches > 0) {
            if (nameCount > 3) {
                current.objects += ", …";
            }
            sheet.blocks.push_back(current);
        }
        current = ThreadBlock{};
        names.clear();
        nameCount = 0;
        open = false;
    };
    for (const auto& c : sequence->commands) {
        if (c.type == stitch::CommandType::ColorChange || c.type == stitch::CommandType::End) {
            flush();
            havePrev = false;
            continue;
        }
        if (c.type != stitch::CommandType::Stitch) {
            havePrev = false; // un saut ou une coupe ne compte pas dans la longueur cousue
            continue;
        }
        const auto* obj = project.findEmbroidery(c.source);
        if (!open) {
            open = true;
            if (obj != nullptr) {
                current.rgb = obj->rgb;
            }
        }
        if (obj != nullptr && names.insert(obj->name).second) {
            ++nameCount;
            if (nameCount <= 3) {
                current.objects += (current.objects.empty() ? "" : ", ") + obj->name;
            }
        }
        ++current.stitches;
        if (havePrev) {
            const double dx = mm(Micrometers{c.pos.x.value - prev.x.value});
            const double dy = mm(Micrometers{c.pos.y.value - prev.y.value});
            current.lengthMm += std::sqrt(dx * dx + dy * dy);
        }
        prev = c.pos;
        havePrev = true;
    }
    flush();
    return sheet;
}

} // namespace openstitch::desktop_v2
