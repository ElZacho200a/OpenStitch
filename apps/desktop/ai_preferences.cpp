// SPDX-License-Identifier: Apache-2.0
#include "ai_preferences.hpp"

#include <QSettings>

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
}  // namespace

AiPreferences loadAiPreferences() {
    QSettings s;
    AiPreferences prefs;
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
