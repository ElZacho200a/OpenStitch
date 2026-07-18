// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QMainWindow>

class QGraphicsScene;
class QGraphicsView;

namespace openstitch::desktop {

// Fenêtre principale. Règle du projet : aucune logique métier dans les
// widgets — le chargement d'image passe par libs/image ; cette classe ne
// fait que de l'affichage et du câblage d'actions.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

private slots:
    void openImage();

private:
    QGraphicsScene* scene_{nullptr};
    QGraphicsView* view_{nullptr};
};

}  // namespace openstitch::desktop
