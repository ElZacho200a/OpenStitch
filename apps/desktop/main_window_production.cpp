// SPDX-License-Identifier: Apache-2.0
// Fichier > Fiche de production… (HP-PROD-001) : ouvre le dialogue d'aperçu / impression /
// export PDF. Aucun calcul ici : la fiche vient de libs/stitch_analysis, sur la séquence
// EFFECTIVE déjà en cache (`sequence_`), jamais sur la séquence brute.
#include <QAction>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>

#include "fabric_params.hpp"
#include "main_window.hpp"
#include "placement_template_dialog.hpp"
#include "production_dialog.hpp"

namespace openstitch::desktop {

void MainWindow::buildProductionMenu(QMenu* fileMenu) {
    productionAct_ = fileMenu->addAction(tr("&Fiche de production…"));
    productionAct_->setObjectName(QStringLiteral("action_productionSheet"));
    productionAct_->setToolTip(
        tr("Aperçu, impression ou export PDF d'une fiche : dimensions, "
           "points, temps estimé, blocs de couleur, avertissements, notes."));
    connect(productionAct_, &QAction::triggered, this, &MainWindow::showProductionSheet);
    // HP-PROD-003 / HP-PROD-004 (fichiers placement_template_dialog.* et print_preview.*).
    auto* templateAct = fileMenu->addAction(tr("Gabarit de placement 1:1…"));
    templateAct->setObjectName(QStringLiteral("action_placementTemplate"));
    templateAct->setToolTip(tr("Imprime le motif à taille réelle (croix de centrage, axes, "
                               "règle de 100 mm), sur plusieurs pages si besoin."));
    connect(templateAct, &QAction::triggered, this, [this] {
        flushPendingRefresh();
        if (!sequence_) {
            QMessageBox::information(this, tr("Gabarit de placement"),
                                     tr("Générez d'abord des points de broderie."));
            return;
        }
        QString name = QFileInfo(currentProjectPath_).completeBaseName();
        if (name.isEmpty()) {
            name = tr("Sans titre");
        }
        PlacementTemplateDialog dialog(*sequence_, name, this);
        dialog.exec();
    });
    auto* previewAct = fileMenu->addAction(tr("Aperçu avant impression…"));
    previewAct->setObjectName(QStringLiteral("action_printPreviewGeneral"));
    previewAct->setToolTip(tr("Aperçu avant impression d'un document (fiche de production, "
                              "gabarit de placement 1:1)."));
    connect(previewAct, &QAction::triggered, this, [this, templateAct] {
        const QStringList kinds{tr("Fiche de production"), tr("Gabarit de placement 1:1")};
        bool ok = false;
        const QString kind =
            QInputDialog::getItem(this, tr("Aperçu avant impression"),
                                  tr("Document à prévisualiser :"), kinds, 0, false, &ok);
        if (!ok) {
            return;
        }
        if (kind == kinds.at(0)) {
            showProductionSheet();
        } else {
            templateAct->trigger();
        }
    });
    auto* renderPngAct = fileMenu->addAction(tr("Exporter le rendu réalis&te (PNG)…"));
    renderPngAct->setObjectName(QStringLiteral("action_exportRealisticPng"));
    renderPngAct->setToolTip(tr("Image du rendu réaliste (fils texturés) du motif complet."));
    connect(renderPngAct, &QAction::triggered, this, &MainWindow::exportRealisticPng);
    buildExportImageAction(fileMenu); // HP-FMT-011 (main_window_export_image.cpp)
}

void MainWindow::showProductionSheet() {
    flushPendingRefresh(); // HP-PERF-009
    if (!sequence_) {
        QMessageBox::information(this, tr("Fiche de production"),
                                 tr("Générez d'abord des points de broderie."));
        return;
    }
    QString name = QFileInfo(currentProjectPath_).completeBaseName();
    if (name.isEmpty()) {
        name = tr("Sans titre");
    }
    ProductionDialog dialog(project_, *sequence_, name,
                            withProjectFabric(realistic_.prefs.params, project_), this);
    dialog.exec();
}

} // namespace openstitch::desktop
