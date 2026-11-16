// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <map>
#include <mutex>
#include <string>

namespace openstitch::desktop_v2 {

// Vignettes du catalogue de recettes (STCH-CROS-01) : `image://recipe/<identifiant>`. Le rendu
// est celui du moteur (`project_ops::render_recipe_swatch`) ; ce fournisseur ne fait que le
// décoder et le garder en cache (déterministe, donc jamais invalidé). À utiliser avec
// `Image { asynchronous: true }` : Qt appelle alors `requestImage` hors du fil d'interface.
class RecipeSwatchProvider final : public QQuickImageProvider {
public:
    RecipeSwatchProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    std::mutex mutex_;
    std::map<std::string, QImage> cache_;
};

} // namespace openstitch::desktop_v2
