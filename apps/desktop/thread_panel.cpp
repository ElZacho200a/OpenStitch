// SPDX-License-Identifier: Apache-2.0
// Panneau « Fils » : présentation seule (voir thread_panel.hpp). Aucune règle métier :
// les listes viennent de stitch_analysis / thread_palette, les actions partent en signaux.
#include "thread_panel.hpp"

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "app_theme.hpp"
#include "design_tokens.hpp"
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

// Rôles ajoutés (en plus de DisplayRole / DecorationRole / ToolTipRole, inchangés) pour
// que le délégué dessine la pastille et les deux textes sans réanalyser le texte affiché.
constexpr int kSwatchRole = Qt::UserRole + 101;
constexpr int kPrimaryRole = Qt::UserRole + 102;
constexpr int kSecondaryRole = Qt::UserRole + 103;

constexpr int kSwatchDiameter = 16;

// Ligne de fil : pastille ronde de la couleur du fil, code/nom, puis compteurs en texte
// secondaire (2e ligne si `twoLine`, sinon à droite). Couleurs de l'interface tirées des
// jetons à chaque dessin : le changement de thème n'exige qu'un `update()` de la vue.
class ThreadRowDelegate : public QStyledItemDelegate {
public:
    ThreadRowDelegate(bool twoLine, QObject* parent)
        : QStyledItemDelegate(parent), twoLine_(twoLine) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const Tokens& t = AppTheme::instance().tokens();
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const QRect r = option.rect.adjusted(2, 1, -2, -1);
        if (selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(t.selection);
            painter->drawRoundedRect(r, t.radiusSm, t.radiusSm);
        } else if (option.state.testFlag(QStyle::State_MouseOver)) {
            QColor hover = t.selection;
            hover.setAlpha(60);
            painter->setPen(Qt::NoPen);
            painter->setBrush(hover);
            painter->drawRoundedRect(r, t.radiusSm, t.radiusSm);
        }

        const int pad = t.space3;
        const QRect swatch(r.left() + pad, r.center().y() - kSwatchDiameter / 2 + 1,
                           kSwatchDiameter, kSwatchDiameter);
        const QColor fill = index.data(kSwatchRole).value<QColor>();
        QColor rim = t.textSecondary; // liseré visible sur fond clair comme sombre
        rim.setAlpha(170);
        painter->setPen(QPen(rim, 1.0));
        painter->setBrush(fill);
        painter->drawEllipse(QRectF(swatch).adjusted(0.5, 0.5, -0.5, -0.5));

        const QColor mainColor = selected ? t.selectionText : t.text;
        const QColor subColor = selected ? t.selectionText : t.textSecondary;
        const QString primary = index.data(kPrimaryRole).toString();
        const QString secondary = index.data(kSecondaryRole).toString();
        const int textLeft = swatch.right() + 1 + pad;
        const int textRight = r.right() - pad;

        QFont subFont = option.font;
        if (option.font.pointSizeF() > 0) {
            subFont.setPointSizeF(option.font.pointSizeF() * 0.9);
        }
        const QFontMetrics fm(option.font);
        const QFontMetrics sfm(subFont);

