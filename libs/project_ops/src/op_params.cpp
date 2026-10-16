// SPDX-License-Identifier: Apache-2.0
// Opérations de style, de paramètres de couture, d'ordre et de projet.
//
// Chaque opération mute le document UNIQUEMENT par une `ICommand` empilée sur la pile locale du
// lot (jamais d'écriture directe). Les refus passent par `Refusal` (exception interne, jamais
// propagée : `guarded` la convertit en `OpStatus::failure`).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "op_registry.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/finishing_commands.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/optimization/order.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"
#include "openstitch/thread_palette/thread_library.hpp"

namespace openstitch::project_ops::detail {

namespace {

using json = nlohmann::json;

// --- Refus ---------------------------------------------------------------------------------

struct Refusal {
    std::string code;
    std::string message;
    std::string hint;
};

[[noreturn]] void refuse(const char* code, std::string message, std::string hint = {}) {
    throw Refusal{code, std::move(message), std::move(hint)};
}

OpHandler guarded(std::function<OpStatus(OpContext&)> fn) {
    return [fn = std::move(fn)](OpContext& ctx) -> OpStatus {
        try {
            return fn(ctx);
        } catch (const Refusal& r) {
            return OpStatus::failure(r.code, r.message, r.hint);
        }
    };
}

// --- Aides d'arguments ---------------------------------------------------------------------

std::string num(double v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

std::string join(const std::vector<std::string>& items, const char* sep = ", ") {
    std::string out;
    for (const auto& s : items) {
        if (!out.empty()) {
            out += sep;
        }
        out += s;
    }
    return out;
}

const document::EmbroideryObject& need_emb(OpContext& ctx, const char* key) {
    const document::EmbroideryObject* found = nullptr;
    const OpStatus s = require_embroidery(ctx, key, found);
    if (!s.ok) {
        throw Refusal{s.error.code, s.error.message, s.error.hint};
    }
    return *found;
}

std::string need_string(const json& args, const char* key) {
    const auto it = args.find(key);
    if (it == args.end() || !it->is_string()) {
        refuse(code::kUsage, std::string("argument « ") + key + " » obligatoire (chaîne)");
    }
    return it->get<std::string>();
}

bool need_bool(const json& args, const char* key) {
    const auto it = args.find(key);
    if (it == args.end() || !it->is_boolean()) {
        refuse(code::kUsage, std::string("argument « ") + key + " » obligatoire (booléen)");
    }
    return it->get<bool>();
}

template <class E> using EnumTable = std::vector<std::pair<std::string, E>>;

template <class E> std::vector<std::string> enum_names(const EnumTable<E>& table) {
    std::vector<std::string> names;
    for (const auto& [name, value] : table) {
        names.push_back(name);
    }
    return names;
}

template <class E>
E parse_enum(const EnumTable<E>& table, const std::string& text, const std::string& what) {
    for (const auto& [name, value] : table) {
        if (name == text) {
            return value;
        }
    }
    refuse(code::kOutOfRange, what + " : valeur « " + text + " » inconnue",
           "valeurs valides : " + join(enum_names(table)));
}

template <class E> std::string enum_name(const EnumTable<E>& table, E value) {
    for (const auto& [name, v] : table) {
        if (v == value) {
            return name;
        }
    }
    return "?";
}

const EnumTable<document::JoinMode>& join_table() {
    static const EnumTable<document::JoinMode> t{{"inherit", document::JoinMode::Inherit},
                                                 {"auto", document::JoinMode::Auto},
                                                 {"off", document::JoinMode::Off}};
    return t;
}
const EnumTable<document::ConnectorMode>& connector_table() {
    static const EnumTable<document::ConnectorMode> t{
        {"inherit", document::ConnectorMode::Inherit},
        {"always_trim", document::ConnectorMode::AlwaysTrim},
        {"never_trim", document::ConnectorMode::NeverTrim}};
    return t;
}
const EnumTable<document::LockStitch>& lock_stitch_table() {
    static const EnumTable<document::LockStitch> t{
        {"none", document::LockStitch::None},
        {"backforth", document::LockStitch::BackAndForth},
        {"triangle", document::LockStitch::Triangle},
        {"zigzag", document::LockStitch::MicroZigzag}};
    return t;
}
const EnumTable<document::SatinLock>& satin_lock_table() {
    static const EnumTable<document::SatinLock> t{{"none", document::SatinLock::None},
                                                  {"backforth", document::SatinLock::BackAndForth},
                                                  {"triangle", document::SatinLock::Triangle},
                                                  {"zigzag", document::SatinLock::MicroZigzag}};
    return t;
}
const EnumTable<document::SatinShortStitch>& short_stitch_table() {
    static const EnumTable<document::SatinShortStitch> t{
        {"disabled", document::SatinShortStitch::Disabled},
        {"remove_and_redistribute", document::SatinShortStitch::RemoveAndRedistribute},
        {"single_inset", document::SatinShortStitch::SingleInset},
        {"multi_level_inset", document::SatinShortStitch::MultiLevelInset}};
    return t;
}
const EnumTable<document::SatinSplit>& split_table() {
    static const EnumTable<document::SatinSplit> t{
        {"disabled", document::SatinSplit::Disabled},
        {"simple", document::SatinSplit::Simple},
        {"staggered", document::SatinSplit::Staggered},
        {"deterministic_jitter", document::SatinSplit::DeterministicJitter}};
    return t;
}
const EnumTable<document::SatinCap>& cap_table() {
    static const EnumTable<document::SatinCap> t{{"flat", document::SatinCap::Flat},
                                                 {"rounded", document::SatinCap::Rounded},
                                                 {"tapered", document::SatinCap::Tapered},
                                                 {"automatic", document::SatinCap::Automatic}};
    return t;
}
const EnumTable<document::UnderlayMode>& underlay_mode_table() {
    static const EnumTable<document::UnderlayMode> t{{"manual", document::UnderlayMode::Manual},
                                                     {"auto", document::UnderlayMode::Auto}};
    return t;
}
const EnumTable<document::AutoSatinStrategy>& auto_satin_strategy_table() {
    static const EnumTable<document::AutoSatinStrategy> t{
        {"skeleton", document::AutoSatinStrategy::Skeleton},
        {"parallel_sweep", document::AutoSatinStrategy::ParallelSweep},
        {"medial_axis", document::AutoSatinStrategy::MedialAxis}};
    return t;
}
const EnumTable<optimization::OrderStrategy>& strategy_table() {
    static const EnumTable<optimization::OrderStrategy> t{
        {"document", optimization::OrderStrategy::Document},
        {"by_color", optimization::OrderStrategy::ByColor},
        {"by_proximity", optimization::OrderStrategy::ByProximity},
        {"color_then_proximity", optimization::OrderStrategy::ColorThenProximity},
        {"layered", optimization::OrderStrategy::LayeredColorThenProximity}};
    return t;
}

ArgSpec arg(std::string name, std::string type, std::string description, bool required = false,
            std::string unit = {}, std::string range = {}) {
    ArgSpec a;
    a.name = std::move(name);
    a.type = std::move(type);
    a.description = std::move(description);
    a.required = required;
    a.unit = std::move(unit);
    a.range = std::move(range);
    return a;
}

std::string hex_color(const std::array<std::uint8_t, 3>& rgb) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string out = "#";
    for (const std::uint8_t c : rgb) {
        out += digits[c >> 4];
        out += digits[c & 0x0F];
    }
    return out;
}

std::array<std::uint8_t, 3> parse_hex_color(const std::string& text) {
    const auto digit = [&](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    bool ok = text.size() == 7 && text[0] == '#';
    std::array<std::uint8_t, 3> rgb{};
    for (std::size_t i = 0; ok && i < 3; ++i) {
        const int hi = digit(text[1 + 2 * i]);
        const int lo = digit(text[2 + 2 * i]);
        if (hi < 0 || lo < 0) {
            ok = false;
        } else {
            rgb[i] = static_cast<std::uint8_t>(hi * 16 + lo);
        }
    }
    if (!ok) {
        refuse(code::kOutOfRange, "rgb « " + text + " » invalide", "format attendu : #RRGGBB");
    }
    return rgb;
}

const char* stitch_type_name(const document::StitchParams& p) {
    static constexpr const char* names[] = {"running", "tatami", "satin", "directional",
                                            "auto_satin"};
    return names[p.index()];
}

json id_array(const std::vector<ObjectId>& ids) {
    json a = json::array();
    for (const ObjectId id : ids) {
        a.push_back(id.value);
    }
    return a;
}

std::vector<ObjectId> current_order(const document::Project& project) {
    std::vector<ObjectId> order;
    for (const auto& e : project.embroidery_objects) {
        order.push_back(e.id);
    }
    return order;
}

// --- Champs de paramètres (set_params / set_finishing) -------------------------------------

template <class P> struct Field {
    std::string name;
    std::string type; // type JSON attendu
    std::string unit;
    std::string range; // texte « [lo ; hi] » ou liste de valeurs
    std::function<void(P&, const json&)> set;
    std::function<json(const P&)> get;
};

double number_in_range(const std::string& name, const json& v, double lo, double hi,
                       const std::string& unit) {
    if (!v.is_number()) {
        refuse(code::kUsage, "champ « " + name + " » : nombre attendu");
    }
    const double d = v.get<double>();
    if (!(d >= lo && d <= hi)) {
        refuse(code::kOutOfRange, "champ « " + name + " » : " + num(d) + " hors plage [" + num(lo) +
                                      " ; " + num(hi) + "]" + (unit.empty() ? "" : " " + unit));
    }
    return d;
}

double round6(double v) {
    return std::round(v * 1e6) / 1e6;
}

template <class P, class Proj>
Field<P> mm_field(std::string name, Proj proj, double lo, double hi) {
    Field<P> f;
    f.name = name;
    f.type = "number";
    f.unit = "mm";
    f.range = "[" + num(lo) + " ; " + num(hi) + "]";
    f.set = [=](P& p, const json& v) {
        const double d = number_in_range(name, v, lo, hi, "mm");
        std::invoke(proj, p) = to_micrometers(Millimeters{d});
    };
    f.get = [=](const P& p) { return json(to_millimeters(std::invoke(proj, p)).value); };
    return f;
}

template <class P, class Proj>
Field<P> deg_field(std::string name, Proj proj, double lo, double hi) {
    Field<P> f;
    f.name = name;
    f.type = "number";
    f.unit = "deg";
    f.range = "[" + num(lo) + " ; " + num(hi) + "]";
    f.set = [=](P& p, const json& v) {
        const double d = number_in_range(name, v, lo, hi, "deg");
        std::invoke(proj, p) = Angle{d * std::numbers::pi / 180.0};
    };
    f.get = [=](const P& p) {
        return json(round6(std::invoke(proj, p).radians * 180.0 / std::numbers::pi));
    };
    return f;
}

// Angle facultatif en degrés : `null` l'efface (comportement automatique).
template <class P, class Proj>
Field<P> optional_deg_field(std::string name, Proj proj, double lo, double hi) {
    Field<P> f;
    f.name = name;
    f.type = "number|null";
    f.unit = "deg";
    f.range = "[" + num(lo) + " ; " + num(hi) + "] ou null (automatique)";
    f.set = [=](P& p, const json& v) {
        if (v.is_null()) {
            std::invoke(proj, p).reset();
            return;
        }
        const double d = number_in_range(name, v, lo, hi, "deg");
        std::invoke(proj, p) = Angle{d * std::numbers::pi / 180.0};
    };
    f.get = [=](const P& p) {
        const auto& a = std::invoke(proj, p);
        return a ? json(round6(a->radians * 180.0 / std::numbers::pi)) : json(nullptr);
    };
    return f;
}

template <class P, class Proj>
Field<P> ratio_field(std::string name, Proj proj, double lo, double hi) {
    Field<P> f;
    f.name = name;
    f.type = "number";
    f.range = "[" + num(lo) + " ; " + num(hi) + "]";
    f.set = [=](P& p, const json& v) {
        std::invoke(proj, p) = number_in_range(name, v, lo, hi, "");
    };
    f.get = [=](const P& p) { return json(std::invoke(proj, p)); };
    return f;
}

template <class P, class Proj>
Field<P> int_field(std::string name, Proj proj, long long lo, long long hi) {
    using T = std::remove_cvref_t<std::invoke_result_t<Proj, P&>>;
    Field<P> f;
    f.name = name;
    f.type = "integer";
    f.range = "[" + std::to_string(lo) + " ; " + std::to_string(hi) + "]";
    f.set = [=](P& p, const json& v) {
        if (!v.is_number_integer()) {
            refuse(code::kUsage, "champ « " + name + " » : entier attendu");
        }
        const long long d = v.get<long long>();
        if (d < lo || d > hi) {
            refuse(code::kOutOfRange, "champ « " + name + " » : " + std::to_string(d) +
                                          " hors plage [" + std::to_string(lo) + " ; " +
                                          std::to_string(hi) + "]");
        }
        std::invoke(proj, p) = static_cast<T>(d);
    };
    f.get = [=](const P& p) { return json(static_cast<long long>(std::invoke(proj, p))); };
    return f;
}

template <class P, class Proj> Field<P> bool_field(std::string name, Proj proj) {
    Field<P> f;
    f.name = name;
    f.type = "boolean";
    f.range = "true | false";
    f.set = [=](P& p, const json& v) {
        if (!v.is_boolean()) {
            refuse(code::kUsage, "champ « " + name + " » : booléen attendu");
        }
        std::invoke(proj, p) = v.get<bool>();
    };
    f.get = [=](const P& p) { return json(static_cast<bool>(std::invoke(proj, p))); };
    return f;
}

template <class P, class Proj, class E>
Field<P> enum_field(std::string name, Proj proj, const EnumTable<E>& table) {
    Field<P> f;
    f.name = name;
    f.type = "string";
    f.range = join(enum_names(table), " | ");
    f.set = [=](P& p, const json& v) {
        if (!v.is_string()) {
            refuse(code::kUsage, "champ « " + name + " » : chaîne attendue");
        }
        std::invoke(proj, p) = parse_enum(table, v.get<std::string>(), "champ « " + name + " »");
    };
    f.get = [=](const P& p) { return json(enum_name(table, std::invoke(proj, p))); };
    return f;
}

// Point optionnel exprimé en mm : [x, y] ou null (efface).
template <class P, class Proj> Field<P> point_field(std::string name, Proj proj) {
    Field<P> f;
    f.name = name;
    f.type = "array";
    f.unit = "mm";
    f.range = "[x, y] dans ±5000, ou null pour effacer";
    f.set = [=](P& p, const json& v) {
        if (v.is_null()) {
            std::invoke(proj, p).reset();
            return;
        }
        if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number()) {
            refuse(code::kUsage, "champ « " + name + " » : [x_mm, y_mm] ou null attendu");
        }
        const double x = number_in_range(name, v[0], -5000.0, 5000.0, "mm");
        const double y = number_in_range(name, v[1], -5000.0, 5000.0, "mm");
        std::invoke(proj, p) =
            Vec2um{to_micrometers(Millimeters{x}), to_micrometers(Millimeters{y})};
    };
    f.get = [=](const P& p) {
        const auto& pt = std::invoke(proj, p);
        if (!pt) {
            return json(nullptr);
        }
        return json::array({to_millimeters(pt->x).value, to_millimeters(pt->y).value});
    };
    return f;
}

// --- Géométrie des guides, ruptures, rails et dégradés (polylignes en mm) ---------------------
// Les chemins se lisent/écrivent en POLYLIGNES de sommets [x_mm, y_mm] (repère du modèle). Un
// chemin dont les sommets sont inchangés est conservé tel quel (tangentes de Bézier comprises) :
// relire l'inspection puis la réécrire ne dégrade jamais une courbe. Un chemin modifié devient
// une polyligne.

json polyline_json(const geometry::Path& path) {
    json out = json::array();
    for (const auto& n : path.nodes) {
        out.push_back(json::array({to_millimeters(n.pos.x).value, to_millimeters(n.pos.y).value}));
    }
    return out;
}

json point_json(Vec2um p) {
    return json::array({to_millimeters(p.x).value, to_millimeters(p.y).value});
}

Vec2um point_from_json(const std::string& name, const json& v) {
    if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number()) {
        refuse(code::kUsage, "champ « " + name + " » : point [x_mm, y_mm] attendu");
    }
    const double x = number_in_range(name, v[0], -5000.0, 5000.0, "mm");
    const double y = number_in_range(name, v[1], -5000.0, 5000.0, "mm");
    return Vec2um{to_micrometers(Millimeters{x}), to_micrometers(Millimeters{y})};
}

geometry::Path path_from_json(const std::string& name, const json& v,
                              const geometry::Path* existing, std::size_t minPoints) {
    if (!v.is_array() || v.size() < minPoints || v.size() > 2000) {
        refuse(code::kUsage, "champ « " + name + " » : polyligne [[x_mm,y_mm],...] de " +
                                 std::to_string(minPoints) + " à 2000 points attendue");
    }
    geometry::Path path;
    path.closed = false;
    for (const auto& p : v) {
        geometry::PathNode node;
        node.pos = point_from_json(name, p);
        path.nodes.push_back(node);
    }
    if (existing != nullptr && existing->nodes.size() == path.nodes.size()) {
        bool same = true;
        for (std::size_t i = 0; i < path.nodes.size(); ++i) {
            same = same && existing->nodes[i].pos == path.nodes[i].pos;
        }
        if (same) {
            return *existing;
        }
    }
    return path;
}

// std::vector<geometry::Path> (guides et lignes de rupture du remplissage directionnel).
template <class P, class Proj> Field<P> paths_field(std::string name, Proj proj) {
    Field<P> f;
    f.name = name;
    f.type = "array";
    f.unit = "mm";
    f.range = "liste (au plus 64) de polylignes [[x,y],...] d'au moins 2 points, [] pour effacer";
    f.set = [=](P& p, const json& v) {
        if (!v.is_array() || v.size() > 64) {
            refuse(code::kUsage, "champ « " + name + " » : liste de polylignes (64 au plus)");
        }
        auto& target = std::invoke(proj, p);
        std::vector<geometry::Path> out;
        for (std::size_t i = 0; i < v.size(); ++i) {
            out.push_back(path_from_json(name, v[i], i < target.size() ? &target[i] : nullptr, 2));
        }
        target = std::move(out);
    };
    f.get = [=](const P& p) {
        json out = json::array();
        for (const auto& path : std::invoke(proj, p)) {
            out.push_back(polyline_json(path));
        }
        return out;
    };
    return f;
}

// Un rail de satin (Path) : polyligne d'au moins 2 points.
template <class P, class Proj> Field<P> rail_field(std::string name, Proj proj) {
    Field<P> f;
    f.name = name;
    f.type = "array";
    f.unit = "mm";
    f.range = "polyligne [[x,y],...] de 2 à 2000 points";
    f.set = [=](P& p, const json& v) {
        auto& target = std::invoke(proj, p);
        target = path_from_json(name, v, &target, 2);
    };
    f.get = [=](const P& p) { return polyline_json(std::invoke(proj, p)); };
    return f;
}

// Barreaux d'un satin : [{a_mm:[x,y], b_mm:[x,y]}]. Un barreau inchangé garde son lien.
template <class P, class Proj> Field<P> rungs_field(std::string name, Proj proj) {
    Field<P> f;
    f.name = name;
    f.type = "array";
    f.unit = "mm";
    f.range = "liste (au plus 2000) de {a_mm:[x,y], b_mm:[x,y]}";
    f.set = [=](P& p, const json& v) {
        if (!v.is_array() || v.size() > 2000) {
            refuse(code::kUsage, "champ « " + name + " » : liste de {a_mm, b_mm} (2000 au plus)");
        }
        auto& target = std::invoke(proj, p);
        std::vector<document::SatinRung> out;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (!v[i].is_object() || !v[i].contains("a_mm") || !v[i].contains("b_mm")) {
                refuse(code::kUsage, "champ « " + name + " » : chaque barreau est {a_mm, b_mm}");
            }
            document::SatinRung rung;
            rung.a = point_from_json(name, v[i]["a_mm"]);
            rung.b = point_from_json(name, v[i]["b_mm"]);
            if (i < target.size() && target[i].a == rung.a && target[i].b == rung.b) {
                rung.link_id = target[i].link_id;
            }
            out.push_back(rung);
        }
        target = std::move(out);
    };
    f.get = [=](const P& p) {
        json out = json::array();
        for (const auto& r : std::invoke(proj, p)) {
            out.push_back({{"a_mm", point_json(r.a)}, {"b_mm", point_json(r.b)}});
        }
        return out;
    };
    return f;
}

