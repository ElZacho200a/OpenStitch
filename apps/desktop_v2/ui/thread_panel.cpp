// SPDX-License-Identifier: Apache-2.0
#include "thread_panel.hpp"

#include <QColor>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include "thread_plan.hpp"

namespace openstitch::desktop_v2 {

namespace {
QString qs(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
QIcon swatch(const std::array<std::uint8_t, 3>& c) {
    QPixmap pm(14, 14);
    pm.fill(QColor(c[0], c[1], c[2]));
    return QIcon(pm);
}
} // namespace

ThreadPanel::ThreadPanel(DocumentSession& session, QWidget* parent)
    : QWidget(parent), session_(session), tabs_(new QTabWidget(this)),
      threads_(new QListWidget(this)), order_(new QListWidget(this)),
      colorways_(new QListWidget(this)) {
    setObjectName(QStringLiteral("threadPanel"));
    threads_->setObjectName(QStringLiteral("threadList"));
    order_->setObjectName(QStringLiteral("orderList"));
    colorways_->setObjectName(QStringLiteral("colorwayList"));
    threads_->setAccessibleName(tr("Fils, dans l'ordre de couture"));
    order_->setAccessibleName(tr("Ordre de couture des objets"));
    colorways_->setAccessibleName(tr("Coloris du motif"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(tabs_);

    const auto button = [this](const QString& text, const char* name, auto&& action) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(QString::fromLatin1(name));
        connect(b, &QPushButton::clicked, this, action);
        return b;
    };
    const auto tab = [this](QListWidget* list, const QString& title,
                            std::initializer_list<QPushButton*> buttons) {
        auto* page = new QWidget(this);
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(0, 4, 0, 0);
        v->addWidget(list, 1);
        auto* row = new QHBoxLayout;
        for (auto* b : buttons) {
            row->addWidget(b);
        }
        v->addLayout(row);
        tabs_->addTab(page, title);
    };

    tab(threads_, tr("Fils"),
        {button(tr("Sélectionner"), "threadSelect", [this] { selectCurrentThread(); }),
         button(tr("Recolorer…"), "threadRecolor",
                [this] {
                    const QColor c = QColorDialog::getColor(Qt::white, this, tr("Couleur du fil"));
                    if (c.isValid()) {
                        recolorCurrentThread(c);
                    }
                }),
         button(tr("Fusionner dans…"), "threadMerge", [this] {
             QStringList names;
             for (int i = 0; i < threads_->count(); ++i) {
                 names << threads_->item(i)->text();
             }
             bool ok = false;
             const QString pick =
                 QInputDialog::getItem(this, tr("Fusionner des fils"),
                                       tr("Fusionner le fil courant dans :"), names, 0, false, &ok);
             if (ok) {
                 mergeCurrentThreadInto(static_cast<int>(names.indexOf(pick)));
             }
         })});
    tab(order_, tr("Ordre"),
        {button(tr("▲ Plus tôt"), "orderEarlier", [this] { moveCurrentOrder(-1); }),
         button(tr("▼ Plus tard"), "orderLater", [this] { moveCurrentOrder(+1); })});
    tab(colorways_, tr("Coloris"),
        {button(tr("Ajouter…"), "colorwayAdd",
                [this] {
                    bool ok = false;
                    const QString name =
                        QInputDialog::getText(this, tr("Nouveau coloris"), tr("Nom du coloris :"),
                                              QLineEdit::Normal, tr("Coloris"), &ok);
                    if (ok) {
                        addColorwayNamed(name);
                    }
                }),
         button(tr("Activer"), "colorwayActivate", [this] { activateCurrentColorway(); }),
         button(tr("Renommer…"), "colorwayRename",
                [this] {
                    bool ok = false;
                    const QString name = QInputDialog::getText(
                        this, tr("Renommer le coloris"), tr("Nom :"), QLineEdit::Normal, {}, &ok);
                    if (ok) {
                        renameCurrentColorway(name);
                    }
                }),
         button(tr("Supprimer"), "colorwayRemove", [this] { removeCurrentColorway(); })});

    connect(threads_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (refreshing_ || item == nullptr) {
            return;
        }
        report(setThreadVisible(session_, static_cast<std::size_t>(threads_->row(item)),
                                item->checkState() == Qt::Checked)
                   .ok,
               {});
    });
    connect(order_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item != nullptr) {
            session_.selection().replace(
                {{TargetKind::EmbroideryObject, item->data(Qt::UserRole).toULongLong()}});
        }
    });
    sessionToken_ = session_.subscribe([this](ChangeKind) { refresh(); });
    refresh();
}

ThreadPanel::~ThreadPanel() {
    session_.unsubscribe(sessionToken_);
}

