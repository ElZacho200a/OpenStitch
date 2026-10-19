// SPDX-License-Identifier: Apache-2.0
#include "calque_panel.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace openstitch::desktop {

namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr int kNameCol = 0;
constexpr int kVisibleCol = 1;
constexpr int kLockCol = 2;
constexpr int kMaxVisibleRows = 6;

} // namespace

CalquePanel::CalquePanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // En-tête compact : titre + quatre boutons étroits (le dock ne doit pas s'élargir et rogner
    // le canevas) ; les infobulles disent ce que chacun fait.
    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(2);
    auto* title = new QLabel(tr("Calques"), this);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header->addWidget(title, 1);
    layout->addLayout(header);

    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("tree_calques"));
    list_->setAccessibleName(tr("Calques du document"));
    list_->setColumnCount(3);
    list_->setHeaderLabels({tr("Calque"), tr("Vis."), tr("Verr.")});
    list_->headerItem()->setToolTip(kVisibleCol,
                                    tr("Afficher ou masquer le calque (un calque masqué n'est "
                                       "ni dessiné ni cousu)."));
    list_->headerItem()->setToolTip(
        kLockCol, tr("Verrouiller le calque : ses formes ne se sélectionnent plus au canevas."));
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(kNameCol, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(kVisibleCol, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(kLockCol, QHeaderView::ResizeToContents);
    list_->setRootIsDecorated(false);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(list_);

    const auto makeButton = [&](const QString& text, const QString& name, const QString& tip) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(name);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setMaximumWidth(30);
        header->addWidget(b);
        return b;
    };
    addBtn_ =
        makeButton(tr("+"), QStringLiteral("button_calqueAdd"),
                   tr("Ajouter un calque au-dessus des autres (il devient le calque actif)."));
    removeBtn_ = makeButton(tr("−"), QStringLiteral("button_calqueRemove"),
                            tr("Supprimer le calque choisi : ses formes passent dans le calque "
                               "en dessous."));
    upBtn_ = makeButton(tr("▲"), QStringLiteral("button_calqueUp"),
                        tr("Monter le calque : cousu plus tard, donc par-dessus."));
    downBtn_ = makeButton(tr("▼"), QStringLiteral("button_calqueDown"),
                          tr("Descendre le calque vers le fond : cousu plus tôt."));

    connect(addBtn_, &QPushButton::clicked, this, &CalquePanel::addCalqueRequested);
    connect(removeBtn_, &QPushButton::clicked, this, [this] {
        if (const QTreeWidgetItem* row = currentRow()) {
            emit removeCalqueRequested(ObjectId{row->data(kNameCol, kIdRole).toULongLong()});
        }
    });
    connect(upBtn_, &QPushButton::clicked, this, [this] {
        if (const QTreeWidgetItem* row = currentRow()) {
            emit moveCalqueRequested(ObjectId{row->data(kNameCol, kIdRole).toULongLong()}, +1);
        }
    });
    connect(downBtn_, &QPushButton::clicked, this, [this] {
        if (const QTreeWidgetItem* row = currentRow()) {
            emit moveCalqueRequested(ObjectId{row->data(kNameCol, kIdRole).toULongLong()}, -1);
        }
    });

    // Un clic sur une ligne la rend active ; l'émission est différée car MainWindow rafraîchit
    // la liste (et détruirait la ligne en plein traitement de son propre signal).
    connect(list_, &QTreeWidget::itemSelectionChanged, this, [this] {
        updateButtons();
        if (syncing_) {
            return;
        }
        if (const QTreeWidgetItem* row = currentRow()) {
            const ObjectId id{row->data(kNameCol, kIdRole).toULongLong()};
            if (id.valid()) {
                QTimer::singleShot(0, this, [this, id] { emit calqueActivated(id); });
            }
        }
    });
    connect(list_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int col) {
        if (syncing_) {
            return;
        }
        const ObjectId id{item->data(kNameCol, kIdRole).toULongLong()};
        if (!id.valid()) {
            return;
        }
        if (col == kVisibleCol) {
            const bool on = item->checkState(kVisibleCol) == Qt::Checked;
            QTimer::singleShot(0, this, [this, id, on] { emit calqueVisibilityToggled(id, on); });
        } else if (col == kLockCol) {
            const bool on = item->checkState(kLockCol) == Qt::Checked;
            QTimer::singleShot(0, this, [this, id, on] { emit calqueLockToggled(id, on); });
        }
    });
    connect(list_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int col) {
        const ObjectId id{item->data(kNameCol, kIdRole).toULongLong()};
        if (col == kNameCol && id.valid()) {
            QTimer::singleShot(0, this, [this, id] { emit renameCalqueRequested(id); });
        }
    });
    connect(list_, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) { showContextMenu(pos); });
    updateButtons();
}

QTreeWidgetItem* CalquePanel::currentRow() const {
    const QList<QTreeWidgetItem*> items = list_->selectedItems();
    return items.isEmpty() ? nullptr : items.front();
}