// Guides d'orientation de l'auto-satin : [{anchor_mm:[x,y], angle_deg, absolute?}].
template <class P> Field<P> auto_satin_guides_field(std::string name) {
    Field<P> f;
    f.name = name;
    f.type = "array";
    f.unit = "mm / deg";
    f.range = "au plus 64 guides {anchor_mm:[x,y], angle_deg, absolute?}, [] pour effacer";
    f.set = [=](P& p, const json& v) {
        if (!v.is_array() || v.size() > 64) {
            refuse(code::kUsage, "champ « " + name + " » : liste de guides (64 au plus)");
        }
        std::vector<document::AutoSatinGuide> out;
        for (const auto& g : v) {
            if (!g.is_object() || !g.contains("anchor_mm") || !g.contains("angle_deg")) {
                refuse(code::kUsage, "champ « " + name + " » : guide {anchor_mm, angle_deg}");
            }
            document::AutoSatinGuide guide;
            guide.anchor = point_from_json(name, g["anchor_mm"]);
            const double a = number_in_range(name, g["angle_deg"], -360.0, 360.0, "deg");
            guide.angle = Angle{a * std::numbers::pi / 180.0};
            guide.absolute = g.value("absolute", false);
            out.push_back(guide);
        }
        p.guides = std::move(out);
    };
    f.get = [=](const P& p) {
        json out = json::array();
        for (const auto& g : p.guides) {
            out.push_back({{"anchor_mm", point_json(g.anchor)},
                           {"angle_deg", round6(g.angle.radians * 180.0 / std::numbers::pi)},
                           {"absolute", g.absolute}});
        }
        return out;
    };
    return f;
}

