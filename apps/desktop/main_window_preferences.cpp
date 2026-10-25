// SPDX-License-Identifier: Apache-2.0
// Préférences générales (HP-UX-007/008) et insertion d'un design dans le projet ouvert
// (HP-FILE-007). Câblage uniquement : la fusion est `commands::InsertDesignCommand`.
#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>

#include <filesystem>

#include "general_preferences_dialog.hpp"
#include "length_units.hpp"
#include "main_window.hpp"
#include "openstitch/commands/insert_design_command.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "ruler.hpp"

namespace openstitch::desktop {

void MainWindow::openGeneralPreferences() {
    GeneralPreferencesDialog dialog(this);
    dialog.setPreferences(loadGeneralPreferences());
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const GeneralPreferences prefs = dialog.preferences();
    saveGeneralPreferences(prefs);
    gridStepMm_ = prefs.gridStepMm;
    if (autosaveTimer_ != nullptr) {
        autosaveTimer_->setInterval(prefs.autosaveMinutes * 60'000);
    }
    // Les règles relisent l'unité à chaque tracé ; la position du curseur à son prochain mouvement.
    for (auto* ruler : findChildren<Ruler*>()) {
        ruler->update();
    }
    statusBar()->showMessage(
        tr("Préférences enregistrées — unité : %1").arg(length_units::suffix(prefs.unit)), 5000);
}

void MainWindow::insertDesign() {
    QStringList all{QStringLiteral("*.osp")};
    for (const auto& f : formats::registered_formats()) {
        if (!f.can_read || f.decode == nullptr) {
            continue;
        }
        for (const auto& e : f.extensions) {
            all << QStringLiteral("*.") + QString::fromStdString(e);
        }
    }
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Insérer un design"), QString(),
        tr("Projet ou broderie machine (%1)").arg(all.join(QLatin1Char(' '))));
    if (!file.isEmpty()) {
        (void)insertDesignFile(file);
    }
}

bool MainWindow::insertDesignFile(const QString& file) {
    const std::filesystem::path path(file.toStdWString());
    const QString suffix = QFileInfo(file).suffix().toLower();
    document::Project incoming;
    if (suffix == QLatin1String("osp")) {
        auto loaded = project_io::load_project(path);
        if (!loaded) {
            QMessageBox::warning(this, tr("Insertion impossible"),
                                 tr("Le fichier « %1 » n'a pas pu être lu comme projet "
                                    "OpenStitch.")
                                     .arg(QFileInfo(file).fileName()));
            return false;
        }
        incoming = std::move(*loaded);
    } else {
        auto machine = project_io::import_machine_file(path);
        if (!machine) {
            QMessageBox::warning(this, tr("Insertion impossible"),
                                 tr("Ce fichier n'a pas pu être lu comme broderie machine."));
            return false;
        }
        incoming.imported_design = std::move(*machine);
    }
    std::string why;
    auto command = commands::make_insert_design_command(
        project_, std::move(incoming), Vec2um{Micrometers{0}, Micrometers{0}}, &why);
    if (!command) {
        QMessageBox::information(this, tr("Insertion impossible"),
                                 tr("Ce design ne peut pas être inséré dans le projet courant "
                                    "(%1).")
                                     .arg(QString::fromStdString(why)));
        return false;
    }
    undoStack_.execute(std::move(command), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Design inséré : %1 (annulable)").arg(QFileInfo(file).fileName()));
    return true;
}

} // namespace openstitch::desktop
