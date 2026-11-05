// SPDX-License-Identifier: Apache-2.0
#include "param_spec.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>
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

        // Réglages avancés (clés et plages du registre de champs de `set_params`).
        {"tatami", "stagger", "Décalage des rangées (motif en quinconce)", K::Integer, 1, 8, 1, ""},
        {"tatami", "row_end_clip_mm", "Raccourcissement des fins de rangée", K::Number, 0.0, 5.0,
         0.1, "mm"},
        {"tatami", "edge_outline", "Contour de bordure", K::Bool, 0, 1, 1, ""},
        {"directional", "stagger", "Décalage des rangées (motif en quinconce)", K::Integer, 1, 8, 1,
         ""},
        {"directional", "spacing_regularity", "Régularité de l'écart", K::Number, 0.0, 1.0, 0.05,
         ""},
        {"directional", "edge_weight", "Poids des bords", K::Number, 0.0, 1.0, 0.05, ""},
        {"directional", "underlay_inset_mm", "Retrait de la sous-couche", K::Number, 0.0, 5.0, 0.1,
         "mm"},
        {"directional", "underlay_spacing_mm", "Écart de la sous-couche", K::Number, 0.1, 10.0, 0.1,
         "mm"},
        {"directional", "underlay_zigzag", "Sous-couche en zigzag", K::Bool, 0, 1, 1, ""},
        {"directional", "sector_overlap_mm", "Recouvrement des secteurs", K::Number, 0.0, 1.0, 0.05,
         "mm"},
        {"directional", "edge_outline", "Contour de bordure", K::Bool, 0, 1, 1, ""},
        {"satin", "max_stitch_length_mm", "Longueur maximale d'un point", K::Number, 1.0, 15.0, 0.5,
         "mm"},
        {"satin", "max_width_hard_mm", "Largeur au-delà de laquelle la colonne est refusée",
         K::Number, 1.0, 60.0, 0.5, "mm"},

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

namespace {
// Minuscules sans accents pour la recherche : « sous-couche » trouve « Sous-couche », « ecart »
// trouve « Écart ». Couvre les lettres accentuées du français ; le reste est copié tel quel.
std::string foldForSearch(std::string_view text) {
    static const std::pair<std::string_view, char> kMap[] = {
        {"é", 'e'}, {"è", 'e'}, {"ê", 'e'}, {"ë", 'e'}, {"à", 'a'}, {"â", 'a'},
        {"î", 'i'}, {"ï", 'i'}, {"ô", 'o'}, {"ù", 'u'}, {"û", 'u'}, {"ç", 'c'},
        {"É", 'e'}, {"È", 'e'}, {"À", 'a'}, {"Ê", 'e'}, {"Ç", 'c'}};
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        bool mapped = false;
        for (const auto& [from, to] : kMap) {
            if (text.compare(i, from.size(), from) == 0) {
                out.push_back(to);
                i += from.size();
                mapped = true;
                break;
            }
        }
        if (!mapped) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(text[i]))));
            ++i;
        }
    }
    return out;
}
} // namespace

ParamLevel paramLevel(const ParamSpec& spec) {
    static constexpr std::string_view kAdvanced[] = {
        "underlay_inset_mm", "underlay_spacing_mm", "underlay_zigzag",      "hidden_underpath",
        "stagger",           "row_end_clip_mm",     "edge_outline",         "spacing_regularity",
        "edge_weight",       "sector_overlap_mm",   "max_stitch_length_mm", "max_width_hard_mm",
        "underlay_mode"};
    return std::find(std::begin(kAdvanced), std::end(kAdvanced), spec.key) != std::end(kAdvanced)
               ? ParamLevel::Advanced
               : ParamLevel::Essential;
}

bool paramMatches(const ParamSpec& spec, std::string_view query) {
    const std::string needle = foldForSearch(query);
    if (needle.empty()) {
        return true;
    }
    return foldForSearch(spec.label).find(needle) != std::string::npos ||
           foldForSearch(spec.key).find(needle) != std::string::npos;
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
        if (key == "stagger")
            return static_cast<double>(t->stagger);
        if (key == "row_end_clip_mm")
            return mm(t->row_end_clip);
        if (key == "edge_outline")
            return b2d(t->edge_outline);
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
        if (key == "stagger")
            return static_cast<double>(d->stagger);
        if (key == "spacing_regularity")
            return d->spacing_regularity;
        if (key == "edge_weight")
            return d->edge_weight;
        if (key == "underlay_inset_mm")
            return mm(d->underlay_inset);
        if (key == "underlay_spacing_mm")
            return mm(d->underlay_spacing);
        if (key == "underlay_zigzag")
            return b2d(d->underlay_zigzag);
        if (key == "sector_overlap_mm")
            return mm(d->sector_overlap);
        if (key == "edge_outline")
            return b2d(d->edge_outline);
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
        if (key == "max_stitch_length_mm")
            return mm(s->max_stitch_length);
        if (key == "max_width_hard_mm")
            return mm(s->max_width_hard);
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