// Dégradé de densité du remplissage directionnel (ou null pour le retirer).
template <class P> Field<P> gradient_field(std::string name) {
    Field<P> f;
    f.name = name;
    f.type = "object";
    f.unit = "mm";
    f.range = "{from_mm:[x,y], to_mm:[x,y], spacing_from_mm, spacing_to_mm [0,1 ; 5], "
              "interpolate_density?} ou null";
    f.set = [=](P& p, const json& v) {
        if (v.is_null()) {
            p.density_gradient.reset();
            return;
        }
        if (!v.is_object()) {
            refuse(code::kUsage, "champ « " + name + " » : objet ou null attendu");
        }
        for (const char* key : {"from_mm", "to_mm", "spacing_from_mm", "spacing_to_mm"}) {
            if (!v.contains(key)) {
                refuse(code::kUsage, "champ « " + name + " » : « " + key + " » manquant");
            }
        }
        document::DensityGradient g;
        g.from = point_from_json(name, v["from_mm"]);
        g.to = point_from_json(name, v["to_mm"]);
        g.spacing_from = to_micrometers(
            Millimeters{number_in_range(name, v["spacing_from_mm"], 0.1, 5.0, "mm")});
        g.spacing_to =
            to_micrometers(Millimeters{number_in_range(name, v["spacing_to_mm"], 0.1, 5.0, "mm")});
        g.interpolate_density = v.value("interpolate_density", false);
        p.density_gradient = g;
    };
    f.get = [=](const P& p) {
        if (!p.density_gradient) {
            return json(nullptr);
        }
        const auto& g = *p.density_gradient;
        return json{{"from_mm", point_json(g.from)},
                    {"to_mm", point_json(g.to)},
                    {"spacing_from_mm", to_millimeters(g.spacing_from).value},
                    {"spacing_to_mm", to_millimeters(g.spacing_to).value},
                    {"interpolate_density", g.interpolate_density}};
    };
    return f;
}

template <class P> std::vector<std::string> field_names(const std::vector<Field<P>>& fields) {
    std::vector<std::string> names;
    for (const auto& f : fields) {
        names.push_back(f.name);
    }
    return names;
}

template <class P>
const Field<P>* find_field(const std::vector<Field<P>>& fields, const std::string& n) {
    for (const auto& f : fields) {
        if (f.name == n) {
            return &f;
        }
    }
    return nullptr;
}

