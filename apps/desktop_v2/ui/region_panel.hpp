// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include <QWidget>

#include "region_edit.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace openstitch::desktop_v2 {

// Panneau « Régions » : la liste des régions de la segmentation, leurs corrections (supprimer,
// fusionner dans une voisine, nettoyer les îlots) et la régénération des objets. Aucune logique
// métier : tout passe par `core/region_edit`.
class RegionPanel final : public QWidget {
    Q_OBJECT

public:
    explicit RegionPanel(QWidget* parent = nullptr);

    void setRegions(const std::vector<RegionInfo>& regions, std::uint64_t selected);
    // Sélection multiple : `selected` (la principale en dernier) est surlignée dans la liste.
    void setRegions(const std::vector<RegionInfo>& regions,
                    const std::vector<std::uint64_t>& selected);
    void setNeighbours(const std::vector<RegionInfo>& neighbours);
    void setColorTargets(const std::vector<RegionInfo>& targets);
    void setDirty(bool dirty);
    void setBusy(bool busy);
    void setMessage(const QString& message);

    [[nodiscard]] int regionCount() const;
    [[nodiscard]] std::uint64_t selectedRegion() const { return selected_; }
    [[nodiscard]] int selectedCount() const { return selectedCount_; }
    [[nodiscard]] bool regenerateEnabled() const;
    [[nodiscard]] QString message() const;
    [[nodiscard]] int neighbourCount() const;
    [[nodiscard]] int brushRadius() const;
    void setBrushRadius(int pixels);
    // Réduction de palette : l'aperçu « N → M couleurs » vient du contrôleur.
    void setReducePreview(int before, int after);
    void requestReducePreview();

signals:
    void regionChosen(std::uint64_t id); // une seule région choisie dans la liste
    void regionsChosen(const std::vector<std::uint64_t>& ids); // choix (multiple) dans la liste
    void selectAllRequested();
    void selectSameColorRequested();
    void selectNeighboursRequested();
    void invertRequested();
    void selectObjectsRequested(); // objets issus des régions choisies
    void clearRequested();
    void deleteRequested();
    void mergeRequested(std::uint64_t absorbIntoThis); // fusionne la région choisie DANS celle-ci
    void cleanRequested(int minPixels);
    void fillHolesRequested(int maxPixels);
    void recolorRequested();
    void mergeColorsRequested(std::uint64_t targetRegion);
    void regenerateRequested();
    void reduceRequested(int maxColors, double maxDistance, bool mergeAdjacent);
    void reduceParamsChanged(int maxColors, double maxDistance, bool mergeAdjacent);
    void brushRadiusChanged(int pixels); // rayon du pinceau de frontière (outil « Pinceau »)

private:
    void updateButtons();

    QListWidget* list_{nullptr};
    QComboBox* neighbours_{nullptr};
    QPushButton* delete_{nullptr};
    QPushButton* merge_{nullptr};
    QComboBox* colorTargets_{nullptr};
    QPushButton* recolor_{nullptr};
    QPushButton* mergeColors_{nullptr};
    QSpinBox* minPixels_{nullptr};
    QPushButton* clean_{nullptr};
    QSpinBox* maxHolePixels_{nullptr};
    QPushButton* fillHoles_{nullptr};
    QSpinBox* brushRadius_{nullptr};
    QPushButton* regenerate_{nullptr};
    QLabel* banner_{nullptr};
    QLabel* status_{nullptr};
    std::uint64_t selected_{0};
    int selectedCount_{0};
    QSpinBox* reduceColors_{nullptr};
    QDoubleSpinBox* reduceDistance_{nullptr};
    QCheckBox* reduceMerge_{nullptr};
    QLabel* reduceSummary_{nullptr};
    QPushButton* reduce_{nullptr};
    QPushButton* all_{nullptr};
    QPushButton* sameColor_{nullptr};
    QPushButton* neighbourSel_{nullptr};
    QPushButton* invert_{nullptr};
    QPushButton* objects_{nullptr};
    QPushButton* clear_{nullptr};
    bool dirty_{false};
    bool busy_{false};
};

} // namespace openstitch::desktop_v2
