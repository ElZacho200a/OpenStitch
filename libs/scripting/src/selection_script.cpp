// SPDX-License-Identifier: Apache-2.0
// Interpréteur CPython embarqué (pybind11) pour le script de sélection. Seul fichier du dépôt
// qui inclut Python : l'interface publique ne transporte que des chaînes et des types OpenStitch.
//
// Cycle de vie : l'interpréteur démarre au premier script (configuration isolée, graine de
// hachage 0) puis rend le GIL ; chaque exécution le reprend (`gil_scoped_acquire`) depuis
// n'importe quel fil, sous un mutex qui sérialise les scripts. Il n'est jamais arrêté
// (Py_Finalize puis redémarrage n'est pas fiable) : il vit jusqu'à la fin du processus.
#include <pybind11/embed.h>

#include "openstitch/scripting/selection_script.hpp"

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "openstitch/project_ops/inspect.hpp"
#include "prelude.hpp"

namespace openstitch::scripting {

namespace py = pybind11;
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::mutex g_mutex; // sérialise le démarrage et les exécutions
fs::path g_home;
bool g_started = false;
std::optional<Error> g_failure; // échec de démarrage : définitif pour ce processus
std::string g_version;

// État de la fonction de trace du délai (protégé par g_mutex : une exécution à la fois).
struct TraceState {
    std::chrono::steady_clock::time_point deadline{};
    PyObject* timeout_type{nullptr}; // classe ScriptTimeout du prélude
    PyObject* script_file{nullptr};  // "<script>"
    unsigned counter{0};
    bool expired{false};
};
TraceState g_trace;

// N'interrompt que les cadres du script de l'utilisateur : le prélude finit toujours
// proprement (restauration de sys.stdout, construction du résultat).
int deadline_trace(PyObject* /*obj*/, PyFrameObject* frame, int what, PyObject* /*arg*/) {
    if (what != PyTrace_LINE && what != PyTrace_CALL) {
        return 0;
    }
    if (!g_trace.expired) {
        if ((++g_trace.counter & 63U) != 0U) {
            return 0;
        }
        if (std::chrono::steady_clock::now() < g_trace.deadline) {
            return 0;
        }
        g_trace.expired = true;
    }
    PyCodeObject* code = PyFrame_GetCode(frame);
    const int cmp = PyUnicode_Compare(code->co_filename, g_trace.script_file);
    Py_DECREF(code);
    if (cmp != 0) {
        PyErr_Clear(); // PyUnicode_Compare ne peut pas échouer sur deux str ; prudence
        return 0;
    }
    PyErr_SetString(g_trace.timeout_type, "délai du script dépassé");
    return -1;
}

std::string env_value(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t size = 0;
    std::string value;
    if (_dupenv_s(&buffer, &size, name) == 0 && buffer != nullptr) {
        value = buffer;
        std::free(buffer);
    }
    return value;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? value : "";
#endif
}

fs::path executable_dir() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(n);
    return fs::path(buffer).parent_path();
#else
    std::error_code ec;
    const auto self = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path{} : self.parent_path();
#endif
}

fs::path resolve_home() {
    if (!g_home.empty()) {
        return g_home;
    }
    if (const auto env = env_value("OPENSTITCH_PYTHONHOME"); !env.empty()) {
        return fs::path(env);
    }
    return executable_dir() / "python";
}

Error unavailable(std::string message) {
    return Error{ErrorCategory::OperationImpossible,
                 std::move(message),
                 {},
                 code::kUnavailable,
                 "réinstaller OpenStitch (dossier « python » à côté de l'exécutable) ou "
                 "définir OPENSTITCH_PYTHONHOME vers une bibliothèque standard Python " PY_VERSION};
}