// Champs communs au satin à rails et à l'auto-satin (mêmes noms, mêmes plages).
template <class P> void add_satin_common(std::vector<Field<P>>& v) {
    v.push_back(mm_field<P>("pull_compensation_mm", &P::pull_compensation, 0.0, 2.0));
    v.push_back(bool_field<P>("center_underlay", &P::center_underlay));
    v.push_back(bool_field<P>("underlay_edge", &P::underlay_edge));
    v.push_back(bool_field<P>("underlay_zigzag", &P::underlay_zigzag));
    v.push_back(mm_field<P>("pull_left_mm", &P::pull_left, 0.0, 3.0));
    v.push_back(mm_field<P>("pull_right_mm", &P::pull_right, 0.0, 3.0));
    v.push_back(mm_field<P>("push_start_mm", &P::push_start, 0.0, 3.0));
    v.push_back(mm_field<P>("push_end_mm", &P::push_end, 0.0, 3.0));
    v.push_back(enum_field<P>("cap_start", &P::cap_start, cap_table()));
    v.push_back(enum_field<P>("cap_end", &P::cap_end, cap_table()));
    v.push_back(enum_field<P>("lock_start", &P::lock_start, satin_lock_table()));
    v.push_back(enum_field<P>("lock_end", &P::lock_end, satin_lock_table()));
    v.push_back(mm_field<P>("lock_length_mm", &P::lock_length, 0.2, 3.0));
    v.push_back(int_field<P>("lock_passes", &P::lock_passes, 1, 4));
    v.push_back(point_field<P>("entry_point_mm", &P::entry_point));
    v.push_back(point_field<P>("exit_point_mm", &P::exit_point));
    v.push_back(enum_field<P>("underlay_mode", &P::underlay_mode, underlay_mode_table()));
    v.push_back(enum_field<P>("short_stitch", &P::short_stitch, short_stitch_table()));
    v.push_back(enum_field<P>("split_stitch", &P::split_stitch, split_table()));
}

template <class P> const std::vector<Field<P>>& fields_of();

template <>
const std::vector<Field<document::RunningStitchParams>>&
fields_of<document::RunningStitchParams>() {
    using P = document::RunningStitchParams;
    static const auto fields = [] {
        std::vector<Field<P>> v;
        v.push_back(mm_field<P>("stitch_length_mm", &P::stitch_length, 0.5, 20.0));
        v.push_back(mm_field<P>("min_length_mm", &P::min_length, 0.1, 20.0));
        v.push_back(int_field<P>("repeats", &P::repeats, 1, 3));
        return v;
    }();
    return fields;
}

template <> const std::vector<Field<document::TatamiParams>>& fields_of<document::TatamiParams>() {
    using P = document::TatamiParams;
    static const auto fields = [] {
        std::vector<Field<P>> v;
        v.push_back(deg_field<P>("angle_deg", &P::angle, -360.0, 360.0));
        v.push_back(mm_field<P>("row_spacing_mm", &P::row_spacing, 0.1, 5.0));
        v.push_back(mm_field<P>("stitch_length_mm", &P::stitch_length, 1.0, 10.0));
        v.push_back(mm_field<P>("inset_mm", &P::inset, 0.0, 5.0));
        v.push_back(int_field<P>("stagger", &P::stagger, 1, 8));
        v.push_back(bool_field<P>("underlay_edge", &P::underlay_edge));
        v.push_back(bool_field<P>("underlay_parallel", &P::underlay_parallel));
        v.push_back(mm_field<P>("underlay_inset_mm", &P::underlay_inset, 0.0, 5.0));
        v.push_back(mm_field<P>("underlay_spacing_mm", &P::underlay_spacing, 0.1, 10.0));
        v.push_back(bool_field<P>("hidden_underpath", &P::hidden_underpath));
        v.push_back(mm_field<P>("pull_compensation_mm", &P::pull_compensation, 0.0, 3.0));
        v.push_back(enum_field<P>("underlay_mode", &P::underlay_mode, underlay_mode_table()));
        v.push_back(point_field<P>("entry_point_mm", &P::entry_point));
        return v;
    }();
    return fields;
}

