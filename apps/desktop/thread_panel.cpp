// SPDX-License-Identifier: Apache-2.0
// Panneau « Fils » : présentation seule (voir thread_panel.hpp). Aucune règle métier :
// les listes viennent de stitch_analysis / thread_palette, les actions partent en signaux.
#include "thread_panel.hpp"

#include <QAbstractItemModel>
#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "app_theme.hpp"
#include "openstitch/thread_palette/chart_import.hpp"
#include "ui_icons.hpp"

namespace openstitch::desktop {

namespace {

constexpr int kMaxCatalogRows = 600;

QColor toColor(const std::array<std::uint8_t, 3>& rgb) {
    return QColor(rgb[0], rgb[1], rgb[2]);
}

QString fromStd(const std::string& s) {
    return QString::fromStdString(s);
}

QString formatDuration(double seconds) {
    const int total = static_cast<int>(seconds + 0.5);
    return QObject::tr("%1 min %2 s").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

ThreadPanel::ThreadPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    selectionInfo_ = new QLabel(this);
    selectionInfo_->setObjectName(QStringLiteral("threadSelectionInfo"));
    selectionInfo_->setWordWrap(true);
    markSecondaryText(selectionInfo_);
    root->addWidget(selectionInfo_);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("threadTabs"));
    root->addWidget(tabs_, 1);

    // ---- Onglet « Projet » : fils utilisés ------------------------------------------------
    auto* projectTab = new QWidget(tabs_);
    auto* pl = new QVBoxLayout(projectTab);
    pl->setContentsMargins(4, 4, 4, 4);
    usageSummary_ = new QLabel(projectTab);
    usageSummary_->setWordWrap(true);
    usageSummary_->setObjectName(QStringLiteral("threadUsageSummary"));
    pl->addWidget(usageSummary_);
    usageList_ = new QListWidget(projectTab);
    usageList_->setObjectName(QStringLiteral("threadUsageList"));
    usageList_->setAccessibleName(tr("Fils utilisés par le motif"));
    pl->addWidget(usageList_, 1);

    selectUsageBtn_ = new QPushButton(tr("Sélectionner les objets de ce fil"), projectTab);
    selectUsageBtn_->setObjectName(QStringLiteral("threadSelectUsageBtn"));
    replaceUsageBtn_ = new QPushButton(tr("Remplacer ce fil par celui du catalogue"), projectTab);
    replaceUsageBtn_->setObjectName(QStringLiteral("threadReplaceUsageBtn"));
    replaceUsageBtn_->setToolTip(tr("Remplace, sur tout le motif, ce fil par le fil choisi dans "
                                    "l'onglet Catalogues (un seul pas d'annulation)."));
    snapBtn_ = new QPushButton(tr("Couleurs libres → fil le plus proche"), projectTab);
    snapBtn_->setObjectName(QStringLiteral("threadSnapBtn"));
    snapBtn_->setToolTip(tr("Associe chaque objet sans fil au fil le plus proche du nuancier "
                            "choisi (distance perceptuelle CIEDE2000). Agit sur la sélection, "
                            "ou sur tout le motif si rien n'est sélectionné."));
    pl->addWidget(selectUsageBtn_);
    pl->addWidget(replaceUsageBtn_);
    pl->addWidget(snapBtn_);

    auto* reduceRow = new QHBoxLayout();
    reduceRow->addWidget(new QLabel(tr("Limiter à"), projectTab));
    reduceSpin_ = new QSpinBox(projectTab);
    reduceSpin_->setObjectName(QStringLiteral("threadReduceSpin"));
    reduceSpin_->setRange(1, 99);
    reduceSpin_->setValue(8);
    reduceSpin_->setSuffix(tr(" fils"));
    reduceSpin_->setAccessibleName(tr("Nombre maximal de fils"));
    reduceRow->addWidget(reduceSpin_);
    reduceBtn_ = new QPushButton(tr("Réduire"), projectTab);
    reduceBtn_->setObjectName(QStringLiteral("threadReduceBtn"));
    reduceBtn_->setToolTip(tr("Fusionne les couleurs les plus proches du motif jusqu'à n'en "
                              "garder que ce nombre (la couleur de l'aplat le plus grand est "
                              "conservée). Un seul pas d'annulation."));
    reduceRow->addWidget(reduceBtn_);
    pl->addLayout(reduceRow);