void CalquePanel::updateButtons() {
    const QTreeWidgetItem* row = currentRow();
    const bool real = row != nullptr && row->data(kNameCol, kIdRole).toULongLong() != 0;
    removeBtn_->setEnabled(real && calqueCount_ > 1);
    const int index =
        row != nullptr ? list_->indexOfTopLevelItem(const_cast<QTreeWidgetItem*>(row)) : -1;
    // La liste va du dessus (ligne 0) au fond : « monter » = vers la ligne 0.
    upBtn_->setEnabled(real && index > 0);
    downBtn_->setEnabled(real && index >= 0 && index + 1 < list_->topLevelItemCount());
}

void CalquePanel::refresh(const document::Project& project) {
    syncing_ = true;
    list_->clear();
    calqueCount_ = static_cast<int>(project.calques.size());

    // Nombre de formes par calque (infobulle).
    std::vector<int> shapes(std::max<std::size_t>(project.calques.size(), 1), 0);
    for (const auto& shape : project.vector_objects) {
        if (!project.calques.empty()) {
            ++shapes[project.calqueRank(project.calqueOfShape(shape.id)->id)];
        } else {
            ++shapes[0];
        }
    }

    const ObjectId activeId =
        !project.calques.empty() && project.findCalque(project.active_calque)
            ? project.active_calque
            : (project.calques.empty() ? ObjectId{} : project.calques.front().id);
    QTreeWidgetItem* activeRow = nullptr;
    const auto addRow = [&](const document::Calque& calque, bool real, int count) {
        auto* row = new QTreeWidgetItem(list_);
        row->setText(kNameCol, QString::fromStdString(calque.name));
        row->setData(kNameCol, kIdRole, static_cast<qulonglong>(real ? calque.id.value : 0));
        row->setToolTip(kNameCol, tr("%n forme(s) · double-clic : renommer · le calque actif "
                                     "reçoit les formes créées.",
                                     nullptr, count));
        if (real) {
            row->setCheckState(kVisibleCol, calque.visible ? Qt::Checked : Qt::Unchecked);
            row->setCheckState(kLockCol, calque.locked ? Qt::Checked : Qt::Unchecked);
            row->setToolTip(kVisibleCol, tr("Afficher ou masquer le calque."));
            row->setToolTip(kLockCol, tr("Verrouiller ou déverrouiller le calque."));
        }
        const bool active = real ? calque.id == activeId : true;
        if (active) {
            QFont f = row->font(kNameCol);
            f.setBold(true);
            row->setFont(kNameCol, f);
            activeRow = row;
        }
    };
    if (project.calques.empty()) {
        addRow(document::Calque{ObjectId{}, tr("Calque 1").toStdString(), true, false}, false,
               shapes[0]);
    } else {
        for (std::size_t i = project.calques.size(); i-- > 0;) {
            addRow(project.calques[i], true, shapes[i]);
        }
    }
    if (activeRow != nullptr) {
        list_->setCurrentItem(activeRow);
        activeRow->setSelected(true);
    }
    // Hauteur : toutes les lignes jusqu'à six, au-delà la liste défile.
    const int rows = std::min(list_->topLevelItemCount(), kMaxVisibleRows);
    const int rowHeight = list_->sizeHintForRow(0) > 0 ? list_->sizeHintForRow(0) : 22;
    list_->setFixedHeight(list_->header()->height() + rows * rowHeight + 6);
    syncing_ = false;
    updateButtons();
}

void CalquePanel::showContextMenu(const QPoint& viewportPos) {
    QTreeWidgetItem* row = list_->itemAt(viewportPos);
    if (row == nullptr) {
        return;
    }
    const ObjectId id{row->data(kNameCol, kIdRole).toULongLong()};
    if (!id.valid()) {
        return; // calque implicite : rien à faire avant d'en ajouter un
    }
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("menu_calqueRow"));
    auto* here = menu.addAction(tr("Déplacer la sélection ici"));
    here->setObjectName(QStringLiteral("action_calqueMoveSelectionHere"));
    connect(here, &QAction::triggered, this,
            [this, id] { emit moveSelectionToCalqueRequested(id); });
    menu.addSeparator();
    auto* rename = menu.addAction(tr("Renommer…"));
    rename->setObjectName(QStringLiteral("action_calqueRename"));
    connect(rename, &QAction::triggered, this, [this, id] { emit renameCalqueRequested(id); });
    auto* del = menu.addAction(tr("Supprimer le calque"));
    del->setObjectName(QStringLiteral("action_calqueDelete"));
    del->setEnabled(calqueCount_ > 1);
    connect(del, &QAction::triggered, this, [this, id] { emit removeCalqueRequested(id); });
    menu.exec(list_->viewport()->mapToGlobal(viewportPos));
}

} // namespace openstitch::desktop
