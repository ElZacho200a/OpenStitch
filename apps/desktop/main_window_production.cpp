// SPDX-License-Identifier: Apache-2.0
// Fichier > Fiche de production… (HP-PROD-001) : ouvre le dialogue d'aperçu / impression /
// export PDF. Aucun calcul ici : la fiche vient de libs/stitch_analysis, sur la séquence
// EFFECTIVE déjà en cache (`sequence_`), jamais sur la séquence brute.
#include <QAction>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>

#include "main_window.hpp"
#include "production_dialog.hpp"

namespace openstitch::desktop {

void MainWindow::buildProductionMenu(QMenu* fileMenu) {
    productionAct_ = fileMenu->addAction(tr("&Fiche de production…"));
    productionAct_->setObjectName(QStringLiteral("action_productionSheet"));
    productionAct_->setToolTip(
        tr("Aperçu, impression ou export PDF d'une fiche : dimensions, "
           "points, temps estimé, blocs de couleur, avertissements, notes."));
    connect(productionAct_, &QAction::triggered, this, &MainWindow::showProductionSheet);
}

void MainWindow::showProductionSheet() {
    if (!sequence_) {
        QMessageBox::information(this, tr("Fiche de production"),
                                 tr("Générez d'abord des points de broderie."));
        return;
    }
    QString name = QFileInfo(currentProjectPath_).completeBaseName();
    if (name.isEmpty()) {
        name = tr("Sans titre");
    }
    ProductionDialog dialog(project_, *sequence_, name, realistic_.prefs.params, this);
    dialog.exec();
}

} // namespace openstitch::desktop