        if (twoLine_) {
            const int width = textRight - textLeft + 1;
            const int total = fm.height() + (secondary.isEmpty() ? 0 : sfm.height());
            int y = r.center().y() - total / 2;
            painter->setFont(option.font);
            painter->setPen(mainColor);
            painter->drawText(QRect(textLeft, y, width, fm.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              fm.elidedText(primary, Qt::ElideRight, width));
            y += fm.height();
            if (!secondary.isEmpty()) {
                painter->setFont(subFont);
                painter->setPen(subColor);
                painter->drawText(QRect(textLeft, y, width, sfm.height()),
                                  Qt::AlignLeft | Qt::AlignVCenter,
                                  sfm.elidedText(secondary, Qt::ElideRight, width));
            }
        } else {
            int right = textRight;
            if (!secondary.isEmpty()) {
                const int w = sfm.horizontalAdvance(secondary);
                painter->setFont(subFont);
                painter->setPen(subColor);
                painter->drawText(QRect(right - w + 1, r.top(), w, r.height()),
                                  Qt::AlignRight | Qt::AlignVCenter, secondary);
                right -= w + pad;
            }
            painter->setFont(option.font);
            painter->setPen(mainColor);
            painter->drawText(QRect(textLeft, r.top(), right - textLeft + 1, r.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              fm.elidedText(primary, Qt::ElideRight, right - textLeft + 1));
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
        const Tokens& t = AppTheme::instance().tokens();
        const int lines = twoLine_ ? 2 : 1;
        const int h = option.fontMetrics.height() * lines + 2 * t.space2 + 2;
        return {option.rect.width(), std::max(h, kSwatchDiameter + 2 * t.space2 + 2)};
    }

private:
    bool twoLine_;
};

void setThreadRow(QListWidgetItem* item, const QColor& color, const QString& primary,
                  const QString& secondary) {
    item->setData(kSwatchRole, color);
    item->setData(kPrimaryRole, primary);
    item->setData(kSecondaryRole, secondary);
}

// Résumé affiché dans une carte (QFrame[card="true"]) ; le style local ne dépend que des
// jetons, il est ré-émis à chaque changement de thème.
QFrame* wrapInCard(QLabel* label, QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setProperty("card", true);
    auto* lay = new QVBoxLayout(card);
    const int m = AppTheme::instance().tokens().space3;
    lay->setContentsMargins(m, m, m, m);
    lay->setSpacing(0);
    lay->addWidget(label);
    return card;
}

// Saisie d'un fil du nuancier personnel : référence, nom, couleur. Valide seulement avec une
// référence non vide (la règle « code unique » est celle de ThreadStock::add_personal).
std::optional<thread_palette::Thread> askPersonalThread(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("personalThreadDialog"));
    dialog.setWindowTitle(QObject::tr("Ajouter un fil personnel"));
    auto* code = new QLineEdit(&dialog);
    code->setObjectName(QStringLiteral("personalThreadCode"));
    code->setPlaceholderText(QObject::tr("Référence (ex. M-042)"));
    auto* name = new QLineEdit(&dialog);
    name->setObjectName(QStringLiteral("personalThreadName"));
    name->setPlaceholderText(QObject::tr("Nom (ex. Rouge cerise)"));
    QColor color(200, 16, 46);
    auto* colorBtn = new QPushButton(&dialog);
    colorBtn->setObjectName(QStringLiteral("personalThreadColor"));
    const auto showColor = [&] {
        QPixmap swatch(40, 16);
        swatch.fill(color);
        colorBtn->setIcon(QIcon(swatch));
        colorBtn->setText(color.name().toUpper());
    };
    showColor();
    QObject::connect(colorBtn, &QPushButton::clicked, &dialog, [&] {
        const QColor chosen = QColorDialog::getColor(color, &dialog, QObject::tr("Couleur du fil"));
        if (chosen.isValid()) {
            color = chosen;
            showColor();
        }
    });
    auto* form = new QFormLayout;
    form->addRow(QObject::tr("Référence"), code);
    form->addRow(QObject::tr("Nom"), name);
    form->addRow(QObject::tr("Couleur"), colorBtn);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Ajouter"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QObject::tr("Annuler"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    QObject::connect(code, &QLineEdit::textChanged, &dialog, [&] {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!code->text().trimmed().isEmpty());
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* root = new QVBoxLayout(&dialog);
    root->addLayout(form);
    root->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    thread_palette::Thread thread;
    thread.key = {thread_palette::kPersonalChartId, code->text().trimmed().toStdString()};
    thread.name = name->text().trimmed().toStdString();
    thread.brand = QObject::tr("Perso").toStdString();
    thread.rgb = {static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                  static_cast<std::uint8_t>(color.blue())};
    return thread;
}

} // namespace

