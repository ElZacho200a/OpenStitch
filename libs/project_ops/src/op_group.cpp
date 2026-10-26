// SPDX-License-Identifier: Apache-2.0
// Opérations de groupes et d'empilement (HP-OBJ-009, HP-OBJ-011, HP-OBJ-015) : grouper,
// dégrouper, avant/arrière-plan, position et taille numériques. Aucune géométrie ici : tout est
// calculé par libs/commands et appliqué par une `ICommand` sur la pile.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "op_registry.hpp"
#include "openstitch/commands/group_commands.hpp"
#include "openstitch/commands/order_ops.hpp"
#include "openstitch/commands/placement_ops.hpp"
#include "openstitch/commands/project_commands.hpp"

namespace openstitch::project_ops::detail {

namespace {

using json = nlohmann::json;

ArgSpec arg(std::string name, std::string type, std::string description, bool required = false,
            std::string unit = {}, std::string range = {}) {
    return ArgSpec{std::move(name),        std::move(type), std::move(unit),
                   std::move(description), required,        std::move(range)};
}

// Objets de broderie nommés par `objects` -> formes sources distinctes, dans l'ordre donné.
// Renvoie un refus nommé si la liste est absente, vide ou contient un identifiant inconnu.
OpStatus shapes_of_objects(const OpContext& ctx, std::vector<ObjectId>& embroidery,
                           std::vector<ObjectId>& shapes) {
    const auto it = ctx.args.find("objects");
    if (it == ctx.args.end() || !it->is_array() || it->empty()) {
        return OpStatus::failure(code::kUsage,
                                 "argument « objects » obligatoire (tableau d'objets de broderie)");
    }
    for (const auto& item : *it) {
        if (!item.is_number_integer()) {
            return OpStatus::failure(code::kUsage,
                                     "« objects » ne contient que des identifiants entiers");
        }
        const ObjectId id{item.get<std::uint64_t>()};
        const auto* emb = ctx.project.findEmbroidery(id);
        if (emb == nullptr) {
            return OpStatus::failure(code::kUnknownObject,
                                     "objet de broderie " + std::to_string(id.value) + " inconnu");
        }
        embroidery.push_back(id);
        if (std::find(shapes.begin(), shapes.end(), emb->source_vector) == shapes.end()) {
            shapes.push_back(emb->source_vector);
        }
    }
    return OpStatus::success();
}

OpSpec op_group() {
    OpSpec s;
    s.name = "group";
    s.summary = "Réunit des objets en un groupe (sélection et transformation d'un seul tenant ; "
                "l'ordre de couture ne change pas). Un objet déjà groupé amène son groupe.";
    s.args = {arg("objects", "array", "objets de broderie à grouper (au moins deux formes)", true),
              arg("name", "string", "nom du groupe (défaut « Groupe N »)")};
    s.example = R"({"op":"group","objects":[2,3],"name":"Fleur"})";
    s.apply = [](OpContext& ctx) -> OpStatus {
        std::vector<ObjectId> embroidery;
        std::vector<ObjectId> shapes;
        if (auto st = shapes_of_objects(ctx, embroidery, shapes); !st.ok) {
            return st;
        }
        const ObjectId id = ctx.project.object_ids.next();
        std::string name = ctx.args.value("name", std::string{});
        if (name.empty()) {
            name = "Groupe " + std::to_string(ctx.project.groups.size() + 1);
        }
        auto cmd = std::make_unique<commands::GroupShapesCommand>(id, name, shapes);
        const auto* raw = cmd.get();
        ctx.stack.execute(std::move(cmd), ctx.project);
        if (!raw->applied()) {
            return OpStatus::failure(code::kRefused, "un groupe réunit au moins deux éléments");
        }
        return OpStatus::success({{"id", id.value}, {"name", name}, {"shapes", shapes.size()}});
    };
    return s;
}

OpSpec op_ungroup() {
    OpSpec s;
    s.name = "ungroup";
    s.summary = "Dissout un groupe : ses formes et sous-groupes passent au groupe parent.";
    s.args = {arg("group", "object_id", "identifiant du groupe", true)};
    s.example = R"({"op":"ungroup","group":9})";
    s.apply = [](OpContext& ctx) -> OpStatus {
        const auto it = ctx.args.find("group");
        if (it == ctx.args.end() || !it->is_number_integer()) {
            return OpStatus::failure(code::kUsage, "argument « group » obligatoire (entier)");
        }
        const ObjectId id{it->get<std::uint64_t>()};
        if (ctx.project.findGroup(id) == nullptr) {
            return OpStatus::failure(code::kUnknownObject,
                                     "groupe " + std::to_string(id.value) + " inconnu");
        }
        ctx.stack.execute(std::make_unique<commands::UngroupCommand>(id), ctx.project);
        return OpStatus::success({{"id", id.value}});
    };
    return s;
}

OpSpec op_stack_order() {
    OpSpec s;
    s.name = "stack_order";
    s.summary = "Premier plan (cousu en dernier), arrière-plan (cousu en premier), avancer ou "
                "reculer d'un cran un ou plusieurs objets.";
    s.args = {arg("objects", "array", "objets de broderie à déplacer", true),
              arg("move", "string", "front | back | forward | backward", true, {},
                  "front | back | forward | backward")};
    s.example = R"({"op":"stack_order","objects":[2],"move":"front"})";
    s.apply = [](OpContext& ctx) -> OpStatus {
        std::vector<ObjectId> embroidery;
        std::vector<ObjectId> shapes;
        if (auto st = shapes_of_objects(ctx, embroidery, shapes); !st.ok) {
            return st;
        }
        const std::string move = ctx.args.value("move", std::string{});
        commands::StackMove m{};
        if (move == "front") {
            m = commands::StackMove::ToFront;
        } else if (move == "back") {
            m = commands::StackMove::ToBack;
        } else if (move == "forward") {
            m = commands::StackMove::Forward;
        } else if (move == "backward") {
            m = commands::StackMove::Backward;
        } else {
            return OpStatus::failure(code::kOutOfRange, "move : valeur « " + move + " » inconnue",
                                     "valeurs valides : front, back, forward, backward");
        }
        std::vector<ObjectId> order;
        for (const auto& e : ctx.project.embroidery_objects) {
            order.push_back(e.id);
        }
        const std::vector<ObjectId> next = commands::stacked_order(order, embroidery, m);
        if (next != order) {
            ctx.stack.execute(std::make_unique<commands::ReorderEmbroideryCommand>(next),
                              ctx.project);
        }
        return OpStatus::success({{"moved", embroidery.size()}, {"changed", next != order}});
    };
    return s;
}

OpSpec op_place() {
    OpSpec s;
    s.name = "place";
    s.summary = "Position (centre de la boîte englobante), taille et rotation numériques d'une "
                "sélection ; un champ absent reste inchangé.";
    s.args = {
        arg("objects", "array", "objets de broderie visés", true),
        arg("x", "number", "abscisse du centre", false, "mm"),
        arg("y", "number", "ordonnée du centre (Y vers le haut)", false, "mm"),
        arg("width", "number", "largeur de la boîte", false, "mm", "> 0"),
        arg("height", "number", "hauteur de la boîte", false, "mm", "> 0"),
        arg("rotate", "number", "rotation relative autour du centre (anti-horaire)", false, "deg")};
    s.example = R"({"op":"place","objects":[2],"x":0,"y":10,"width":30})";
    s.apply = [](OpContext& ctx) -> OpStatus {
        std::vector<ObjectId> embroidery;
        std::vector<ObjectId> shapes;
        if (auto st = shapes_of_objects(ctx, embroidery, shapes); !st.ok) {
            return st;
        }
        const auto mm = [&](const char* key) -> std::optional<Micrometers> {
            const auto it = ctx.args.find(key);
            if (it == ctx.args.end() || !it->is_number()) {
                return std::nullopt;
            }
            return Micrometers{static_cast<std::int32_t>(std::lround(it->get<double>() * 1000.0))};
        };
        const auto width = mm("width");
        const auto height = mm("height");
        if ((width && width->value <= 0) || (height && height->value <= 0)) {
            return OpStatus::failure(code::kOutOfRange, "width et height doivent être > 0");
        }
        bool changed = false;
        if (auto cmd = commands::make_place_command(ctx.project, shapes, mm("x"), mm("y"), width,
                                                    height, "Position et taille")) {
            ctx.stack.execute(std::move(cmd), ctx.project);
            changed = true;
        }
        const double degrees = ctx.args.value("rotate", 0.0);
        if (auto cmd = commands::make_rotate_about_center_command(ctx.project, shapes, degrees,
                                                                  "Rotation")) {
            ctx.stack.execute(std::move(cmd), ctx.project);
            changed = true;
        }
        const auto geometry = commands::selection_geometry(ctx.project, shapes);
        json r{{"changed", changed}};
        if (geometry) {
            r["x_mm"] = geometry->center.x.value / 1000.0;
            r["y_mm"] = geometry->center.y.value / 1000.0;
            r["width_mm"] = geometry->width.value / 1000.0;
            r["height_mm"] = geometry->height.value / 1000.0;
        }
        return OpStatus::success(std::move(r));
    };
    return s;
}

} // namespace

void register_group_ops(std::vector<OpSpec>& out) {
    out.push_back(op_group());
    out.push_back(op_ungroup());
    out.push_back(op_stack_order());
    out.push_back(op_place());
}

} // namespace openstitch::project_ops::detail
