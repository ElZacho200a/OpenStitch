// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QWidget>

#include "openstitch/document/project.hpp"

class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace openstitch::desktop {

// Panneau Calques (HP-OBJ-024) : la pile de calques du document, du dessus vers le fond comme dans
// un éditeur d'images. Une ligne par calque (nom, œil, cadenas) ; le calque actif reçoit les formes
// créées. Aucune vérité métier ici : simple présentation + signaux, MainWindow exécute les
// commandes annulables (`commands::*Calque*`).
//
// Un document sans calque affiche un calque implicite « Calque 1 » (identifiant invalide) : le
// premier « + Calque » le matérialise.
class CalquePanel : public QWidget {
    Q_OBJECT

public:
    explicit CalquePanel(QWidget* parent = nullptr);

    // Reconstruit la liste depuis le document (calque actif en gras et sélectionné).
    void refresh(const document::Project& project);

signals:
    void calqueActivated(ObjectId id);
    void addCalqueRequested();
    void removeCalqueRequested(ObjectId id);
    void renameCalqueRequested(ObjectId id);
    // `direction` +1 = vers le dessus de la pile (cousu plus tard), -1 = vers le fond.
    void moveCalqueRequested(ObjectId id, int direction);
    void calqueVisibilityToggled(ObjectId id, bool visible);
    void calqueLockToggled(ObjectId id, bool locked);
    // Menu de ligne : déplace la sélection courante du canevas dans ce calque.
    void moveSelectionToCalqueRequested(ObjectId id);

private:
    [[nodiscard]] QTreeWidgetItem* currentRow() const;
    void updateButtons();
    void showContextMenu(const QPoint& viewportPos);

    QTreeWidget* list_{nullptr};
    QPushButton* addBtn_{nullptr};
    QPushButton* removeBtn_{nullptr};
    QPushButton* upBtn_{nullptr};
    QPushButton* downBtn_{nullptr};
    int calqueCount_{0}; // calques réels du document (0 = calque implicite)
    bool syncing_{false};
};

} // namespace openstitch::desktop
