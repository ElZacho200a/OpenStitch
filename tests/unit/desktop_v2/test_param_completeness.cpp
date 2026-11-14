// SPDX-License-Identifier: Apache-2.0
// Complétude de `param_spec` : tout champ de paramètres du moteur est exposé par la table (donc par
// l'inspecteur) ou figure dans une liste d'exclusion JUSTIFIÉE. Le moteur est la source de vérité :
//  - le registre de champs de l'opération `set_params` (catalogue `operations_catalog_json`) donne
//    les champs éditables de chaque type ;
//  - le nombre de membres de chaque structure de paramètres est vérifié à la compilation (liaison
//    structurée) : ajouter un champ à une structure casse ce fichier jusqu'à ce qu'on le range.
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/project_ops/operations.hpp"
#include "param_spec.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {

// --- Nombre de membres de chaque structure (vérifié par le compilateur)
// ---------------------------
#define OS_COUNT_MEMBERS(Type, ...)                                                                \
    {                                                                                              \
        Type value{};                                                                              \
        [[maybe_unused]] auto& [__VA_ARGS__] = value;                                              \
    }

struct MemberCounts {
    std::map<std::string, std::size_t> byType;
};

MemberCounts memberCounts() {
    using namespace document;
    // Chaque ligne ne compile que si le nombre de noms est exactement celui des membres.
    OS_COUNT_MEMBERS(RunningStitchParams, a, b, c, d, e)
    OS_COUNT_MEMBERS(TatamiParams, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q)
    OS_COUNT_MEMBERS(DirectionalFillParams, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s,
                     t, u, v, w)
    OS_COUNT_MEMBERS(SatinParams, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v,
                     w, x, y, z, A, B, C, D, E, F, G, H, I)
    OS_COUNT_MEMBERS(AutoSatinParams, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u,
                     v, w, x, y, z, A, B, C, D, E, F, G)
    OS_COUNT_MEMBERS(BlanketStitchParams, a, b, c, d)
    OS_COUNT_MEMBERS(ContourFillParams, a, b, c, d, e, f, g, h)
    OS_COUNT_MEMBERS(MotifRunParams, a, b, c, d, e, f)
    OS_COUNT_MEMBERS(MotifFillParams, a, b, c, d, e, f, g, h, i, j, k)
    OS_COUNT_MEMBERS(RadialFillParams, a, b, c, d, e, f, g, h)
    OS_COUNT_MEMBERS(SpiralFillParams, a, b, c, d, e, f, g, h, i)
    OS_COUNT_MEMBERS(CrosshatchFillParams, a, b, c, d, e, f, g, h)
    OS_COUNT_MEMBERS(CrossStitchParams, a, b, c, d, e)
    OS_COUNT_MEMBERS(AppliqueParams, a, b, c, d, e, f, g, h)
    OS_COUNT_MEMBERS(MulticolorGradientParams, a, b, c, d, e, f, g)
    return {{{"running", 5},
             {"tatami", 17},
             {"directional", 23},
             {"satin", 35},
             {"auto_satin", 33},
             {"blanket", 4},
             {"contour_fill", 8},
             {"motif_run", 6},
             {"motif_fill", 11},
             {"radial", 8},
             {"spiral", 9},
             {"crosshatch", 8},
             {"cross_stitch", 5},
             {"applique", 8},
             {"multicolor_gradient", 7}}};
}

// Membres de structure SANS champ dans le registre de `set_params` (donc sans réglage) : chacun a
// sa raison. Le nombre de membres = champs du registre + ces membres.
const std::map<std::string, std::set<std::string>>& unregisteredMembers() {
    static const std::map<std::string, std::set<std::string>> k{
        // Identité de section dans un réseau satin décomposé : dérivée par la décomposition
        // (`SatinSectionTopology`), jamais saisie.
        {"satin", {"topology", "border"}},
        // Palette et transition du fondu multicolore : édition par la commande dédiée
        // (`make_create/edit_multicolor_gradient_command`), pas par un champ numérique simple
        // (comme `rail_a`/`rail_b` pour le satin).
        {"multicolor_gradient", {"palette", "transition"}},
    };
    return k;
}
// `border` : spécification d'origine d'un satin de bordure (largeur, côté, coins) qui régénère les
// rails ; éditée par une commande de `libs/commands` (convert_ops), sans champ dans `set_params`.
// LACUNE SIGNALÉE : l'exposer demande un champ `set_params` dans libs/project_ops (hors périmètre).

// Champs du registre de `set_params` volontairement absents de la table (éditeur dédié).
const std::set<std::pair<std::string, std::string>>& excludedRegistryFields() {
    static const std::set<std::pair<std::string, std::string>> k{
        // Géométrie du satin : les deux rails sont des polylignes éditées par poignées sur le
        // canevas (outil d'édition de nœuds), pas par des champs numériques.
        {"satin", "rail_a"},
        {"satin", "rail_b"},
    };
    return k;
}

// Identifiant de type de `set_params` -> identifiant de `stitchTypeId`.
std::string specTypeOf(const std::string& engineType) {
    if (engineType == "radial_fill") {
        return "radial";
    }
    if (engineType == "spiral_fill") {
        return "spiral";
    }
    if (engineType == "crosshatch_fill") {
        return "crosshatch";
    }
    return engineType;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\n')) {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '.' || s.back() == '"')) {
        s.pop_back();
    }
    return s;
}

