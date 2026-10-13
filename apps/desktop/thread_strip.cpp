// SPDX-License-Identifier: Apache-2.0
#include "thread_strip.hpp"

#include <QApplication>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <cmath>

#include "app_theme.hpp"

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
    setAccessibleName(tr("Fils du motif"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(&AppTheme::instance(), &AppTheme::changed, this, [this] { update(); });
}

void ThreadStrip::setThreads(std::vector<Swatch> swatches, const QString& summary) {
    swatches_ = std::move(swatches);
    summary_ = summary;
    hovered_ = -1;
    endDrag();
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
    const qreal available = width() - summaryWidth - kMargin - first;
    const int fit = std::max(0, int((available + kGap) / (kDot + kGap)));
    return std::min(fit, threadCount());
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
    QString right = summary_;
    if (hidden > 0) {
        right = tr("+%1 · %2").arg(hidden).arg(summary_);
    }
    p.setPen(t.textSecondary);
    p.drawText(QRectF(0, 0, width() - kMargin, height()), Qt::AlignVCenter | Qt::AlignRight, right);
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
        pressed_ = indexAt(event->position());
        pressPos_ = event->position();
        dragging_ = false;
    }
}

void ThreadStrip::mouseReleaseEvent(QMouseEvent* event) {
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
