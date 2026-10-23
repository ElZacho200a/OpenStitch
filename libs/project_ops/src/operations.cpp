// SPDX-License-Identifier: Apache-2.0
// Dispatcher transactionnel des opérations (lots JSON `openstitch.batch/1`).
//
// Principe : tout le lot est VALIDÉ (JSON, noms d'opérations, arguments, alias) avant la
// moindre application ; les opérations sont ensuite jouées une à une sur une COPIE du projet,
// chacune par des `ICommand` empilées sur une `UndoStack` locale. Au premier refus la copie est
// abandonnée : le projet de l'appelant n'est jamais touché à moitié.
#include "openstitch/project_ops/operations.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include <nlohmann/json.hpp>

#include "op_registry.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/project_ops/errors.hpp"

namespace openstitch::project_ops {

namespace {

using json = nlohmann::json;
using ojson = nlohmann::ordered_json;
using detail::ArgSpec;
using detail::OpContext;
using detail::OpSpec;
using detail::OpStatus;

// --- Registre ------------------------------------------------------------------------------

struct Registry {
    std::vector<OpSpec> ops; // trié par nom (ordre stable du catalogue)
    std::string error;       // non vide : défaut interne (nom d'opération en double)
};

const Registry& registry() {
    static const Registry reg = [] {
        Registry r;
        detail::register_param_ops(r.ops);
        detail::register_geometry_ops(r.ops);
        detail::register_import_ops(r.ops);
        detail::register_semantic_ops(r.ops);
        std::stable_sort(r.ops.begin(), r.ops.end(),
                         [](const OpSpec& a, const OpSpec& b) { return a.name < b.name; });
        for (std::size_t i = 1; i < r.ops.size(); ++i) {
            if (r.ops[i].name == r.ops[i - 1].name) {
                r.error = "opération « " + r.ops[i].name +
                          " » déclarée par deux familles d'opérations (défaut interne)";
                break;
            }
        }
        return r;
    }();
    return reg;
}

const OpSpec* find_op(const std::string& name) {
    for (const auto& op : registry().ops) {
        if (op.name == name) {
            return &op;
        }
    }
    return nullptr;
}

std::string join_names(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& n : names) {
        if (!out.empty()) {
            out += ", ";
        }
        out += n;
    }
    return out;
}

// --- Rapports ------------------------------------------------------------------------------

BatchReport global_error(const char* code, std::string message, bool dry_run,
                         std::size_t operation_count = 0) {
    BatchReport r;
    r.dry_run = dry_run;
    r.operation_count = operation_count;
    r.error_code = code;
    r.error_message = std::move(message);
    return r;
}

OperationOutcome failed_outcome(std::size_t index, std::string op, const char* code,
                                std::string message, std::string hint = {}) {
    OperationOutcome o;
    o.index = index;
    o.op = std::move(op);
    o.ok = false;
    o.code = code;
    o.message = std::move(message);
    o.hint = std::move(hint);
    return o;
}

BatchReport op_failure(OperationOutcome outcome, std::vector<OperationOutcome> done, bool dry_run,
                       std::size_t count) {
    BatchReport r;
    r.dry_run = dry_run;
    r.operation_count = count;
    r.outcomes = std::move(done);
    r.outcomes.push_back(std::move(outcome));
    return r;
}

// --- Validation ----------------------------------------------------------------------------

bool is_alias_text(const json& v) {
    return v.is_string() && !v.get_ref<const std::string&>().empty() &&
           v.get_ref<const std::string&>().front() == '$';
}

bool valid_alias_name(const std::string& name) {
    if (name.empty() || (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_')) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
}

const char* json_kind(const json& v) {
    if (v.is_null()) {
        return "null";
    }
    if (v.is_boolean()) {
        return "boolean";
    }
    if (v.is_number_integer()) {
        return "integer";
    }
    if (v.is_number()) {
        return "number";
    }
    if (v.is_string()) {
        return "string";
    }
    if (v.is_array()) {
        return "array";
    }
    return "object";
}

bool type_matches(const json& v, const std::string& type) {
    if (type == "object_id") {
        return v.is_number_unsigned() || is_alias_text(v);
    }
    if (type == "number") {
        return v.is_number();
    }
    if (type == "integer") {
        return v.is_number_integer();
    }
    if (type == "string") {
        return v.is_string();
    }
    if (type == "boolean") {
        return v.is_boolean();
    }
    if (type == "array") {
        return v.is_array();
    }
    if (type == "object") {
        return v.is_object();
    }
    return false;
}

// Alias `$nom` rencontrés dans une valeur (objet_id) ou les éléments d'un tableau.
void collect_aliases(const json& v, std::vector<std::string>& out) {
    if (is_alias_text(v)) {
        out.push_back(v.get<std::string>().substr(1));
    } else if (v.is_array()) {
        for (const auto& e : v) {
            collect_aliases(e, out);
        }
    }
}

struct Prepared {
    const OpSpec* spec{nullptr};
    json args; // objet de l'opération sans « as »
    std::optional<std::string> alias;
};

// Valide une opération ; renvoie false et remplit `out` au premier défaut.
bool validate_operation(std::size_t index, const json& raw, const std::set<std::string>& defined,
                        Prepared& prepared, OperationOutcome& out) {
    if (!raw.is_object()) {
        out = failed_outcome(index, {}, code::kSchema,
                             std::string("l'opération doit être un objet JSON (reçu : ") +
                                 json_kind(raw) + ")",
                             "forme attendue : {\"op\":\"nom\", ...arguments}");
        return false;
    }
    const auto op_it = raw.find("op");
    if (op_it == raw.end() || !op_it->is_string()) {
        out = failed_outcome(index, {}, code::kSchema,
                             "champ « op » (nom de l'opération, chaîne) obligatoire",
                             "voir `project ops --json` pour le catalogue des opérations");
        return false;
    }
    const std::string op_name = op_it->get<std::string>();
    const OpSpec* spec = find_op(op_name);
    if (spec == nullptr) {
        std::vector<std::string> names;
        for (const auto& s : registry().ops) {
            names.push_back(s.name);
        }
        out = failed_outcome(index, op_name, code::kUnknownOp,
                             "opération « " + op_name + " » inconnue",
                             "opérations disponibles : " + join_names(names));
        return false;
    }
    std::set<std::string> valid{"op", "as"};
    for (const auto& a : spec->args) {
        valid.insert(a.name);
    }
    const auto valid_list = [&] {
        std::vector<std::string> v;
        for (const auto& a : spec->args) {
            v.push_back(a.name);
        }
        v.push_back("as");
        return join_names(v);
    };
    for (const auto& [key, value] : raw.items()) {
        if (valid.count(key) == 0) {
            out = failed_outcome(index, op_name, code::kUsage,
                                 "argument inconnu « " + key + " » pour l'opération « " + op_name +
                                     " »",
                                 "arguments valides : " + valid_list());
            return false;
        }
    }
    for (const auto& a : spec->args) {
        const auto it = raw.find(a.name);
        if (it == raw.end()) {
            if (a.required) {
                out = failed_outcome(index, op_name, code::kUsage,
                                     "argument « " + a.name + " » obligatoire pour « " + op_name +
                                         " »",
                                     a.description);
                return false;
            }
            continue;
        }
        if (!type_matches(*it, a.type)) {
            out = failed_outcome(index, op_name, code::kUsage,
                                 "argument « " + a.name + " » : type " + a.type +
                                     " attendu, reçu " + json_kind(*it),
                                 a.description);
            return false;
        }
        std::vector<std::string> used;
        if (a.type == "object_id" || a.type == "array") {
            collect_aliases(*it, used);
        }
        for (const auto& name : used) {
            if (defined.count(name) == 0) {
                out = failed_outcome(index, op_name, code::kUnknownAlias,
                                     "alias « $" + name + " » jamais défini avant cette opération",
                                     "définissez-le avec \"as\":\"" + name +
                                         "\" sur une opération antérieure du lot");
                return false;
            }
        }
    }
    prepared.spec = spec;
    prepared.args = raw;
    prepared.args.erase("as");
    prepared.alias.reset();
    if (const auto as_it = raw.find("as"); as_it != raw.end()) {
        if (!as_it->is_string() || !valid_alias_name(as_it->get<std::string>())) {
            out = failed_outcome(index, op_name, code::kUsage,
                                 "« as » doit être un nom d'alias (lettres, chiffres, _ ; "
                                 "ne commence pas par un chiffre)",
                                 "exemple : \"as\":\"copie\" puis \"object\":\"$copie\"");
            return false;
        }
        const std::string alias = as_it->get<std::string>();
        if (defined.count(alias) != 0) {
            out = failed_outcome(index, op_name, code::kUsage,
                                 "alias « " + alias + " » déjà défini dans ce lot",
                                 "choisissez un nom d'alias distinct par opération");
            return false;
        }
        prepared.alias = alias;
    }
    return true;
}

// Remplace les « $alias » par leur identifiant (arguments object_id et éléments de tableaux).
void resolve_aliases(json& v, const std::map<std::string, std::uint64_t>& aliases) {
    if (is_alias_text(v)) {
        v = aliases.at(v.get<std::string>().substr(1));
    } else if (v.is_array()) {
        for (auto& e : v) {
            resolve_aliases(e, aliases);
        }
    }
}

// --- Exécution -----------------------------------------------------------------------------

BatchReport run(document::Project& project, std::string_view text, const BatchOptions& options,
                bool single) {
    const bool dry = options.dry_run;
    if (!registry().error.empty()) {
        return global_error(code::kInternal, registry().error, dry);
    }

    json doc;
    try {
        doc = json::parse(text);
    } catch (const json::parse_error& e) {
        return global_error(code::kSchema, std::string("JSON illisible : ") + e.what(), dry);
    }

    std::vector<json> raw_ops;
    if (doc.is_object() && doc.contains("operations")) {
        for (const auto& [key, value] : doc.items()) {
            if (key != "schema" && key != "operations") {
                return global_error(
                    code::kSchema,
                    "clé « " + key + " » inconnue dans le lot (clés valides : schema, operations)",
                    dry);
            }
        }
        const auto schema = doc.find("schema");
        if (schema == doc.end() || !schema->is_string()) {
            return global_error(
                code::kSchema,
                std::string("champ « schema » obligatoire : \"") + kBatchSchema + "\"", dry);
        }
        if (schema->get<std::string>() != kBatchSchema) {
            return global_error(code::kSchema,
                                "schéma « " + schema->get<std::string>() +
                                    " » non reconnu (attendu : " + kBatchSchema + ")",
                                dry);
        }
        const auto& ops = doc.at("operations");
        if (!ops.is_array()) {
            return global_error(code::kSchema, "« operations » doit être un tableau", dry);
        }
        if (ops.empty()) {
            return global_error(code::kSchema, "le lot ne contient aucune opération", dry);
        }
        raw_ops.assign(ops.begin(), ops.end());
    } else if (single && doc.is_object() && doc.contains("op")) {
        raw_ops.push_back(doc);
    } else {
        return global_error(code::kSchema,
                            single ? std::string("attendu : une opération {\"op\":...} ou un lot "
                                                 "{\"schema\":\"") +
                                         kBatchSchema + "\",\"operations\":[...]}"
                                   : std::string("attendu : un lot {\"schema\":\"") + kBatchSchema +
                                         "\",\"operations\":[...]}",
                            dry);
    }
    const std::size_t count = raw_ops.size();

    // 1) Validation COMPLÈTE avant toute application.
    std::vector<Prepared> prepared(count);
    std::set<std::string> defined;
    for (std::size_t i = 0; i < count; ++i) {
        OperationOutcome failure;
        if (!validate_operation(i, raw_ops[i], defined, prepared[i], failure)) {
            return op_failure(std::move(failure), {}, dry, count);
        }
        if (prepared[i].alias) {
            defined.insert(*prepared[i].alias);
        }
    }

    // 2) Application sur une copie, un gestionnaire à la fois.
    document::Project work = project;
    commands::UndoStack stack;
    stack.setMergeWindow(std::chrono::milliseconds{0}); // jamais de fusion d'annulation en lot
    std::map<std::string, std::uint64_t> aliases;
    std::vector<OperationOutcome> done;
    for (std::size_t i = 0; i < count; ++i) {
        const OpSpec& spec = *prepared[i].spec;
        json args = prepared[i].args;
        for (const auto& a : spec.args) {
            if (a.type == "object_id" || a.type == "array") {
                if (const auto it = args.find(a.name); it != args.end()) {
                    resolve_aliases(*it, aliases);
                }
            }
        }
        OpStatus status;
        try {
            OpContext ctx{work, stack, args};
            status = spec.apply(ctx);
        } catch (const std::exception& e) {
            status =
                OpStatus::failure(code::kInternal, std::string("exception interne : ") + e.what());
        } catch (...) {
            status = OpStatus::failure(code::kInternal, "exception interne inconnue");
        }
        if (status.ok && prepared[i].alias) {
            const bool has_id = status.result.is_object() && status.result.contains("id") &&
                                status.result.at("id").is_number_unsigned();
            if (has_id) {
                aliases[*prepared[i].alias] = status.result.at("id").get<std::uint64_t>();
            } else {
                status =
                    OpStatus::failure(code::kUsage,
                                      "l'opération « " + spec.name +
                                          " » ne produit pas d'identifiant : « as » "
                                          "est inutilisable ici",
                                      "retirez \"as\" ou utilisez une opération qui crée un objet");
            }
        }
        if (!status.ok) {
            return op_failure(failed_outcome(i, spec.name, status.error.code.c_str(),
                                             status.error.message, status.error.hint),
                              std::move(done), dry, count);
        }
        OperationOutcome ok;
        ok.index = i;
        ok.op = spec.name;
        ok.ok = true;
        if (!status.result.is_null()) {
            ok.result_json = status.result.dump(-1, ' ', false, json::error_handler_t::replace);
        }
        done.push_back(std::move(ok));
    }

    // 3) Succès : l'état final est publié en une fois (jamais en dry-run).
    BatchReport r;
    r.ok = true;
    r.dry_run = dry;
    r.operation_count = count;
    r.outcomes = std::move(done);
    if (!dry) {
        project = std::move(work);
        r.applied = true;
    }
    return r;
}

} // namespace

namespace detail {

OpStatus run_nested_op(OpContext& parent, const std::string& name, const nlohmann::json& args) {
    const OpSpec* spec = find_op(name);
    if (spec == nullptr) {
        return OpStatus::failure(code::kUnknownOp, "opération « " + name + " » inconnue");
    }
    OpContext nested{parent.project, parent.stack, args};
    try {
        return spec->apply(nested);
    } catch (const std::exception& e) {
        return OpStatus::failure(code::kInternal, std::string("défaut interne : ") + e.what());
    }
}

} // namespace detail

// --- API publique --------------------------------------------------------------------------

std::string BatchReport::to_json() const {
    ojson out;
    out["schema"] = kBatchReportSchema;
    out["ok"] = ok;
    out["applied"] = applied;
    out["dry_run"] = dry_run;
    out["operation_count"] = operation_count;
    if (error_code.empty()) {
        out["error"] = nullptr;
    } else {
        ojson e;
        e["code"] = error_code;
        e["message"] = error_message;
        out["error"] = std::move(e);
    }
    ojson list = ojson::array();
    for (const auto& o : outcomes) {
        ojson item;
        item["index"] = o.index;
        item["op"] = o.op;
        item["ok"] = o.ok;
        if (o.result_json.empty()) {
            item["result"] = nullptr;
        } else {
            item["result"] = ojson::parse(o.result_json);
        }
        if (o.ok) {
            item["error"] = nullptr;
        } else {
            ojson e;
            e["code"] = o.code;
            e["message"] = o.message;
            e["hint"] = o.hint;
            item["error"] = std::move(e);
        }
        list.push_back(std::move(item));
    }
    out["outcomes"] = std::move(list);
    return out.dump(2, ' ', false, ojson::error_handler_t::replace);
}

BatchReport apply_batch(document::Project& project, std::string_view batch_json,
                        const BatchOptions& options) {
    return run(project, batch_json, options, false);
}

BatchReport apply_operation(document::Project& project, std::string_view op_json,
                            const BatchOptions& options) {
    return run(project, op_json, options, true);
}

std::string operations_catalog_json() {
    ojson out;
    out["schema"] = kCatalogSchema;
    out["batch_schema"] = kBatchSchema;
    out["report_schema"] = kBatchReportSchema;
    ojson common = ojson::array();
    {
        ojson as;
        as["name"] = "as";
        as["type"] = "string";
        as["description"] = "alias lié à l'identifiant produit par l'opération (result.id) ; les "
                            "opérations suivantes du lot écrivent \"$alias\" à la place d'un "
                            "identifiant d'objet";
        as["required"] = false;
        common.push_back(std::move(as));
    }
    out["common_args"] = std::move(common);
    ojson ops = ojson::array();
    if (!registry().error.empty()) {
        out["error"] = registry().error;
    }
    for (const auto& spec : registry().ops) {
        ojson o;
        o["name"] = spec.name;
        o["summary"] = spec.summary;
        ojson args = ojson::array();
        for (const auto& a : spec.args) {
            ojson j;
            j["name"] = a.name;
            j["type"] = a.type;
            if (!a.unit.empty()) {
                j["unit"] = a.unit;
            }
            j["required"] = a.required;
            if (!a.range.empty()) {
                j["range"] = a.range;
            }
            j["description"] = a.description;
            args.push_back(std::move(j));
        }
        o["args"] = std::move(args);
        const ojson example = ojson::parse(spec.example, nullptr, false);
        if (example.is_discarded()) {
            o["example"] = nullptr;
            o["example_invalid"] = true;
        } else {
            o["example"] = example;
        }
        ops.push_back(std::move(o));
    }
    out["operations"] = std::move(ops);
    return out.dump(2, ' ', false, ojson::error_handler_t::replace);
}

} // namespace openstitch::project_ops
