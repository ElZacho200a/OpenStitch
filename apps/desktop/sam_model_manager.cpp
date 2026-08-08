// SPDX-License-Identifier: Apache-2.0
#include "sam_model_manager.hpp"

#include <QDir>
#include <QFileInfo>
#include <utility>

namespace openstitch::desktop {

namespace {
QString toQString(std::string_view sv) {
    return QString::fromUtf8(sv.data(), static_cast<qsizetype>(sv.size()));
}
}  // namespace

SamModelManager::SamModelManager(QString modelsDir) : modelsDir_(std::move(modelsDir)) {}

void SamModelManager::setModelsDir(QString modelsDir) {
    modelsDir_ = std::move(modelsDir);
}

InstalledModelInfo SamModelManager::status(ai_segmentation::ModelId id) const {
    InstalledModelInfo info;
    info.id = id;
    const auto& descriptor = ai_segmentation::model_descriptor(id);
    const QString checkpointFile = toQString(descriptor.checkpoint_file);
    info.checkpointPath = QDir(modelsDir_).filePath(checkpointFile);
    const QFileInfo fileInfo(info.checkpointPath);
    info.installed = fileInfo.exists() && fileInfo.isFile();
    info.fileSizeBytes = info.installed ? fileInfo.size() : 0;
    return info;
}

QVector<InstalledModelInfo> SamModelManager::allStatuses() const {
    QVector<InstalledModelInfo> result;
    for (const auto& descriptor : ai_segmentation::all_models()) {
        result.push_back(status(descriptor.id));
    }
    return result;
}

bool SamModelManager::isInstalled(ai_segmentation::ModelId id) const {
    return status(id).installed;
}

}  // namespace openstitch::desktop
