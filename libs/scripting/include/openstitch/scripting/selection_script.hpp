// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_ops/operations.hpp"

// Script Python sur une sélection d'objets de broderie (SCRIPT-01).
//
// Un petit script Python (interpréteur CPython embarqué via pybind11) lit les caractéristiques
// des objets sélectionnés (couleur, taille, position, type, paramètres…) et décide des valeurs
// à appliquer :
//
//     for o in selection:
//         if o.type == "tatami" and o.area_mm2 > 400:
//             o.set(row_spacing_mm=0.40)
//         elif o.color.hue < 30:
//             o.set(angle_deg=45 + o.x * 0.5)
//
// Le script ne touche JAMAIS le document : il produit un lot `openstitch.batch/1`
// (`set_params`, `set_color`) limité aux objets sélectionnés, que l'appelant simule puis
// applique par `project_ops::apply_batch` (transactionnel, `commands::ICommand`, une seule
// annulation dans le Studio). pybind11 et CPython n'apparaissent pas dans cette interface.
//
// Déterminisme : interpréteur isolé (ni variables d'environnement PYTHON*, ni site-packages),
// graine de hachage fixe (PYTHONHASHSEED=0) : même projet, même sélection, même script → même
// lot octet pour octet. Un script qui dépasse `timeout` est interrompu (exception
// `ScriptTimeout`, non rattrapable par `except Exception`) ; un appel C très long (ex.
// `sum(range(10**12))`) n'est interrompu qu'à la ligne Python suivante.
namespace openstitch::scripting {

namespace code {
inline constexpr const char* kUnavailable = "E_SCRIPT_UNAVAILABLE";
inline constexpr const char* kNoSelection = "E_SCRIPT_NO_SELECTION";
inline constexpr const char* kScriptError = "E_SCRIPT_ERROR";
inline constexpr const char* kTimeout = "E_SCRIPT_TIMEOUT";
inline constexpr const char* kRefused = "E_SCRIPT_REFUSED";
} // namespace code

struct ScriptOptions {
    std::chrono::milliseconds timeout{5000};
    // Génère les points pour renseigner `o.stitches` (plus lent) ; faux = `o.stitches` vaut None.
    bool with_stitches{false};
};

// Une valeur modifiée par le script (aperçu « avant → après » du Studio et de la CLI).
// `before`/`after` : valeurs JSON compactes (« 0.45 », « "#FF0000" », « null »).
struct ParamChange {
    ObjectId object{};
    std::string object_name;
    std::string key; // nom du paramètre, ou « color »
    std::string before;
    std::string after;
};

struct ScriptResult {
    std::string batch_json; // lot `openstitch.batch/1` (opérations vides si rien ne change)
    std::size_t operation_count{0};
    std::vector<ParamChange> changes; // dans l'ordre de la sélection
    std::string output;               // ce que le script a écrit (print), tronqué à 64 Kio
};

// Dossier de la bibliothèque standard Python (contient `Lib/`). À appeler avant le premier
// script ; sinon : variable d'environnement OPENSTITCH_PYTHONHOME, puis `<dossier de
// l'exécutable>/python`. Sans effet une fois l'interpréteur démarré.
void set_python_home(const std::filesystem::path& home);

// Démarre l'interpréteur si besoin. Erreur `E_SCRIPT_UNAVAILABLE` si la bibliothèque standard
// est introuvable ou si cette compilation n'embarque pas Python.
[[nodiscard]] Result<void> ensure_python();

// Version de l'interpréteur embarqué (« 3.12.13 »), vide s'il n'est pas disponible.
[[nodiscard]] std::string python_version();

// Exécute `source` sur les objets de broderie `selection` (ordre conservé). N'écrit rien dans
// le projet. Erreurs : `E_SCRIPT_NO_SELECTION` (sélection vide ou id inconnu),
// `E_SCRIPT_ERROR` (exception ou erreur de syntaxe : message = ligne et cause, `detail` =
// sortie du script), `E_SCRIPT_TIMEOUT`, `E_SCRIPT_UNAVAILABLE`. Sûr d'appel depuis
// n'importe quel fil (les exécutions sont sérialisées).
[[nodiscard]] Result<ScriptResult> run_selection_script(const document::Project& project,
                                                        const std::vector<ObjectId>& selection,
                                                        std::string_view source,
                                                        const ScriptOptions& options = {});

struct ScriptApplication {
    ScriptResult script;
    project_ops::BatchReport report;
};

// Exécute le script puis applique son lot (`apply_batch`, transactionnel). `dry_run` : simule
// sans modifier `project`. Un lot refusé (paramètre inconnu, valeur hors plage…) donne
// `E_SCRIPT_REFUSED` avec le message de l'opération fautive ; `project` reste inchangé.
[[nodiscard]] Result<ScriptApplication>
apply_selection_script(document::Project& project, const std::vector<ObjectId>& selection,
                       std::string_view source, const ScriptOptions& options = {},
                       bool dry_run = false);

} // namespace openstitch::scripting
