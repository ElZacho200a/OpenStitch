// SPDX-License-Identifier: Apache-2.0
#include "ai_preferences.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

#include "wsl_path_converter.hpp"

namespace openstitch::desktop {

namespace {
constexpr auto kEnabled = "ai/enabled";
constexpr auto kRuntime = "ai/runtime";
constexpr auto kWslDistro = "ai/wslDistro";
constexpr auto kVenvPython = "ai/venvPythonPath";
constexpr auto kWorkerScript = "ai/workerScriptPath";
constexpr auto kModelsDir = "ai/modelsDir";
constexpr auto kDefaultModel = "ai/defaultModel";
constexpr auto kDefaultDevice = "ai/defaultDevice";
constexpr auto kMaxResolution = "ai/maxAnalysisResolution";
constexpr auto kKeepDiagnostics = "ai/keepDiagnosticFiles";
constexpr auto kLogLevel = "ai/logLevel";

// Remonte depuis le dossier de l'exécutable (build/msvc/apps/desktop/<Config>
// en dev) à la recherche de la racine du dépôt (repérée par la présence de
// CMakeLists.txt ET sam-worker/) : permet de préremplir la configuration IA
// sans que l'utilisateur ait à ressaisir des chemins qu'il vient d'installer
// « dans le même dépôt » (sam-worker/.venv-wsl, sam-worker/models).
// Renvoie une chaîne vide si la racine n'est pas trouvée (build packagé
// hors du dépôt source) : l'utilisateur configure alors manuellement.
QString findRepoRoot() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 10; ++i) {
        if (dir.exists(QStringLiteral("sam-worker")) && dir.exists(QStringLiteral("CMakeLists.txt"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

// Valeurs par défaut calculées : le worker WSL, son venv et le dossier des
// modèles vivent tous sous sam-worker/ à la racine du dépôt trouvé.
AiPreferences defaultAiPreferences() {
    AiPreferences prefs;
    const QString repoRoot = findRepoRoot();
    if (!repoRoot.isEmpty()) {
        const QString repoRootWsl = WslPathConverter::toWsl(repoRoot);
        prefs.venvPythonPath = repoRootWsl + QStringLiteral("/sam-worker/.venv-wsl/bin/python");
        prefs.workerScriptPath = repoRootWsl + QStringLiteral("/sam-worker/openstitch_sam_worker.py");
        prefs.modelsDir = QDir::toNativeSeparators(repoRoot + QStringLiteral("/sam-worker/models"));
    }
    return prefs;
}

}  // namespace

AiPreferences loadAiPreferences() {
    QSettings s;
    AiPreferences prefs = defaultAiPreferences();
    prefs.enabled = s.value(kEnabled, prefs.enabled).toBool();
    prefs.runtime = s.value(kRuntime, static_cast<int>(prefs.runtime)).toInt() ==
                            static_cast<int>(AiRuntimeKind::NativePython)
                        ? AiRuntimeKind::NativePython
                        : AiRuntimeKind::WslPython;
    prefs.wslDistro = s.value(kWslDistro, prefs.wslDistro).toString();
    prefs.venvPythonPath = s.value(kVenvPython, prefs.venvPythonPath).toString();
    prefs.workerScriptPath = s.value(kWorkerScript, prefs.workerScriptPath).toString();
    prefs.modelsDir = s.value(kModelsDir, prefs.modelsDir).toString();
    const int modelValue = s.value(kDefaultModel, static_cast<int>(prefs.defaultModel)).toInt();
    prefs.defaultModel = (modelValue >= 0 && modelValue <= 3) ? static_cast<ai_segmentation::ModelId>(modelValue)
                                                              : prefs.defaultModel;
    prefs.defaultDevice = s.value(kDefaultDevice, prefs.defaultDevice).toString();
    prefs.maxAnalysisResolution = s.value(kMaxResolution, prefs.maxAnalysisResolution).toInt();
    prefs.keepDiagnosticFiles = s.value(kKeepDiagnostics, prefs.keepDiagnosticFiles).toBool();
    prefs.logLevel = s.value(kLogLevel, prefs.logLevel).toString();
    return prefs;
}

void saveAiPreferences(const AiPreferences& prefs) {
    QSettings s;
    s.setValue(kEnabled, prefs.enabled);
    s.setValue(kRuntime, static_cast<int>(prefs.runtime));
    s.setValue(kWslDistro, prefs.wslDistro);
    s.setValue(kVenvPython, prefs.venvPythonPath);
    s.setValue(kWorkerScript, prefs.workerScriptPath);
    s.setValue(kModelsDir, prefs.modelsDir);
    s.setValue(kDefaultModel, static_cast<int>(prefs.defaultModel));
    s.setValue(kDefaultDevice, prefs.defaultDevice);
    s.setValue(kMaxResolution, prefs.maxAnalysisResolution);
    s.setValue(kKeepDiagnostics, prefs.keepDiagnosticFiles);
    s.setValue(kLogLevel, prefs.logLevel);
}

SamWorkerConfig toWorkerConfig(const AiPreferences& prefs) {
    SamWorkerConfig config;
    config.runtime = prefs.runtime;
    config.wslDistro = prefs.wslDistro;
    config.pythonExecutable = prefs.venvPythonPath;
    config.workerScriptPath = prefs.workerScriptPath;
    config.modelsDir = prefs.modelsDir;
    config.device = prefs.defaultDevice;
    return config;
}

}  // namespace openstitch::desktop
