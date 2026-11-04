// SPDX-License-Identifier: Apache-2.0
#include "param_spec.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace openstitch::desktop_v2 {

namespace {

using K = ParamKind;

constexpr std::string_view kPull =
    "manual|Manuelle;auto|Automatique;adaptive|Adaptative;predicted|Prédictive";
constexpr std::string_view kUnderlay = "manual|Manuelle;auto|Automatique";

// type, clé, libellé, nature, min, max, pas, unité[, options]
const std::vector<ParamSpec>& table() {
    static const std::vector<ParamSpec> kTable{
        {"running", "stitch_length_mm", "Longueur de point", K::Number, 0.5, 12.0, 0.1, "mm"},
        {"running", "repeats", "Passages", K::Integer, 1, 3, 1, ""},

        {"tatami", "angle_deg", "Angle des rangées", K::Number, -360.0, 360.0, 5.0, "°"},
        {"tatami", "row_spacing_mm", "Écart entre rangées", K::Number, 0.1, 2.0, 0.05, "mm"},
        {"tatami", "stitch_length_mm", "Longueur de point", K::Number, 1.0, 7.0, 0.1, "mm"},
        {"tatami", "inset_mm", "Retrait du bord", K::Number, 0.0, 2.0, 0.05, "mm"},
        {"tatami", "pull_compensation_mm", "Compensation de tirage", K::Number, 0.0, 1.5, 0.05,
         "mm"},
        {"tatami", "underlay_edge", "Sous-couche de contour", K::Bool, 0, 1, 1, ""},
        {"tatami", "underlay_parallel", "Sous-couche de rangées", K::Bool, 0, 1, 1, ""},

        {"directional", "row_spacing_mm", "Écart entre rangées", K::Number, 0.1, 2.0, 0.05, "mm"},
        {"directional", "stitch_length_mm", "Longueur de point", K::Number, 1.0, 7.0, 0.1, "mm"},
        {"directional", "inset_mm", "Retrait du bord", K::Number, 0.0, 2.0, 0.05, "mm"},
        {"directional", "pull_compensation_mm", "Compensation de tirage", K::Number, 0.0, 1.5, 0.05,
         "mm"},
        {"directional", "underlay_edge", "Sous-couche de contour", K::Bool, 0, 1, 1, ""},

        {"satin", "density_mm", "Densité (écart des points)", K::Number, 0.1, 2.0, 0.05, "mm"},
        {"satin", "pull_compensation_mm", "Compensation de tirage", K::Number, 0.0, 1.5, 0.05,
         "mm"},
        {"satin", "max_width_mm", "Largeur maximale", K::Number, 1.0, 48.0, 0.5, "mm"},
        {"satin", "center_underlay", "Sous-couche centrale", K::Bool, 0, 1, 1, ""},
        {"satin", "underlay_edge", "Sous-couche de bord", K::Bool, 0, 1, 1, ""},

        // Sous-couches et compensation de tirage (modes manuel / auto / adaptatif / PRÉDICTIF).
        {"tatami", "pull_mode", "Mode de compensation", K::Choice, 0, 3, 1, "", kPull},
        {"tatami", "underlay_mode", "Sous-couche", K::Choice, 0, 1, 1, "", kUnderlay},
        {"tatami", "underlay_zigzag", "Sous-couche en zigzag", K::Bool, 0, 1, 1, ""},
        {"tatami", "underlay_inset_mm", "Retrait de la sous-couche", K::Number, 0.0, 3.0, 0.1,
         "mm"},
        {"tatami", "underlay_spacing_mm", "Écart de la sous-couche", K::Number, 0.5, 6.0, 0.1,
         "mm"},
        {"tatami", "hidden_underpath", "Liaisons cachées", K::Bool, 0, 1, 1, ""},
        {"directional", "pull_mode", "Mode de compensation", K::Choice, 0, 3, 1, "", kPull},
        {"directional", "underlay_mode", "Sous-couche", K::Choice, 0, 1, 1, "", kUnderlay},
        {"directional", "underlay_parallel", "Sous-couche de rangées", K::Bool, 0, 1, 1, ""},
        {"directional", "hidden_underpath", "Liaisons cachées", K::Bool, 0, 1, 1, ""},
        {"satin", "pull_mode", "Mode de compensation", K::Choice, 0, 3, 1, "", kPull},
        {"satin", "underlay_mode", "Sous-couche", K::Choice, 0, 1, 1, "", kUnderlay},
        {"satin", "underlay_zigzag", "Sous-couche en zigzag", K::Bool, 0, 1, 1, ""},

        // Remplissages spéciaux.
        {"spiral", "row_spacing_mm", "Écart entre tours", K::Number, 0.1, 2.0, 0.05, "mm"},
        {"spiral", "stitch_length_mm", "Longueur de point", K::Number, 1.0, 7.0, 0.1, "mm"},
        {"spiral", "inset_mm", "Retrait du bord", K::Number, 0.0, 2.0, 0.05, "mm"},
        {"spiral", "clockwise", "Sens horaire", K::Bool, 0, 1, 1, ""},
        {"spiral", "pull_mode", "Mode de compensation", K::Choice, 0, 3, 1, "", kPull},
        {"cross_stitch", "cell_mm", "Côté d'une cellule", K::Number, 1.0, 8.0, 0.25, "mm"},
        {"cross_stitch", "grid_angle_deg", "Angle de la grille", K::Number, -360.0, 360.0, 5.0,
         "°"},
        {"cross_stitch", "full_cross", "Croix complète", K::Bool, 0, 1, 1, ""},
        {"cross_stitch", "passes", "Passes", K::Integer, 1, 3, 1, ""},
        {"applique", "placement_length_mm", "Point de placement", K::Number, 1.0, 7.0, 0.25, "mm"},
        {"applique", "tack_width_mm", "Largeur du maintien", K::Number, 0.2, 20.0, 0.25, "mm"},
        {"applique", "tack_spacing_mm", "Écart du maintien", K::Number, 0.3, 20.0, 0.25, "mm"},
        {"applique", "cover_width_mm", "Largeur du satin de couverture", K::Number, 0.5, 20.0, 0.25,
         "mm"},
        {"applique", "cover_density_mm", "Densité de la couverture", K::Number, 0.2, 1.5, 0.05,
         "mm"},
        {"applique", "stops", "Arrêts machine", K::Bool, 0, 1, 1, ""},
    };
    return kTable;
}

double mm(Micrometers um) {
    return static_cast<double>(um.value) / 1000.0;
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    return buf;
}

} // namespace

