// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>

#include "openstitch/ai_segmentation/backend_registry.hpp"
#include "openstitch/ai_segmentation/model_catalog.hpp"
#include "sam_worker_client.hpp"

namespace openstitch::desktop {

// Préférences « Intelligence artificielle », persistées via QSettings comme
// le reste des préférences UI (cf. `ui/geometry` dans main_window.cpp) — pas
// de fichier de config parallèle.
struct AiPreferences {
    // Activé par défaut : quand sam-worker/.venv-wsl et sam-worker/models
    // existent dans le dépôt (cf. `defaultAiPreferences()`), la fonctionnalité
    // doit être utilisable sans étape de configuration supplémentaire.
    bool enabled{true};
    AiRuntimeKind runtime{AiRuntimeKind::WslPython};
    QString wslDistro{QStringLiteral("Ubuntu")};
    // Python du venv worker : chemin côté WSL (ex.
    // "/home/user/openstitch-sam-venv/bin/python") ou natif selon `runtime`.
    // Valeur par défaut calculée par `loadAiPreferences()` (racine du dépôt +
    // sam-worker/.venv-wsl) tant qu'aucune valeur n'a été sauvegardée.
    QString venvPythonPath;
    // openstitch_sam_worker.py, même remarque que ci-dessus.
    QString workerScriptPath;
    // Dossier des checkpoints, chemin WINDOWS (cf. WslPathConverter). Même
    // remarque : préempli avec sam-worker/models du dépôt par défaut.
    QString modelsDir;
    ai_segmentation::ModelId defaultModel{ai_segmentation::ModelId::Small};
    QString defaultDevice{QStringLiteral("auto")};
    int maxAnalysisResolution{1536};
    bool keepDiagnosticFiles{false};
    QString logLevel{QStringLiteral("INFO")};
    // Backends du contrat `openstitch.ai.backend/1` : fichier `openstitch.ai.backends/1` choisi
    // par l'utilisateur (vide = variable OPENSTITCH_AI_BACKENDS). OpenStitch ne télécharge ni
    // n'installe aucun modèle : la commande lancée ne vient que de ce fichier.
    QString backendsFile;
    // Moteur proposé par défaut dans le dialogue : voir `AiEngine`. Le worker SAM historique
    // reste le défaut (ADR-001 : SAM 2.1 ne change pas de comportement par défaut).
    QString engine{QStringLiteral("legacy")};
};

// Moteur de segmentation choisi : le worker SAM historique, un backend déclaré (registre) ou la
// segmentation couleur CIELAB. Sérialisé en texte : « legacy », « classical », « backend:<id> ».
struct AiEngine {
    enum class Kind { Legacy, Classical, Backend };
    Kind kind{Kind::Legacy};
    QString backendId; // seulement pour Kind::Backend

    [[nodiscard]] QString toString() const;
    [[nodiscard]] static AiEngine fromString(const QString& text);
    friend bool operator==(const AiEngine&, const AiEngine&) = default;
};

// Fichier de backends effectif : celui des préférences, sinon OPENSTITCH_AI_BACKENDS ; vide si
// aucun n'est désigné (alors seuls le worker historique et la segmentation couleur existent).
[[nodiscard]] QString effectiveBackendsFile(const AiPreferences& prefs);
// Lit le fichier ; en cas d'erreur, `errorMessage` dit pourquoi (la liste est alors vide).
[[nodiscard]] ai_segmentation::BackendSettings loadBackendSettings(const AiPreferences& prefs,
                                                                   QString* errorMessage);

[[nodiscard]] AiPreferences loadAiPreferences();
void saveAiPreferences(const AiPreferences& prefs);

// Construit la configuration du client worker à partir des préférences —
// un seul endroit traduit AiPreferences -> SamWorkerConfig.
[[nodiscard]] SamWorkerConfig toWorkerConfig(const AiPreferences& prefs);

} // namespace openstitch::desktop