template <>
const std::vector<Field<document::DirectionalFillParams>>&
fields_of<document::DirectionalFillParams>() {
    using P = document::DirectionalFillParams;
    static const auto fields = [] {
        std::vector<Field<P>> v;
        v.push_back(mm_field<P>("row_spacing_mm", &P::row_spacing, 0.1, 5.0));
        v.push_back(ratio_field<P>("spacing_regularity", &P::spacing_regularity, 0.0, 1.0));
        v.push_back(mm_field<P>("stitch_length_mm", &P::stitch_length, 1.0, 7.0));
        v.push_back(ratio_field<P>("edge_weight", &P::edge_weight, 0.0, 1.0));
        v.push_back(mm_field<P>("inset_mm", &P::inset, 0.0, 5.0));
        v.push_back(int_field<P>("stagger", &P::stagger, 1, 8));
        v.push_back(bool_field<P>("underlay_edge", &P::underlay_edge));
        v.push_back(bool_field<P>("underlay_parallel", &P::underlay_parallel));
        v.push_back(mm_field<P>("underlay_inset_mm", &P::underlay_inset, 0.0, 5.0));
        v.push_back(mm_field<P>("underlay_spacing_mm", &P::underlay_spacing, 0.1, 10.0));
        v.push_back(bool_field<P>("hidden_underpath", &P::hidden_underpath));
        v.push_back(mm_field<P>("sector_overlap_mm", &P::sector_overlap, 0.0, 1.0));
        v.push_back(bool_field<P>("handmade", &P::handmade));
        v.push_back(int_field<P>("handmade_intensity", &P::handmade_intensity, 0, 100));
        v.push_back(int_field<P>("seed", &P::seed, 0, 4'294'967'295LL));
        v.push_back(enum_field<P>("underlay_mode", &P::underlay_mode, underlay_mode_table()));
        v.push_back(paths_field<P>("guides", &P::guides));
        v.push_back(paths_field<P>("break_lines", &P::break_lines));
        v.push_back(gradient_field<P>("density_gradient"));
        return v;
    }();
    return fields;
}

template <> const std::vector<Field<document::SatinParams>>& fields_of<document::SatinParams>() {
    using P = document::SatinParams;
    static const auto fields = [] {
        std::vector<Field<P>> v;
        v.push_back(mm_field<P>("density_mm", &P::density, 0.1, 2.0));
        v.push_back(mm_field<P>("max_width_mm", &P::max_width, 1.0, 60.0));
        v.push_back(mm_field<P>("max_width_hard_mm", &P::max_width_hard, 1.0, 60.0));
        v.push_back(mm_field<P>("max_stitch_length_mm", &P::max_stitch_length, 1.0, 15.0));
        v.push_back(rail_field<P>("rail_a", &P::rail_a));
        v.push_back(rail_field<P>("rail_b", &P::rail_b));
        v.push_back(rungs_field<P>("rungs", &P::rungs));
        add_satin_common<P>(v);
        return v;
    }();
    return fields;
}

template <>
const std::vector<Field<document::AutoSatinParams>>& fields_of<document::AutoSatinParams>() {
    using P = document::AutoSatinParams;
    static const auto fields = [] {
        std::vector<Field<P>> v;
        v.push_back(mm_field<P>("spacing_mm", &P::spacing, 0.1, 2.0));
        v.push_back(mm_field<P>("split_threshold_mm", &P::split_threshold, 1.0, 20.0));
        v.push_back(mm_field<P>("split_length_mm", &P::split_length, 0.5, 20.0));
        v.push_back(bool_field<P>("compact_axis", &P::compact_axis));
        v.push_back(enum_field<P>("strategy", &P::strategy, auto_satin_strategy_table()));
        v.push_back(optional_deg_field<P>("sweep_angle_deg", &P::sweep_angle, -360.0, 360.0));
        v.push_back(auto_satin_guides_field<P>("guides"));
        add_satin_common<P>(v);
        return v;
    }();
    return fields;
}

// Types de points (noms) qui possèdent le champ `name`.
std::vector<std::string> types_having(const std::string& name) {
    std::vector<std::string> out;
    if (find_field(fields_of<document::RunningStitchParams>(), name) != nullptr) {
        out.emplace_back("running");
    }
    if (find_field(fields_of<document::TatamiParams>(), name) != nullptr) {
        out.emplace_back("tatami");
    }
    if (find_field(fields_of<document::SatinParams>(), name) != nullptr) {
        out.emplace_back("satin");
    }
    if (find_field(fields_of<document::DirectionalFillParams>(), name) != nullptr) {
        out.emplace_back("directional");
    }
    if (find_field(fields_of<document::AutoSatinParams>(), name) != nullptr) {
        out.emplace_back("auto_satin");
    }
    return out;
}

std::string params_catalog_text() {
    std::string out = "objet des champs à modifier (mise à jour partielle) ; champs par type — ";
    out += "running : " + join(field_names(fields_of<document::RunningStitchParams>())) + " ; ";
    out += "tatami : " + join(field_names(fields_of<document::TatamiParams>())) + " ; ";
    out +=
        "directional : " + join(field_names(fields_of<document::DirectionalFillParams>())) + " ; ";
    out += "satin : " + join(field_names(fields_of<document::SatinParams>())) + " ; ";
    out += "auto_satin : " + join(field_names(fields_of<document::AutoSatinParams>())) + ". ";
    out += "Suffixe _mm = millimètres, _deg = degrés ; énumérations en chaîne";
    return out;
}

// Applique `req` (objet {champ: valeur}) à `target` ; renvoie {champ: {from, to}}.
template <class P>
json apply_fields(P& target, const json& req, const std::vector<Field<P>>& fields,
                  const std::string& type_label) {
    for (const auto& [key, value] : req.items()) {
        if (find_field(fields, key) == nullptr) {
            const auto owners = types_having(key);
            std::string hint =
                "champs valides pour « " + type_label + " » : " + join(field_names(fields));
            if (!owners.empty()) {
                hint += " ; « " + key + " » existe pour : " + join(owners) +
                        " (changez d'abord le type avec set_stitch_type)";
            }
            refuse(code::kWrongType,
                   "champ « " + key + " » inexistant pour le type de points « " + type_label + " »",
                   hint);
        }
    }
    json changes = json::object();
    for (const auto& [key, value] : req.items()) {
        const Field<P>& f = *find_field(fields, key);
        const json before = f.get(target);
        f.set(target, value);
        changes[key] = {{"from", before}, {"to", f.get(target)}};
    }
    return changes;
}

// --- Opérations : style ---------------------------------------------------------------------

OpSpec op_set_color() {
    OpSpec s;
    s.name = "set_color";
    s.summary = "Change la couleur de fil d'un objet : couleur libre (rgb) ou fil de nuancier "
                "(chart + code).";
    s.args = {arg("object", "object_id", "objet de broderie à recolorer", true),
              arg("rgb", "string", "couleur libre au format #RRGGBB (exclusif avec chart/code)",
                  false, {}, "#000000 .. #FFFFFF"),
              arg("chart", "string", "identifiant du nuancier (ex. generic) ; avec code"),
              arg("code", "string", "référence du fil dans le nuancier ; avec chart")};
    s.example = R"({"op":"set_color","object":2,"rgb":"#FF0000"})";
    s.apply = guarded([](OpContext& ctx) {
        const auto& emb = need_emb(ctx, "object");
        const ObjectId id = emb.id;
        const auto previous = emb.rgb;
        const bool has_rgb = ctx.args.contains("rgb");
        const bool has_chart = ctx.args.contains("chart");
        const bool has_code = ctx.args.contains("code");
        if (has_rgb == (has_chart || has_code)) {
            refuse(code::kUsage, "indiquez soit « rgb », soit « chart » + « code » (un seul mode)",
                   "exemples : {\"rgb\":\"#FF0000\"} ou {\"chart\":\"generic\",\"code\":\"...\"}");
        }
        std::array<std::uint8_t, 3> rgb{};
        std::optional<thread_palette::ThreadKey> key;
        std::string thread_name;
        if (has_rgb) {
            rgb = parse_hex_color(need_string(ctx.args, "rgb"));
        } else {
            if (!(has_chart && has_code)) {
                refuse(code::kUsage, "« chart » et « code » vont ensemble");
            }
            thread_palette::ThreadKey k{need_string(ctx.args, "chart"),
                                        need_string(ctx.args, "code")};
            static const thread_palette::ThreadLibrary library =
                thread_palette::ThreadLibrary::with_builtin();
            const auto thread = library.find(k);
            if (!thread) {
                std::vector<std::string> charts;
                for (const auto& c : library.charts()) {
                    charts.push_back(c.chart_id);
                }
                refuse(code::kRefused,
                       "fil « " + k.code + " » introuvable dans le nuancier « " + k.chart_id + " »",
                       "nuanciers intégrés : " + join(charts));
            }
            rgb = thread->rgb;
            thread_name = thread->name;
            key = std::move(k);
        }
        ctx.stack.execute(std::make_unique<commands::SetObjectThreadCommand>(
                              std::vector<ObjectId>{id}, key, rgb, std::string{}),
                          ctx.project);
        json r = {{"id", id.value}, {"rgb", hex_color(rgb)}, {"previous_rgb", hex_color(previous)}};
        if (key) {
            r["thread"] = {{"chart", key->chart_id}, {"code", key->code}, {"name", thread_name}};
        } else {
            r["thread"] = nullptr;
        }
        return OpStatus::success(std::move(r));
    });
    return s;
}

OpSpec op_rename() {
    OpSpec s;
    s.name = "rename";
    s.summary = "Renomme un objet de broderie.";
    s.args = {arg("object", "object_id", "objet de broderie", true),
              arg("name", "string", "nouveau nom (non vide)", true)};
    s.example = R"({"op":"rename","object":2,"name":"Fond bleu"})";
    s.apply = guarded([](OpContext& ctx) {
        const auto& emb = need_emb(ctx, "object");
        const ObjectId id = emb.id;
        const std::string previous = emb.name;
        const std::string name = need_string(ctx.args, "name");
        if (name.empty()) {
            refuse(code::kRefused, "le nom ne peut pas être vide");
        }
        ctx.stack.execute(std::make_unique<commands::RenameEmbroideryCommand>(id, name),
                          ctx.project);
        return OpStatus::success({{"id", id.value}, {"name", name}, {"previous_name", previous}});
    });
    return s;
}

OpSpec op_set_visible() {
    OpSpec s;
    s.name = "set_visible";
    s.summary = "Affiche ou masque un objet de broderie (un objet masqué n'est pas cousu).";
    s.args = {arg("object", "object_id", "objet de broderie", true),
              arg("visible", "boolean", "true = affiché, false = masqué", true)};
    s.example = R"({"op":"set_visible","object":2,"visible":false})";
    s.apply = guarded([](OpContext& ctx) {
        const auto& emb = need_emb(ctx, "object");
        const ObjectId id = emb.id;
        const bool previous = emb.visible;
        const bool visible = need_bool(ctx.args, "visible");
        ctx.stack.execute(std::make_unique<commands::SetEmbroideryVisibleCommand>(id, visible),
                          ctx.project);
        return OpStatus::success(
            {{"id", id.value}, {"visible", visible}, {"previous_visible", previous}});
    });
    return s;
}

OpSpec op_set_locked() {
    OpSpec s;
    s.name = "set_locked";
    s.summary = "Verrouille ou libère la position d'un objet (l'optimisation d'ordre ne déplace "
                "pas un objet verrouillé).";
    s.args = {arg("object", "object_id", "objet de broderie", true),
              arg("locked", "boolean", "true = position figée", true)};
    s.example = R"({"op":"set_locked","object":2,"locked":true})";
    s.apply = guarded([](OpContext& ctx) {
        const auto& emb = need_emb(ctx, "object");
        const ObjectId id = emb.id;
        const bool previous = emb.locked;
        const bool locked = need_bool(ctx.args, "locked");
        ctx.stack.execute(std::make_unique<commands::SetEmbroideryLockCommand>(id, locked),
                          ctx.project);
        return OpStatus::success(
            {{"id", id.value}, {"locked", locked}, {"previous_locked", previous}});
    });
    return s;
}

OpSpec op_set_join() {
    OpSpec s;
    s.name = "set_join";
    s.summary = "Entrée/sortie automatiques d'un objet : suit le projet, forcées ou désactivées.";
    s.args = {arg("object", "object_id", "objet de broderie", true),
              arg("mode", "string", "inherit | auto | off", true, {}, "inherit | auto | off")};
    s.example = R"({"op":"set_join","object":2,"mode":"auto"})";
    s.apply = guarded([](OpContext& ctx) {
        const auto& emb = need_emb(ctx, "object");
        const ObjectId id = emb.id;
        const auto previous = emb.join;
        const auto mode = parse_enum(join_table(), need_string(ctx.args, "mode"), "mode");
        ctx.stack.execute(std::make_unique<commands::SetEmbroideryJoinModeCommand>(id, mode),
                          ctx.project);
        return OpStatus::success({{"id", id.value},
                                  {"mode", enum_name(join_table(), mode)},
                                  {"previous_mode", enum_name(join_table(), previous)}});
    });
    return s;
}

OpSpec op_set_connector() {
    OpSpec s;
    s.name = "set_connector";
    s.summary = "Connecteur après l'objet : règles du projet, coupe forcée ou jamais de coupe.";
    s.args = {arg("object", "object_id", "objet de broderie", true),
              arg("mode", "string", "inherit | always_trim | never_trim", true, {},
                  "inherit | always_trim | never_trim")};
    s.example = R"({"op":"set_connector","object":2,"mode":"always_trim"})";
    s.apply = guarded([](OpContext& ctx) {
        const auto& emb = need_emb(ctx, "object");
        const ObjectId id = emb.id;
        const auto previous = emb.connector;
        const auto mode = parse_enum(connector_table(), need_string(ctx.args, "mode"), "mode");
        ctx.stack.execute(std::make_unique<commands::SetEmbroideryConnectorModeCommand>(id, mode),
                          ctx.project);
        return OpStatus::success({{"id", id.value},
                                  {"mode", enum_name(connector_table(), mode)},
                                  {"previous_mode", enum_name(connector_table(), previous)}});
    });
    return s;
}

// --- Opérations : type et paramètres de couture ----------------------------------------------

OpSpec op_set_stitch_type() {
    OpSpec s;
    s.name = "set_stitch_type";
    s.summary = "Change le type de points d'un objet : contour (running), tatami ou directionnel. "
                "Le satin passe par l'opération auto_satin.";
    s.args = {arg("object", "object_id", "objet de broderie", true),
              arg("type", "string", "running | tatami | directional", true, {},
                  "running | tatami | directional")};
    s.example = R"({"op":"set_stitch_type","object":2,"type":"running"})";
    s.apply = guarded([](OpContext& ctx) {
        const auto& emb = need_emb(ctx, "object");
        const ObjectId id = emb.id;
        const std::string type = need_string(ctx.args, "type");
        if (type == "satin" || type == "auto_satin") {
            refuse(code::kRefused, "le type « " + type + " » n'est pas disponible ici",
                   "utilisez l'opération auto_satin");
        }
        if (type != "running" && type != "tatami" && type != "directional") {
            refuse(code::kOutOfRange, "type de points « " + type + " » inconnu",
                   "valeurs valides : running, tatami, directional (satin : opération auto_satin)");
        }
        const std::string from = stitch_type_name(emb.params);
        if (from == type) {
            return OpStatus::success(
                {{"id", id.value}, {"from", from}, {"to", type}, {"changed", false}});
        }
        // Mêmes règles que `MainWindow::stitchParamsForType` (le bureau reste un simple câblage).
        document::StitchParams params;
        std::string label;
        if (type == "running") {
            document::RunningStitchParams rp;
            rp.repeats = 3;
            params = rp;
            label = "Type : contour";
        } else if (type == "tatami") {
            params = document::TatamiParams{};
            label = "Type : tatami";
        } else {
            const auto* source = ctx.project.findObject(emb.source_vector);
            if (source == nullptr || source->paths.empty()) {
                refuse(code::kRefused, "aucun contour source pour le remplissage directionnel");
            }
            document::TatamiParams base; // réglages du tatami repris s'il y a lieu
            if (const auto* tatami = std::get_if<document::TatamiParams>(&emb.params)) {
                base = *tatami;
            }
            params = stitch_generation::directional_from_tatami(
                base, source->paths, static_cast<std::uint32_t>(id.value));
            label = "Type : remplissage directionnel";
        }
        const std::size_t before = ctx.project.embroidery_objects.size();
        ctx.stack.execute(
            std::make_unique<commands::ConvertFillGroupCommand>(id, std::move(params), label),
            ctx.project);
        return OpStatus::success(
            {{"id", id.value},
             {"from", from},
             {"to", type},
             {"changed", true},
             {"removed_sections", before - ctx.project.embroidery_objects.size()}});
    });
    return s;
}

OpSpec op_directional_guides() {
    OpSpec s;
    s.name = "directional_guides";
    s.summary = "Remplissage directionnel guidé par la forme : convertit l'objet (tatami ou "
                "directionnel) et REMPLACE ses guides par ceux de l'axe médian de sa forme (même "
                "algorithme que « Générer un guide depuis la forme » du bureau). Les lignes de "
                "rupture sont conservées.";
    s.args = {arg("object", "object_id", "objet de broderie (tatami ou directionnel)", true)};
    s.example = R"({"op":"directional_guides","object":28})";
    s.apply = guarded([](OpContext& ctx) {
        const auto& emb = need_emb(ctx, "object");
        const ObjectId id = emb.id;
        if (!(emb.is_tatami() || emb.is_directional())) {
            refuse(code::kRefused,
                   "seul un tatami ou un remplissage directionnel reçoit des guides",
                   "convertir d'abord avec set_stitch_type");
        }
        const auto* source = ctx.project.findObject(emb.source_vector);
        if (source == nullptr || source->paths.empty()) {
            refuse(code::kRefused, "aucun contour source pour le remplissage directionnel");
        }
        std::vector<geometry::Path> guides;
        for (const auto& set : source->paths) {
            auto g = stitch_generation::directional_guides_from_region(set);
            guides.insert(guides.end(), std::make_move_iterator(g.begin()),
                          std::make_move_iterator(g.end()));
        }
        if (guides.empty()) {
            refuse(code::kRefused, "aucun axe exploitable pour cette forme",
                   "forme trop compacte ou trop étroite : garder le guide par défaut");
        }
        const bool wasDirectional = emb.is_directional();
        document::DirectionalFillParams params;
        if (const auto* d = std::get_if<document::DirectionalFillParams>(&emb.params)) {
            params = *d;
        } else {
            document::TatamiParams base;
            if (const auto* t = std::get_if<document::TatamiParams>(&emb.params)) {
                base = *t;
            }
            params = stitch_generation::directional_from_tatami(
                base, source->paths, static_cast<std::uint32_t>(id.value));
        }
        params.guides = std::move(guides);
        const std::size_t count = params.guides.size();
        if (wasDirectional) {
            ctx.stack.execute(std::make_unique<commands::EditDirectionalFillCommand>(
                                  id, std::move(params), "Générer un guide de direction"),
                              ctx.project);
        } else {
            ctx.stack.execute(std::make_unique<commands::ConvertFillGroupCommand>(
                                  id, document::StitchParams{std::move(params)},
                                  "Type : remplissage directionnel"),
                              ctx.project);
        }
        return OpStatus::success(
            {{"id", id.value}, {"guides", count}, {"converted", !wasDirectional}});
    });
    return s;
}

OpSpec op_set_params() {
    OpSpec s;
    s.name = "set_params";
    s.summary = "Modifie PARTIELLEMENT les paramètres de couture d'un objet (un seul pas "
                "d'annulation). Aucun champ n'est ignoré : champ inexistant pour le type = refus.";
    s.args = {arg("object", "object_id", "objet de broderie", true),
              arg("params", "object", params_catalog_text(), true)};
    s.example = R"({"op":"set_params","object":2,"params":{"row_spacing_mm":0.45,)"
                R"("angle_deg":30,"underlay_edge":true}})";
    s.apply = guarded([](OpContext& ctx) {
        const auto& emb = need_emb(ctx, "object");
        const ObjectId id = emb.id;
        const json& req = ctx.args.at("params");
        if (!req.is_object() || req.empty()) {
            refuse(code::kUsage, "« params » doit être un objet non vide {champ: valeur}");
        }
        document::StitchParams updated = emb.params;
        const std::string type = stitch_type_name(emb.params);
        json changes;
        std::visit(
            [&](auto& p) {
                using P = std::remove_cvref_t<decltype(p)>;
                changes = apply_fields<P>(p, req, fields_of<P>(), type);
                if constexpr (std::is_same_v<P, document::AutoSatinParams>) {
                    if (p.split_length > p.split_threshold) {
                        refuse(code::kOutOfRange,
                               "split_length_mm (" + num(to_millimeters(p.split_length).value) +
                                   ") ne peut pas dépasser split_threshold_mm (" +
                                   num(to_millimeters(p.split_threshold).value) + ")");
                    }
                }
            },
            updated);
        std::vector<std::string> names;
        for (const auto& [key, value] : req.items()) {
            names.push_back(key);
        }
        ctx.stack.execute(
            std::make_unique<commands::SetStitchParamsCommand>(id, std::move(updated), join(names)),
            ctx.project);
        return OpStatus::success({{"id", id.value}, {"type", type}, {"changed", changes}});
    });
    return s;
}

// --- Opérations : ordre ------------------------------------------------------------------------

OpSpec op_set_order() {
    OpSpec s;
    s.name = "set_order";
    s.summary = "Déplace un objet dans l'ordre de couture : position 0-based, avant/après un "
                "autre objet, ou au début/à la fin.";
    s.args = {arg("object", "object_id", "objet de broderie à déplacer", true),
              arg("position", "integer", "index final 0-based dans l'ordre de couture", false, {},
                  "[0 ; nombre d'objets - 1]"),
              arg("before", "object_id", "placer juste avant cet objet"),
              arg("after", "object_id", "placer juste après cet objet"),
              arg("to_front", "boolean", "true : en premier"),
              arg("to_back", "boolean", "true : en dernier")};
    s.example = R"({"op":"set_order","object":2,"to_back":true})";
    s.apply = guarded([](OpContext& ctx) {
        const auto& emb = need_emb(ctx, "object");
        const ObjectId id = emb.id;
        const char* modes[] = {"position", "before", "after", "to_front", "to_back"};
        std::vector<std::string> given;
        for (const char* m : modes) {
            if (ctx.args.contains(m)) {
                given.emplace_back(m);
            }
        }
        if (given.size() != 1) {
            refuse(code::kUsage,
                   "indiquez exactement un de : position, before, after, to_front, to_back",
                   "reçu : " + (given.empty() ? std::string("aucun") : join(given)));
        }
        const std::vector<ObjectId> order = current_order(ctx.project);
        std::vector<ObjectId> rest;
        std::size_t from = 0;
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == id) {
                from = i;
            } else {
                rest.push_back(order[i]);
            }
        }
        const auto index_in_rest = [&](ObjectId ref) {
            return static_cast<std::size_t>(std::find(rest.begin(), rest.end(), ref) -
                                            rest.begin());
        };
        std::size_t to = 0;
        const std::string mode = given.front();
        if (mode == "position") {
            const long long p = ctx.args.at("position").get<long long>();
            if (p < 0 || p >= static_cast<long long>(order.size())) {
                refuse(code::kOutOfRange, "position " + std::to_string(p) + " hors plage [0 ; " +
                                              std::to_string(order.size() - 1) + "]");
            }
            to = static_cast<std::size_t>(p);
        } else if (mode == "before" || mode == "after") {
            const auto& ref = need_emb(ctx, mode.c_str());
            if (ref.id == id) {
                refuse(code::kRefused, "« " + mode + " » désigne l'objet déplacé lui-même");
            }
            to = index_in_rest(ref.id) + (mode == "after" ? 1U : 0U);
        } else {
            if (!ctx.args.at(mode).get<bool>()) {
                refuse(code::kUsage, "« " + mode + " » doit valoir true");
            }
            to = mode == "to_front" ? 0 : rest.size();
        }
        std::vector<ObjectId> next = rest;
        next.insert(next.begin() + static_cast<std::ptrdiff_t>(to), id);
        const bool changed = next != order;
        if (changed) {
            ctx.stack.execute(std::make_unique<commands::ReorderEmbroideryCommand>(next),
                              ctx.project);
        }
        return OpStatus::success({{"id", id.value},
                                  {"from_position", from},
                                  {"to_position", to},
                                  {"changed", changed},
                                  {"order_before", id_array(order)},
                                  {"order_after", id_array(next)}});
    });
    return s;
}

