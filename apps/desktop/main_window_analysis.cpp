// SPDX-License-Identifier: Apache-2.0
// Panneau « Analyse » (libs/stitch_analysis) : liste des problèmes détectés avec gravité en
// toutes lettres, compteurs et filtre, objet fautif nommé, sélection + centrage au clic ou à
// Entrée, indice de correction, menu contextuel, plafond par catégorie visible, résultat périmé
// signalé et ré-analyse différée quand le document change.
#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <map>

#include "app_theme.hpp"
#include "canvas_view.hpp"
#include "document_panel.hpp"
#include "main_window.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_analysis/text_rules.hpp"

namespace openstitch::desktop {

namespace {

constexpr int kSeverityRole = Qt::UserRole + 1; // 0 info, 1 avertissement, 2 erreur, -1 note
constexpr int kObjectRole = Qt::UserRole + 2;   // id de l'objet de broderie (0 = global)
constexpr int kHintRole = Qt::UserRole + 3;     // piste de correction
constexpr int kHasLocationRole = Qt::UserRole + 4;

int severityRank(stitch_analysis::Severity v) {
    switch (v) {
    case stitch_analysis::Severity::Info:
        return 0;
    case stitch_analysis::Severity::Warning:
        return 1;
    case stitch_analysis::Severity::Error:
        return 2;
    }
    return 0;
}

QString severityWord(int rank) {
    return rank == 2   ? QObject::tr("Erreur")
           : rank == 1 ? QObject::tr("Avertissement")
                       : QObject::tr("Information");
}

QString severityGlyph(int rank) {
    return rank == 2   ? QStringLiteral("⛔ ")
           : rank == 1 ? QStringLiteral("⚠ ")
                       : QStringLiteral("ℹ ");
}

} // namespace

void MainWindow::buildAnalysisPanel() {
    analysisDock_ = new QDockWidget(tr("Analyse"), this);
    analysisDock_->setObjectName(QStringLiteral("analysisDock"));
    analysisDock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);

    auto* container = new QWidget(analysisDock_);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Compteurs par gravité + filtre.
    auto* header = new QHBoxLayout();
    analysisSummary_ = new QLabel(tr("Aucune analyse."), container);
    analysisSummary_->setWordWrap(true);
    analysisSummary_->setAccessibleName(tr("Résumé de l'analyse"));
    header->addWidget(analysisSummary_, 1);
    analysisFilter_ = new QComboBox(container);
    analysisFilter_->setAccessibleName(tr("Filtrer les problèmes par gravité"));
    analysisFilter_->addItem(tr("Tout"), -1);
    analysisFilter_->addItem(tr("Erreurs"), 2);
    analysisFilter_->addItem(tr("Avertissements"), 1);
    analysisFilter_->addItem(tr("Informations"), 0);
    header->addWidget(analysisFilter_);
    layout->addLayout(header);

    analysisStale_ = new QLabel(tr("Résultat périmé : le motif a changé, mise à jour…"), container);
    analysisStale_->setWordWrap(true);
    analysisStale_->setObjectName(QStringLiteral("analysisStaleLabel"));
    analysisStale_->setStyleSheet(
        QStringLiteral("color:%1;").arg(AppTheme::instance().tokens().warning.name()));
    analysisStale_->hide();
    layout->addWidget(analysisStale_);

    analysisList_ = new QListWidget(container);
    analysisList_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(analysisList_, 1);

    analysisHint_ = new QLabel(container);
    analysisHint_->setWordWrap(true);
    analysisHint_->setAccessibleName(tr("Piste de correction"));
    markSecondaryText(analysisHint_);
    layout->addWidget(analysisHint_);

    analysisDock_->setWidget(container);
    addDockWidget(Qt::RightDockWidgetArea, analysisDock_);
    analysisDock_->hide();

