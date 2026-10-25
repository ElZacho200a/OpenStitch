// SPDX-License-Identifier: Apache-2.0
// Moteur de points : satin de bordure (HP-STI-004) et entrée/sortie automatiques par objet
// (HP-ENG-010). Membres de MainWindow séparés de main_window.cpp. Aucune logique métier ici :
// les rails viennent de `stitch_generation::border_satin_*`, toute mutation passe par une
// commande annulable.
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <future>

#include "main_window.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/stitch_generation/border_satin.hpp"
#include "properties_panel.hpp"
#include "ui_memory.hpp"

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

void MainWindow::setConnectorMode(ObjectId id, int mode) {
    if (project_.findEmbroidery(id) == nullptr) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::SetEmbroideryConnectorModeCommand>(
                           id, static_cast<document::ConnectorMode>(std::clamp(mode, 0, 2))),
                       project_);
    refreshImage();
    updateActions();
}

// --- HP-PERF-009 : régénération différée -----------------------------------------------------

// Calcul en cours sur un instantané du projet. L'instantané est tenu par la tâche elle-même (pas
// de cycle avec cette structure) ; `serial` date la demande : toute édition ou tout
// rafraîchissement ultérieur la rend périmée.
struct MainWindow::AsyncRefresh {
    std::future<Result<stitch_generation::RefreshContext>> future;
    std::uint64_t serial{0};
    std::optional<ObjectId> target;
};

void MainWindow::commitCreationMemory() {
    if (pendingCreationMemory_) {
        ui_memory::creation::remember(*pendingCreationMemory_);
        pendingCreationMemory_.reset();
    }
}

void MainWindow::scheduleParamsRefresh() {
    if (paramsRefreshTimer_ == nullptr) {
        paramsRefreshTimer_ = new QTimer(this);
        paramsRefreshTimer_->setSingleShot(true);
        paramsRefreshTimer_->setInterval(kParamsRefreshDelayMs);
        connect(paramsRefreshTimer_, &QTimer::timeout, this, [this] {
            commitCreationMemory();
            startAsyncRefresh();
        });
        asyncPollTimer_ = new QTimer(this);
        asyncPollTimer_->setInterval(20);
        connect(asyncPollTimer_, &QTimer::timeout, this, &MainWindow::pollAsyncRefresh);
    }
    // Ce qui ne dépend pas des points se met à jour tout de suite : marqueur « modifié »,
    // annuler/rétablir, indicateurs.
    setWindowModified(!undoStack_.isClean());
    updateActions();
    ++refreshSerial_; // tout résultat déjà en route est périmé
    paramsRefreshPending_ = true;
    // HP-VIEW-012 : un objet isolé / un petit motif se régénère en quelques millisecondes, donc
    // l'aperçu suit presque sans attente ; un gros motif garde le débounce complet.
    constexpr std::size_t kLiveMaxCommands = 20'000;
    constexpr int kLiveDelayMs = 40;
    const bool small = sequence_ && sequence_->commands.size() <= kLiveMaxCommands;
    paramsRefreshTimer_->start(small ? kLiveDelayMs : kParamsRefreshDelayMs); // réarme la rafale
}

void MainWindow::flushPendingRefresh() {
    if (!paramsRefreshPending_) {
        return;
    }
    if (paramsRefreshTimer_ != nullptr) {
        paramsRefreshTimer_->stop();
    }
    commitCreationMemory();
    // refreshImage() incrémente la série : un calcul encore en route sera rejeté à son retour.
    refreshImage();
    updateActions();
}

void MainWindow::startAsyncRefresh() {
    if (!paramsRefreshPending_ || asyncRefresh_ != nullptr) {
        return; // rien à faire, ou calcul déjà en cours : pollAsyncRefresh relancera au besoin
    }
    auto state = std::make_shared<AsyncRefresh>();
    state->serial = refreshSerial_;
    state->target = stitchEditTarget_;
    auto snapshot = std::make_shared<const document::Project>(project_);
    auto hook = refreshWorkerHook_;
    const auto target = state->target;
    state->future = std::async(std::launch::async, [snapshot, target, hook] {
        if (hook) {
            hook();
        }
        return stitch_generation::refresh_context(*snapshot, target);
    });
    asyncRefresh_ = std::move(state);
    asyncPollTimer_->start();
}

void MainWindow::pollAsyncRefresh() {
    if (asyncRefresh_ == nullptr) {
        asyncPollTimer_->stop();
        return;
    }
    if (asyncRefresh_->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    const std::shared_ptr<AsyncRefresh> state = std::move(asyncRefresh_);
    asyncRefresh_.reset();
    asyncPollTimer_->stop();
    auto result = state->future.get();
    if (state->serial != refreshSerial_) {
        // Périmé (nouvelle édition, annulation, autre mutation) : rejeté. Si des éditions
        // attendent encore et que le débounce n'est pas armé, on repart sur le document actuel.
        if (paramsRefreshPending_ && !paramsRefreshTimer_->isActive()) {
            startAsyncRefresh();
        }
        return;
    }
    if (result && state->target == stitchEditTarget_) {
        precomputedContext_ = std::move(*result);
    }
    refreshImage();
    precomputedContext_.reset();
    updateActions();
}

} // namespace openstitch::desktop
