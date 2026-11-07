// SPDX-License-Identifier: Apache-2.0
#include "radial_menu_widget.hpp"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <algorithm>
#include <cmath>
#include <numbers>

#include "floating_action_bar.hpp"
#include "radial_menu.hpp"

namespace openstitch::desktop_v2 {

namespace {
constexpr int kSize = 320;
constexpr double kInner = 44.0;
constexpr double kOuter = 150.0;
} // namespace

RadialMenuWidget::RadialMenuWidget(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint) {
    setObjectName(QStringLiteral("radialMenu"));
    setAccessibleName(tr("Menu radial : touches 1 à 8, flèches et Entrée, Échap pour fermer"));
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(kSize, kSize);
}

void RadialMenuWidget::setEntries(std::vector<CommandEntry> entries) {
    if (entries.size() > static_cast<std::size_t>(kRadialMaxSectors)) {
        entries.resize(kRadialMaxSectors);
    }
    entries_ = std::move(entries);
    hovered_ = -1;
    update();
}

void RadialMenuWidget::popupAt(const QPoint& globalCentre) {
    QPoint topLeft = globalCentre - QPoint(kSize / 2, kSize / 2);
    if (const auto* screen = QGuiApplication::screenAt(globalCentre)) {
        const QRect area = screen->availableGeometry();
        topLeft.setX(std::clamp(topLeft.x(), area.left(), area.right() - kSize));
        topLeft.setY(std::clamp(topLeft.y(), area.top(), area.bottom() - kSize));
    }
    move(topLeft);
    show();
    setFocus();
}

QPoint RadialMenuWidget::sectorCentre(int sector) const {
    const int n = sectorCount();
    const double width = 2.0 * std::numbers::pi / std::max(n, 1);
    const double a = sector * width; // 0 = haut, sens horaire
    const double r = (kInner + kOuter) / 2.0;
    return QPoint(kSize / 2 + static_cast<int>(std::lround(r * std::sin(a))),
                  kSize / 2 - static_cast<int>(std::lround(r * std::cos(a))));
}

int RadialMenuWidget::sectorAtLocal(const QPoint& p) const {
    const int s = radialSectorAt(p.x() - kSize / 2, p.y() - kSize / 2, kInner, sectorCount());
    return (s >= 0 && entries_[static_cast<std::size_t>(s)].enabled) ? s : -1;
}

void RadialMenuWidget::hoverSector(int sector) {
    const int s = (sector >= 0 && sector < sectorCount()) ? sector : -1;
    if (s != hovered_) {
        hovered_ = s;
        update();
    }
}

bool RadialMenuWidget::activateSector(int sector) {
    if (sector < 0 || sector >= sectorCount() ||
        !entries_[static_cast<std::size_t>(sector)].enabled) {
        return false;
    }
    const QString id = QString::fromStdString(entries_[static_cast<std::size_t>(sector)].id);
    close();
    emit chosen(id);
    return true;
}

void RadialMenuWidget::mouseMoveEvent(QMouseEvent* event) {
    hoverSector(sectorAtLocal(event->position().toPoint()));
}

void RadialMenuWidget::mousePressEvent(QMouseEvent* event) {
    const int s = sectorAtLocal(event->position().toPoint());
    if (s >= 0) {
        activateSector(s);
    } else {
        close(); // clic au centre ou hors des secteurs : annule
    }
}

void RadialMenuWidget::keyPressEvent(QKeyEvent* event) {
    const int key = event->key();
    if (key == Qt::Key_Escape) {
        close();
    } else if (key >= Qt::Key_1 && key <= Qt::Key_8) {
        activateSector(radialSectorForDigit(key - Qt::Key_0, sectorCount()));
    } else if (key == Qt::Key_Right || key == Qt::Key_Down) {
        hoverSector(radialNeighbour(hovered_, +1, sectorCount()));
    } else if (key == Qt::Key_Left || key == Qt::Key_Up) {
        hoverSector(radialNeighbour(hovered_, -1, sectorCount()));
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        activateSector(hovered_);
    } else {
        QWidget::keyPressEvent(event);
    }
}

void RadialMenuWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int n = sectorCount();
    const QPointF c(kSize / 2.0, kSize / 2.0);
    const QRectF outer(c.x() - kOuter, c.y() - kOuter, 2 * kOuter, 2 * kOuter);
    const QRectF inner(c.x() - kInner, c.y() - kInner, 2 * kInner, 2 * kInner);
    const auto& pal = palette();
    if (n == 0) {
        p.setBrush(pal.window());
        p.setPen(pal.mid().color());
        p.drawEllipse(inner);
        return;
    }
    const double span = 360.0 / n;
    for (int i = 0; i < n; ++i) {
        // Qt : angle 0 = trois heures, sens anti-horaire. Le secteur i est centré à (i * span)
        // degrés depuis le haut dans le sens horaire.
        const double centreDeg = 90.0 - i * span;
        const double startDeg = centreDeg - span / 2.0 + 0.8;
        QPainterPath path;
        path.arcMoveTo(outer, startDeg);
        path.arcTo(outer, startDeg, span - 1.6);
        path.arcTo(inner, startDeg + span - 1.6, -(span - 1.6));
        path.closeSubpath();
        const auto& e = entries_[static_cast<std::size_t>(i)];
        QColor fill = pal.window().color();
        if (i == hovered_) {
            fill = pal.highlight().color();
        } else if (!e.enabled) {
            fill.setAlpha(150);
        } else {
            fill.setAlpha(235);
        }
        p.setPen(QPen(pal.mid().color(), 1));
        p.setBrush(fill);
        p.drawPath(path);
        p.setPen(i == hovered_ ? pal.highlightedText().color()
                               : (e.enabled ? pal.windowText().color() : pal.mid().color()));
        const QPoint mid = sectorCentre(i);
        const QRect box(mid.x() - 52, mid.y() - 32, 104, 64);
        const QString label = commandShortLabel(e.id, QString::fromStdString(e.label));
        p.drawText(box, Qt::AlignCenter | Qt::TextWordWrap,
                   QStringLiteral("%1\n%2").arg(i + 1).arg(label));
    }
    p.setBrush(pal.window());
    p.setPen(pal.mid().color());
    p.drawEllipse(inner);
    p.setPen(pal.windowText().color());
    p.drawText(inner, Qt::AlignCenter, tr("Échap"));
}

} // namespace openstitch::desktop_v2
