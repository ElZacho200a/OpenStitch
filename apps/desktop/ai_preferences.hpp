// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>

#include "openstitch/ai_segmentation/model_catalog.hpp"
#include "sam_worker_client.hpp"

namespace openstitch::desktop {

// Préférences « Intelligence artificielle », persistées via QSettings comme
// le reste des préférences UI (cf. `ui/geometry` dans main_window.cpp) — pas
// de fichier de config parallèle.
struct AiPreferences {
    bool enabled{false};
    AiRuntimeKind runtime{AiRuntimeKind::WslPython};
    QString wslDistro{QStringLiteral("Ubuntu")};
    // Python du venv worker : chemin côté WSL (ex.
    // "/home/user/openstitch-sam-venv/bin/python") ou natif selon `runtime`.
    QString venvPythonPath;
    // openstitch_sam_worker.py, même remarque que ci-dessus.
    QString workerScriptPath;
    // Dossier des checkpoints, chemin WINDOWS (cf. WslPathConverter).
    QString modelsDir;
    ai_segmentation::ModelId defaultModel{ai_segmentation::ModelId::Small};
    QString defaultDevice{QStringLiteral("auto")};
    int maxAnalysisResolution{1536};
    bool keepDiagnosticFiles{false};
    QString logLevel{QStringLiteral("INFO")};
};

[[nodiscard]] AiPreferences loadAiPreferences();
void saveAiPreferences(const AiPreferences& prefs);

// Construit la configuration du client worker à partir des préférences —
// un seul endroit traduit AiPreferences -> SamWorkerConfig.
[[nodiscard]] SamWorkerConfig toWorkerConfig(const AiPreferences& prefs);

}  // namespace openstitch::desktop
