// SPDX-License-Identifier: Apache-2.0
// Moteur de points : satin de bordure (HP-STI-004) et entrée/sortie automatiques par objet
// (HP-ENG-010). Membres de MainWindow séparés de main_window.cpp. Aucune logique métier ici :
// les rails viennent de `stitch_generation::border_satin_*`, toute mutation passe par une
// commande annulable.
#include <QMessageBox>
#include <QStatusBar>

#include <algorithm>

#include "main_window.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/stitch_generation/border_satin.hpp"
#include "properties_panel.hpp"

namespace openstitch::desktop {

namespace {

document::BorderSatinSpec makeSpec(double widthMm, int side, int corner) {
    document::BorderSatinSpec spec;
    spec.width = stitch_generation::clamp_border_width(to_micrometers(Millimeters{widthMm}));
    spec.side = static_cast<document::BorderSide>(std::clamp(side, 0, 2));
    spec.corner = static_cast<document::BorderCorner>(std::clamp(corner, 0, 1));
    return spec;
}

} // namespace

void MainWindow::createBorderSatin(ObjectId vectorId, double widthMm, int side, int corner) {
    const auto* source = project_.findObject(vectorId);
    if (source == nullptr) {
        return;
    }
    auto satins =
        stitch_generation::border_satin_from_paths(source->paths, makeSpec(widthMm, side, corner));
    if (satins.empty()) {
        QMessageBox::warning(this, tr("Satin de bordure"),
                             tr("Ce contour est trop petit ou dégénéré pour un satin de bordure."));
        return;
    }
    auto group =
        std::make_unique<commands::CompositeCommand>(tr("Créer un satin de bordure").toStdString());
    std::optional<ObjectId> firstId;
    for (auto& params : satins) {
        document::EmbroideryObject object;
        object.id = project_.object_ids.next();
        object.name = tr("Bordure de %1").arg(QString::fromStdString(source->name)).toStdString();
        object.source_vector = source->id;
        object.rgb = source->rgb;
        object.params = std::move(params);
        object.intent = document::EmbroideryIntent::ForcedUserChoice;
        if (!firstId) {
            firstId = object.id;
        }
        group->add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(object)));
    }
    undoStack_.execute(std::move(group), project_);
    if (firstId) {
        editSelection([id = *firstId](Selection& sel) { sel.embroidery = id; });
    }
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Satin de bordure créé (%1 colonne(s))").arg(satins.size()));
}

void MainWindow::editBorderSatin(ObjectId id, double widthMm, int side, int corner) {
    const auto* emb = project_.findEmbroidery(id);
    const auto* satin = emb != nullptr ? std::get_if<document::SatinParams>(&emb->params) : nullptr;
    const auto* source = emb != nullptr ? project_.findObject(emb->source_vector) : nullptr;
    if (satin == nullptr || !satin->border || source == nullptr) {
        return;
    }
    auto regenerated = stitch_generation::regenerate_border_satin(source->paths, *satin,
                                                                  makeSpec(widthMm, side, corner));
    if (!regenerated) {
        statusBar()->showMessage(tr("Le contour source de cette bordure n'existe plus."));
        return;
    }
    undoStack_.execute(std::make_unique<commands::SetStitchParamsCommand>(
                           id, document::StitchParams{std::move(*regenerated)},
                           tr("Satin de bordure").toStdString()),
                       project_);
    if (const auto* now = project_.findEmbroidery(id)) {
        propertiesPanel_->adoptParams(id, now->params);
    }
    refreshImage();
    updateActions();
}

void MainWindow::setJoinMode(ObjectId id, int mode) {
    if (project_.findEmbroidery(id) == nullptr) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::SetEmbroideryJoinModeCommand>(
                           id, static_cast<document::JoinMode>(std::clamp(mode, 0, 2))),
                       project_);
    refreshImage();
    updateActions();
}

} // namespace openstitch::desktop