// Centre représentatif d'un objet : même règle que le bureau (`MainWindow::embroideryCentroid`,
// non réutilisable : méthode d'une fenêtre Qt) -- satin : moyenne des nœuds des rails ; autres :
// moyenne des nœuds du contour extérieur du premier morceau de l'objet vectoriel source.
Vec2um embroidery_centroid(const document::Project& project,
                           const document::EmbroideryObject& obj) {
    std::int64_t sx = 0;
    std::int64_t sy = 0;
    std::int64_t n = 0;
    const auto accumulate = [&](const geometry::Path& path) {
        for (const auto& node : path.nodes) {
            sx += node.pos.x.value;
            sy += node.pos.y.value;
            ++n;
        }
    };
    if (const auto* satin = std::get_if<document::SatinParams>(&obj.params)) {
        accumulate(satin->rail_a);
        accumulate(satin->rail_b);
    } else if (const auto* vec = project.findObject(obj.source_vector);
               vec != nullptr && !vec->paths.empty()) {
        accumulate(vec->paths.front().outer);
    }
    if (n == 0) {
        return {};
    }
    return Vec2um{Micrometers{static_cast<std::int32_t>(sx / n)},
                  Micrometers{static_cast<std::int32_t>(sy / n)}};
}

std::vector<optimization::OrderItem> order_items(const document::Project& project) {
    std::vector<optimization::OrderItem> items;
    for (const auto& obj : project.embroidery_objects) {
        optimization::OrderItem item;
        item.id = obj.id;
        item.rgb = obj.rgb;
        item.centroid = embroidery_centroid(project, obj);
        item.locked = obj.locked;
        if (const auto* vec =
                project.findObject(obj.source_vector)) { // aire : stratégie « layered »
            for (const auto& set : vec->paths) {
                item.area_mm2 += geometry::path_set_area_um2(set) / 1e6;
            }
        }
        items.push_back(item);
    }
    return items;
}

