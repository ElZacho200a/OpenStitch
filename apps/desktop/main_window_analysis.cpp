// SPDX-License-Identifier: Apache-2.0
// Panneau « Analyse » (libs/stitch_analysis) : liste des problèmes détectés avec gravité en
// toutes lettres, compteurs et filtre, objet fautif nommé, sélection + centrage au clic ou à
// Entrée, indice de correction, menu contextuel, plafond par catégorie visible, résultat périmé
// signalé et ré-analyse différée quand le document change.
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <type_traits>
#include <variant>

#include "app_theme.hpp"
#include "busy_indicator.hpp"
#include "canvas_view.hpp"
#include "density_overlay.hpp"
#include "document_panel.hpp"
#include "font_catalog.hpp"
#include "main_window.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/finishing_commands.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/lettering/lettering.hpp"
#include "openstitch/project_ops/analysis.hpp"
#include "openstitch/project_ops/render.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_analysis/text_rules.hpp"
#include "properties_panel.hpp"
#include "responsive.hpp"
#include "text_dialog.hpp"

namespace openstitch::desktop {

namespace {

constexpr int kSeverityRole = Qt::UserRole + 1; // 0 info, 1 avertissement, 2 erreur, -1 note
constexpr int kObjectRole = Qt::UserRole + 2;   // id de l'objet de broderie (0 = global)
constexpr int kHintRole = Qt::UserRole + 3;     // piste de correction
constexpr int kHasLocationRole = Qt::UserRole + 4;
// Correction portée par le constat (stitch_analysis::Fix), recopiée champ par champ.
constexpr int kFixKindRole = Qt::UserRole + 10; // int(FixKind), 0 = aucune
constexpr int kFixDxRole = Qt::UserRole + 11;
constexpr int kFixDyRole = Qt::UserRole + 12;
constexpr int kFixValueRole = Qt::UserRole + 13;
constexpr int kFixTextRole = Qt::UserRole + 14;
constexpr int kFixLabelRole = Qt::UserRole + 15;

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

// HP-PROD-007 : `trop-de-coupes` et `densite-excessive` sont désactivées par défaut dans le cœur
// (`analyze_project` n'a pas d'options). Quand l'utilisateur les active dans le panneau, on les
// évalue avec `analyze_detailed` (mêmes options de projet + les deux seuils) et on ne reprend
// que leurs constats, ajoutés au rapport.
void addOptionalRuleFindings(const document::Project& project, const stitch::StitchSequence& seq,
                             stitch_analysis::AnalysisReport& report) {
    const ExtraRuleSettings extra = ExtraRuleSettings::load();
    if (!extra.trimsEnabled && !extra.densityEnabled) {
        return;
    }
    stitch_analysis::AnalysisOptions options = stitch_analysis::options_from_project(project);
    options.max_trims = extra.trimsEnabled ? static_cast<std::size_t>(extra.maxTrims) : 0;
    options.max_penetrations_per_mm2 = extra.densityEnabled ? extra.maxDensity : 0.0;
    const auto wanted = [](const std::string& category) {
        return category == "trop-de-coupes" || category == "densite-excessive";
    };
    auto detailed = stitch_analysis::analyze_detailed(seq, options);
    for (auto& f : detailed.findings) {
        if (wanted(f.category)) {
            report.findings.push_back(std::move(f));
        }
    }
    for (const auto& [category, hidden] : detailed.suppressed) {
        if (wanted(category)) {
            report.suppressed[category] = hidden;
        }
    }
    std::stable_sort(report.findings.begin(), report.findings.end(),
                     [](const stitch_analysis::Finding& a, const stitch_analysis::Finding& b) {
                         return a.severity > b.severity;
                     });
}

// Longueur de point plafonnée selon le type de l'objet (même correction que « Corriger »).
document::StitchParams clampedStitchParams(const document::StitchParams& source,
                                           Micrometers value) {
    document::StitchParams params = source;
    std::visit(
        [&](auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, document::RunningStitchParams> ||
                          std::is_same_v<T, document::TatamiParams> ||
                          std::is_same_v<T, document::DirectionalFillParams>) {
                p.stitch_length = value;
            } else if constexpr (std::is_same_v<T, document::SatinParams>) {
                p.max_stitch_length = value;
            } else if constexpr (std::is_same_v<T, document::AutoSatinParams>) {
                p.split_threshold = value;
            }
        },
        params);
    return params;
}

// Corrections « sûres » (déjà proposées une à une par le bouton « Corriger ») : déplacer dans le
// cadre, plafonner une longueur de point, scinder les points longs / activer les finitions. Le
// texte à agrandir passe par l'éditeur de texte : il reste une correction individuelle.
bool isSafeBatchFix(stitch_analysis::FixKind kind) {
    using stitch_analysis::FixKind;
    return kind == FixKind::MoveIntoHoop || kind == FixKind::ClampStitchLength ||
           kind == FixKind::SplitLongStitches || kind == FixKind::EnableFinishing;
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

    // Bouton « Corriger » : applique la correction du constat sélectionné. Sans membre de
    // MainWindow : le bouton appartient au conteneur et les lambdas le captent par pointeur.
    auto* fixButton = new QPushButton(tr("Corriger"), container);
    fixButton->setObjectName(QStringLiteral("analysisFixButton"));
    fixButton->setAccessibleName(tr("Corriger le problème sélectionné"));
    fixButton->setEnabled(false);
    layout->addWidget(fixButton);

    // « Corriger tout ce qui est sûr » (HP-UX-031) : mêmes corrections que le bouton ci-dessus,
    // enchaînées en UNE commande composite (un seul Ctrl+Z). Aucune nouvelle règle d'analyse.
    auto* fixAllButton = new QPushButton(tr("Corriger tout ce qui est sûr"), container);
    fixAllButton->setObjectName(QStringLiteral("analysisFixAllButton"));
    fixAllButton->setAccessibleName(tr("Corriger tous les problèmes sûrs en une seule fois"));
    fixAllButton->setToolTip(tr("Déplace dans le cadre, plafonne les longueurs de point et "
                                "scinde les points longs, en une seule étape annulable."));
    fixAllButton->setEnabled(false);
    layout->addWidget(fixAllButton);

    // Rapports (HP-UX-031) : fidélité, épaisseurs et diagnostic regroupés en onglets du dock au
    // lieu de trois fenêtres de texte. Mêmes mesures que les entrées du menu Analyse
    // (libs/project_ops) ; le menu garde la version avec carte PNG enregistrable.
    auto* reportTabs = new QTabWidget(container);
    reportTabs->setObjectName(QStringLiteral("analysisReportTabs"));
    reportTabs->setMaximumHeight(240);
    reportTabs->setDocumentMode(true);
    const auto addReportTab = [this, reportTabs](const QString& title, const QString& name,
                                                 int kind) {
        auto* page = new QWidget(reportTabs);
        auto* box = new QVBoxLayout(page);
        box->setContentsMargins(4, 4, 4, 4);
        auto* run = new QPushButton(tr("Calculer"), page);
        run->setObjectName(name + QStringLiteral("Run"));
        auto* text = new QPlainTextEdit(page);
        text->setObjectName(name + QStringLiteral("Text"));
        text->setReadOnly(true);
        text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        text->setAccessibleName(title);
        text->setPlaceholderText(tr("Cliquez sur « Calculer » pour mesurer le motif actuel."));
        box->addWidget(run);
        box->addWidget(text, 1);
        connect(run, &QPushButton::clicked, this, [this, kind, text] {
            if (project_.embroidery_objects.empty()) {
                text->setPlainText(tr("Le projet ne contient aucun objet de broderie."));
                return;
            }
            BusyIndicator busy(this, tr("Mesure en cours…"), {autosaveTimer_, simTimer_});
            const document::Project snapshot = project_;
            QString out;
            if (kind == 0) {
                project_ops::FidelityOptions options;
                options.error_map = false;
                const auto report =
                    busy.run([&] { return project_ops::measure_fidelity(snapshot, options); });
                out = report ? QString::fromStdString(report->text)
                             : tr("La mesure n'a pas pu être faite : %1")
                                   .arg(QString::fromStdString(report.error().message));
            } else if (kind == 1) {
                project_ops::DensityOptions options;
                options.heatmap = false;
                const auto report =
                    busy.run([&] { return project_ops::measure_density(snapshot, options); });
                out = report ? QString::fromStdString(report->text)
                             : tr("La mesure n'a pas pu être faite : %1")
                                   .arg(QString::fromStdString(report.error().message));
            } else {
                const auto report =
                    busy.run([&] { return project_ops::diagnose_project(snapshot); });
                out = report ? QString::fromStdString(report->text)
                             : tr("Le diagnostic n'a pas pu être fait : %1")
                                   .arg(QString::fromStdString(report.error().message));
            }
            text->setPlainText(out);
        });
        reportTabs->addTab(page, title);
    };
    addReportTab(tr("Fidélité"), QStringLiteral("reportFidelity"), 0);
    addReportTab(tr("Épaisseur"), QStringLiteral("reportDensity"), 1);
    addReportTab(tr("Diagnostic"), QStringLiteral("reportDiagnose"), 2);
    layout->addWidget(reportTabs);

    // Contenu défilant (HP-UX-024) : le dock rétrécit sous la taille de son contenu.
    analysisDock_->setWidget(scrollableDockContent(container, analysisDock_));
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
            [this, fixButton](QListWidgetItem* current, QListWidgetItem*) {
                const QString hint =
                    current != nullptr ? current->data(kHintRole).toString() : QString();
                analysisHint_->setText(hint.isEmpty() ? QString() : tr("Piste : %1").arg(hint));
                const bool hasFix =
                    current != nullptr && current->data(kFixKindRole).toInt() !=
                                              static_cast<int>(stitch_analysis::FixKind::None);
                fixButton->setEnabled(hasFix && !analysisIsStale_);
                fixButton->setText(
                    hasFix ? tr("Corriger : %1").arg(current->data(kFixLabelRole).toString())
                           : tr("Corriger"));
            });
    // Le widget ne fait que traduire le `Fix` du constat en commande existante (annulable).
    connect(fixButton, &QPushButton::clicked, this, [this, fixButton] {
        QListWidgetItem* item = analysisList_->currentItem();
        if (item == nullptr || analysisIsStale_) {
            return;
        }
        using stitch_analysis::FixKind;
        const auto kind = static_cast<FixKind>(item->data(kFixKindRole).toInt());
        const ObjectId object{item->data(kObjectRole).toULongLong()};
        const Micrometers value{item->data(kFixValueRole).toInt()};
        bool applied = false;
        switch (kind) {
        case FixKind::None:
            return;
        case FixKind::MoveIntoHoop: {
            const auto* emb = project_.findEmbroidery(object);
            if (emb == nullptr || project_.findObject(emb->source_vector) == nullptr) {
                break;
            }
            const Vec2um delta{Micrometers{item->data(kFixDxRole).toInt()},
                               Micrometers{item->data(kFixDyRole).toInt()}};
            undoStack_.execute(
                std::make_unique<commands::TranslateVectorObjectCommand>(emb->source_vector, delta),
                project_);
            applied = true;
            break;
        }
        case FixKind::EnlargeText: {
            const ObjectId textId{item->data(kFixTextRole).toULongLong()};
            const auto* current = project_.findText(textId);
            if (current == nullptr) {
                break;
            }
            document::TextObject text = *current;
            // Comme l'édition du texte : repartir de la position visible des lettres.
            QString ignored;
            if (const auto font = FontCatalog::instance().load(text.font, &ignored)) {
                if (const auto moved = lettering::text_displacement(project_, text, *font,
                                                                    layoutOptionsFor(text))) {
                    text.origin = text.origin + *moved;
                }
            }
            text.cap_height = value;
            applied = applyText(std::move(text), /*isNew=*/false); // refresh + actions inclus
            if (applied) {
                fixButton->setEnabled(false);
                return;
            }
            break;
        }
        case FixKind::ClampStitchLength: {
            const auto* emb = project_.findEmbroidery(object);
            if (emb == nullptr) {
                break;
            }
            const document::StitchParams params = clampedStitchParams(emb->params, value);
            undoStack_.execute(std::make_unique<commands::SetStitchParamsCommand>(
                                   object, params, tr("Longueur de point").toStdString()),
                               project_);
            if (const auto* now = project_.findEmbroidery(object)) {
                propertiesPanel_->adoptParams(object, now->params);
            }
            applied = true;
            break;
        }
        case FixKind::SplitLongStitches:
        case FixKind::EnableFinishing: {
            document::SequenceFinishing finishing = project_.finishing;
            if (kind == FixKind::SplitLongStitches) {
                finishing.split_long_stitches = true;
            } else {
                finishing.enabled = true;
            }
            undoStack_.execute(std::make_unique<commands::SetFinishingCommand>(finishing),
                               project_);
            applied = true;
            break;
        }
        }
        if (!applied) {
            statusBar()->showMessage(tr("Cette correction n'est plus applicable."), 6000);
            return;
        }
        fixButton->setEnabled(false); // le résultat devient périmé et sera recalculé
        refreshImage();
        updateActions();
        statusBar()->showMessage(tr("Correction appliquée. Ctrl+Z pour annuler."), 8000);
    });
    connect(fixAllButton, &QPushButton::clicked, this, [this, fixAllButton] {
        if (analysisIsStale_) {
            return;
        }
        using stitch_analysis::FixKind;
        auto group = std::make_unique<commands::CompositeCommand>(
            tr("Corriger tout ce qui est sûr").toStdString());
        std::set<std::uint64_t> movedSources;
        std::set<std::uint64_t> clamped;
        std::optional<document::SequenceFinishing> finishing;
        std::size_t count = 0;
        for (int i = 0; i < analysisList_->count(); ++i) {
            QListWidgetItem* item = analysisList_->item(i);
            const auto kind = static_cast<FixKind>(item->data(kFixKindRole).toInt());
            if (item->data(kSeverityRole).toInt() < 0 || !isSafeBatchFix(kind)) {
                continue;
            }
            const ObjectId object{item->data(kObjectRole).toULongLong()};
            const Micrometers value{item->data(kFixValueRole).toInt()};
            if (kind == FixKind::MoveIntoHoop) {
                const auto* emb = project_.findEmbroidery(object);
                if (emb == nullptr || project_.findObject(emb->source_vector) == nullptr ||
                    !movedSources.insert(emb->source_vector.value).second) {
                    continue;
                }
                const Vec2um delta{Micrometers{item->data(kFixDxRole).toInt()},
                                   Micrometers{item->data(kFixDyRole).toInt()}};
                group->add(std::make_unique<commands::TranslateVectorObjectCommand>(
                    emb->source_vector, delta));
                ++count;
            } else if (kind == FixKind::ClampStitchLength) {
                const auto* emb = project_.findEmbroidery(object);
                if (emb == nullptr || !clamped.insert(object.value).second) {
                    continue;
                }
                group->add(std::make_unique<commands::SetStitchParamsCommand>(
                    object, clampedStitchParams(emb->params, value),
                    tr("Longueur de point").toStdString()));
                ++count;
            } else {
                if (!finishing) {
                    finishing = project_.finishing;
                }
                if (kind == FixKind::SplitLongStitches) {
                    finishing->split_long_stitches = true;
                } else {
                    finishing->enabled = true;
                }
            }
        }
        if (finishing && !(*finishing == project_.finishing)) {
            group->add(std::make_unique<commands::SetFinishingCommand>(*finishing));
            ++count;
        }
        if (count == 0) {
            statusBar()->showMessage(tr("Aucune correction sûre à appliquer."), 6000);
            return;
        }
        undoStack_.execute(std::move(group), project_);
        fixAllButton->setEnabled(false); // le résultat devient périmé et sera recalculé
        refreshImage();
        updateActions();
        statusBar()->showMessage(
            tr("%1 correction(s) appliquée(s) en une étape. Ctrl+Z pour annuler.").arg(count),
            8000);
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
    // Mêmes mesures que `openstitch-cli project fidelity | density | diagnose` (libs/project_ops).
    analyseMenu->addSeparator();
    auto* fidelityAct = analyseMenu->addAction(tr("&Fidélité à l'image…"));
    fidelityAct->setObjectName(QStringLiteral("action_fidelityReport"));
    connect(fidelityAct, &QAction::triggered, this, &MainWindow::showFidelityReport);
    auto* densityAct = analyseMenu->addAction(tr("&Épaisseurs de fil (densité)…"));
    densityAct->setObjectName(QStringLiteral("action_densityReport"));
    connect(densityAct, &QAction::triggered, this, &MainWindow::showDensityReport);
    auto* diagnoseAct = analyseMenu->addAction(tr("&Diagnostic des défauts connus…"));
    diagnoseAct->setObjectName(QStringLiteral("action_diagnoseReport"));
    connect(diagnoseAct, &QAction::triggered, this, &MainWindow::showDiagnoseReport);
}

namespace {

void reportError(QWidget* parent, const QString& title, const QString& explanation,
                 const std::string& detail) {
    QMessageBox box(QMessageBox::Warning, title, explanation, QMessageBox::Ok, parent);
    box.setInformativeText(QString::fromStdString(detail));
    box.exec();
}

// Rapport texte (police fixe) et, si fournie, carte PNG enregistrable.
void showReportDialog(QWidget* parent, const QString& title, const QString& text,
                      const std::vector<std::uint8_t>& png, const QString& pngDefaultName) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(720, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* view = new QPlainTextEdit(&dialog);
    view->setObjectName(QStringLiteral("reportText"));
    view->setReadOnly(true);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setPlainText(text);
    layout->addWidget(view, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    if (!png.empty()) {
        auto* saveBtn = buttons->addButton(QObject::tr("Enregistrer la carte (PNG)…"),
                                           QDialogButtonBox::ActionRole);
        QObject::connect(saveBtn, &QPushButton::clicked, &dialog, [&] {
            const QString path =
                QFileDialog::getSaveFileName(&dialog, QObject::tr("Enregistrer la carte"),
                                             pngDefaultName, QObject::tr("Image PNG (*.png)"));
            if (path.isEmpty()) {
                return;
            }
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) ||
                file.write(reinterpret_cast<const char*>(png.data()),
                           static_cast<qint64>(png.size())) != static_cast<qint64>(png.size())) {
                QMessageBox::warning(&dialog, QObject::tr("Enregistrer la carte"),
                                     QObject::tr("Le fichier %1 n'a pas pu être écrit.").arg(path));
            }
        });
    }
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

} // namespace

bool MainWindow::requireEmbroideryForReport(const QString& title) {
    if (project_.embroidery_objects.empty()) {
        QMessageBox::information(this, title, tr("Le projet ne contient aucun objet de broderie."));
        return false;
    }
    return true;
}

void MainWindow::showFidelityReport() {
    const QString title = tr("Fidélité à l'image");
    if (!requireEmbroideryForReport(title)) {
        return;
    }
    BusyIndicator busy(this, tr("Mesure de la fidélité…"), {autosaveTimer_, simTimer_});
    const document::Project snapshot = project_;
    project_ops::FidelityOptions options;
    options.error_map = true;
    auto report = busy.run([&] { return project_ops::measure_fidelity(snapshot, options); });
    if (!report) {
        reportError(this, title, tr("La mesure n'a pas pu être faite."), report.error().message);
        return;
    }
    showReportDialog(this, title, QString::fromStdString(report->text), report->error_map_png,
                     QStringLiteral("fidelite.png"));
}

void MainWindow::showDensityReport() {
    const QString title = tr("Épaisseurs de fil");
    if (!requireEmbroideryForReport(title)) {
        return;
    }
    BusyIndicator busy(this, tr("Mesure des épaisseurs…"), {autosaveTimer_, simTimer_});
    const document::Project snapshot = project_;
    project_ops::DensityOptions options;
    options.heatmap = true;
    auto report = busy.run([&] { return project_ops::measure_density(snapshot, options); });
    if (!report) {
        reportError(this, title, tr("La mesure n'a pas pu être faite."), report.error().message);
        return;
    }
    showReportDialog(this, title, QString::fromStdString(report->text), report->heatmap_png,
                     QStringLiteral("densite.png"));
}

void MainWindow::showDiagnoseReport() {
    const QString title = tr("Diagnostic des défauts connus");
    if (!requireEmbroideryForReport(title)) {
        return;
    }
    BusyIndicator busy(this, tr("Recherche des défauts…"), {autosaveTimer_, simTimer_});
    const document::Project snapshot = project_;
    auto report = busy.run([&] { return project_ops::diagnose_project(snapshot); });
    if (!report) {
        reportError(this, title, tr("Le diagnostic n'a pas pu être fait."), report.error().message);
        return;
    }
    showReportDialog(this, title, QString::fromStdString(report->text), {}, QString());
}

void MainWindow::exportRealisticPng() {
    const QString title = tr("Exporter le rendu réaliste");
    if (!requireEmbroideryForReport(title)) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, title, QStringLiteral("rendu.png"),
                                                      tr("Image PNG (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    BusyIndicator busy(this, tr("Rendu réaliste en cours…"), {autosaveTimer_, simTimer_});
    const document::Project snapshot = project_;
    auto rendered = busy.run([&] { return project_ops::render_realistic_png(snapshot); });
    if (!rendered) {
        reportError(this, title, tr("Le rendu n'a pas pu être fait."), rendered.error().message);
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(reinterpret_cast<const char*>(rendered->png.data()),
                   static_cast<qint64>(rendered->png.size())) !=
            static_cast<qint64>(rendered->png.size())) {
        QMessageBox::warning(this, title, tr("Le fichier %1 n'a pas pu être écrit.").arg(path));
        return;
    }
    statusBar()->showMessage(tr("Rendu enregistré : %1 (%2 × %3 px)")
                                 .arg(path)
                                 .arg(rendered->width)
                                 .arg(rendered->height));
}

void MainWindow::runAnalysis() {
    runAnalysisInternal(true);
}

void MainWindow::runAnalysisInternal(bool explicitRequest) {
    analysisTimer_->stop();
    flushPendingRefresh(); // HP-PERF-009 : l'analyse décrit le motif à jour, jamais un périmé
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
    // Point d'entrée unique : options du projet, cadre, règles de texte, réglages hors limites.
    auto report = stitch_analysis::analyze_project(project_, *sequence_);
    addOptionalRuleFindings(project_, *sequence_, report);

    analysisList_->clear();
    if (auto* fix = analysisDock_->findChild<QPushButton*>(QStringLiteral("analysisFixButton"))) {
        fix->setEnabled(false);
        fix->setText(tr("Corriger"));
    }
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
        item->setData(kFixKindRole, static_cast<int>(f.fix.kind));
        item->setData(kFixDxRole, f.fix.delta.x.value);
        item->setData(kFixDyRole, f.fix.delta.y.value);
        item->setData(kFixValueRole, f.fix.value.value);
        item->setData(kFixTextRole, static_cast<qulonglong>(f.fix.text.value));
        item->setData(kFixLabelRole, QString::fromStdString(f.fix.label));
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
    if (auto* fixAll =
            analysisDock_->findChild<QPushButton*>(QStringLiteral("analysisFixAllButton"))) {
        bool anySafe = false;
        for (int i = 0; i < analysisList_->count() && !anySafe; ++i) {
            const auto* item = analysisList_->item(i);
            anySafe = item->data(kSeverityRole).toInt() >= 0 &&
                      isSafeBatchFix(
                          static_cast<stitch_analysis::FixKind>(item->data(kFixKindRole).toInt()));
        }
        fixAll->setEnabled(anySafe);
    }

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

// Ouvre le dock Analyse sur une analyse à jour, sans fenêtre modale (HP-UX-031, étape 1) : appelé
// après la numérisation automatique et avant l'export, pour que l'état de qualité soit visible
// sans avoir à penser à F5.
void MainWindow::openQualityDock() {
    if (analysisDock_ == nullptr || !sequence_) {
        return;
    }
    runAnalysisInternal(false);
    setDockAutoVisible(analysisDock_, true, /*force=*/true);
    if (!hidePanelsMode_) {
        analysisDock_->raise();
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
    // Les corrections portent sur l'ancien motif : plus applicables avant la ré-analyse.
    if (auto* fix = analysisDock_->findChild<QPushButton*>(QStringLiteral("analysisFixButton"))) {
        fix->setEnabled(false);
    }
    if (auto* fixAll =
            analysisDock_->findChild<QPushButton*>(QStringLiteral("analysisFixAllButton"))) {
        fixAll->setEnabled(false);
    }
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