    exportBtn_ = new QPushButton(tr("Exporter la liste des fils (CSV)…"), projectTab);
    exportBtn_->setObjectName(QStringLiteral("threadExportBtn"));
    pl->addWidget(exportBtn_);
    tabs_->addTab(projectTab, tr("Projet"));

    // ---- Onglet « Catalogues » ------------------------------------------------------------
    auto* catalogTab = new QWidget(tabs_);
    auto* cl = new QVBoxLayout(catalogTab);
    cl->setContentsMargins(4, 4, 4, 4);
    chartCombo_ = new QComboBox(catalogTab);
    chartCombo_->setObjectName(QStringLiteral("threadChartCombo"));
    chartCombo_->setAccessibleName(tr("Nuancier"));
    cl->addWidget(chartCombo_);
    searchEdit_ = new QLineEdit(catalogTab);
    searchEdit_->setObjectName(QStringLiteral("threadSearchEdit"));
    searchEdit_->setPlaceholderText(tr("Rechercher (référence, nom, gamme…)"));
    searchEdit_->setClearButtonEnabled(true);
    searchEdit_->setAccessibleName(tr("Rechercher un fil"));
    cl->addWidget(searchEdit_);
    chartNote_ = new QLabel(catalogTab);
    chartNote_->setWordWrap(true);
    chartNote_->setObjectName(QStringLiteral("threadChartNote"));
    markSecondaryText(chartNote_);
    cl->addWidget(chartNote_);
    catalogList_ = new QListWidget(catalogTab);
    catalogList_->setObjectName(QStringLiteral("threadCatalogList"));
    catalogList_->setAccessibleName(tr("Fils du nuancier"));
    catalogList_->setToolTip(tr("Cliquez un fil pour l'assigner à la sélection."));
    cl->addWidget(catalogList_, 1);
    nearestBtn_ = new QPushButton(tr("Fils les plus proches de la sélection"), catalogTab);
    nearestBtn_->setObjectName(QStringLiteral("threadNearestBtn"));
    importBtn_ = new QPushButton(tr("Importer un nuancier (CSV, JSON)…"), catalogTab);
    importBtn_->setObjectName(QStringLiteral("threadImportBtn"));
    removeChartBtn_ = new QPushButton(tr("Retirer ce nuancier importé"), catalogTab);
    removeChartBtn_->setObjectName(QStringLiteral("threadRemoveChartBtn"));
    cl->addWidget(nearestBtn_);
    cl->addWidget(importBtn_);
    cl->addWidget(removeChartBtn_);
    tabs_->addTab(catalogTab, tr("Catalogues"));

    // ---- Onglet « Film couleur » ----------------------------------------------------------
    auto* filmTab = new QWidget(tabs_);
    auto* fl = new QVBoxLayout(filmTab);
    fl->setContentsMargins(4, 4, 4, 4);
    filmSummary_ = new QLabel(filmTab);
    filmSummary_->setWordWrap(true);
    filmSummary_->setObjectName(QStringLiteral("threadFilmSummary"));
    fl->addWidget(filmSummary_);
    filmList_ = new QListWidget(filmTab);
    filmList_->setObjectName(QStringLiteral("threadFilmList"));
    filmList_->setAccessibleName(tr("Film couleur : blocs dans l'ordre de couture"));
    filmList_->setDragDropMode(QAbstractItemView::InternalMove);
    filmList_->setDefaultDropAction(Qt::MoveAction);
    filmList_->setToolTip(tr("Glissez un bloc pour réordonner les couleurs (les objets figés "
                             "ne bougent pas)."));
    fl->addWidget(filmList_, 1);
    auto* moveRow = new QHBoxLayout();
    filmUpBtn_ = new QPushButton(tr("▲ Monter"), filmTab);
    filmUpBtn_->setObjectName(QStringLiteral("threadFilmUpBtn"));
    filmDownBtn_ = new QPushButton(tr("▼ Descendre"), filmTab);
    filmDownBtn_->setObjectName(QStringLiteral("threadFilmDownBtn"));
    moveRow->addWidget(filmUpBtn_);
    moveRow->addWidget(filmDownBtn_);
    fl->addLayout(moveRow);
    filmSelectBtn_ = new QPushButton(tr("Sélectionner les objets du bloc"), filmTab);
    filmSelectBtn_->setObjectName(QStringLiteral("threadFilmSelectBtn"));
    filmMergeBtn_ = new QPushButton(tr("Fusionner les blocs de même fil"), filmTab);
    filmMergeBtn_->setObjectName(QStringLiteral("threadFilmMergeBtn"));
    filmMergeBtn_->setToolTip(tr("Regroupe les passages d'un même fil pour réduire les "
                                 "changements de fil. Attention : un fil cousu plus tard passe "
                                 "plus tôt (ordre des couches) ; annulable."));
    fl->addWidget(filmSelectBtn_);
    fl->addWidget(filmMergeBtn_);
    tabs_->addTab(filmTab, tr("Film couleur"));