double round1(double v) {
    return std::round(v * 10.0) / 10.0;
}

OpSpec op_optimize_order() {
    OpSpec s;
    s.name = "optimize_order";
    s.summary = "Réordonne les objets pour réduire changements de fil et déplacements (les objets "
                "verrouillés ne bougent pas).";
    s.args = {arg("strategy", "string",
                  "document | by_color | by_proximity | color_then_proximity | layered", true, {},
                  "document | by_color | by_proximity | color_then_proximity | layered")};
    s.example = R"({"op":"optimize_order","strategy":"color_then_proximity"})";
    s.apply = guarded([](OpContext& ctx) {
        const std::string name = need_string(ctx.args, "strategy");
        const auto strategy = parse_enum(strategy_table(), name, "strategy");
        const auto items_before = order_items(ctx.project);
        const auto cost_before = optimization::compute_cost(items_before);
        const std::vector<ObjectId> before = current_order(ctx.project);
        const std::vector<ObjectId> next = optimization::optimize_order(items_before, strategy);
        const bool changed = !next.empty() && next != before;
        if (changed) {
            ctx.stack.execute(std::make_unique<commands::ReorderEmbroideryCommand>(next),
                              ctx.project);
        }
        const auto cost_after = optimization::compute_cost(order_items(ctx.project));
        return OpStatus::success({{"strategy", name},
                                  {"changed", changed},
                                  {"order_before", id_array(before)},
                                  {"order_after", id_array(current_order(ctx.project))},
                                  {"travel_mm_before", round1(cost_before.travel_um / 1000.0)},
                                  {"travel_mm_after", round1(cost_after.travel_um / 1000.0)},
                                  {"color_changes_before", cost_before.color_changes},
                                  {"color_changes_after", cost_after.color_changes}});
    });
    return s;
}

// --- Opérations : projet ---------------------------------------------------------------------

OpSpec op_remove_object() {
    OpSpec s;
    s.name = "remove_object";
    s.summary = "Supprime un objet de broderie. La forme vectorielle source est conservée (comme "
                "le bureau), sauf avec with_source=true et si plus aucun objet ne l'utilise.";
    s.args = {
        arg("object", "object_id", "objet de broderie à supprimer", true),
        arg("with_source", "boolean",
            "true : supprime aussi la forme source si elle n'est plus utilisée (défaut false)")};
    s.example = R"({"op":"remove_object","object":4})";
    s.apply = guarded([](OpContext& ctx) {
        const auto& emb = need_emb(ctx, "object");
        const ObjectId id = emb.id;
        const ObjectId source = emb.source_vector;
        bool with_source = false;
        if (const auto it = ctx.args.find("with_source"); it != ctx.args.end()) {
            with_source = need_bool(ctx.args, "with_source");
        }
        const auto* vec = ctx.project.findObject(source);
        const bool used_by_others = std::any_of(ctx.project.embroidery_objects.begin(),
                                                ctx.project.embroidery_objects.end(),
                                                [&](const document::EmbroideryObject& e) {
                                                    return e.id != id && e.source_vector == source;
                                                });
        std::string kept_reason;
        bool remove_source = false;
        if (vec != nullptr) {
            if (used_by_others) {
                kept_reason = "utilisée par d'autres objets de broderie";
            } else if (vec->text_owner) {
                kept_reason = "lettre d'un objet texte";
            } else if (with_source || !vec->visible) { // proxy invisible (satin manuel) : emporté
                remove_source = true;
            }
        }
        auto group = std::make_unique<commands::CompositeCommand>("Supprimer l'objet de broderie");
        group->add(std::make_unique<commands::RemoveEmbroideryObjectCommand>(id));
        if (remove_source) {
            group->add(std::make_unique<commands::RemoveVectorObjectCommand>(source));
        }
        ctx.stack.execute(std::move(group), ctx.project);
        json r = {
            {"id", id.value}, {"source_vector", source.value}, {"source_removed", remove_source}};
        if (with_source && !remove_source && !kept_reason.empty()) {
            r["source_kept_reason"] = kept_reason;
        }
        return OpStatus::success(std::move(r));
    });
    return s;
}

