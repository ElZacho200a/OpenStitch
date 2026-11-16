// SPDX-License-Identifier: Apache-2.0
#include "recipe_swatch_provider.hpp"

#include "openstitch/project_ops/render.hpp"

namespace openstitch::desktop_v2 {

QImage RecipeSwatchProvider::requestImage(const QString& id, QSize* size,
                                          const QSize& requestedSize) {
    const std::string key = id.toStdString();
    QImage image;
    {
        const std::lock_guard lock(mutex_);
        if (const auto it = cache_.find(key); it != cache_.end()) {
            image = it->second;
        }
    }
    if (image.isNull()) {
        if (const auto rendered = project_ops::render_recipe_swatch(key)) {
            image = QImage::fromData(rendered->png.data(), static_cast<int>(rendered->png.size()),
                                     "PNG");
        }
        if (image.isNull()) {
            image = QImage(1, 1, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
        }
        const std::lock_guard lock(mutex_);
        cache_.emplace(key, image);
    }
    if (size != nullptr) {
        *size = image.size();
    }
    if (requestedSize.isValid() && !requestedSize.isEmpty()) {
        return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

} // namespace openstitch::desktop_v2