    // ---- Câblage -------------------------------------------------------------------------
    connect(usageList_, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
    connect(selectUsageBtn_, &QPushButton::clicked, this, [this] {
        if (const auto id = currentUsage()) {
            emit selectUsageObjectsRequested(*id);
        }
    });
    connect(usageList_, &QListWidget::itemActivated, this, [this] {
        if (const auto id = currentUsage()) {
            emit selectUsageObjectsRequested(*id);
        }
    });
    connect(replaceUsageBtn_, &QPushButton::clicked, this, [this] {
        const auto id = currentUsage();
        const auto thread = currentCatalogThread();
        if (id && thread) {
            emit replaceUsageRequested(*id, *thread);
        }
    });
    connect(snapBtn_, &QPushButton::clicked, this,
            [this] { emit snapFreeColorsRequested(currentChartId()); });
    connect(reduceBtn_, &QPushButton::clicked, this,
            [this] { emit reduceColorsRequested(reduceSpin_->value()); });
    connect(exportBtn_, &QPushButton::clicked, this, &ThreadPanel::exportListRequested);

    connect(chartCombo_, &QComboBox::currentIndexChanged, this, [this] { rebuildCatalog(); });
    connect(searchEdit_, &QLineEdit::textChanged, this, [this] { rebuildCatalog(); });
    const auto assignFromRow = [this](QListWidgetItem*) {
        if (const auto t = currentCatalogThread()) {
            emit assignThreadRequested(*t);
        }
    };
    connect(catalogList_, &QListWidget::itemClicked, this, assignFromRow);
    connect(catalogList_, &QListWidget::itemActivated, this, assignFromRow);
    connect(catalogList_, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
    connect(nearestBtn_, &QPushButton::clicked, this,
            [this] { emit nearestRequested(currentChartId()); });
    connect(importBtn_, &QPushButton::clicked, this, &ThreadPanel::importChartRequested);
    connect(removeChartBtn_, &QPushButton::clicked, this, [this] {
        const QString id = currentChartId();
        if (!id.isEmpty()) {
            emit removeChartRequested(id);
        }
    });

    connect(filmList_->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex&, int start, int, const QModelIndex&, int destRow) {
                const int to = destRow > start ? destRow - 1 : destRow;
                emit filmBlockMoved(start, to);
            });
    connect(filmList_, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
    connect(filmUpBtn_, &QPushButton::clicked, this, [this] {
        const int row = filmList_->currentRow();
        if (row > 0) {
            emit filmBlockMoved(row, row - 1);
        }
    });
    connect(filmDownBtn_, &QPushButton::clicked, this, [this] {
        const int row = filmList_->currentRow();
        if (row >= 0 && row + 1 < filmList_->count()) {
            emit filmBlockMoved(row, row + 1);
        }
    });
    connect(filmSelectBtn_, &QPushButton::clicked, this, [this] {
        if (filmList_->currentRow() >= 0) {
            emit filmBlockSelected(filmList_->currentRow());
        }
    });
    connect(filmList_, &QListWidget::itemActivated, this, [this] {
        if (filmList_->currentRow() >= 0) {
            emit filmBlockSelected(filmList_->currentRow());
        }
    });
    connect(filmMergeBtn_, &QPushButton::clicked, this, &ThreadPanel::mergeFilmBlocksRequested);

    updateButtons();
}

void ThreadPanel::setLibrary(const thread_palette::ThreadLibrary* library) {
    library_ = library;
    refreshCharts();
}

void ThreadPanel::refreshCharts() {
    const QString previous = currentChartId();
    {
        const QSignalBlocker block(chartCombo_);
        chartCombo_->clear();
        chartCombo_->addItem(tr("Tous les nuanciers"), QString());
        if (library_ != nullptr) {
            for (const auto& chart : library_->charts()) {
                const bool demo = thread_palette::is_demo_chart(chart);
                const QString name =
                    demo ? tr("%1 (démo, données fictives)").arg(fromStd(chart.display_name))
                         : fromStd(chart.display_name);
                chartCombo_->addItem(name, fromStd(chart.chart_id));
            }
        }
        const int idx = chartCombo_->findData(previous);
        chartCombo_->setCurrentIndex(idx >= 0 ? idx : (chartCombo_->count() > 1 ? 1 : 0));
    }
    rebuildCatalog();
}

QString ThreadPanel::currentChartId() const {
    return chartCombo_->currentData().toString();
}

void ThreadPanel::rebuildCatalog() {
    showingMatches_ = false;
    catalogRows_.clear();
    const QSignalBlocker block(catalogList_);
    catalogList_->clear();
    if (library_ == nullptr) {
        return;
    }
    const QString chartId = currentChartId();
    catalogRows_ =
        library_->search(searchEdit_->text().toStdString(), chartId.toStdString(), kMaxCatalogRows);
    for (const auto& t : catalogRows_) {
        auto* item = new QListWidgetItem(tr("%1 — %2").arg(fromStd(t.key.code), fromStd(t.name)));
        item->setIcon(icons::colorSwatch(toColor(t.rgb), 16));
        item->setToolTip(tr("%1 %2 — %3\n%4")
                             .arg(fromStd(t.brand), fromStd(t.range), fromStd(t.name),
                                  fromStd(thread_palette::to_hex_color(t.rgb))));
        catalogList_->addItem(item);
    }
    QString note;
    if (const auto* chart = library_->find_chart(chartId.toStdString())) {
        note = fromStd(chart->source_note);
        if (thread_palette::is_demo_chart(*chart)) {
            note = tr("Données de démonstration (fictives) : ne pas utiliser comme référence "
                      "colorimétrique. Importez votre propre nuancier.");
        }
    } else {
        note = tr("%1 fil(s) affiché(s).").arg(catalogRows_.size());
    }
    chartNote_->setText(note);
    updateButtons();
}

void ThreadPanel::showMatches(const std::vector<thread_palette::Thread>& matches,
                              const QString& title) {
    showingMatches_ = true;
    catalogRows_ = matches;
    {
        const QSignalBlocker block(catalogList_);
        catalogList_->clear();
        for (const auto& t : matches) {
            auto* item = new QListWidgetItem(
                tr("%1 %2 — %3").arg(fromStd(t.brand), fromStd(t.key.code), fromStd(t.name)));
            item->setIcon(icons::colorSwatch(toColor(t.rgb), 16));
            catalogList_->addItem(item);
        }
    }
    chartNote_->setText(title);
    tabs_->setCurrentIndex(1);
    updateButtons();
}

void ThreadPanel::setSelectionInfo(const QString& text) {
    selectionInfo_->setText(text);
}

QString ThreadPanel::labelOf(const stitch_analysis::ThreadIdentity& id) const {
    if (!id.key) {
        return fromStd(thread_palette::to_hex_color(id.rgb));
    }
    stitch_analysis::ThreadUsage u;
    u.identity = id;
    u.code = id.key->code;
    if (library_ != nullptr) {
        if (const auto t = library_->find(*id.key)) {
            u.brand = t->brand;
            u.code = t->key.code;
            u.name = t->name;
        }
    }
    return fromStd(stitch_analysis::thread_label(u));
}

void ThreadPanel::setUsage(std::vector<stitch_analysis::ThreadUsage> usage) {
    const auto previous = currentUsage();
    usage_ = std::move(usage);
    const QSignalBlocker block(usageList_);
    usageList_->clear();
    std::size_t stitches = 0;
    double seconds = 0.0;
    int row = -1;
    for (std::size_t i = 0; i < usage_.size(); ++i) {
        const auto& u = usage_[i];
        stitches += u.stitch_count;
        seconds += u.estimated_seconds;
        auto* item =
            new QListWidgetItem(tr("%1\n%2 objet(s) · %3 points · %4 m")
                                    .arg(fromStd(stitch_analysis::thread_label(u)))
                                    .arg(u.objects.size())
                                    .arg(u.stitch_count)
                                    .arg(QLocale().toString(u.length_mm / 1000.0, 'f', 2)));
        item->setIcon(icons::colorSwatch(toColor(u.identity.rgb), 18));
        item->setToolTip(tr("%1 passage(s) · durée estimée %2")
                             .arg(u.color_blocks)
                             .arg(formatDuration(u.estimated_seconds)));
        usageList_->addItem(item);
        if (previous && *previous == u.identity) {
            row = static_cast<int>(i);
        }
    }
    if (row >= 0) {
        usageList_->setCurrentRow(row);
    }
    usageSummary_->setText(usage_.empty() ? tr("Aucun fil : générez des points de broderie.")
                                          : tr("%1 fil(s) · %2 points · durée estimée %3")
                                                .arg(usage_.size())
                                                .arg(stitches)
                                                .arg(formatDuration(seconds)));
    updateButtons();
}

void ThreadPanel::setFilm(std::vector<stitch_analysis::ColorFilmBlock> film) {
    const int previousRow = filmList_->currentRow();
    film_ = std::move(film);
    {
        const QSignalBlocker block(filmList_);
        filmList_->clear();
        int rank = 0;
        for (const auto& b : film_) {
            auto* item = new QListWidgetItem(tr("%1. %2\n%3 objet(s) · %4 points%5")
                                                 .arg(++rank)
                                                 .arg(labelOf(b.identity))
                                                 .arg(b.objects.size())
                                                 .arg(b.stitch_count)
                                                 .arg(b.locked ? tr(" · ordre figé") : QString()));
            item->setIcon(icons::colorSwatch(toColor(b.identity.rgb), 18));
            filmList_->addItem(item);
        }
        if (previousRow >= 0 && previousRow < filmList_->count()) {
            filmList_->setCurrentRow(previousRow);
        }
    }
    filmSummary_->setText(film_.empty() ? tr("Aucun bloc : générez des points de broderie.")
                                        : tr("%1 bloc(s) · %2 changement(s) de fil")
                                              .arg(film_.size())
                                              .arg(stitch_analysis::color_change_count(film_)));
    updateButtons();
}

std::optional<thread_palette::Thread> ThreadPanel::currentCatalogThread() const {
    const int row = catalogList_->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= catalogRows_.size()) {
        return std::nullopt;
    }
    return catalogRows_[static_cast<std::size_t>(row)];
}

std::optional<stitch_analysis::ThreadIdentity> ThreadPanel::currentUsage() const {
    const int row = usageList_->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= usage_.size()) {
        return std::nullopt;
    }
    return usage_[static_cast<std::size_t>(row)].identity;
}

int ThreadPanel::currentFilmRow() const {
    return filmList_->currentRow();
}

void ThreadPanel::updateButtons() {
    const bool usageSel = currentUsage().has_value();
    selectUsageBtn_->setEnabled(usageSel);
    replaceUsageBtn_->setEnabled(usageSel && currentCatalogThread().has_value());
    snapBtn_->setEnabled(!usage_.empty());
    reduceBtn_->setEnabled(usage_.size() > 1);
    exportBtn_->setEnabled(!usage_.empty());

    const bool userChart = library_ != nullptr && !currentChartId().isEmpty() &&
                           !library_->is_builtin(currentChartId().toStdString());
    removeChartBtn_->setEnabled(userChart);
    nearestBtn_->setEnabled(library_ != nullptr);

    const int row = filmList_->currentRow();
    filmUpBtn_->setEnabled(row > 0);
    filmDownBtn_->setEnabled(row >= 0 && row + 1 < filmList_->count());
    filmSelectBtn_->setEnabled(row >= 0);
    filmMergeBtn_->setEnabled(film_.size() > 2);
}

} // namespace openstitch::desktop
