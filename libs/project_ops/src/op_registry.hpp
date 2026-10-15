// SPDX-License-Identifier: Apache-2.0
// En-tête INTERNE (jamais installé) : registre d'opérations partagé entre le dispatcher
// (`operations.cpp`) et les familles d'opérations (`op_params.cpp`, `op_geometry.cpp`).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_ops/errors.hpp"

namespace openstitch::project_ops::detail {

// Argument d'une opération, pour le catalogue ET la validation de type du dispatcher.
struct ArgSpec {
    std::string name;
    std::string type;        // "object_id" | "number" | "integer" | "string" | "boolean" | "array" | "object"
    std::string unit;        // "mm", "deg", "um", "" …
    std::string description; // une phrase
    bool required{false};
    std::string range;       // texte libre « [0.1 ; 10] », "" = aucune
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
void register_param_ops(std::vector<OpSpec>& out);    // op_params.cpp   : style, paramètres, ordre, projet
void register_geometry_ops(std::vector<OpSpec>& out); // op_geometry.cpp : forme, transformation, fusion, auto-satin

// Aides communes (implémentées dans json_common.cpp).
// Lit un identifiant d'objet de broderie existant dans `ctx.args[key]` ; refus nommé sinon.
[[nodiscard]] OpStatus require_embroidery(const OpContext& ctx, const char* key,
                                          const document::EmbroideryObject*& out);

} // namespace openstitch::project_ops::detail