void ThreadPanel::refresh() {
    refreshing_ = true;
    const int keepT = threads_->currentRow();
    const int keepO = order_->currentRow();
    const int keepC = colorways_->currentRow();
    threads_->clear();
    order_->clear();
    colorways_->clear();
    const auto& project = session_.project();
    for (const auto& e : threadEntries(project)) {
        auto* item = new QListWidgetItem(
            swatch(e.rgb),
            tr("%1 — %n objet(s)", nullptr, static_cast<int>(e.objects.size())).arg(qs(e.label)),
            threads_);
        bool allVisible = true;
        for (const auto id : e.objects) {
            const auto* o = project.findEmbroidery(ObjectId{id});
            allVisible = allVisible && o != nullptr && o->visible;
        }
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(allVisible ? Qt::Checked : Qt::Unchecked);
    }
    int position = 1;
    for (const auto& r : orderRows(project)) {
        auto* item = new QListWidgetItem(
            swatch(r.rgb), QStringLiteral("%1. %2").arg(position++).arg(qs(r.name)), order_);
        item->setData(Qt::UserRole, static_cast<qulonglong>(r.id));
    }
    for (const auto& c : colorwayRows(project)) {
        colorways_->addItem(qs(c.name) + (c.active ? tr("  ● actif") : QString()));
    }
    const auto restore = [](QListWidget* l, int row) {
        if (row >= 0 && row < l->count()) {
            l->setCurrentRow(row);
        }
    };
    restore(threads_, keepT);
    restore(order_, keepO);
    restore(colorways_, keepC);
    refreshing_ = false;
}

bool ThreadPanel::report(bool ok, const std::string& message) {
    if (!ok && !message.empty()) {
        emit refused(qs(message));
    }
    return ok;
}

int ThreadPanel::threadCount() const {
    return threads_->count();
}
QString ThreadPanel::threadText(int row) const {
    return threads_->item(row) != nullptr ? threads_->item(row)->text() : QString();
}
void ThreadPanel::setCurrentThread(int row) {
    threads_->setCurrentRow(row);
}
bool ThreadPanel::selectCurrentThread() {
    const auto entries = threadEntries(session_.project());
    const int row = threads_->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= entries.size()) {
        return report(false, "Choisissez un fil.");
    }
    std::vector<Target> targets;
    for (const auto id : entries[static_cast<std::size_t>(row)].objects) {
        targets.push_back({TargetKind::EmbroideryObject, id});
    }
    session_.selection().replace(std::move(targets));
    return true;
}
bool ThreadPanel::recolorCurrentThread(const QColor& color) {
    const auto r = recolorThread(
        session_, static_cast<std::size_t>(std::max(0, threads_->currentRow())),
        {static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
         static_cast<std::uint8_t>(color.blue())});
    return report(r.ok, r.message);
}
bool ThreadPanel::mergeCurrentThreadInto(int targetRow) {
    const auto r =
        mergeThreads(session_, static_cast<std::size_t>(std::max(0, threads_->currentRow())),
                     static_cast<std::size_t>(std::max(0, targetRow)));
    return report(r.ok, r.message);
}
bool ThreadPanel::setThreadRowVisible(int row, bool visible) {
    auto* item = threads_->item(row);
    if (item == nullptr) {
        return false;
    }
    const auto depth = session_.undoNames().size();
    item->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
    return session_.undoNames().size() > depth;
}

int ThreadPanel::orderCount() const {
    return order_->count();
}
QString ThreadPanel::orderText(int row) const {
    return order_->item(row) != nullptr ? order_->item(row)->text() : QString();
}
void ThreadPanel::setCurrentOrder(int row) {
    order_->setCurrentRow(row);
}
bool ThreadPanel::moveCurrentOrder(int direction) {
    auto* item = order_->currentItem();
    if (item == nullptr) {
        return report(false, "Choisissez un objet dans l'ordre de couture.");
    }
    const int target = order_->currentRow() + (direction < 0 ? -1 : 1);
    const auto r = moveInOrder(session_, item->data(Qt::UserRole).toULongLong(),
                               static_cast<std::size_t>(std::max(0, target)));
    if (r.ok) {
        order_->setCurrentRow(std::clamp(target, 0, order_->count() - 1));
    }
    return report(r.ok, r.message);
}

int ThreadPanel::colorwayCount() const {
    return colorways_->count();
}
QString ThreadPanel::colorwayText(int row) const {
    return colorways_->item(row) != nullptr ? colorways_->item(row)->text() : QString();
}
void ThreadPanel::setCurrentColorway(int row) {
    colorways_->setCurrentRow(row);
}
bool ThreadPanel::addColorwayNamed(const QString& name) {
    const auto r = addColorway(session_, name.toStdString());
    return report(r.ok, r.message);
}
bool ThreadPanel::activateCurrentColorway() {
    const auto r = activateColorway(session_, colorways_->currentRow());
    return report(r.ok, r.message);
}
bool ThreadPanel::renameCurrentColorway(const QString& name) {
    const auto r = renameColorway(session_, colorways_->currentRow(), name.toStdString());
    return report(r.ok, r.message);
}
bool ThreadPanel::removeCurrentColorway() {
    const auto r = removeColorway(session_, colorways_->currentRow());
    return report(r.ok, r.message);
}

} // namespace openstitch::desktop_v2