// Champs du registre par type, lus dans le catalogue des opérations (texte de l'argument `params`
// de `set_params` : « running : a, b ; tatami : c, d ; ... »).
std::map<std::string, std::vector<std::string>> engineRegistry() {
    const std::string catalog = project_ops::operations_catalog_json();
    const auto marker = catalog.find("champs par type");
    REQUIRE(marker != std::string::npos);
    const auto start = catalog.find("running : ", marker);
    REQUIRE(start != std::string::npos);
    const auto end = catalog.find("Suffixe", start);
    REQUIRE(end != std::string::npos);
    const std::string body = catalog.substr(start, end - start);
    std::map<std::string, std::vector<std::string>> out;
    std::size_t pos = 0;
    while (pos < body.size()) {
        auto next = body.find(" ; ", pos);
        const std::string entry = body.substr(pos, next == std::string::npos ? next : next - pos);
        const auto colon = entry.find(" : ");
        REQUIRE(colon != std::string::npos);
        const std::string type = trim(entry.substr(0, colon));
        std::string list = entry.substr(colon + 3);
        std::size_t p = 0;
        while (p < list.size()) {
            const auto comma = list.find(", ", p);
            out[type].push_back(
                trim(list.substr(p, comma == std::string::npos ? comma : comma - p)));
            if (comma == std::string::npos) {
                break;
            }
            p = comma + 2;
        }
        if (next == std::string::npos) {
            break;
        }
        pos = next + 3;
    }
    return out;
}

} // namespace

TEST_CASE("every engine parameter field is exposed by param_spec or excluded with a reason",
          "[desktop_v2][params][completeness]") {
    const auto registry = engineRegistry();
    const auto counts = memberCounts();
    REQUIRE(registry.size() == 15);
    std::size_t engineFields = 0;
    std::size_t exposed = 0;
    std::size_t excluded = 0;
    for (const auto& [engineType, keys] : registry) {
        const std::string type = specTypeOf(engineType);
        INFO("type " << engineType);
        const auto specs = allParamSpecs(type);
        REQUIRE_FALSE(specs.empty());
        std::set<std::string> specKeys;
        for (const auto& s : specs) {
            CHECK(specKeys.insert(std::string(s.key)).second); // pas de clé en double
        }
        std::set<std::string> registryKeys(keys.begin(), keys.end());
        REQUIRE(registryKeys.size() == keys.size());
        for (const auto& key : keys) {
            ++engineFields;
            INFO("champ " << type << "." << key);
            if (excludedRegistryFields().count({type, key}) != 0) {
                ++excluded;
                CHECK(specKeys.count(key) == 0);
                continue;
            }
            CHECK(specKeys.count(key) == 1);
            exposed += specKeys.count(key);
        }
        // Aucune ligne de la table ne vise un champ que le moteur ignore.
        for (const auto& key : specKeys) {
            INFO("ligne de table " << type << "." << key);
            CHECK(registryKeys.count(key) == 1);
        }
        // Les membres de la structure = champs du registre + membres sans champ (justifiés).
        const auto unreg = unregisteredMembers().find(type);
        const std::size_t unregistered =
            unreg == unregisteredMembers().end() ? 0 : unreg->second.size();
        CHECK(counts.byType.at(type) == keys.size() + unregistered);
        excluded += unregistered;
        engineFields += unregistered;
    }
    // Le recensement chiffré (consigné dans le CSV de la famille J).
    CHECK(exposed + excluded == engineFields);
    CHECK(excluded == 6); // rail_a, rail_b, topology, border, palette, transition
    CHECK(exposed == 181);
}

TEST_CASE("every table row reads back from a default object, inside its own range",
          "[desktop_v2][params][completeness]") {
    using namespace document;
    const std::map<std::string, StitchParams> defaults{
        {"running", RunningStitchParams{}},
        {"tatami", TatamiParams{}},
        {"satin", SatinParams{}},
        {"directional", DirectionalFillParams{}},
        {"auto_satin", AutoSatinParams{}},
        {"blanket", BlanketStitchParams{}},
        {"contour_fill", ContourFillParams{}},
        {"motif_run", MotifRunParams{}},
        {"motif_fill", MotifFillParams{}},
        {"radial", RadialFillParams{}},
        {"spiral", SpiralFillParams{}},
        {"crosshatch", CrosshatchFillParams{}},
        {"cross_stitch", CrossStitchParams{}},
        {"applique", AppliqueParams{}},
        {"multicolor_gradient", MulticolorGradientParams{}}};
    REQUIRE(defaults.size() == paramTypeIds().size());
    for (const auto typeId : paramTypeIds()) {
        EmbroideryObject probe;
        probe.params = defaults.at(std::string(typeId));
        for (const auto& s : allParamSpecs(typeId)) {
            INFO(std::string(typeId) + "." + std::string(s.key));
            const auto v = readParam(probe, s.key);
            REQUIRE(v.has_value());
            CHECK(*v >= s.min - 1e-6);
            CHECK(*v <= s.max + 1e-6);
            if (s.kind == ParamKind::Choice) {
                CHECK(paramOptions(s).size() == static_cast<std::size_t>(s.max) + 1);
            }
            // Un réglage a un libellé français, un niveau et un groupe.
            CHECK_FALSE(s.label.empty());
            CHECK_FALSE(paramGroup(s).empty());
        }
    }
}

TEST_CASE("the scalar view keeps only scalar settings", "[desktop_v2][params][completeness]") {
    for (const auto typeId : paramTypeIds()) {
        for (const auto& s : paramSpecs(typeId)) {
            CHECK(isScalarKind(s.kind));
        }
        CHECK(paramSpecs(typeId).size() <= allParamSpecs(typeId).size());
    }
    CHECK(paramSpecs("directional").size() < allParamSpecs("directional").size());
}