std::vector<std::pair<std::string, std::string>> paramOptions(const ParamSpec& spec) {
    std::vector<std::pair<std::string, std::string>> out;
    std::string_view rest = spec.options;
    while (!rest.empty()) {
        const auto semi = rest.find(';');
        const std::string_view item = rest.substr(0, semi);
        const auto bar = item.find('|');
        out.emplace_back(std::string(item.substr(0, bar)), bar == std::string_view::npos
                                                               ? std::string(item)
                                                               : std::string(item.substr(bar + 1)));
        rest = semi == std::string_view::npos ? std::string_view{} : rest.substr(semi + 1);
    }
    return out;
}

std::vector<ParamSpec> paramSpecs(std::string_view typeId) {
    std::vector<ParamSpec> out;
    for (const auto& s : table()) {
        if (s.type == typeId) {
            out.push_back(s);
        }
    }
    return out;
}

std::optional<double> readParam(const document::EmbroideryObject& o, std::string_view key) {
    using namespace document;
    const auto b2d = [](bool v) { return v ? 1.0 : 0.0; };
    const auto deg = [](Angle a) { return a.radians * 180.0 / std::numbers::pi; };
    if (const auto* p = std::get_if<RunningStitchParams>(&o.params)) {
        if (key == "stitch_length_mm")
            return mm(p->stitch_length);
        if (key == "repeats")
            return static_cast<double>(p->repeats);
    } else if (const auto* t = std::get_if<TatamiParams>(&o.params)) {
        if (key == "angle_deg")
            return deg(t->angle);
        if (key == "row_spacing_mm")
            return mm(t->row_spacing);
        if (key == "stitch_length_mm")
            return mm(t->stitch_length);
        if (key == "inset_mm")
            return mm(t->inset);
        if (key == "pull_compensation_mm")
            return mm(t->pull_compensation);
        if (key == "underlay_edge")
            return b2d(t->underlay_edge);
        if (key == "underlay_parallel")
            return b2d(t->underlay_parallel);
        if (key == "pull_mode")
            return static_cast<double>(t->pull_mode);
        if (key == "underlay_mode")
            return static_cast<double>(t->underlay_mode);
        if (key == "underlay_zigzag")
            return b2d(t->underlay_zigzag);
        if (key == "underlay_inset_mm")
            return mm(t->underlay_inset);
        if (key == "underlay_spacing_mm")
            return mm(t->underlay_spacing);
        if (key == "hidden_underpath")
            return b2d(t->hidden_underpath);
    } else if (const auto* d = std::get_if<DirectionalFillParams>(&o.params)) {
        if (key == "row_spacing_mm")
            return mm(d->row_spacing);
        if (key == "stitch_length_mm")
            return mm(d->stitch_length);
        if (key == "inset_mm")
            return mm(d->inset);
        if (key == "pull_compensation_mm")
            return mm(d->pull_compensation);
        if (key == "underlay_edge")
            return b2d(d->underlay_edge);
        if (key == "pull_mode")
            return static_cast<double>(d->pull_mode);
        if (key == "underlay_mode")
            return static_cast<double>(d->underlay_mode);
        if (key == "underlay_parallel")
            return b2d(d->underlay_parallel);
        if (key == "hidden_underpath")
            return b2d(d->hidden_underpath);
    } else if (const auto* s = std::get_if<SatinParams>(&o.params)) {
        if (key == "density_mm")
            return mm(s->density);
        if (key == "pull_compensation_mm")
            return mm(s->pull_compensation);
        if (key == "max_width_mm")
            return mm(s->max_width);
        if (key == "center_underlay")
            return b2d(s->center_underlay);
        if (key == "underlay_edge")
            return b2d(s->underlay_edge);
        if (key == "pull_mode")
            return static_cast<double>(s->pull_mode);
        if (key == "underlay_mode")
            return static_cast<double>(s->underlay_mode);
        if (key == "underlay_zigzag")
            return b2d(s->underlay_zigzag);
    } else if (const auto* sp = std::get_if<SpiralFillParams>(&o.params)) {
        if (key == "row_spacing_mm")
            return mm(sp->row_spacing);
        if (key == "stitch_length_mm")
            return mm(sp->stitch_length);
        if (key == "inset_mm")
            return mm(sp->inset);
        if (key == "clockwise")
            return b2d(sp->clockwise);
        if (key == "pull_mode")
            return static_cast<double>(sp->pull_mode);
    } else if (const auto* cs = std::get_if<CrossStitchParams>(&o.params)) {
        if (key == "cell_mm")
            return mm(cs->cell);
        if (key == "grid_angle_deg")
            return deg(cs->grid_angle);
        if (key == "full_cross")
            return b2d(cs->full_cross);
        if (key == "passes")
            return static_cast<double>(cs->passes);
    } else if (const auto* ap = std::get_if<AppliqueParams>(&o.params)) {
        if (key == "placement_length_mm")
            return mm(ap->placement_length);
        if (key == "tack_width_mm")
            return mm(ap->tack_width);
        if (key == "tack_spacing_mm")
            return mm(ap->tack_spacing);
        if (key == "cover_width_mm")
            return mm(ap->cover_width);
        if (key == "cover_density_mm")
            return mm(ap->cover_density);
        if (key == "stops")
            return b2d(ap->stops);
    }
    return std::nullopt;
}