ThreadPanel::ThreadPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    selectionInfo_ = new QLabel(this);
    selectionInfo_->setObjectName(QStringLiteral("threadSelectionInfo"));
    selectionInfo_->setWordWrap(true);
    markSecondaryText(selectionInfo_);
    cards_.push_back(wrapInCard(selectionInfo_, this));
    root->addWidget(cards_.back());

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("threadTabs"));
    root->addWidget(tabs_, 1);

    // ---- Onglet « Projet » : fils utilisés ------------------------------------------------
    auto* projectTab = new QWidget(tabs_);
    auto* pl = new QVBoxLayout(projectTab);
    pl->setContentsMargins(8, 8, 8, 8);
    pl->setSpacing(8);
    auto* roleHint = new QLabel(
        tr("Fils de broderie : couleurs et références réellement prévues pour la machine. "
           "La segmentation de l'image se règle séparément dans Couleurs de segmentation."),
        projectTab);
    roleHint->setObjectName(QStringLiteral("threadColorRoleHint"));
    roleHint->setAccessibleName(tr("Rôle des couleurs de fil"));
    roleHint->setWordWrap(true);
    markSecondaryText(roleHint);
    pl->addWidget(roleHint);
    usageSummary_ = new QLabel(projectTab);
    usageSummary_->setWordWrap(true);
    usageSummary_->setObjectName(QStringLiteral("threadUsageSummary"));
    cards_.push_back(wrapInCard(usageSummary_, projectTab));
    pl->addWidget(cards_.back());
    usageList_ = new QListWidget(projectTab);
    usageList_->setObjectName(QStringLiteral("threadUsageList"));
    usageList_->setAccessibleName(tr("Fils utilisés par le motif"));
    usageList_->setItemDelegate(new ThreadRowDelegate(true, usageList_));
    usageList_->setMouseTracking(true);
    usageList_->setDragDropMode(QAbstractItemView::InternalMove);
    usageList_->setDefaultDropAction(Qt::MoveAction);
    usageList_->setToolTip(
        tr("Les couleurs sont listées dans l'ordre de couture : une couleur plus "
           "bas est cousue après, donc passe au-dessus. Glissez une couleur ou "
           "utilisez les flèches pour la déplacer."));
    pl->addWidget(usageList_, 1);
    auto* threadMoveRow = new QHBoxLayout();
    threadMoveRow->setSpacing(8);
    threadUpBtn_ = new QPushButton(tr("▲ Passer dessous"), projectTab);
    threadUpBtn_->setObjectName(QStringLiteral("threadMoveUpBtn"));
    threadUpBtn_->setToolTip(tr("Coud cette couleur plus tôt : elle passe sous la couleur "
                                "précédente. Tous ses objets bougent ensemble (annulable)."));
    threadDownBtn_ = new QPushButton(tr("▼ Passer dessus"), projectTab);
    threadDownBtn_->setObjectName(QStringLiteral("threadMoveDownBtn"));
    threadDownBtn_->setToolTip(
        tr("Coud cette couleur plus tard : elle passe au-dessus de la "
           "couleur suivante. Tous ses objets bougent ensemble (annulable)."));
    threadMoveRow->addWidget(threadUpBtn_);
    threadMoveRow->addWidget(threadDownBtn_);
    pl->addLayout(threadMoveRow);

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
    reduceRow->setSpacing(8);
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
    cl->setContentsMargins(8, 8, 8, 8);
    cl->setSpacing(8);
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
    catalogList_->setToolTip(
        tr("Assignez une référence de fil à la sélection de broderie, sans modifier "
           "les couleurs techniques de segmentation."));
    catalogList_->setItemDelegate(new ThreadRowDelegate(false, catalogList_));
    catalogList_->setMouseTracking(true);
    cl->addWidget(catalogList_, 1);
    nearestBtn_ = new QPushButton(tr("Fils les plus proches de la sélection"), catalogTab);
    nearestBtn_->setObjectName(QStringLiteral("threadNearestBtn"));
    importBtn_ = new QPushButton(tr("Importer un nuancier (CSV, JSON)…"), catalogTab);
    importBtn_->setObjectName(QStringLiteral("threadImportBtn"));
    removeChartBtn_ = new QPushButton(tr("Retirer ce nuancier importé"), catalogTab);
    removeChartBtn_->setObjectName(QStringLiteral("threadRemoveChartBtn"));
    ownCheck_ = new QCheckBox(tr("Je possède ce fil"), catalogTab);
    ownCheck_->setObjectName(QStringLiteral("threadOwnCheck"));
    ownCheck_->setToolTip(tr("Ajoute le fil sélectionné à « Mes fils » (préférence personnelle, "
                             "hors du projet)."));
    cl->addWidget(ownCheck_);
    cl->addWidget(nearestBtn_);
    cl->addWidget(importBtn_);
    cl->addWidget(removeChartBtn_);
    tabs_->addTab(catalogTab, tr("Catalogues"));

    // ---- Onglet « Mes fils » (HP-THR-007) ---------------------------------------------------
    auto* stockTab = new QWidget(tabs_);
    auto* sl = new QVBoxLayout(stockTab);
    sl->setContentsMargins(8, 8, 8, 8);
    sl->setSpacing(8);
    stockSummary_ = new QLabel(stockTab);
    stockSummary_->setWordWrap(true);
    stockSummary_->setObjectName(QStringLiteral("threadStockSummary"));
    cards_.push_back(wrapInCard(stockSummary_, stockTab));
    sl->addWidget(cards_.back());
    limitCheck_ =
        new QCheckBox(tr("Limiter la recherche du fil le plus proche à mes fils"), stockTab);
    limitCheck_->setObjectName(QStringLiteral("threadLimitStockCheck"));
    limitCheck_->setToolTip(tr("Les recherches « fil le plus proche » ne proposent alors que des "
                               "fils que vous possédez."));
    sl->addWidget(limitCheck_);
    stockList_ = new QListWidget(stockTab);
    stockList_->setObjectName(QStringLiteral("threadStockList"));
    stockList_->setAccessibleName(tr("Mes fils"));
    stockList_->setItemDelegate(new ThreadRowDelegate(false, stockList_));
    stockList_->setMouseTracking(true);
    sl->addWidget(stockList_, 1);
    addPersonalBtn_ = new QPushButton(tr("Ajouter un fil personnel…"), stockTab);
    addPersonalBtn_->setObjectName(QStringLiteral("threadAddPersonalBtn"));
    addPersonalBtn_->setToolTip(tr("Ajoute à votre nuancier personnel un fil absent des "
                                   "catalogues (nom, référence et couleur)."));
    removeStockBtn_ = new QPushButton(tr("Retirer de mes fils"), stockTab);
    removeStockBtn_->setObjectName(QStringLiteral("threadRemoveStockBtn"));
    sl->addWidget(addPersonalBtn_);
    sl->addWidget(removeStockBtn_);
    tabs_->addTab(stockTab, tr("Mes fils"));

    // ---- Onglet « Film couleur » ----------------------------------------------------------
    auto* filmTab = new QWidget(tabs_);
    auto* fl = new QVBoxLayout(filmTab);
    fl->setContentsMargins(8, 8, 8, 8);
    fl->setSpacing(8);
    filmSummary_ = new QLabel(filmTab);
    filmSummary_->setWordWrap(true);
    filmSummary_->setObjectName(QStringLiteral("threadFilmSummary"));
    cards_.push_back(wrapInCard(filmSummary_, filmTab));
    fl->addWidget(cards_.back());
    filmList_ = new QListWidget(filmTab);
    filmList_->setObjectName(QStringLiteral("threadFilmList"));
    filmList_->setAccessibleName(tr("Film couleur : blocs dans l'ordre de couture"));
    filmList_->setDragDropMode(QAbstractItemView::InternalMove);
    filmList_->setDefaultDropAction(Qt::MoveAction);
    filmList_->setToolTip(tr("Glissez un bloc pour réordonner les couleurs (les objets figés "
                             "ne bougent pas)."));
    filmList_->setItemDelegate(new ThreadRowDelegate(true, filmList_));
    filmList_->setMouseTracking(true);
    fl->addWidget(filmList_, 1);
    auto* moveRow = new QHBoxLayout();
    moveRow->setSpacing(8);
    filmUpBtn_ = new QPushButton(tr("▲ Monter"), filmTab);
    filmUpBtn_->setObjectName(QStringLiteral("threadFilmUpBtn"));
    filmDownBtn_ = new QPushButton(tr("▼ Descendre"), filmTab);
    filmDownBtn_->setObjectName(QStringLiteral("threadFilmDownBtn"));
    moveRow->addWidget(filmUpBtn_);
    moveRow->addWidget(filmDownBtn_);
    fl->addLayout(moveRow);
    filmSelectBtn_ = new QPushButton(tr("Sélectionner les objets du bloc"), filmTab);
    filmSelectBtn_->setObjectName(QStringLiteral("threadFilmSelectBtn"));
    filmMergeBtn_ =
        new QPushButton(tr("Séquencer par couleur (fusionner les blocs de même fil)"), filmTab);
    filmMergeBtn_->setObjectName(QStringLiteral("threadFilmMergeBtn"));
    filmMergeBtn_->setToolTip(tr("Regroupe les passages d'un même fil pour réduire les "
                                 "changements de fil. Attention : un fil cousu plus tard passe "
                                 "plus tôt (ordre des couches) ; annulable."));
    fl->addWidget(filmSelectBtn_);
    fl->addWidget(filmMergeBtn_);
    tabs_->addTab(filmTab, tr("Film couleur"));

    // ---- Onglet « Visibilité » (masquer un fil depuis le film) -----------------------------
    auto* visTab = new QWidget(tabs_);
    auto* vl = new QVBoxLayout(visTab);
    vl->setContentsMargins(8, 8, 8, 8);
    vl->setSpacing(8);
    visSummary_ = new QLabel(visTab);
    visSummary_->setWordWrap(true);
    visSummary_->setObjectName(QStringLiteral("threadVisibilitySummary"));
    cards_.push_back(wrapInCard(visSummary_, visTab));
    vl->addWidget(cards_.back());
    visList_ = new QListWidget(visTab);
    visList_->setObjectName(QStringLiteral("threadVisibilityList"));
    visList_->setAccessibleName(tr("Visibilité des fils du motif"));
    visList_->setToolTip(tr("Décochez un fil pour masquer tous ses objets (ils sortent de la "
                            "séquence et du film) ; recochez-le pour le réafficher. Annulable."));
    vl->addWidget(visList_, 1);
    tabs_->addTab(visTab, tr("Visibilité"));
    connect(visList_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        const int row = visList_->row(item);
        if (row >= 0 && static_cast<std::size_t>(row) < visibility_.size()) {
            emit threadVisibilityToggled(visibility_[static_cast<std::size_t>(row)].identity,
                                         item->checkState() != Qt::Unchecked);
        }
    });

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

    connect(ownCheck_, &QCheckBox::clicked, this, [this](bool checked) {
        if (const auto t = currentCatalogThread()) {
            emit stockOwnedToggled(t->key, checked);
        }
    });
    connect(limitCheck_, &QCheckBox::toggled, this, &ThreadPanel::limitToStockToggled);
    connect(stockList_, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
    connect(addPersonalBtn_, &QPushButton::clicked, this, [this] {
        if (auto thread = askPersonalThread(this)) {
            emit personalThreadAddRequested(*thread);
        }
    });
    connect(removeStockBtn_, &QPushButton::clicked, this, [this] {
        if (const auto key = currentStockKey()) {
            emit personalThreadRemoveRequested(*key);
        }
    });

    connect(usageList_->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex&, int start, int, const QModelIndex&, int destRow) {
                const int to = destRow > start ? destRow - 1 : destRow;
                emit threadMoved(start, to);
            });
    connect(threadUpBtn_, &QPushButton::clicked, this, [this] {
        const int row = usageList_->currentRow();
        if (row > 0) {
            emit threadMoved(row, row - 1);
        }
    });
    connect(threadDownBtn_, &QPushButton::clicked, this, [this] {
        const int row = usageList_->currentRow();
        if (row >= 0 && row + 1 < usageList_->count()) {
            emit threadMoved(row, row + 1);
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

    connect(&AppTheme::instance(), &AppTheme::changed, this, [this] { applyTheme(); });
    applyTheme();
    updateButtons();
}

void ThreadPanel::applyTheme() {
    const Tokens& t = AppTheme::instance().tokens();
    const QString css =
        QStringLiteral("QFrame[card=\"true\"] { background: %1; border: 1px solid %2; "
                       "border-radius: %3px; }")
            .arg(t.surfaceRaised.name(), t.border.name())
            .arg(t.radiusMd);
    for (QFrame* card : cards_) {
        card->setStyleSheet(css);
    }
    for (QListWidget* list : {usageList_, catalogList_, filmList_, stockList_}) {
        list->doItemsLayout();
        list->viewport()->update();
    }
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
        setThreadRow(item, toColor(t.rgb), tr("%1 — %2").arg(fromStd(t.key.code), fromStd(t.name)),
                     fromStd(thread_palette::to_hex_color(t.rgb)));
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

void ThreadPanel::setStock(thread_palette::ThreadStock stock) {
    stock_ = std::move(stock);
    rebuildStock();
    updateButtons();
}

void ThreadPanel::setLimitToStock(bool limit) {
    const QSignalBlocker block(limitCheck_);
    limitCheck_->setChecked(limit);
}

bool ThreadPanel::limitToStock() const {
    return limitCheck_->isChecked();
}

void ThreadPanel::rebuildStock() {
    const auto previous = currentStockKey();
    stockRows_.clear();
    if (library_ != nullptr) {
        stockRows_ = stock_.resolve(*library_);
    } else {
        for (const auto& t : stock_.personal_threads()) {
            stockRows_.push_back(t);
        }
    }
    const QSignalBlocker block(stockList_);
    stockList_->clear();
    int row = -1;
    for (std::size_t i = 0; i < stockRows_.size(); ++i) {
        const auto& t = stockRows_[i];
        const bool personal = t.key.chart_id == thread_palette::kPersonalChartId;
        const QString label = tr("%1 %2 — %3")
                                  .arg(personal ? tr("Perso") : fromStd(t.brand),
                                       fromStd(t.key.code), fromStd(t.name));
        auto* item = new QListWidgetItem(label);
        item->setIcon(icons::colorSwatch(toColor(t.rgb), 16));
        setThreadRow(item, toColor(t.rgb), label, fromStd(thread_palette::to_hex_color(t.rgb)));
        stockList_->addItem(item);
        if (previous && *previous == t.key) {
            row = static_cast<int>(i);
        }
    }
    if (row >= 0) {
        stockList_->setCurrentRow(row);
    }
    stockSummary_->setText(
        stock_.empty()
            ? tr("Aucun fil possédé : cochez « Je possède ce fil » dans les catalogues ou "
                 "ajoutez un fil personnel.")
            : tr("%1 fil(s) possédé(s) dont %2 personnel(s).")
                  .arg(stock_.size())
                  .arg(stock_.personal_threads().size()));
}

std::optional<thread_palette::ThreadKey> ThreadPanel::currentStockKey() const {
    const int row = stockList_ != nullptr ? stockList_->currentRow() : -1;
    if (row < 0 || static_cast<std::size_t>(row) >= stockRows_.size()) {
        return std::nullopt;
    }
    return stockRows_[static_cast<std::size_t>(row)].key;
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
            setThreadRow(
                item, toColor(t.rgb),
                tr("%1 %2 — %3").arg(fromStd(t.brand), fromStd(t.key.code), fromStd(t.name)),
                fromStd(thread_palette::to_hex_color(t.rgb)));
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
        setThreadRow(item, toColor(u.identity.rgb), fromStd(stitch_analysis::thread_label(u)),
                     tr("%1 objet(s) · %2 points · %3 m")
                         .arg(u.objects.size())
                         .arg(u.stitch_count)
                         .arg(QLocale().toString(u.length_mm / 1000.0, 'f', 2)));
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
            setThreadRow(item, toColor(b.identity.rgb),
                         tr("%1. %2").arg(rank).arg(labelOf(b.identity)),
                         tr("%1 objet(s) · %2 points%3")
                             .arg(b.objects.size())
                             .arg(b.stitch_count)
                             .arg(b.locked ? tr(" · ordre figé") : QString()));
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

void ThreadPanel::setVisibility(std::vector<stitch_analysis::ThreadVisibility> visibility) {
    visibility_ = std::move(visibility);
    const QSignalBlocker block(visList_);
    const int previousRow = visList_->currentRow();
    visList_->clear();
    std::size_t hiddenThreads = 0;
    for (const auto& v : visibility_) {
        const QString label = labelOf(v.identity);
        QString counts = tr("%1 objet(s)").arg(v.objects.size());
        if (v.partly_hidden()) {
            counts += tr(" · %1 masqué(s)").arg(v.hidden);
        } else if (v.fully_hidden()) {
            counts += tr(" · masqué");
            ++hiddenThreads;
        }
        auto* item = new QListWidgetItem(label + QLatin1Char('\n') + counts);
        item->setIcon(icons::colorSwatch(toColor(v.identity.rgb), 18));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(v.fully_hidden()    ? Qt::Unchecked
                            : v.partly_hidden() ? Qt::PartiallyChecked
                                                : Qt::Checked);
        visList_->addItem(item);
    }
    if (previousRow >= 0 && previousRow < visList_->count()) {
        visList_->setCurrentRow(previousRow);
    }
    visSummary_->setText(
        visibility_.empty()
            ? tr("Aucun objet de broderie.")
            : tr("%1 fil(s) · %2 masqué(s)").arg(visibility_.size()).arg(hiddenThreads));
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
    const int usageRow = usageList_->currentRow();
    threadUpBtn_->setEnabled(usageRow > 0);
    threadDownBtn_->setEnabled(usageRow >= 0 && usageRow + 1 < usageList_->count());
    replaceUsageBtn_->setEnabled(usageSel && currentCatalogThread().has_value());
    snapBtn_->setEnabled(!usage_.empty());
    reduceBtn_->setEnabled(usage_.size() > 1);
    exportBtn_->setEnabled(!usage_.empty());

    const bool userChart = library_ != nullptr && !currentChartId().isEmpty() &&
                           !library_->is_builtin(currentChartId().toStdString());
    removeChartBtn_->setEnabled(userChart);
    nearestBtn_->setEnabled(library_ != nullptr);
    if (const auto t = currentCatalogThread()) {
        const QSignalBlocker block(ownCheck_);
        ownCheck_->setChecked(stock_.owns(t->key));
        ownCheck_->setEnabled(t->key.chart_id != thread_palette::kPersonalChartId);
    } else {
        const QSignalBlocker block(ownCheck_);
        ownCheck_->setChecked(false);
        ownCheck_->setEnabled(false);
    }
    removeStockBtn_->setEnabled(currentStockKey().has_value());

    const int row = filmList_->currentRow();
    filmUpBtn_->setEnabled(row > 0);
    filmDownBtn_->setEnabled(row >= 0 && row + 1 < filmList_->count());
    filmSelectBtn_->setEnabled(row >= 0);
    filmMergeBtn_->setEnabled(film_.size() > 2);
}

} // namespace openstitch::desktop