    analysisTimer_ = new QTimer(this);
    analysisTimer_->setSingleShot(true);
    analysisTimer_->setInterval(300);
    connect(analysisTimer_, &QTimer::timeout, this, [this] { runAnalysisInternal(false); });
    // Ouvrir le dock sur un résultat périmé le met à jour.
    connect(analysisDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && analysisIsStale_) {
            analysisTimer_->start();
        }
    });
    connect(analysisFilter_, &QComboBox::currentIndexChanged, this,
            [this] { rebuildAnalysisList(); });

    // Clic ou Entrée sur un problème : sélectionne l'objet fautif et centre la vue dessus.
    connect(analysisList_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) { activateAnalysisItem(item, true); });
    connect(analysisList_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) { activateAnalysisItem(item, true); });
    connect(analysisList_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) {
                const QString hint =
                    current != nullptr ? current->data(kHintRole).toString() : QString();
                analysisHint_->setText(hint.isEmpty() ? QString() : tr("Piste : %1").arg(hint));
            });
    connect(analysisList_, &QListWidget::customContextMenuRequested, this, [this](QPoint pos) {
        QListWidgetItem* item = analysisList_->itemAt(pos);
        if (item == nullptr || item->data(kSeverityRole).toInt() < 0) {
            return;
        }
        QMenu menu(this);
        const ObjectId id{item->data(kObjectRole).toULongLong()};
        auto* selectAct = menu.addAction(tr("&Sélectionner l'objet"));
        selectAct->setEnabled(id.value != 0 && project_.findEmbroidery(id) != nullptr);
        auto* centerAct = menu.addAction(tr("&Centrer la vue sur le problème"));
        centerAct->setEnabled(item->data(kHasLocationRole).toBool());
        QAction* chosen = menu.exec(analysisList_->viewport()->mapToGlobal(pos));
        if (chosen == selectAct) {
            activateAnalysisItem(item, true);
        } else if (chosen == centerAct) {
            activateAnalysisItem(item, false);
        }
    });

    auto* analyseMenu = menuBar()->addMenu(tr("A&nalyse"));
    analyzeAct_ = analyseMenu->addAction(tr("&Analyser le motif"));
    analyseMenu->addAction(statsAct_);
    analyzeAct_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(analyzeAct_, &QAction::triggered, this, &MainWindow::runAnalysis);
}

void MainWindow::runAnalysis() {
    runAnalysisInternal(true);
}

void MainWindow::runAnalysisInternal(bool explicitRequest) {
    analysisTimer_->stop();
    if (!sequence_) {
        if (explicitRequest) {
            QMessageBox::information(this, tr("Analyse"),
                                     tr("Générez d'abord des points de broderie."));
            return;
        }
        // Le motif a disparu (suppression, annulation) : un ancien résultat serait trompeur.
        analysisList_->clear();
        analysisHasResult_ = false;
        analysisIsStale_ = false;
        analysisStale_->hide();
        analysisSummary_->setText(tr("Aucun point à analyser."));
        analysisHint_->clear();
        return;
    }
    stitch_analysis::AnalysisOptions opts;
    const document::Canvas& canvas = project_.canvas; // cadre défini par l'utilisateur
    opts.hoop = stitch::BoundsUm{
        Vec2um{Micrometers{-canvas.width.value / 2}, Micrometers{-canvas.height.value / 2}},
        Vec2um{Micrometers{canvas.width.value / 2}, Micrometers{canvas.height.value / 2}}};
    auto report = stitch_analysis::analyze_detailed(*sequence_, opts);
    // Règles du lettrage (texte trop petit, trait trop fin) : elles lisent l'intention du texte.
    for (auto& finding : stitch_analysis::analyze_text_objects(project_)) {
        report.findings.push_back(std::move(finding));
    }

    analysisList_->clear();
    std::map<std::string, int> severityOfCategory;
    for (const auto& f : report.findings) {
        const int rank = severityRank(f.severity);
        severityOfCategory.emplace(f.category, rank);
        const auto* emb = project_.findEmbroidery(f.object);
        // Nom de l'objet fautif devant le message (« Cercle 1 » : Point long (9,5 mm)… ).
        const QString where = (emb != nullptr && !emb->name.empty())
                                  ? tr("« %1 » : ").arg(QString::fromStdString(emb->name))
                                  : QString();
        const QString plain =
            severityWord(rank) + QStringLiteral(" : ") + where + QString::fromStdString(f.message);
        auto* item = new QListWidgetItem(severityGlyph(rank) + plain);
        item->setToolTip(f.hint.empty() ? plain
                                        : plain + QStringLiteral("\n") +
                                              tr("Piste : %1").arg(QString::fromStdString(f.hint)));
        // Sans emoji pour les lecteurs d'écran : le mot de gravité suffit.
        item->setData(Qt::AccessibleTextRole, plain);
        item->setData(Qt::UserRole, QPointF(to_millimeters(f.location.x).value,
                                            -to_millimeters(f.location.y).value));
        item->setData(kSeverityRole, rank);
        item->setData(kObjectRole, static_cast<qulonglong>(f.object.value));
        item->setData(kHintRole, QString::fromStdString(f.hint));
        // (0, 0) est une position légitime : « sans position » = ni objet ni coordonnée.
        item->setData(kHasLocationRole,
                      f.object.value != 0 || f.location.x.value != 0 || f.location.y.value != 0);
        analysisList_->addItem(item);
    }
    // Plafond par catégorie : le nombre de problèmes masqués est dit, jamais passé sous silence.
    for (const auto& [category, hidden] : report.suppressed) {
        const int rank = severityOfCategory.count(category) != 0 ? severityOfCategory[category] : 1;
        auto* note =
            new QListWidgetItem(tr("… et %1 autre(s) problème(s) de type « %2 » non affiché(s)")
                                    .arg(hidden)
                                    .arg(QString::fromStdString(category)));
        note->setFlags(Qt::ItemIsEnabled);
        note->setData(kSeverityRole, -1);
        note->setData(Qt::UserRole + 5, rank); // gravité du groupe, pour le filtre
        note->setData(Qt::UserRole + 6, static_cast<qulonglong>(hidden));
        analysisList_->addItem(note);
    }

    analysisHasResult_ = true;
    analysisIsStale_ = false;
    analysisStale_->hide();
    rebuildAnalysisList();

    if (explicitRequest) {
        // Résultat d'une demande explicite (F5) : affiché d'office, sauf en mode canevas seul
        // où le dock reste masqué (réaffiché à la sortie du mode) : seul le message d'état
        // ci-dessous signale alors le résultat.
        setDockAutoVisible(analysisDock_, true, /*force=*/true);
        if (!hidePanelsMode_) {
            analysisDock_->raise();
        }
        statusBar()->showMessage(
            tr("Analyse : %1 problème(s) détecté(s)").arg(report.findings.size()), 8000);
    }
}

