// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include <QWidget>

#include "region_edit.hpp"

class QComboBox;
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
    void setNeighbours(const std::vector<RegionInfo>& neighbours);
    void setColorTargets(const std::vector<RegionInfo>& targets);
    void setDirty(bool dirty);
    void setBusy(bool busy);
    void setMessage(const QString& message);

    [[nodiscard]] int regionCount() const;
    [[nodiscard]] std::uint64_t selectedRegion() const { return selected_; }
    [[nodiscard]] bool regenerateEnabled() const;
    [[nodiscard]] QString message() const;
    [[nodiscard]] int neighbourCount() const;

signals:
    void regionChosen(std::uint64_t id);
    void deleteRequested();
    void mergeRequested(std::uint64_t absorbIntoThis); // fusionne la région choisie DANS celle-ci
    void cleanRequested(int minPixels);
    void fillHolesRequested(int maxPixels);
    void recolorRequested();
    void mergeColorsRequested(std::uint64_t targetRegion);
    void regenerateRequested();

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
    QPushButton* regenerate_{nullptr};
    QLabel* banner_{nullptr};
    QLabel* status_{nullptr};
    std::uint64_t selected_{0};
    bool dirty_{false};
    bool busy_{false};
};

} // namespace openstitch::desktop_v2
