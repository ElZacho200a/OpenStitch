// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- menu radial : jusqu'à huit commandes autour du curseur (mêmes commandes que la
// barre flottante). Souris : survol puis clic ; clavier : touches 1 à 8, flèches + Entrée, Échap.
// Aucune logique métier : le shell fournit les entrées et exécute l'identifiant choisi.

#include <QWidget>
#include <vector>

#include "command_catalog.hpp"

namespace openstitch::desktop_v2 {

class RadialMenuWidget : public QWidget {
    Q_OBJECT
public:
    explicit RadialMenuWidget(QWidget* parent = nullptr);

    // Au plus `kRadialMaxSectors` entrées sont gardées (les suivantes restent au clic droit).
    void setEntries(std::vector<CommandEntry> entries);
    [[nodiscard]] int sectorCount() const { return static_cast<int>(entries_.size()); }
    [[nodiscard]] int hoveredSector() const { return hovered_; }
    // Ouvre centré sur `globalCentre` (ramené dans l'écran).
    void popupAt(const QPoint& globalCentre);
    // Actions déclenchables sans souris (tests, clavier).
    bool activateSector(int sector);
    void hoverSector(int sector);
    [[nodiscard]] QPoint sectorCentre(int sector) const; // coordonnées locales

signals:
    void chosen(const QString& commandId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] int sectorAtLocal(const QPoint& p) const;

    std::vector<CommandEntry> entries_;
    int hovered_{-1};
};

} // namespace openstitch::desktop_v2
