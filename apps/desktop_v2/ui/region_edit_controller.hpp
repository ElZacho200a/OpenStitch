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
#include <QVector>

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
        // Demande avant d'écraser des retouches (renvoie vrai pour continuer) ; absent = continuer.
        std::function<bool(const QString&)> confirmReplace{};
        // Signale un changement d'état visible hors du panneau (corrections en attente…).
        std::function<void()> stateChanged{};
    };

    RegionEditController(Host host, RegionPanel* panel, QObject* parent = nullptr);
    ~RegionEditController() override;

    // Recalcule la liste, la surimpression et l'état des boutons depuis le document et l'outil.
    void refresh();
    // Clic sur le motif avec l'outil « Régions » : choisit la région sous le curseur. Ctrl =
    // bascule (ajoute / retire), Maj = ajoute ; un clic sur le fond sans modificateur désélectionne
    // tout.
    bool onCanvasClick(QPointF scenePx, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    // Le document a été remplacé (nouveau, ouverture, annulation d'une régénération) : on repart.
    void onDocumentReplaced();

    void selectRegion(std::uint64_t id);
    // Sélection multiple, PARTAGÉE avec le modèle de sélection du document (cibles `Region`) : la
    // dernière identifiant est la région principale. Les identifiants disparus sont ignorés.
    void selectRegions(std::vector<std::uint64_t> ids);
    void selectAll();
    void selectSameColor();
    void selectNeighbours();
    void invertSelection();
    void clearSelection();
    // Lien région <-> objets : sélectionne les objets de broderie issus des régions choisies
    // (quitte la sélection de régions) ; `selectRegionsOfObjects` fait le chemin inverse.
    bool selectObjectsOfSelection();
    bool selectRegionsOfSelectedObjects();
    bool mergeSelection(); // fusionne les régions choisies dans la plus grande (≥ 2)
    void requestRecolor(); // ouvre le choix de couleur pour les régions choisies
    [[nodiscard]] const std::vector<std::uint64_t>& selection() const { return sel_; }
    [[nodiscard]] bool active() const;
    bool deleteSelected();
    bool mergeSelectedInto(std::uint64_t target);
    bool cleanIslands(int minPixels);
    bool fillHoles(int maxPixels);
    // Outil « Pinceau de frontière » : peint (ou, avec `erase`, efface) la région choisie le long
    // du trait, en pixels de l'aperçu. Faux et message si aucune région n'est choisie ou si c'est
    // refusé.
    bool paintStroke(const QVector<QPointF>& scenePx, bool erase);
    bool recolorSelected(std::array<std::uint8_t, 3> color);
    bool mergeSelectedColorInto(std::uint64_t target);
    void regenerate(bool resegmentFirst = false);
    void regenerateConfirmed(bool resegmentFirst);
    bool reducePalette(int maxColors, double maxDistance, bool mergeAdjacent);
    void updatePaletteReducePreview(int maxColors, double maxDistance, bool mergeAdjacent);

    [[nodiscard]] std::uint64_t selected() const { return selected_; }
    [[nodiscard]] bool dirty() const { return dirty_; }

private:
    void showOverlay();
    void adoptSelection(std::vector<std::uint64_t> ids, bool publish);

    Host host_;
    RegionPanel* panel_;
    std::uint64_t selected_{0};      // région principale (dernière de `sel_`)
    std::vector<std::uint64_t> sel_; // ensemble choisi, sans doublon
    bool publishing_{false};         // on écrit dans le modèle : ne pas se relire
    std::size_t selectionToken_{0};
    bool dirty_{false}; // des corrections n'ont pas encore été répercutées sur les objets
    std::uint64_t task_{0};
};

} // namespace openstitch::desktop_v2
