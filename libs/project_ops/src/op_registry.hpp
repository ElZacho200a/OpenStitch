// SPDX-License-Identifier: Apache-2.0
// En-tête INTERNE (jamais installé) : registre d'opérations partagé entre le dispatcher
// (`operations.cpp`) et les familles d'opérations (`op_params.cpp`, `op_geometry.cpp`).
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/project_ops/errors.hpp"

namespace openstitch::project_ops::detail {

// Argument d'une opération, pour le catalogue ET la validation de type du dispatcher.
struct ArgSpec {
    std::string name;
    std::string
        type; // "object_id" | "number" | "integer" | "string" | "boolean" | "array" | "object"
    std::string unit;        // "mm", "deg", "um", "" …
    std::string description; // une phrase
    bool required{false};
    std::string range; // texte libre « [0.1 ; 10] », "" = aucune
};

// Résultat d'un gestionnaire. `result` (objet JSON ou null) est copié dans le rapport ;
// `result["id"]` (entier) lie l'alias de l'opération (`"as"`).
struct OpStatus {
    bool ok{true};
    OpError error;
    nlohmann::json result; // null = aucun résultat
    [[nodiscard]] static OpStatus success(nlohmann::json r = nullptr) {
        OpStatus s;
        s.result = std::move(r);
        return s;
    }
    [[nodiscard]] static OpStatus failure(std::string code, std::string message,
                                          std::string hint = {}) {
        OpStatus s;
        s.ok = false;
        s.error = OpError{std::move(code), std::move(message), std::move(hint)};
        return s;
    }
};

// Contexte d'exécution. Le gestionnaire MUTE le document uniquement via
// `stack.execute(std::make_unique<...Command>(...), project)` (jamais en direct) ;
// `project` est la COPIE de travail de la transaction. `args` est l'objet de l'opération,
// alias `$x` déjà résolus en entiers par le dispatcher.
struct OpContext {
    document::Project& project;
    commands::UndoStack& stack;
    const nlohmann::json& args;
};

using OpHandler = std::function<OpStatus(OpContext&)>;

struct OpSpec {
    std::string name;    // snake_case, stable
    std::string summary; // une phrase
    std::vector<ArgSpec> args;
    std::string example; // JSON complet d'une opération valide (documentation + test)
    OpHandler apply;
};

// Chaque famille ajoute ses opérations à `out` ; `operations.cpp` appelle les deux et
// refuse un nom en double.
void register_param_ops(
    std::vector<OpSpec>& out); // op_params.cpp   : style, paramètres, ordre, projet
void register_import_ops(std::vector<OpSpec>& out); // op_import.cpp   : import SVG, lettrage
void register_geometry_ops(
    std::vector<OpSpec>& out); // op_geometry.cpp : forme, transformation, fusion, auto-satin
void register_semantic_ops(
    std::vector<OpSpec>& out); // op_semantic.cpp : import / remplacement sémantique
void register_group_ops(
    std::vector<OpSpec>& out); // op_group.cpp    : groupes, empilement, position/taille

// Aides communes (implémentées dans json_common.cpp).
// Lit un identifiant d'objet de broderie existant dans `ctx.args[key]` ; refus nommé sinon.
// Exécute une autre opération du registre dans le même contexte (même projet, même pile) :
// permet à une opération composée (ex. `semantic_replace`) de réutiliser `remove_object`,
// `auto_satin`… sans rien réimplémenter. Nom inconnu : refus `E_UNKNOWN_OP`.
[[nodiscard]] OpStatus run_nested_op(OpContext& parent, const std::string& name,
                                     const nlohmann::json& args);

// Image source d'un projet bâti sans image (import_svg, semantic_import) : lecture + calcul de
// l'échelle (op_import.cpp). Rien n'est modifié tant que `apply_source_image` n'est pas appelée ;
// celle-ci passe par une `ICommand` annulable et ne touche ni objets ni identifiants.
struct SourceImagePlan {
    image::Image image;
    double mm_per_px{0.0};
};
// `mm_per_px` absent : l'image remplit le cadre du projet sans déformation (calcul en µm entiers).
[[nodiscard]] OpStatus load_source_image(const document::Project& project, const std::string& path,
                                         std::optional<double> mm_per_px, SourceImagePlan& out);
[[nodiscard]] OpStatus apply_source_image(OpContext& ctx, SourceImagePlan&& plan);

[[nodiscard]] OpStatus require_embroidery(const OpContext& ctx, const char* key,
                                          const document::EmbroideryObject*& out);

} // namespace openstitch::project_ops::detail
