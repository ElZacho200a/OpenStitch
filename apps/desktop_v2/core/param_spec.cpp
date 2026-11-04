// SPDX-License-Identifier: Apache-2.0
#include "param_spec.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace openstitch::desktop_v2 {

namespace {

using K = ParamKind;

// type, clé, libellé, nature, min, max, pas, unité
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
    if (const auto* p = std::get_if<RunningStitchParams>(&o.params)) {
        if (key == "stitch_length_mm")
            return mm(p->stitch_length);
        if (key == "repeats")
            return static_cast<double>(p->repeats);
    } else if (const auto* t = std::get_if<TatamiParams>(&o.params)) {
        if (key == "angle_deg")
            return t->angle.radians * 180.0 / std::numbers::pi;
        if (key == "row_spacing_mm")
            return mm(t->row_spacing);
        if (key == "stitch_length_mm")
            return mm(t->stitch_length);
        if (key == "inset_mm")
            return mm(t->inset);
        if (key == "pull_compensation_mm")
            return mm(t->pull_compensation);
        if (key == "underlay_edge")
            return t->underlay_edge ? 1.0 : 0.0;
        if (key == "underlay_parallel")
            return t->underlay_parallel ? 1.0 : 0.0;
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
            return d->underlay_edge ? 1.0 : 0.0;
    } else if (const auto* s = std::get_if<SatinParams>(&o.params)) {
        if (key == "density_mm")
            return mm(s->density);
        if (key == "pull_compensation_mm")
            return mm(s->pull_compensation);
        if (key == "max_width_mm")
            return mm(s->max_width);
        if (key == "center_underlay")
            return s->center_underlay ? 1.0 : 0.0;
        if (key == "underlay_edge")
            return s->underlay_edge ? 1.0 : 0.0;
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