Result<void> start_locked() {
    if (g_started) {
        return {};
    }
    if (g_failure) {
        return std::unexpected(*g_failure);
    }
    const auto home = resolve_home();
    std::error_code ec;
    if (!fs::exists(home / "Lib" / "encodings", ec)) {
        g_failure =
            unavailable("Python embarqué indisponible : bibliothèque standard absente de « " +
                        home.string() + " »");
        return std::unexpected(*g_failure);
    }

    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.use_hash_seed = 1;
    config.hash_seed = 0; // déterminisme : ordre des set/dict de chaînes stable
    config.install_signal_handlers = 0;
    config.write_bytecode = 0;
    config.site_import = 0; // ni site-packages ni .pth : seule la bibliothèque standard
    config.user_site_directory = 0;
    const PyStatus status = PyConfig_SetString(&config, &config.home, home.wstring().c_str());
    if (PyStatus_Exception(status) != 0) {
        PyConfig_Clear(&config);
        g_failure = unavailable("Python embarqué : configuration refusée");
        return std::unexpected(*g_failure);
    }
    try {
        py::initialize_interpreter(&config, 0, nullptr, false);
    } catch (const std::exception& e) {
        g_failure =
            unavailable(std::string("Python embarqué : démarrage impossible (") + e.what() + ")");
        return std::unexpected(*g_failure);
    }
    PyConfig_Clear(&config); // sans effet si pybind11 l'a déjà libérée
    const std::string version = Py_GetVersion();
    g_version = version.substr(0, version.find(' '));
    // Rend le GIL : chaque exécution le reprend depuis son propre fil.
    PyEval_SaveThread();
    g_started = true;
    return {};
}

Error script_error(const char* code_value, std::string message, std::string output,
                   std::string hint) {
    return Error{ErrorCategory::UserInput, std::move(message), std::move(output), code_value,
                 std::move(hint)};
}

// Lit le résultat JSON du prélude.
Result<ScriptResult> decode_result(const std::string& raw, const ScriptOptions& options) {
    const json doc = json::parse(raw, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return std::unexpected(script_error(code::kScriptError, "résultat du script illisible", {},
                                            "signaler le problème (prélude Python)"));
    }
    const auto status = doc.value("status", std::string{});
    if (status == "missing") {
        std::string ids;
        for (const auto& id : doc.value("missing", json::array())) {
            ids += (ids.empty() ? "" : ", ") + std::to_string(id.get<std::uint64_t>());
        }
        return std::unexpected(Error{ErrorCategory::UserInput,
                                     "objet(s) de broderie introuvable(s) : " + ids,
                                     {},
                                     code::kNoSelection,
                                     "resélectionner les objets à traiter"});
    }
    const auto output = doc.value("output", std::string{});
    if (status == "timeout") {
        return std::unexpected(
            script_error(code::kTimeout,
                         "le script a dépassé " + std::to_string(options.timeout.count()) +
                             " ms et a été interrompu",
                         output, "chercher une boucle sans fin, ou alléger le calcul"));
    }
    if (status != "ok") {
        return std::unexpected(script_error(code::kScriptError, doc.value("error", status), output,
                                            "corriger le script à la ligne indiquée"));
    }
    ScriptResult result;
    result.batch_json = doc.value("batch", std::string{});
    result.operation_count = doc.value("operation_count", std::size_t{0});
    result.output = output;
    for (const auto& c : doc.value("changes", json::array())) {
        ParamChange change;
        change.object = ObjectId{c.value("object", std::uint64_t{0})};
        change.object_name = c.value("name", std::string{});
        change.key = c.value("key", std::string{});
        change.before = c.value("before", std::string{});
        change.after = c.value("after", std::string{});
        result.changes.push_back(std::move(change));
    }
    return result;
}

} // namespace

void set_python_home(const fs::path& home) {
    const std::lock_guard lock(g_mutex);
    if (!g_started) {
        g_home = home;
        g_failure.reset();
    }
}

Result<void> ensure_python() {
    const std::lock_guard lock(g_mutex);
    return start_locked();
}

std::string python_version() {
    const std::lock_guard lock(g_mutex);
    return start_locked() ? g_version : std::string{};
}

