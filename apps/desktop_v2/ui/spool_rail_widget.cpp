// SPDX-License-Identifier: Apache-2.0
#include "spool_rail_widget.hpp"

#include <QDropEvent>
#include <QHash>
#include <QKeyEvent>
#include <QPainter>
#include <QPixmap>
#include <QTimer>

#include "spool_rail.hpp"

namespace openstitch::desktop_v2 {

namespace {

constexpr int kIdRole = Qt::UserRole;

QIcon spoolIcon(const std::array<std::uint8_t, 3>& c) {
    static QHash<quint32, QIcon> cache; // une icône par couleur
    const quint32 key = (c[0] << 16) | (c[1] << 8) | c[2];
    if (const auto it = cache.constFind(key); it != cache.constEnd()) {
        return *it;
    }
    QPixmap pm(26, 30);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(110, 110, 120));
    p.drawRoundedRect(QRect(1, 1, 24, 4), 2, 2);
    p.drawRoundedRect(QRect(1, 25, 24, 4), 2, 2);
    p.setBrush(QColor(c[0], c[1], c[2]));
    p.drawRect(QRect(4, 5, 18, 20));
    p.setPen(QColor(255, 255, 255, 70));
    for (int y : {10, 15, 20}) {
        p.drawLine(4, y, 21, y);
    }
    const QIcon icon(pm);
    cache.insert(key, icon);
    return icon;
}

} // namespace

SpoolRailWidget::SpoolRailWidget(DocumentSession& session, QWidget* parent)
    : QListWidget(parent), session_(session) {
    setObjectName(QStringLiteral("spoolRail"));
    setAccessibleName(tr("Rail de bobines : ordre de couture"));
    setFlow(QListView::LeftToRight);
    setWrapping(false);
    setIconSize(QSize(26, 30));
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setFixedHeight(72);
    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item != nullptr) {
            session_.selection().replace(
                {{TargetKind::EmbroideryObject, item->data(kIdRole).toULongLong()}});
        }
    });
    pending_ = new QTimer(this);
    pending_->setSingleShot(true);
    pending_->setInterval(0);
    connect(pending_, &QTimer::timeout, this, &SpoolRailWidget::refresh);
    sessionToken_ = session_.subscribe([this](ChangeKind) { scheduleRefresh(); });
    selectionToken_ =
        session_.selection().subscribe([this](const SelectionModel&) { syncSelection(); });
    refresh();
}

SpoolRailWidget::~SpoolRailWidget() {
    session_.unsubscribe(sessionToken_);
    session_.selection().unsubscribe(selectionToken_);
}

std::uint64_t SpoolRailWidget::spoolId(int index) const {
    return item(index) != nullptr ? item(index)->data(kIdRole).toULongLong() : 0;
}

void SpoolRailWidget::scheduleRefresh() {
    pending_->start();
}

void SpoolRailWidget::refreshNow() {
    pending_->stop();
    refresh();
}

void SpoolRailWidget::refresh() {
    refreshing_ = true;
    clear();
    const auto rail = spoolRail(session_.project());
    int position = 1;
    for (const auto& s : rail.spools) {
        const QString name =
            QString::fromUtf8(s.name.data(), static_cast<qsizetype>(s.name.size()));
        auto* it = new QListWidgetItem(spoolIcon(s.rgb), name, this);
        it->setData(kIdRole, static_cast<qulonglong>(s.id));
        QString tip = tr("%1. %2").arg(position++).arg(name);
        if (s.threadChangeBefore) {
            tip += tr(" — changement de fil avant cette bobine");
        }
        tip += tr("\nGlisser pour réordonner, ou Alt+flèches.");
        it->setToolTip(tip);
        it->setSizeHint(QSize(124, 62));
    }
    summary_ = QString::fromStdString(rail.summary());
    setAccessibleDescription(summary_);
    refreshing_ = false;
    syncSelection();
    emit summaryChanged(summary_);
}

void SpoolRailWidget::syncSelection() {
    if (refreshing_) {
        return;
    }
    const auto& targets = session_.selection().targets();
    const QSignalBlocker block(this);
    clearSelection();
    for (int i = 0; i < count(); ++i) {
        for (const auto& t : targets) {
            if (t.kind == TargetKind::EmbroideryObject && t.id == spoolId(i)) {
                item(i)->setSelected(true);
            }
        }
    }
}

bool SpoolRailWidget::moveSpoolTo(int from, int to) {
    if (from < 0 || from >= count() || to < 0 || to >= count() || from == to) {
        return false;
    }
    const auto r = moveSpool(session_, spoolId(from), static_cast<std::size_t>(to));
    if (!r.ok) {
        emit refused(QString::fromStdString(r.message));
        refreshNow(); // l'ordre affiché redevient celui du document
    }
    return r.ok;
}

void SpoolRailWidget::dropEvent(QDropEvent* event) {
    const int from = currentRow();
    const auto* over = itemAt(event->position().toPoint());
    const int to = over != nullptr ? row(over) : count() - 1;
    event->setDropAction(Qt::IgnoreAction); // le déplacement est fait par la commande, pas par Qt
    event->accept();
    (void)moveSpoolTo(from, to);
}

void SpoolRailWidget::keyPressEvent(QKeyEvent* event) {
    if ((event->modifiers() & Qt::AltModifier) &&
        (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        const int from = currentRow();
        const int to = from + (event->key() == Qt::Key_Left ? -1 : 1);
        if (moveSpoolTo(from, to)) {
            setCurrentRow(to);
        }
        event->accept();
        return;
    }
    QListWidget::keyPressEvent(event);
}

} // namespace openstitch::desktop_v2
