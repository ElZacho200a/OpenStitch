// SPDX-License-Identifier: Apache-2.0
#include "thread_strip.hpp"

#include <QFontMetrics>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

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
    update();
}

QSize ThreadStrip::sizeHint() const {
    return {400, AppTheme::instance().tokens().controlHeight + 12};
}

QSize ThreadStrip::minimumSizeHint() const {
    return {120, AppTheme::instance().tokens().controlHeight + 12};
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
        p.setPen(Qt::NoPen);
        p.setBrush(swatches_[std::size_t(i)].color);
        p.drawEllipse(r);
        // Liseré en deux tons : lisible sur fond clair comme sombre, quelle que soit la teinte.
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 110), 2));
        p.drawEllipse(r.adjusted(2, 2, -2, -2));
        p.setPen(QPen(t.borderStrong, 1));
        p.drawEllipse(r.adjusted(0.5, 0.5, -0.5, -0.5));
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
    const int idx = indexAt(event->position());
    if (idx != hovered_) {
        hovered_ = idx;
        setCursor(idx >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void ThreadStrip::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const int idx = indexAt(event->position());
        if (idx >= 0) {
            emit threadClicked(idx);
        }
    }
}

void ThreadStrip::leaveEvent(QEvent*) {
    hovered_ = -1;
    unsetCursor();
    update();
}

bool ThreadStrip::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(e);
        const int idx = indexAt(help->pos());
        if (idx >= 0) {
            QToolTip::showText(help->globalPos(), swatches_[std::size_t(idx)].tooltip, this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(e);
}

} // namespace openstitch::desktop