Result<ScriptResult> run_selection_script(const document::Project& project,
                                          const std::vector<ObjectId>& selection,
                                          std::string_view source, const ScriptOptions& options) {
    if (selection.empty()) {
        return std::unexpected(Error{ErrorCategory::UserInput,
                                     "aucun objet sélectionné",
                                     {},
                                     code::kNoSelection,
                                     "sélectionner au moins un objet de broderie"});
    }
    project_ops::InspectOptions inspectOptions;
    inspectOptions.with_stitches = options.with_stitches;
    inspectOptions.full_params = true;
    inspectOptions.with_geometry = false;
    auto inspected = project_ops::inspect_project_json(project, inspectOptions);
    if (!inspected) {
        return std::unexpected(inspected.error());
    }

    const std::lock_guard lock(g_mutex);
    if (auto started = start_locked(); !started) {
        return std::unexpected(started.error());
    }

    std::string raw;
    std::optional<Error> failure;
    {
        py::gil_scoped_acquire gil;
        try {
            py::dict globals;
            globals["__builtins__"] = py::module_::import("builtins");
            py::exec(detail::kPrelude, globals);
            const py::object timeoutType = globals["ScriptTimeout"];
            const py::str scriptFile("<script>");
            py::list ids;
            for (const auto id : selection) {
                ids.append(id.value);
            }
            const py::str code(source.data(), source.size());
            const py::str inspectJson(*inspected);
            const py::object run = globals["_openstitch_run"];

            g_trace = TraceState{std::chrono::steady_clock::now() + options.timeout,
                                 timeoutType.ptr(), scriptFile.ptr(), 0, false};
            PyEval_SetTrace(&deadline_trace, nullptr);
            py::object result;
            try {
                result = run(code, inspectJson, ids);
            } catch (...) {
                PyEval_SetTrace(nullptr, nullptr);
                throw;
            }
            PyEval_SetTrace(nullptr, nullptr);
            raw = result.cast<std::string>();
        } catch (const py::error_already_set& e) {
            failure =
                script_error(code::kScriptError, std::string("script illisible : ") + e.what(), {},
                             "vérifier que le script est du texte UTF-8");
        } catch (const std::exception& e) {
            failure = script_error(code::kScriptError, std::string("erreur Python : ") + e.what(),
                                   {}, "signaler le problème");
        }
    }
    if (failure) {
        return std::unexpected(*failure);
    }
    return decode_result(raw, options);
}

Result<ScriptApplication> apply_selection_script(document::Project& project,
                                                 const std::vector<ObjectId>& selection,
                                                 std::string_view source,
                                                 const ScriptOptions& options, bool dry_run) {
    auto script = run_selection_script(project, selection, source, options);
    if (!script) {
        return std::unexpected(script.error());
    }
    ScriptApplication application;
    if (script->operation_count == 0) {
        application.report.ok = true;
        application.report.dry_run = dry_run;
        application.script = std::move(*script);
        return application;
    }
    project_ops::BatchOptions batchOptions;
    batchOptions.dry_run = dry_run;
    auto report = project_ops::apply_batch(project, script->batch_json, batchOptions);
    if (!report.ok) {
        std::string message = report.error_message;
        std::string hint;
        for (const auto& outcome : report.outcomes) {
            if (outcome.ok) {
                continue;
            }
            const json batch = json::parse(script->batch_json, nullptr, false);
            std::string object;
            if (!batch.is_discarded() && outcome.index < batch["operations"].size()) {
                object = "objet " +
                         std::to_string(
                             batch["operations"][outcome.index].value("object", std::uint64_t{0})) +
                         " : ";
            }
            message = object + outcome.message;
            hint = outcome.hint;
            break;
        }
        return std::unexpected(Error{
            ErrorCategory::UserInput, std::move(message), std::move(script->output), code::kRefused,
            hint.empty() ? "vérifier le nom et la plage du paramètre" : std::move(hint)});
    }
    application.script = std::move(*script);
    application.report = std::move(report);
    return application;
}

} // namespace openstitch::scripting
