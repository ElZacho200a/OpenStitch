// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>
#include <QVector>

#include "openstitch/ai_segmentation/model_catalog.hpp"

namespace openstitch::desktop {

struct InstalledModelInfo {
    ai_segmentation::ModelId id{ai_segmentation::ModelId::Tiny};
    bool installed{false};
    QString checkpointPath;  // chemin Windows attendu, existant ou non
    qint64 fileSizeBytes{0};
};

// Détection des modèles SAM 2 installés dans le dossier configuré (jamais de
// téléchargement ici : pour le MVP, l'utilisateur place lui-même les
// checkpoints dans ce dossier depuis Préférences > Intelligence
// artificielle — le téléchargement atomique reste à ajouter plus tard).
class SamModelManager {
public:
    explicit SamModelManager(QString modelsDir = {});

    [[nodiscard]] const QString& modelsDir() const { return modelsDir_; }
    void setModelsDir(QString modelsDir);

    [[nodiscard]] InstalledModelInfo status(ai_segmentation::ModelId id) const;
    [[nodiscard]] QVector<InstalledModelInfo> allStatuses() const;
    [[nodiscard]] bool isInstalled(ai_segmentation::ModelId id) const;

private:
    QString modelsDir_;
};

}  // namespace openstitch::desktop
