// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QMainWindow>

class QGraphicsScene;
class QLabel;

namespace openstitch::desktop {

class CanvasView;

// Fenêtre principale. Règle du projet : aucune logique métier dans les
// widgets — chargement (libs/image) et placement physique (libs/document)
// viennent des bibliothèques cœur ; cette classe câble et affiche.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

private slots:
    void openImage();

private:
    QGraphicsScene* scene_{nullptr};
    CanvasView* view_{nullptr};
    QLabel* cursorLabel_{nullptr};
};

}  // namespace openstitch::desktop
