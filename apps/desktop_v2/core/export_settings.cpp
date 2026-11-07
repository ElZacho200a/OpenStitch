// SPDX-License-Identifier: Apache-2.0
#include "export_settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/finishing_commands.hpp"
#include "openstitch/commands/machine_commands.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/document/hoop_library.hpp"
#include "openstitch/document/machine_profile.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::desktop_v2 {

namespace {
constexpr double kMaxCoordMm = 1000.0;
constexpr double kMaxHoopMm = 1000.0;

ServiceResult refuse(std::string why) {
    return {false, std::move(why)};
}

double toMm(Micrometers v) {
    return to_millimeters(v).value;
}
Micrometers toUmM(double mm) {
    return to_micrometers(Millimeters{mm});
}
PointMm toPointMm(Vec2um p) {
    return {toMm(p.x), toMm(p.y)};
}
Vec2um toVec(PointMm p) {
    return {toUmM(p.x), toUmM(p.y)};
}

LockKind toKind(document::LockStitch l) {
    switch (l) {
    case document::LockStitch::None:
        return LockKind::None;
    case document::LockStitch::BackAndForth:
        return LockKind::BackAndForth;
    case document::LockStitch::Triangle:
        return LockKind::Triangle;
    case document::LockStitch::MicroZigzag:
        return LockKind::MicroZigzag;
    }
    return LockKind::None;
}
document::LockStitch toLock(LockKind k) {
    switch (k) {
    case LockKind::None:
        return document::LockStitch::None;
    case LockKind::BackAndForth:
        return document::LockStitch::BackAndForth;
    case LockKind::Triangle:
        return document::LockStitch::Triangle;
    case LockKind::MicroZigzag:
        return document::LockStitch::MicroZigzag;
    }
    return document::LockStitch::None;
}

bool inRange(const std::optional<PointMm>& p) {
    return !p || (std::isfinite(p->x) && std::isfinite(p->y) && std::fabs(p->x) <= kMaxCoordMm &&
                  std::fabs(p->y) <= kMaxCoordMm);
}

bool sameHoop(const document::Canvas& c, const document::HoopSpec& h) {
    return c.shape == h.shape && c.width.value == h.width_mm * 1000 &&
           c.height.value == h.height_mm * 1000 &&
           (h.shape != document::HoopShape::RoundedRectangle ||
            c.corner_radius.value == h.corner_radius_mm * 1000);
}
} // namespace

std::string lockKindLabel(LockKind kind) {
    switch (kind) {
    case LockKind::None:
        return "Aucun";
    case LockKind::BackAndForth:
        return "Aller-retour";
    case LockKind::Triangle:
        return "Triangle";
    case LockKind::MicroZigzag:
        return "Micro-zigzag";
    }
    return {};
}

ExportOptions exportOptionsOf(const document::Project& project) {
    const auto& f = project.finishing;
    ExportOptions o;
    o.finishingEnabled = f.enabled;
    o.trimThresholdMm = toMm(f.trim_threshold);
    o.trimBeforeColorChange = f.trim_before_color_change;
    o.lockKind = toKind(f.lock_type);
    o.lockLengthMm = toMm(f.lock_length);
    o.lockPasses = f.lock_passes;
    o.filterShortStitches = f.filter_short_stitches;
    o.minStitchMm = toMm(f.min_stitch_length);
    o.splitLongStitches = f.split_long_stitches;
    o.maxStitchMm = toMm(f.max_stitch_length);
    o.autoJoin = f.auto_join;
    if (f.design_start) {
        o.designStart = toPointMm(*f.design_start);
    }
    if (f.design_end) {
        o.designEnd = toPointMm(*f.design_end);
    }
    return o;
}

