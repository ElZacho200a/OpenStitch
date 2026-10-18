// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_io/migrations.hpp"

#include <array>
#include <cstddef>
#include <exception>
#include <nlohmann/json.hpp>

#include "openstitch/project_io/project_io.hpp"

namespace openstitch::project_io {

namespace {

using json = nlohmann::json;

// Parcourt les `params` de chaque objet de broderie du document.
template <typename Fn> void for_each_params(json& root, Fn&& fn) {
    if (!root.is_object() || !root.contains("document") || !root["document"].is_object()) {
        return;
    }
    auto& doc = root["document"];
    if (!doc.contains("embroideryObjects") || !doc["embroideryObjects"].is_array()) {
        return;
    }
    for (auto& eo : doc["embroideryObjects"]) {
        if (eo.is_object() && eo.contains("params") && eo["params"].is_object()) {
            fn(eo["params"]);
        }
    }
}

bool is_satin(const json& params) {
    const auto it = params.find("type");
    return it != params.end() && it->is_string() && it->get<std::string>() == "satin";
}

// v1 -> v2 : le satin porte des barreaux (`rungs`) et le projet un cadre (`canvas`).
// Les fichiers v1 n'ont ni l'un ni l'autre : on matérialise les valeurs par défaut.
void v1_to_v2(json& root) {
    for_each_params(root, [](json& params) {
        if (is_satin(params) && !params.contains("rungs")) {
            params["rungs"] = json::array();
        }
    });
    if (root.is_object() && root.contains("document") && root["document"].is_object() &&
        !root["document"].contains("canvas")) {
        root["document"]["canvas"] = {{"width", 100'000}, {"height", 100'000}};
    }
}

// v2 -> v3 : retouches manuelles. Absence de `overrides` = objet Clean (ADR-014) : rien à
// réécrire, la sémantique « champ absent » est celle de la version 3.
void v2_to_v3(json&) {}

// v3 -> v4 : les satins séparent `maxWidth` (seuil doux) de `maxWidthHard` (plafond du
// corridor). Champ absent -> 48 mm, valeur historique du plafond.
void v3_to_v4(json& root) {
    for_each_params(root, [](json& params) {
        if (is_satin(params) && !params.contains("maxWidthHard")) {
            params["maxWidthHard"] = 48'000;
        }
    });
}

// v4 -> v5 : champs de finition optionnels (valeurs par défaut du lecteur) : rien à réécrire.
void v4_to_v5(json&) {}

// v5 -> v6 : fil de nuancier (`thread`) et objets texte (`textObjects`, `textOwner`).
// Absents = aucun fil (couleur libre) / aucun texte : rien à réécrire.
void v5_to_v6(json&) {}

// v6 -> v7 : méthode d'auto-satin (`strategy`). Absente = squelette : rien à réécrire.
void v6_to_v7(json&) {}

// v7 -> v8 : typographie fine des textes (`kerningPairs`, `ligatures`). Absentes = aucun
// crénage manuel, pas de ligatures : rien à réécrire.
void v7_to_v8(json&) {}

struct Step {
    int from;
    const char* description;
    void (*apply)(json&);
};

constexpr std::array<Step, 7> kSteps{{
    {1, "satin: barreaux (rungs) et cadre (canvas) par defaut", &v1_to_v2},
    {2, "retouches manuelles: absence = objet Clean", &v2_to_v3},
    {3, "satin: maxWidthHard absent = 48 mm", &v3_to_v4},
    {4, "finitions optionnelles: valeurs par defaut du lecteur", &v4_to_v5},
    {5, "fil de nuancier et objets texte: absence = aucun", &v5_to_v6},
    {6, "methode d'auto-satin: absence = squelette", &v6_to_v7},
    {7, "typographie des textes: absence = pas de crenage manuel ni de ligatures", &v7_to_v8},
}};

// La table doit couvrir exactement 1 .. kMaxReadableSchemaVersion-1, sans trou.
constexpr bool steps_are_contiguous() {
    for (std::size_t i = 0; i < kSteps.size(); ++i) {
        if (kSteps[i].from != static_cast<int>(i) + 1) {
            return false;
        }
    }
    return static_cast<int>(kSteps.size()) + 1 == kMaxReadableSchemaVersion;
}
static_assert(steps_are_contiguous(),
              "la table de migrations doit aller de v1 a kMaxReadableSchemaVersion sans trou");

} // namespace

std::vector<MigrationStepInfo> migration_steps() {
    std::vector<MigrationStepInfo> out;
    out.reserve(kSteps.size());
    for (const auto& s : kSteps) {
        out.push_back({s.from, s.from + 1, s.description});
    }
    return out;
}

int declared_schema_version(std::string_view jsonText) {
    const json root = json::parse(jsonText, nullptr, false);
    if (!root.is_object()) {
        return 0;
    }
    const auto it = root.find("schemaVersion");
    return (it != root.end() && it->is_number_integer()) ? it->get<int>() : 0;
}

Result<std::string> migrate_project_json(std::string_view jsonText) {
    try {
        json root = json::parse(jsonText, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            return fail(ErrorCategory::InvalidFile, "project.json illisible");
        }
        const int version = declared_schema_version(jsonText);
        if (version > kMaxReadableSchemaVersion) {
            return fail(ErrorCategory::UnsupportedFormat,
                        "Ce projet a été créé par une version plus récente d'OpenStitch "
                        "(format v" +
                            std::to_string(version) + ", cette version lit jusqu'à v" +
                            std::to_string(kMaxReadableSchemaVersion) +
                            "). Mettez à jour OpenStitch pour l'ouvrir.");
        }
        if (version <= 0) {
            return fail(ErrorCategory::InvalidFile,
                        "Version de projet invalide (" + std::to_string(version) + ")");
        }
        for (int v = version; v < kMaxReadableSchemaVersion; ++v) {
            kSteps[static_cast<std::size_t>(v - 1)].apply(root);
        }
        root["schemaVersion"] = kMaxReadableSchemaVersion;
        return root.dump();
    } catch (const std::exception& ex) {
        return fail(ErrorCategory::InvalidFile, "Migration du projet impossible", ex.what());
    }
}

} // namespace openstitch::project_io
