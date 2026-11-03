// SPDX-License-Identifier: Apache-2.0
// Validateur JSON Schema minimal (HP-QA-018) pour les sorties `--json` d'openstitch-cli.
//
// Usage :
//   json_schema_check <dossier_schemas> <cle|-> <fichier.json>
//       Valide le document. Le schéma est choisi par le champ « schema » du document
//       (« openstitch.batch/1 » -> openstitch.batch.1.schema.json) ou, à défaut, par <cle>
//       (<cle>.schema.json). Un schéma introuvable est un échec : toute sortie --json doit
//       être décrite.
//   json_schema_check --selftest
//
// Sous-ensemble supporté : type, enum, const, properties, required, additionalProperties
// (booléen ou schéma), items, minItems, minimum, maximum, anyOf, oneOf, $ref local
// (#/$defs/...). Les mots-clés d'annotation sont ignorés ; tout autre mot-clé est refusé
// pour ne pas valider « dans le vide ». Aucune dépendance au-delà de nlohmann/json.
#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using nlohmann::json;

const std::set<std::string> kAnnotations = {"$schema",     "$id",   "$comment", "title",
                                            "description", "$defs", "default",  "examples"};
const std::set<std::string> kKeywords = {
    "type",  "enum",     "const",   "properties", "required", "additionalProperties",
    "items", "minItems", "minimum", "maximum",    "anyOf",    "oneOf",
    "$ref"};

struct Validator {
    const json& root;
    std::vector<std::string> errors;

    void fail(const std::string& path, const std::string& message) {
        errors.push_back((path.empty() ? "$" : path) + " : " + message);
    }

    static bool type_matches(const json& value, const std::string& type) {
        if (type == "object") {
            return value.is_object();
        }
        if (type == "array") {
            return value.is_array();
        }
        if (type == "string") {
            return value.is_string();
        }
        if (type == "boolean") {
            return value.is_boolean();
        }
        if (type == "null") {
            return value.is_null();
        }
        if (type == "integer") {
            return value.is_number_integer();
        }
        if (type == "number") {
            return value.is_number();
        }
        return false;
    }

    const json* resolve(const std::string& ref) {
        if (ref.rfind("#/", 0) != 0) {
            return nullptr;
        }
        const json* node = &root;
        std::stringstream ss(ref.substr(2));
        std::string part;
        while (std::getline(ss, part, '/')) {
            if (!node->is_object() || !node->contains(part)) {
                return nullptr;
            }
            node = &(*node)[part];
        }
        return node;
    }

    // Renvoie le nombre d'erreurs ajoutées (0 = valide).
    std::size_t validate(const json& value, const json& schema, const std::string& path) {
        const std::size_t before = errors.size();
        if (schema.is_boolean()) {
            if (!schema.get<bool>()) {
                fail(path, "interdit par le schéma");
            }
            return errors.size() - before;
        }
        if (!schema.is_object()) {
            fail(path, "schéma invalide");
            return errors.size() - before;
        }
        for (const auto& [key, _] : schema.items()) {
            if (!kKeywords.contains(key) && !kAnnotations.contains(key)) {
                fail(path, "mot-clé de schéma non supporté : " + key);
            }
        }
        if (schema.contains("$ref")) {
            const json* target = resolve(schema["$ref"].get<std::string>());
            if (!target) {
                fail(path, "$ref introuvable : " + schema["$ref"].get<std::string>());
            } else {
                validate(value, *target, path);
            }
        }
        if (schema.contains("type")) {
            const auto& t = schema["type"];
            bool ok = false;
            std::string wanted;
            if (t.is_string()) {
                wanted = t.get<std::string>();
                ok = type_matches(value, wanted);
            } else if (t.is_array()) {
                for (const auto& one : t) {
                    wanted += (wanted.empty() ? "" : "|") + one.get<std::string>();
                    ok = ok || type_matches(value, one.get<std::string>());
                }
            }
            if (!ok) {
                fail(path, "type " + std::string(value.type_name()) + " au lieu de " + wanted);
                return errors.size() - before; // inutile d'aller plus loin
            }
        }
        if (schema.contains("enum")) {
            bool found = false;
            for (const auto& allowed : schema["enum"]) {
                found = found || allowed == value;
            }
            if (!found) {
                fail(path, "valeur hors énumération : " + value.dump());
            }
        }
        if (schema.contains("const") && schema["const"] != value) {
            fail(path, "valeur différente de la constante " + schema["const"].dump());
        }
        if (value.is_number()) {
            if (schema.contains("minimum") &&
                value.get<double>() < schema["minimum"].get<double>()) {
                fail(path, "sous le minimum");
            }
            if (schema.contains("maximum") &&
                value.get<double>() > schema["maximum"].get<double>()) {
                fail(path, "au-dessus du maximum");
            }
        }
        if (value.is_object()) {
            const json props =
                schema.contains("properties") ? schema["properties"] : json::object();
            if (schema.contains("required")) {
                for (const auto& name : schema["required"]) {
                    if (!value.contains(name.get<std::string>())) {
                        fail(path, "champ requis absent : " + name.get<std::string>());
                    }
                }
            }
            for (const auto& [key, member] : value.items()) {
                const std::string child = path + "/" + key;
                if (props.contains(key)) {
                    validate(member, props[key], child);
                } else if (schema.contains("additionalProperties")) {
                    const auto& extra = schema["additionalProperties"];
                    if (extra.is_boolean() && !extra.get<bool>()) {
                        fail(child, "champ non déclaré dans le schéma");
                    } else if (extra.is_object()) {
                        validate(member, extra, child);
                    }
                }
            }
        }
        if (value.is_array()) {
            if (schema.contains("minItems") &&
                value.size() < schema["minItems"].get<std::size_t>()) {
                fail(path, "trop peu d'éléments");
            }
            if (schema.contains("items")) {
                for (std::size_t i = 0; i < value.size(); ++i) {
                    validate(value[i], schema["items"], path + "/" + std::to_string(i));
                }
            }
        }
        if (schema.contains("anyOf")) {
            bool any = false;
            for (const auto& alt : schema["anyOf"]) {
                Validator sub{root, {}};
                if (sub.validate(value, alt, path) == 0) {
                    any = true;
                    break;
                }
            }
            if (!any) {
                fail(path, "aucune alternative de anyOf ne convient");
            }
        }
        if (schema.contains("oneOf")) {
            int matches = 0;
            for (const auto& alt : schema["oneOf"]) {
                Validator sub{root, {}};
                if (sub.validate(value, alt, path) == 0) {
                    ++matches;
                }
            }
            if (matches != 1) {
                fail(path, "oneOf : " + std::to_string(matches) + " alternatives conviennent");
            }
        }
        return errors.size() - before;
    }
};