std::string batchSetParam(const std::vector<std::uint64_t>& ids, std::string_view typeId,
                          std::string_view key, double value) {
    if (ids.empty() || !std::isfinite(value)) {
        return {};
    }
    const auto specs = paramSpecs(typeId);
    const auto it = std::ranges::find_if(specs, [&](const auto& s) { return s.key == key; });
    if (it == specs.end() || value < it->min - 1e-9 || value > it->max + 1e-9) {
        return {};
    }
    std::string literal;
    switch (it->kind) {
    case K::Bool:
        literal = value >= 0.5 ? "true" : "false";
        break;
    case K::Integer:
        literal = std::to_string(static_cast<long long>(std::llround(value)));
        break;
    case K::Number:
        literal = num(value);
        break;
    case K::Choice: {
        const auto options = paramOptions(*it);
        const auto index = static_cast<std::size_t>(std::llround(value));
        if (value < -1e-9 || index >= options.size()) {
            return {};
        }
        literal = "\"" + options[index].first + "\"";
        break;
    }
    }
    std::string ops;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ops += std::string(i == 0 ? "" : ",") + R"({"op":"set_params","object":)" +
               std::to_string(ids[i]) + R"(,"params":{")" + std::string(key) + "\":" + literal +
               "}}";
    }
    return R"({"schema":"openstitch.batch/1","operations":[)" + ops + "]}";
}

} // namespace openstitch::desktop_v2