OpSpec op_discard_overrides() {
    OpSpec s;
    s.name = "discard_overrides";
    s.summary =
        "Abandonne les retouches manuelles de points d'un objet (retour aux points générés).";
    s.args = {arg("object", "object_id", "objet de broderie", true)};
    s.example = R"({"op":"discard_overrides","object":2})";
    s.apply = guarded([](OpContext& ctx) {
        const auto& emb = need_emb(ctx, "object");
        const ObjectId id = emb.id;
        const std::size_t count = emb.overrides.size();
        if (count > 0) {
            ctx.stack.execute(std::make_unique<commands::DiscardOverridesCommand>(id), ctx.project);
        }
        return OpStatus::success({{"id", id.value}, {"discarded", count}});
    });
    return s;
}

json canvas_json(const document::Canvas& c) {
    return {{"width_mm", to_millimeters(c.width).value},
            {"height_mm", to_millimeters(c.height).value},
            {"margin_mm", to_millimeters(c.margin).value}};
}

OpSpec op_set_canvas() {
    OpSpec s;
    s.name = "set_canvas";
    s.summary = "Définit le cadre de broderie (largeur, hauteur, marge de sécurité). Au moins un "
                "argument ; les absents gardent leur valeur.";
    s.args = {arg("width_mm", "number", "largeur du cadre", false, "mm", "]0 ; 2000]"),
              arg("height_mm", "number", "hauteur du cadre", false, "mm", "]0 ; 2000]"),
              arg("margin_mm", "number", "marge de sécurité sur chaque côté", false, "mm",
                  "[0 ; 500], 2 x marge < min(largeur, hauteur)")};
    s.example = R"({"op":"set_canvas","width_mm":160,"height_mm":260,"margin_mm":5})";
    s.apply = guarded([](OpContext& ctx) {
        if (!ctx.args.contains("width_mm") && !ctx.args.contains("height_mm") &&
            !ctx.args.contains("margin_mm")) {
            refuse(code::kUsage, "indiquez au moins un de : width_mm, height_mm, margin_mm");
        }
        const document::Canvas before = ctx.project.canvas;
        document::Canvas next = before;
        const auto dimension = [&](const char* key, Micrometers& target) {
            if (const auto it = ctx.args.find(key); it != ctx.args.end()) {
                const double d = it->get<double>();
                if (!(d > 0.0 && d <= 2000.0)) {
                    refuse(code::kOutOfRange,
                           std::string("« ") + key + " » : " + num(d) + " hors plage ]0 ; 2000] mm",
                           "la dimension du cadre doit être strictement positive");
                }
                target = to_micrometers(Millimeters{d});
                if (target.value <= 0) {
                    refuse(code::kOutOfRange,
                           std::string("« ") + key + " » : " + num(d) + " mm arrondi à 0 µm");
                }
            }
        };
        dimension("width_mm", next.width);
        dimension("height_mm", next.height);
        if (const auto it = ctx.args.find("margin_mm"); it != ctx.args.end()) {
            const double d = it->get<double>();
            if (!(d >= 0.0 && d <= 500.0)) {
                refuse(code::kOutOfRange, "« margin_mm » : " + num(d) + " hors plage [0 ; 500] mm");
            }
            next.margin = to_micrometers(Millimeters{d});
        }
        if (2 * static_cast<std::int64_t>(next.margin.value) >=
            std::min(next.width.value, next.height.value)) {
            refuse(code::kOutOfRange,
                   "la marge (" + num(to_millimeters(next.margin).value) +
                       " mm de chaque côté) ne laisse aucune zone brodable",
                   "réduisez margin_mm ou agrandissez le cadre");
        }
        ctx.stack.execute(std::make_unique<commands::SetCanvasCommand>(next), ctx.project);
        return OpStatus::success({{"before", canvas_json(before)}, {"after", canvas_json(next)}});
    });
    return s;
}

const std::vector<Field<document::SequenceFinishing>>& finishing_fields() {
    using P = document::SequenceFinishing;
    static const auto fields = [] {
        std::vector<Field<P>> v;
        v.push_back(bool_field<P>("enabled", &P::enabled));
        v.push_back(mm_field<P>("trim_threshold_mm", &P::trim_threshold, 0.5, 100.0));
        v.push_back(bool_field<P>("trim_before_color_change", &P::trim_before_color_change));
        v.push_back(enum_field<P>("lock", &P::lock_type, lock_stitch_table()));
        v.push_back(mm_field<P>("lock_length_mm", &P::lock_length, 0.2, 3.0));
        v.push_back(int_field<P>("lock_passes", &P::lock_passes, 1, 4));
        v.push_back(bool_field<P>("filter_short_stitches", &P::filter_short_stitches));
        v.push_back(mm_field<P>("min_stitch_length_mm", &P::min_stitch_length, 0.1, 2.0));
        v.push_back(bool_field<P>("split_long_stitches", &P::split_long_stitches));
        v.push_back(mm_field<P>("max_stitch_length_mm", &P::max_stitch_length, 1.0, 12.1));
        v.push_back(bool_field<P>("auto_join", &P::auto_join));
        const auto ht = [](auto member) {
            return [member](auto& f) -> auto& { return f.hidden_travel.*member; };
        };
        using H = document::HiddenTravel;
        v.push_back(bool_field<P>("hidden_travel", ht(&H::enabled)));
        v.push_back(mm_field<P>("hidden_travel_max_jump_mm", ht(&H::max_hidden_jump), 0.0, 20.0));
        v.push_back(
            mm_field<P>("hidden_travel_max_bridge_mm", ht(&H::max_bridge_length), 0.0, 100.0));
        v.push_back(
            mm_field<P>("hidden_travel_bridge_stitch_mm", ht(&H::bridge_stitch_length), 0.5, 7.0));
        v.push_back(mm_field<P>("hidden_travel_cover_thread_width_mm", ht(&H::cover_thread_width),
                                0.1, 2.0));
        v.push_back(mm_field<P>("hidden_travel_cover_margin_mm", ht(&H::cover_margin), 0.0, 2.0));
        v.push_back(bool_field<P>("hidden_travel_between_objects", ht(&H::between_objects)));
        v.push_back(bool_field<P>("hidden_travel_within_object", ht(&H::within_object)));
        v.push_back(bool_field<P>("hidden_travel_reorder_underlay", ht(&H::reorder_underlay)));
        v.push_back(bool_field<P>("hidden_travel_reorder_regions", ht(&H::reorder_regions)));
        v.push_back(
            bool_field<P>("hidden_travel_same_color_surface", ht(&H::allow_same_color_surface)));
        return v;
    }();
    return fields;
}

OpSpec op_set_finishing() {
    OpSpec s;
    s.name = "set_finishing";
    s.summary = "Modifie les finitions de la séquence du projet (coupes, points d'arrêt, points "
                "courts, ponts cachés). Mise à jour partielle : au moins un argument.";
    for (const auto& f : finishing_fields()) {
        s.args.push_back(arg(f.name, f.type,
                             "réglage de finition « " + f.name + " »" +
                                 (f.unit.empty() ? "" : " (" + f.unit + ")"),
                             false, f.unit, f.range));
    }
    s.example = R"({"op":"set_finishing","trim_threshold_mm":4,"lock":"triangle",)"
                R"("hidden_travel":true})";
    s.apply = guarded([](OpContext& ctx) {
        json req = json::object();
        for (const auto& [key, value] : ctx.args.items()) {
            if (key != "op") {
                req[key] = value;
            }
        }
        if (req.empty()) {
            refuse(code::kUsage, "indiquez au moins un réglage à modifier",
                   "réglages : " + join(field_names(finishing_fields())));
        }
        document::SequenceFinishing next = ctx.project.finishing;
        const json changes =
            apply_fields<document::SequenceFinishing>(next, req, finishing_fields(), "finition");
        ctx.stack.execute(std::make_unique<commands::SetFinishingCommand>(next), ctx.project);
        return OpStatus::success({{"changed", changes}});
    });
    return s;
}

} // namespace

void register_param_ops(std::vector<OpSpec>& out) {
    out.push_back(op_set_color());
    out.push_back(op_rename());
    out.push_back(op_set_visible());
    out.push_back(op_set_locked());
    out.push_back(op_set_join());
    out.push_back(op_set_connector());
    out.push_back(op_set_stitch_type());
    out.push_back(op_directional_guides());
    out.push_back(op_set_params());
    out.push_back(op_set_order());
    out.push_back(op_optimize_order());
    out.push_back(op_remove_object());
    out.push_back(op_discard_overrides());
    out.push_back(op_set_canvas());
    out.push_back(op_set_finishing());
}

} // namespace openstitch::project_ops::detail
