// SPDX-License-Identifier: Apache-2.0
// Panneau « Fils » de la fenêtre principale (HP-THR-003/004/005) : câblage UNIQUEMENT.
// Chaque action calcule son plan dans les bibliothèques (stitch_analysis::thread_usage,
// thread_palette) puis l'exécute comme UNE commande annulable (CompositeCommand /
// SetObjectThreadCommand / ReorderEmbroideryCommand) : aucune règle métier ici.
#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStatusBar>

#include <algorithm>
#include <map>

#include "main_window.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/project_io/thread_chart_io.hpp"
#include "openstitch/stitch_analysis/thread_usage.hpp"
#include "openstitch/thread_palette/chart_import.hpp"
#include "openstitch/thread_palette/color_reduction.hpp"
#include "thread_panel.hpp"

namespace openstitch::desktop {

namespace {

QString userChartsDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/thread_charts");
}

} // namespace

std::vector<ObjectId> MainWindow::selectedEmbroideryIds() const {
    std::vector<ObjectId> ids;
    const auto add = [&ids](ObjectId id) {
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
            ids.push_back(id);
        }
    };
    if (selectedEmbroidery_) {
        add(*selectedEmbroidery_);
    }
    for (const ObjectId vectorId : selectedObjectIds()) {
        for (const auto& obj : project_.embroidery_objects) {
            if (obj.source_vector == vectorId) {
                add(obj.id);
            }
        }
    }
    return ids;
}