bool read_json(const std::string& path, json& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "lecture impossible : " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        out = json::parse(ss.str());
    } catch (const std::exception& e) {
        error = path + " : JSON invalide : " + e.what();
        return false;
    }
    return true;
}

// « openstitch.batch/1 » -> « openstitch.batch.1 ».
std::string schema_file_stem(std::string name) {
    for (auto& c : name) {
        if (c == '/') {
            c = '.';
        }
    }
    return name;
}

int run_selftest() {
    int failures = 0;
    auto check = [&failures](bool cond, const char* what) {
        if (!cond) {
            std::fprintf(stderr, "selftest : echec : %s\n", what);
            ++failures;
        }
    };
    const json schema = json::parse(R"({
      "type": "object",
      "required": ["schema", "n"],
      "additionalProperties": false,
      "properties": {
        "schema": {"const": "x/1"},
        "n": {"type": "integer", "minimum": 0},
        "tags": {"type": "array", "items": {"enum": ["a", "b"]}},
        "opt": {"anyOf": [{"type": "null"}, {"type": "string"}]},
        "sub": {"$ref": "#/$defs/sub"}
      },
      "$defs": {"sub": {"type": "object", "required": ["k"], "properties": {"k": {"type": "number"}}}}
    })");
    auto errors_of = [&schema](const std::string& doc) {
        Validator v{schema, {}};
        v.validate(json::parse(doc), schema, "");
        return v.errors.size();
    };
    check(errors_of(R"({"schema":"x/1","n":3})") == 0, "document valide accepte");
    check(errors_of(R"({"schema":"x/1"})") > 0, "champ requis absent refuse");
    check(errors_of(R"({"schema":"x/1","n":3,"extra":1})") > 0, "champ ajoute refuse");
    check(errors_of(R"({"schema":"x/1","n":"3"})") > 0, "mauvais type refuse");
    check(errors_of(R"({"schema":"x/1","n":3.5})") > 0, "reel pour integer refuse");
    check(errors_of(R"({"schema":"x/2","n":3})") > 0, "const refuse");
    check(errors_of(R"({"schema":"x/1","n":-1})") > 0, "minimum refuse");
    check(errors_of(R"({"schema":"x/1","n":1,"tags":["a","c"]})") > 0, "enum d'element refuse");
    check(errors_of(R"({"schema":"x/1","n":1,"opt":null})") == 0, "anyOf null accepte");
    check(errors_of(R"({"schema":"x/1","n":1,"opt":4})") > 0, "anyOf refuse");
    check(errors_of(R"({"schema":"x/1","n":1,"sub":{"k":1.5}})") == 0, "$ref accepte");
    check(errors_of(R"({"schema":"x/1","n":1,"sub":{}})") > 0, "$ref requis refuse");
    const json unknown = json::parse(R"({"pattern":"a"})");
    Validator v{unknown, {}};
    check(v.validate(json("a"), unknown, "") > 0, "mot-cle inconnu refuse");
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--selftest") {
        return run_selftest();
    }
    if (argc != 4) {
        std::fprintf(stderr,
                     "usage : json_schema_check <dossier_schemas> <cle|-> <fichier.json>\n");
        return 2;
    }
    const std::string dir = argv[1];
    const std::string key = argv[2];
    const std::string file = argv[3];
    json doc;
    std::string error;
    if (!read_json(file, doc, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::string stem;
    if (doc.is_object() && doc.contains("schema") && doc["schema"].is_string()) {
        stem = schema_file_stem(doc["schema"].get<std::string>());
    } else if (key != "-") {
        stem = key;
    } else {
        std::fprintf(stderr, "document sans champ « schema » et sans cle de commande\n");
        return 1;
    }
    const std::string schemaPath = dir + "/" + stem + ".schema.json";
    json schema;
    if (!read_json(schemaPath, schema, error)) {
        std::fprintf(stderr, "schema manquant pour cette sortie --json : %s\n", error.c_str());
        return 1;
    }
    Validator v{schema, {}};
    v.validate(doc, schema, "");
    if (!v.errors.empty()) {
        std::fprintf(stderr, "sortie non conforme a %s :\n", schemaPath.c_str());
        for (const auto& e : v.errors) {
            std::fprintf(stderr, "  %s\n", e.c_str());
        }
        return 1;
    }
    return 0;
}