std::string validateExportOptions(const ExportOptions& o) {
    const auto bad = [](double v, double lo, double hi) {
        return !std::isfinite(v) || v < lo || v > hi;
    };
    if (bad(o.trimThresholdMm, 0.5, 100.0)) {
        return "Seuil de coupe : entre 0,5 et 100 mm.";
    }
    if (bad(o.lockLengthMm, 0.2, 3.0)) {
        return "Longueur des points d'arrêt : entre 0,2 et 3 mm.";
    }
    if (o.lockPasses < 1 || o.lockPasses > 6) {
        return "Passes des points d'arrêt : entre 1 et 6.";
    }
    if (bad(o.minStitchMm, 0.1, 2.0)) {
        return "Point minimal : entre 0,1 et 2 mm.";
    }
    if (bad(o.maxStitchMm, 2.0, 12.1)) {
        return "Point maximal : entre 2 et 12 mm.";
    }
    if (o.maxStitchMm <= o.minStitchMm) {
        return "Le point maximal doit dépasser le point minimal.";
    }
    if (!inRange(o.designStart) || !inRange(o.designEnd)) {
        return "Départ ou arrivée hors limites (±1000 mm).";
    }
    return {};
}

ServiceResult setExportOptions(DocumentSession& session, const ExportOptions& options,
                               bool allowUnchanged) {
    if (const auto why = validateExportOptions(options); !why.empty()) {
        return refuse(why);
    }
    auto f = session.project().finishing; // conserve ce que cette fenêtre ne règle pas
    f.enabled = options.finishingEnabled;
    f.trim_threshold = toUmM(options.trimThresholdMm);
    f.trim_before_color_change = options.trimBeforeColorChange;
    f.lock_type = toLock(options.lockKind);
    f.lock_length = toUmM(options.lockLengthMm);
    f.lock_passes = options.lockPasses;
    f.filter_short_stitches = options.filterShortStitches;
    f.min_stitch_length = toUmM(options.minStitchMm);
    f.split_long_stitches = options.splitLongStitches;
    f.max_stitch_length = toUmM(options.maxStitchMm);
    f.auto_join = options.autoJoin;
    f.design_start =
        options.designStart ? std::optional<Vec2um>{toVec(*options.designStart)} : std::nullopt;
    f.design_end =
        options.designEnd ? std::optional<Vec2um>{toVec(*options.designEnd)} : std::nullopt;
    if (f == session.project().finishing) { // comparaison en µm : aucune dérive flottante
        if (allowUnchanged)
            return {true, {}};
        return refuse("Ces options sont déjà celles du projet.");
    }
    session.execute(std::make_unique<commands::SetFinishingCommand>(f));
    return {true, {}};
}

std::vector<HoopChoice> hoopChoices() {
    std::vector<HoopChoice> out;
    for (const auto& h : document::hoop_library()) {
        out.push_back({std::string(h.id), std::string(h.family), std::string(h.name),
                       static_cast<double>(h.width_mm), static_cast<double>(h.height_mm)});
    }
    return out;
}

std::vector<MachineChoice> machineChoices() {
    std::vector<MachineChoice> out;
    for (const auto& m : document::machine_profiles()) {
        MachineChoice c{std::string(m.id),
                        std::string(m.name),
                        std::string(m.preferred_format),
                        static_cast<double>(m.max_field_width_mm),
                        static_cast<double>(m.max_field_height_mm),
                        m.auto_trim,
                        m.needles,
                        {}};
        for (const auto id : m.hoops()) {
            c.hoopIds.emplace_back(id);
        }
        out.push_back(std::move(c));
    }
    return out;
}

std::string currentHoopId(const document::Project& project) {
    for (const auto& h : document::hoop_library()) {
        if (sameHoop(project.canvas, h)) {
            return std::string(h.id);
        }
    }
    return {};
}

double currentMarginMm(const document::Project& project) {
    return toMm(project.canvas.margin);
}

namespace {
ServiceResult setCanvas(DocumentSession& session, const document::Canvas& canvas) {
    const auto& cur = session.project().canvas;
    if (cur.width == canvas.width && cur.height == canvas.height && cur.margin == canvas.margin &&
        cur.shape == canvas.shape && cur.corner_radius == canvas.corner_radius) {
        return refuse("Le cadre est déjà celui-là.");
    }
    session.execute(std::make_unique<commands::SetCanvasCommand>(canvas));
    return {true, {}};
}

bool marginOk(double marginMm, double widthMm, double heightMm) {
    return std::isfinite(marginMm) && marginMm >= 0.0 &&
           2.0 * marginMm < std::min(widthMm, heightMm);
}
} // namespace