// Applique le filtre de gravité aux lignes et met les compteurs à jour.
void MainWindow::rebuildAnalysisList() {
    if (analysisList_ == nullptr || analysisSummary_ == nullptr) {
        return;
    }
    const int filter = analysisFilter_->currentData().toInt();
    std::size_t counts[3] = {0, 0, 0};
    for (int i = 0; i < analysisList_->count(); ++i) {
        QListWidgetItem* item = analysisList_->item(i);
        const int rank = item->data(kSeverityRole).toInt();
        if (rank >= 0) {
            ++counts[rank];
        } else {
            counts[item->data(Qt::UserRole + 5).toInt()] +=
                item->data(Qt::UserRole + 6).toULongLong();
        }
        const int shown = rank >= 0 ? rank : item->data(Qt::UserRole + 5).toInt();
        item->setHidden(filter >= 0 && shown != filter);
    }
    if (!analysisHasResult_) {
        return;
    }
    if (counts[0] + counts[1] + counts[2] == 0) {
        analysisSummary_->setText(tr("✓ Aucun problème détecté."));
    } else {
        analysisSummary_->setText(tr("%1 erreur(s) · %2 avertissement(s) · %3 information(s)")
                                      .arg(counts[2])
                                      .arg(counts[1])
                                      .arg(counts[0]));
    }
}

void MainWindow::markAnalysisStale() {
    if (!analysisHasResult_ || analysisDock_ == nullptr) {
        return;
    }
    analysisIsStale_ = true;
    analysisStale_->show();
    // Dock visible : ré-analyse différée de 300 ms (regroupe les rafales de modifications) ;
    // sinon le résultat reste marqué périmé et sera recalculé à l'ouverture du dock.
    if (analysisDock_->isVisible()) {
        analysisTimer_->start();
    }
}

void MainWindow::activateAnalysisItem(QListWidgetItem* item, bool selectObject) {
    if (item == nullptr || item->data(kSeverityRole).toInt() < 0) {
        return;
    }
    if (selectObject) {
        const ObjectId id{item->data(kObjectRole).toULongLong()};
        if (id.value != 0) {
            if (const auto* e = project_.findEmbroidery(id)) {
                Selection sel{.region = std::nullopt, .embroidery = id, .objects = {}};
                if (project_.findObject(e->source_vector) != nullptr) {
                    sel.objects = {e->source_vector}; // met en évidence la forme au canevas
                }
                setSelection(std::move(sel));
                displayImage(processed_);
                updateActions();
            }
        }
    }
    if (item->data(kHasLocationRole).toBool()) {
        view_->centerOn(item->data(Qt::UserRole).toPointF());
    }
}

} // namespace openstitch::desktop