void MainWindow::buildThreadPanel() {
    threadDock_ = new QDockWidget(tr("Fils"), this);
    threadDock_->setObjectName(QStringLiteral("threadDock"));
    threadDock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    threadPanel_ = new ThreadPanel(threadDock_);
    threadPanel_->setObjectName(QStringLiteral("threadPanel"));
    threadDock_->setWidget(threadPanel_);
    threadDock_->setAccessibleName(threadDock_->windowTitle());
    addDockWidget(Qt::RightDockWidgetArea, threadDock_);
    threadDock_->hide();
    panelsMenu_->addAction(threadDock_->toggleViewAction());

    // Nuanciers importés par l'utilisateur : fichiers copiés dans le dossier de données de
    // l'application, relus à chaque lancement (jamais embarqués : voir docs).
    const QDir dir(userChartsDir());
    for (const QFileInfo& info : dir.entryInfoList(
             {QStringLiteral("*.csv"), QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        auto chart = project_io::read_thread_chart_file(info.absoluteFilePath().toStdWString());
        if (chart && threadLibrary_.add_chart(*chart)) {
            userChartFiles_.insert(QString::fromStdString(chart->chart_id),
                                   info.absoluteFilePath());
        }
    }
    threadPanel_->setLibrary(&threadLibrary_);

    auto* menu = menuBar()->addMenu(tr("Fi&ls"));
    menu->addAction(threadDock_->toggleViewAction());
    auto* importAct = menu->addAction(tr("&Importer un nuancier (CSV, JSON)…"));
    importAct->setObjectName(QStringLiteral("action_importThreadChart"));
    auto* formatAct = menu->addAction(tr("&Format d'import des nuanciers…"));
    auto* exportAct = menu->addAction(tr("&Exporter la liste des fils (CSV)…"));
    exportAct->setObjectName(QStringLiteral("action_exportThreadList"));

    connect(threadDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            refreshThreadPanel();
        }
    });
    connect(importAct, &QAction::triggered, threadPanel_, &ThreadPanel::importChartRequested);
    connect(exportAct, &QAction::triggered, threadPanel_, &ThreadPanel::exportListRequested);
    connect(formatAct, &QAction::triggered, this, [this] {
        QMessageBox::information(
            this, tr("Format d'import des nuanciers"),
            tr("<p>Les cartes de fils des marques ne sont pas fournies avec OpenStitch : "
               "importez les vôtres.</p>"
               "<p><b>CSV</b> (UTF-8, séparateur « , » ou « ; », en-tête obligatoire) :<br>"
               "<code>code,name,hex</code> ou <code>code,name,r,g,b</code><br>"
               "colonnes facultatives : <code>brand</code>, <code>range</code>.</p>"
               "<p><b>JSON</b> :<br><code>{\"name\":\"Mes fils\",\"threads\":[{\"code\":\"1001\","
               "\"name\":\"Rouge\",\"rgb\":\"#C8102E\"}]}</code></p>"
               "<p>Le nom du fichier (CSV) ou « name » (JSON) donne le nom du nuancier ; les "
               "codes doivent être uniques.</p>"));
    });

    connect(
        threadPanel_, &ThreadPanel::assignThreadRequested, this,
        [this](const thread_palette::Thread& t) {
            const auto ids = selectedEmbroideryIds();
            if (ids.empty()) {
                statusBar()->showMessage(
                    tr("Sélectionnez d'abord un ou plusieurs objets de broderie."), 5000);
                return;
            }
            undoStack_.execute(
                std::make_unique<commands::SetObjectThreadCommand>(
                    ids, t.key, t.rgb,
                    tr("Assigner le fil %1").arg(QString::fromStdString(t.key.code)).toStdString()),
                project_);
            refreshImage();
            updateActions();
            statusBar()->showMessage(tr("Fil %1 assigné à %2 objet(s).")
                                         .arg(QString::fromStdString(t.key.code))
                                         .arg(ids.size()),
                                     5000);
        });

    const auto selectEmbroideries = [this](const std::vector<ObjectId>& embroideryIds) {
        std::vector<ObjectId> vectors;
        for (const ObjectId id : embroideryIds) {
            const auto* emb = project_.findEmbroidery(id);
            if (emb != nullptr && project_.findObject(emb->source_vector) != nullptr &&
                std::find(vectors.begin(), vectors.end(), emb->source_vector) == vectors.end()) {
                vectors.push_back(emb->source_vector);
            }
        }
        if (vectors.empty()) {
            statusBar()->showMessage(tr("Aucun objet à sélectionner."), 4000);
            return;
        }
        setSelection({.region = std::nullopt,
                      .embroidery = std::nullopt,
                      .objects = vectors,
                      .extraRegions = {}});
        displayImage(processed_);
        syncDocumentSelection();
        updateActions();
        statusBar()->showMessage(tr("%1 forme(s) sélectionnée(s).").arg(vectors.size()), 4000);
    };

    connect(threadPanel_, &ThreadPanel::selectUsageObjectsRequested, this,
            [this, selectEmbroideries](const stitch_analysis::ThreadIdentity& id) {
                selectEmbroideries(stitch_analysis::objects_using_thread(project_, id));
            });

    connect(threadPanel_, &ThreadPanel::replaceUsageRequested, this,
            [this](const stitch_analysis::ThreadIdentity& from, const thread_palette::Thread& t) {
                const auto ids = stitch_analysis::objects_using_thread(project_, from);
                if (ids.empty()) {
                    return;
                }
                undoStack_.execute(std::make_unique<commands::SetObjectThreadCommand>(
                                       ids, t.key, t.rgb, tr("Remplacer un fil").toStdString()),
                                   project_);
                refreshImage();
                updateActions();
                statusBar()->showMessage(tr("Fil remplacé sur %1 objet(s).").arg(ids.size()), 5000);
            });

    connect(
        threadPanel_, &ThreadPanel::snapFreeColorsRequested, this, [this](const QString& chartId) {
            auto ids = selectedEmbroideryIds();
            if (ids.empty()) {
                for (const auto& obj : project_.embroidery_objects) {
                    ids.push_back(obj.id);
                }
            }
            std::vector<commands::ObjectThreadAssignment> plan;
            for (const ObjectId id : ids) {
                const auto* obj = project_.findEmbroidery(id);
                if (obj == nullptr || obj->thread) {
                    continue;
                }
                const auto match = threadLibrary_.nearest(obj->rgb, chartId.toStdString(), 1);
                if (match.empty()) {
                    continue;
                }
                if (const auto t = threadLibrary_.find(match.front().key)) {
                    plan.push_back({id, t->key, t->rgb});
                }
            }
            if (plan.empty()) {
                statusBar()->showMessage(tr("Aucune couleur libre à associer."), 4000);
                return;
            }
            const std::size_t count = plan.size();
            undoStack_.execute(
                std::make_unique<commands::SetObjectThreadCommand>(
                    std::move(plan), tr("Couleurs libres → fils les plus proches").toStdString()),
                project_);
            refreshImage();
            updateActions();
            statusBar()->showMessage(tr("%1 objet(s) associé(s) au fil le plus proche.").arg(count),
                                     5000);
        });

    connect(threadPanel_, &ThreadPanel::reduceColorsRequested, this, [this](int maxThreads) {
        if (!sequence_) {
            return;
        }
        const auto usage = stitch_analysis::thread_usage(project_, *sequence_, &threadLibrary_);
        std::vector<thread_palette::WeightedColor> colors;
        for (const auto& u : usage) {
            colors.push_back({u.identity.rgb, static_cast<double>(u.stitch_count)});
        }
        const auto reduction =
            thread_palette::reduce_colors(colors, static_cast<std::size_t>(maxThreads));
        if (reduction.palette.size() >= usage.size()) {
            statusBar()->showMessage(
                tr("Le motif utilise déjà %1 fil(s) ou moins.").arg(maxThreads), 5000);
            return;
        }
        // Entrée cible de chaque couleur retenue : le fil le plus cousu parmi ceux qui la portent.
        std::vector<commands::ObjectThreadAssignment> plan;
        for (std::size_t i = 0; i < usage.size(); ++i) {
            const auto& kept = reduction.palette[reduction.mapping[i]];
            if (usage[i].identity.rgb == kept) {
                continue; // cette entrée est (équivalente à) la représentante
            }
            const stitch_analysis::ThreadUsage* target = nullptr;
            for (const auto& u : usage) {
                if (u.identity.rgb == kept &&
                    (target == nullptr || u.stitch_count > target->stitch_count)) {
                    target = &u;
                }
            }
            for (const ObjectId id : usage[i].objects) {
                plan.push_back({id, target->identity.key, kept});
            }
        }
        if (plan.empty()) {
            return;
        }
        undoStack_.execute(
            std::make_unique<commands::SetObjectThreadCommand>(
                std::move(plan), tr("Limiter à %1 fils").arg(maxThreads).toStdString()),
            project_);
        refreshImage();
        updateActions();
        statusBar()->showMessage(tr("Palette réduite de %1 à %2 fil(s) — Ctrl+Z pour annuler.")
                                     .arg(usage.size())
                                     .arg(reduction.palette.size()),
                                 7000);
    });

    connect(threadPanel_, &ThreadPanel::nearestRequested, this, [this](const QString& chartId) {
        std::array<std::uint8_t, 3> rgb{};
        bool have = false;
        for (const ObjectId id : selectedEmbroideryIds()) {
            if (const auto* obj = project_.findEmbroidery(id)) {
                rgb = obj->rgb;
                have = true;
                break;
            }
        }
        if (!have) {
            if (const auto cur = threadPanel_->currentUsage()) {
                rgb = cur->rgb;
                have = true;
            }
        }
        if (!have) {
            statusBar()->showMessage(
                tr("Sélectionnez un objet (ou un fil du projet) pour chercher le plus proche."),
                5000);
            return;
        }
        std::vector<thread_palette::Thread> found;
        for (const auto& m : threadLibrary_.nearest(rgb, chartId.toStdString(), 8)) {
            if (const auto t = threadLibrary_.find(m.key)) {
                found.push_back(*t);
            }
        }
        threadPanel_->showMatches(
            found, tr("Fils les plus proches de %1 (distance CIEDE2000)")
                       .arg(QString::fromStdString(thread_palette::to_hex_color(rgb))));
    });

    connect(threadPanel_, &ThreadPanel::importChartRequested, this, [this] {
        const QString path =
            QFileDialog::getOpenFileName(this, tr("Importer un nuancier"), QString(),
                                         tr("Nuanciers (*.csv *.json);;Tous les fichiers (*)"));
        if (path.isEmpty()) {
            return;
        }
        auto chart = project_io::read_thread_chart_file(path.toStdWString());
        if (!chart) {
            QMessageBox::warning(this, tr("Import du nuancier"),
                                 QString::fromStdString(chart.error().message));
            return;
        }
        if (auto added = threadLibrary_.add_chart(*chart); !added) {
            QMessageBox::warning(this, tr("Import du nuancier"),
                                 QString::fromStdString(added.error().message));
            return;
        }
        QDir().mkpath(userChartsDir());
        const QString target = userChartsDir() + QLatin1Char('/') + QFileInfo(path).fileName();
        QFile::remove(target);
        if (QFile::copy(path, target)) {
            userChartFiles_.insert(QString::fromStdString(chart->chart_id), target);
        }
        threadPanel_->refreshCharts();
        statusBar()->showMessage(tr("Nuancier « %1 » importé : %2 fil(s).")
                                     .arg(QString::fromStdString(chart->display_name))
                                     .arg(chart->threads.size()),
                                 6000);
    });

    connect(threadPanel_, &ThreadPanel::removeChartRequested, this, [this](const QString& id) {
        const auto answer = QMessageBox::question(
            this, tr("Retirer le nuancier"),
            tr("Retirer ce nuancier de la bibliothèque ? Les objets qui l'utilisent gardent "
               "leur couleur ; seul le nom du fil ne sera plus affiché."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
        if (threadLibrary_.remove_chart(id.toStdString())) {
            QFile::remove(userChartFiles_.take(id));
            threadPanel_->refreshCharts();
            refreshThreadPanel();
        }
    });

    connect(threadPanel_, &ThreadPanel::exportListRequested, this, [this] {
        if (!sequence_) {
            statusBar()->showMessage(tr("Aucun point généré : rien à exporter."), 4000);
            return;
        }
        const QString path =
            QFileDialog::getSaveFileName(this, tr("Exporter la liste des fils"),
                                         QStringLiteral("fils.csv"), tr("Fichier CSV (*.csv)"));
        if (path.isEmpty()) {
            return;
        }
        const std::string csv = stitch_analysis::thread_usage_csv(
            stitch_analysis::thread_usage(project_, *sequence_, &threadLibrary_));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, tr("Export des fils"),
                                 tr("Impossible d'écrire le fichier : %1").arg(path));
            return;
        }
        file.write(csv.data(), static_cast<qint64>(csv.size()));
        statusBar()->showMessage(tr("Liste des fils exportée : %1").arg(path), 6000);
    });

    // ---- Film couleur ----------------------------------------------------------------------
    connect(threadPanel_, &ThreadPanel::filmBlockMoved, this, [this](int from, int to) {
        if (!sequence_ || from < 0 || to < 0) {
            refreshThreadPanel();
            return;
        }
        const auto film = stitch_analysis::color_film(project_, *sequence_);
        const auto order = stitch_analysis::reorder_film_blocks(
            project_, film, static_cast<std::size_t>(from), static_cast<std::size_t>(to));
        std::vector<ObjectId> current;
        for (const auto& obj : project_.embroidery_objects) {
            current.push_back(obj.id);
        }
        if (!order || *order == current) {
            statusBar()->showMessage(
                tr("Ce bloc ne peut pas bouger : les objets figés gardent leur place."), 5000);
            refreshThreadPanel();
            return;
        }
        undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(*order), project_);
        refreshImage();
        updateActions();
    });

    connect(threadPanel_, &ThreadPanel::filmBlockSelected, this,
            [this, selectEmbroideries](int row) {
                if (!sequence_) {
                    return;
                }
                const auto film = stitch_analysis::color_film(project_, *sequence_);
                if (row >= 0 && static_cast<std::size_t>(row) < film.size()) {
                    selectEmbroideries(film[static_cast<std::size_t>(row)].objects);
                }
            });

    connect(threadPanel_, &ThreadPanel::mergeFilmBlocksRequested, this, [this] {
        if (!sequence_) {
            return;
        }
        const auto film = stitch_analysis::color_film(project_, *sequence_);
        const auto order = stitch_analysis::merge_same_thread_blocks(project_, film);
        std::vector<ObjectId> current;
        for (const auto& obj : project_.embroidery_objects) {
            current.push_back(obj.id);
        }
        if (order == current) {
            statusBar()->showMessage(tr("Aucun bloc de même fil à regrouper."), 5000);
            return;
        }
        const std::size_t before = stitch_analysis::color_change_count(film);
        undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order), project_);
        refreshImage();
        updateActions();
        const std::size_t after = sequence_ ? stitch_analysis::color_change_count(
                                                  stitch_analysis::color_film(project_, *sequence_))
                                            : before;
        statusBar()->showMessage(
            tr("Blocs regroupés : %1 → %2 changement(s) de fil — Ctrl+Z pour annuler (l'ordre "
               "des couches a changé).")
                .arg(before)
                .arg(after),
            9000);
    });
}

