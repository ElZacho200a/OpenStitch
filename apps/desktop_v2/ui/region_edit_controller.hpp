// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- contrôleur de la correction de segmentation : surimpression de la carte des régions
// sur l'aperçu, choix d'une région au clic, corrections (supprimer, fusionner, nettoyer les îlots)
// et régénération des objets en tâche annulable. N'agit que par `core/region_edit`.

#include <array>
#include <cstdint>
#include <functional>

#include <QObject>
#include <QPointF>
#include <QString>

#include "document_session.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "preview_service.hpp"
#include "task_manager.hpp"

namespace openstitch::desktop_v2 {

class PreviewCanvas;
class RegionPanel;

class RegionEditController final : public QObject {
    Q_OBJECT

public:
    struct Host {
        DocumentSession& session;
        PreviewCanvas& canvas;
        const PreviewImage& frame;
        TaskManager& tasks;
        std::function<QString()> activeTool;
        std::function<void(const QString&, bool)> notice;
        std::function<const image::Image*()> workingImage;       // image lue par la segmentation
        std::function<project_ops::DigitizeSettings()> settings; // réglages de numérisation
    };

    RegionEditController(Host host, RegionPanel* panel, QObject* parent = nullptr);

    // Recalcule la liste, la surimpression et l'état des boutons depuis le document et l'outil.
    void refresh();
    // Clic sur le motif avec l'outil « Régions » : choisit la région sous le curseur.
    bool onCanvasClick(QPointF scenePx);
    // Le document a été remplacé (nouveau, ouverture, annulation d'une régénération) : on repart.
    void onDocumentReplaced();

    void selectRegion(std::uint64_t id);
    bool deleteSelected();
    bool mergeSelectedInto(std::uint64_t target);
    bool cleanIslands(int minPixels);
    bool recolorSelected(std::array<std::uint8_t, 3> color);
    bool mergeSelectedColorInto(std::uint64_t target);
    void regenerate();

    [[nodiscard]] std::uint64_t selected() const { return selected_; }
    [[nodiscard]] bool dirty() const { return dirty_; }

private:
    [[nodiscard]] bool active() const;
    void showOverlay();

    Host host_;
    RegionPanel* panel_;
    std::uint64_t selected_{0};
    bool dirty_{false}; // des corrections n'ont pas encore été répercutées sur les objets
    std::uint64_t task_{0};
};

} // namespace openstitch::desktop_v2