ServiceResult applyHoop(DocumentSession& session, const std::string& hoopId, double marginMm) {
    const auto* hoop = document::find_hoop(hoopId);
    if (hoop == nullptr) {
        return refuse("Cadre inconnu : " + hoopId + ".");
    }
    if (!marginOk(marginMm, hoop->width_mm, hoop->height_mm)) {
        return refuse("Marge de sécurité invalide : positive, et inférieure à la moitié du plus "
                      "petit côté du cadre.");
    }
    document::Canvas c;
    c.width = toUmM(hoop->width_mm);
    c.height = toUmM(hoop->height_mm);
    c.margin = toUmM(marginMm);
    c.shape = hoop->shape;
    c.corner_radius = toUmM(hoop->corner_radius_mm);
    return setCanvas(session, c);
}

ServiceResult applyCustomHoop(DocumentSession& session, double widthMm, double heightMm,
                              double marginMm) {
    if (!std::isfinite(widthMm) || !std::isfinite(heightMm) || widthMm < 10.0 || heightMm < 10.0 ||
        widthMm > kMaxHoopMm || heightMm > kMaxHoopMm) {
        return refuse("Dimensions du cadre : entre 10 et 1000 mm.");
    }
    if (!marginOk(marginMm, widthMm, heightMm)) {
        return refuse("Marge de sécurité invalide : positive, et inférieure à la moitié du plus "
                      "petit côté du cadre.");
    }
    document::Canvas c;
    c.width = toUmM(widthMm);
    c.height = toUmM(heightMm);
    c.margin = toUmM(marginMm);
    return setCanvas(session, c);
}

ServiceResult applyMachine(DocumentSession& session, const std::string& machineId) {
    if (!machineId.empty() && document::find_machine(machineId) == nullptr) {
        return refuse("Machine inconnue : " + machineId + ".");
    }
    if (machineId == session.project().machine_id) {
        return refuse("Cette machine est déjà choisie.");
    }
    session.execute(std::make_unique<commands::SetMachineCommand>(machineId));
    return {true, {}};
}

std::string ExportCheck::summary() const {
    if (!ok) {
        return message;
    }
    std::string out = std::to_string(stitches) + " points cousus. ";
    out += fitsHoop ? "Le motif tient dans le cadre. "
                    : std::to_string(outsideHoop) + " point(s) hors de la zone brodable du cadre. ";
    if (!fitsMachineField) {
        out += "Le motif dépasse le champ de la machine. ";
    }
    if (!formatAccepted) {
        out += "Ce format n'est pas accepté par la machine choisie. ";
    }
    if (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

ExportCheck checkExport(const document::Project& project, const std::string& format) {
    ExportCheck out;
    const auto seq = stitch_generation::effective_sequence(project);
    if (!seq) {
        out.message = seq.error().message;
        return out;
    }
    return checkExportSequence(project, *seq, format);
}

ExportCheck checkExportSequence(const document::Project& project,
                                const stitch::StitchSequence& sequence, const std::string& format) {
    ExportCheck out;
    bool first = true;
    std::int32_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    for (const auto& c : sequence.commands) {
        if (c.type != stitch::CommandType::Stitch) {
            continue;
        }
        ++out.stitches;
        if (!project.canvas.contains_usable(c.pos)) {
            ++out.outsideHoop;
        }
        if (first) {
            x0 = x1 = c.pos.x.value;
            y0 = y1 = c.pos.y.value;
            first = false;
        } else {
            x0 = std::min(x0, c.pos.x.value);
            x1 = std::max(x1, c.pos.x.value);
            y0 = std::min(y0, c.pos.y.value);
            y1 = std::max(y1, c.pos.y.value);
        }
    }
    if (out.stitches == 0) {
        out.message = "Aucun point cousu : rien à contrôler avant l'export.";
        return out;
    }
    out.fitsHoop = out.outsideHoop == 0;
    if (const auto* machine = document::find_machine(project.machine_id); machine != nullptr) {
        const double w = (static_cast<double>(x1) - x0) / 1000.0;
        const double h = (static_cast<double>(y1) - y0) / 1000.0;
        out.fitsMachineField =
            w <= machine->max_field_width_mm && h <= machine->max_field_height_mm;
        out.formatAccepted = format.empty() || machine->accepts_format(format);
    }
    out.ok = true;
    return out;
}

} // namespace openstitch::desktop_v2