void MainWindow::refreshThreadPanel() {
    if (threadPanel_ == nullptr || threadDock_ == nullptr || !threadDock_->isVisible()) {
        return;
    }
    if (sequence_) {
        threadPanel_->setUsage(
            stitch_analysis::thread_usage(project_, *sequence_, &threadLibrary_));
        threadPanel_->setFilm(stitch_analysis::color_film(project_, *sequence_));
    } else {
        threadPanel_->setUsage({});
        threadPanel_->setFilm({});
    }
    refreshThreadSelectionInfo();
}

void MainWindow::refreshThreadSelectionInfo() {
    if (threadPanel_ == nullptr || threadDock_ == nullptr || !threadDock_->isVisible()) {
        return;
    }
    const auto ids = selectedEmbroideryIds();
    QString text;
    if (ids.empty()) {
        text = tr("Aucun objet sélectionné : choisir un fil du catalogue l'assignera à la "
                  "sélection.");
    } else if (ids.size() == 1) {
        const auto* obj = project_.findEmbroidery(ids.front());
        text = tr("Sélection : %1")
                   .arg(obj != nullptr ? QString::fromStdString(obj->name) : QString());
        if (obj != nullptr && obj->thread) {
            text += tr(" — fil %1 %2")
                        .arg(QString::fromStdString(obj->thread->chart_id),
                             QString::fromStdString(obj->thread->code));
        }
    } else {
        text = tr("Sélection : %1 objets de broderie").arg(ids.size());
    }
    threadPanel_->setSelectionInfo(text);
}

} // namespace openstitch::desktop
