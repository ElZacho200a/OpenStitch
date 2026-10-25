// SPDX-License-Identifier: Apache-2.0
#include "thread_strip.hpp"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <cmath>

#include "app_theme.hpp"
#include "ui_swatch.hpp"

namespace openstitch::desktop {

namespace {

constexpr int kDot = 24;      // diamètre d'une pastille
constexpr int kGap = 6;       // espace entre pastilles
constexpr int kMargin = 12;   // marge gauche / droite
constexpr int kLabelGap = 12; // espace entre « Fils » et la première pastille

} // namespace

ThreadStrip::ThreadStrip(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("threadStrip"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Fils du motif"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(&AppTheme::instance(), &AppTheme::changed, this, [this] { update(); });
}

void ThreadStrip::setThreads(std::vector<Swatch> swatches, const QString& summary) {
    swatches_ = std::move(swatches);
    summary_ = summary;
    hovered_ = -1;
    endDrag();
    focus_ = std::clamp(focus_, 0, std::max(0, threadCount() - 1));
    update();
}

QSize ThreadStrip::sizeHint() const {
    return {400, AppTheme::instance().tokens().controlHeight + 12};
}

QSize ThreadStrip::minimumSizeHint() const {
    return {120, AppTheme::instance().tokens().controlHeight + 12};
}

QPointF ThreadStrip::swatchCenter(int index) const {
    return swatchRect(index).center();
}

QRectF ThreadStrip::swatchRect(int index) const {
    const QFontMetrics fm(font());
    const int labelWidth = fm.horizontalAdvance(tr("Fils"));
    const qreal x = kMargin + labelWidth + kLabelGap + index * (kDot + kGap);
    return {x, (height() - kDot) / 2.0, double(kDot), double(kDot)};
}

int ThreadStrip::visibleCount() const {
    const QFontMetrics fm(font());
    const int summaryWidth = summary_.isEmpty() ? 0 : fm.horizontalAdvance(summary_) + kMargin * 2;
    const qreal first = swatchRect(0).left();
    qreal available = width() - summaryWidth - kMargin - first;
    int fit = std::max(0, int((available + kGap) / (kDot + kGap)));
    if (fit < threadCount()) {
        // Tout ne tient pas : la case « +N » prend sa place à la suite des pastilles.
        const int chip = fm.horizontalAdvance(QStringLiteral("+%1").arg(threadCount())) + 16;
        available -= chip + kGap;
        fit = std::max(0, int((available + kGap) / (kDot + kGap)));
    }
    return std::min(fit, threadCount());
}

int ThreadStrip::hiddenCount() const {
    return threadCount() - visibleCount();
}

QRectF ThreadStrip::moreRect() const {
    const QFontMetrics fm(font());
    const int shown = visibleCount();
    const qreal width = fm.horizontalAdvance(QStringLiteral("+%1").arg(hiddenCount())) + 16;
    const qreal x = swatchRect(0).left() + shown * (kDot + kGap);
    return {x, (height() - kDot) / 2.0, width, double(kDot)};
}

bool ThreadStrip::moreAt(const QPointF& pos) const {
    return hiddenCount() > 0 && moreRect().adjusted(-kGap / 2.0, -4, kGap / 2.0, 4).contains(pos);
}

int ThreadStrip::indexAt(const QPointF& pos) const {
    const int n = visibleCount();
    for (int i = 0; i < n; ++i) {
        if (swatchRect(i).adjusted(-kGap / 2.0, -4, kGap / 2.0, 4).contains(pos)) {
            return i;
        }
    }
    return -1;
}

int ThreadStrip::dropSlotAt(qreal x) const {
    const int n = visibleCount();
    if (n <= 0) {
        return -1;
    }
    const qreal first = swatchRect(0).left();
    const int slot = int(std::floor((x - first + kGap / 2.0) / (kDot + kGap)));
    return std::clamp(slot, 0, n - 1);
}

void ThreadStrip::endDrag() {
    pressed_ = -1;
    dragging_ = false;
    dropSlot_ = -1;
    unsetCursor();
}

void ThreadStrip::setCurrent(int index) {
    if (current_ != index) {
        current_ = index;
        update();
    }
}

void ThreadStrip::paintEvent(QPaintEvent*) {
    const Tokens& t = AppTheme::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QFont bold = font();
    bold.setBold(true);
    p.setFont(bold);
    p.setPen(t.textSecondary);
    p.drawText(QRectF(kMargin, 0, 200, height()), Qt::AlignVCenter | Qt::AlignLeft, tr("Fils"));
    p.setFont(font());

    if (swatches_.empty()) {
        p.drawText(QRectF(swatchRect(0).left(), 0, width(), height()),
                   Qt::AlignVCenter | Qt::AlignLeft, tr("Aucun fil : le motif est vide."));
        return;
    }

    const int shown = visibleCount();
    for (int i = 0; i < shown; ++i) {
        const QRectF r = swatchRect(i);
        const bool hover = i == hovered_;
        if (hover) {
            p.setPen(QPen(t.accent, 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(r.adjusted(-3, -3, 3, 3));
        }
        if (i == current_) {
            p.setPen(QPen(t.text, 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(r.adjusted(-5, -5, 5, 5));
        }
        const bool lifted = dragging_ && i == pressed_;
        p.setOpacity(lifted ? 0.3 : 1.0);
        p.setPen(Qt::NoPen);
        p.setBrush(swatches_[std::size_t(i)].color);
        p.drawEllipse(r);
        // Liseré en deux tons : lisible sur fond clair comme sombre, quelle que soit la teinte.
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 110), 2));
        p.drawEllipse(r.adjusted(2, 2, -2, -2));
        p.setPen(QPen(t.borderStrong, 1));
        p.drawEllipse(r.adjusted(0.5, 0.5, -0.5, -0.5));
        p.setOpacity(1.0);
        if (hasFocus() && i == focus_) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(t.accent, 2, Qt::DashLine));
            p.drawEllipse(r.adjusted(-4, -4, 4, 4));
        }
    }
    if (dragging_ && pressed_ >= 0 && pressed_ < shown && dropSlot_ >= 0) {
        // Repère d'insertion : du côté où la pastille arrive, puis la pastille suit le curseur.
        const QRectF target = swatchRect(dropSlot_);
        if (dropSlot_ != pressed_) {
            const qreal x =
                dropSlot_ < pressed_ ? target.left() - kGap / 2.0 : target.right() + kGap / 2.0;
            p.setPen(QPen(t.accent, 3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(x, target.top() - 4), QPointF(x, target.bottom() + 4));
        }
        const QRectF floating(dragX_ - kDot / 2.0, swatchRect(0).top(), kDot, kDot);
        p.setOpacity(0.85);
        p.setPen(QPen(t.accent, 2));
        p.setBrush(swatches_[std::size_t(pressed_)].color);
        p.drawEllipse(floating);
        p.setOpacity(1.0);
    }
    const int hidden = threadCount() - shown;
    if (hidden > 0) {
        // Case « +N » : ouvre la liste complète des fils (clic, Entrée ou Espace).
        const QRectF chip = moreRect();
        p.setPen(QPen(hasFocus() && focus_ == shown ? t.accent : t.borderStrong,
                      hasFocus() && focus_ == shown ? 2 : 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(chip, kDot / 2.0, kDot / 2.0);
        p.setPen(t.textSecondary);
        p.drawText(chip, Qt::AlignCenter, QStringLiteral("+%1").arg(hidden));
    }
    p.setPen(t.textSecondary);
    p.drawText(QRectF(0, 0, width() - kMargin, height()), Qt::AlignVCenter | Qt::AlignRight,
               summary_);
}

void ThreadStrip::mouseMoveEvent(QMouseEvent* event) {
    if (pressed_ >= 0 && (event->buttons() & Qt::LeftButton)) {
        if (!dragging_ && (event->position() - pressPos_).manhattanLength() >=
                              QApplication::startDragDistance()) {
            dragging_ = true;
            setCursor(Qt::ClosedHandCursor);
        }
        if (dragging_) {
            dragX_ = event->position().x();
            dropSlot_ = dropSlotAt(dragX_);
            update();
        }
        return;
    }
    const int idx = indexAt(event->position());
    if (idx != hovered_) {
        hovered_ = idx;
        setCursor(idx >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void ThreadStrip::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        pressedMore_ = moreAt(event->position());
        pressed_ = pressedMore_ ? -1 : indexAt(event->position());
        pressPos_ = event->position();
        dragging_ = false;
    }
}

void ThreadStrip::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && pressedMore_) {
        pressedMore_ = false;
        if (moreAt(event->position())) {
            focus_ = visibleCount();
            announceFocus();
            showOverflowMenu();
        }
        return;
    }
    if (event->button() != Qt::LeftButton || pressed_ < 0) {
        return;
    }
    const int from = pressed_;
    const bool wasDragging = dragging_;
    const int to = wasDragging ? dropSlotAt(event->position().x()) : -1;
    endDrag();
    update();
    if (!wasDragging) {
        if (indexAt(event->position()) == from) {
            emit threadClicked(from); // un clic, pas un glissement
        }
        return;
    }
    if (to >= 0 && to != from) {
        emit threadMoved(from, to);
    }
}

void ThreadStrip::leaveEvent(QEvent*) {
    hovered_ = -1;
    if (!dragging_) {
        unsetCursor();
    }
    update();
}

QMenu* ThreadStrip::showOverflowMenu() {
    if (overflowMenu_ == nullptr) {
        overflowMenu_ = new QMenu(this);
        overflowMenu_->setObjectName(QStringLiteral("threadStripOverflowMenu"));
    }
    overflowMenu_->clear();
    for (int i = 0; i < threadCount(); ++i) {
        const auto& swatch = swatches_[std::size_t(i)];
        const QString label = swatch.tooltip.section(QLatin1Char('\n'), 0, 0);
        auto* action = overflowMenu_->addAction(
            QIcon(makeSwatch(swatch.color, 16)),
            tr("%1. %2").arg(i + 1).arg(label.isEmpty() ? tr("Fil") : label));
        connect(action, &QAction::triggered, this, [this, i] { emit threadClicked(i); });
    }
    overflowMenu_->popup(mapToGlobal(QPoint(int(moreRect().left()), height())));
    return overflowMenu_;
}

void ThreadStrip::keyPressEvent(QKeyEvent* event) {
    const int shown = visibleCount();
    const bool hasMore = hiddenCount() > 0;
    const int lastSlot =
        shown + (hasMore ? 1 : 0) - 1; // dernier arrêt : la case « +N » si elle existe
    if (threadCount() == 0 || lastSlot < 0) {
        QWidget::keyPressEvent(event);
        return;
    }
    focus_ = std::clamp(focus_, 0, lastSlot);
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right: {
        const int step = event->key() == Qt::Key_Left ? -1 : 1;
        const int target = std::clamp(focus_ + step, 0, lastSlot);
        if (ctrl && focus_ < shown && target < shown && target != focus_) {
            // Réordonner au clavier : même signal qu'un glisser-déposer.
            const int from = focus_;
            focus_ = target;
            emit threadMoved(from, target);
        } else if (!ctrl) {
            focus_ = target;
        }
        break;
    }
    case Qt::Key_Home:
        focus_ = 0;
        break;
    case Qt::Key_End:
        focus_ = lastSlot;
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (focus_ >= shown) {
            showOverflowMenu();
        } else {
            emit threadClicked(focus_);
        }
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    announceFocus();
    update();
}

void ThreadStrip::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    announceFocus();
    update();
}

void ThreadStrip::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    update();
}

// Nom et code du fil focalisé (première ligne de l'info-bulle) dans la description accessible.
void ThreadStrip::announceFocus() {
    QString description;
    if (threadCount() > 0 && focus_ < visibleCount()) {
        const QString label =
            swatches_[std::size_t(focus_)].tooltip.section(QLatin1Char('\n'), 0, 0);
        description = tr("Fil %1 sur %2 : %3").arg(focus_ + 1).arg(threadCount()).arg(label);
    } else if (hiddenCount() > 0) {
        description = tr("Plus de fils : %1 autres. Entrée pour afficher la liste complète.")
                          .arg(hiddenCount());
    }
    setAccessibleDescription(description);
    QAccessibleEvent changed(this, QAccessible::DescriptionChanged);
    QAccessible::updateAccessibility(&changed);
}

bool ThreadStrip::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(e);
        const int idx = indexAt(help->pos());
        if (idx >= 0) {
            QToolTip::showText(help->globalPos(),
                               swatches_[std::size_t(idx)].tooltip + QLatin1Char('\n') +
                                   tr("Glisser : changer l'ordre (à droite = cousue après, "
                                      "donc au-dessus)"),
                               this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(e);
}

} // namespace openstitch::desktop
