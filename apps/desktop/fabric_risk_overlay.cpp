// SPDX-License-Identifier: Apache-2.0
#include "fabric_risk_overlay.hpp"

#include <QEvent>
#include <QPainter>

#include <algorithm>

#include "density_overlay.hpp"

namespace openstitch::desktop {

namespace {

using project_ops::FabricRiskLevel;

std::uint64_t fnv(std::uint64_t h, std::uint64_t v) {
    constexpr std::uint64_t prime = 1099511628211ULL;
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (8 * i)) & 0xFFU;
        h *= prime;
    }
    return h;
}

} // namespace

std::array<std::uint8_t, 4> fabricRiskColor(FabricRiskLevel level) {
    switch (level) {
    case FabricRiskLevel::Low:
        return {0, 150, 110, 110};
    case FabricRiskLevel::Moderate:
        return {240, 160, 0, 160};
    case FabricRiskLevel::High:
        return {205, 30, 30, 210};
    case FabricRiskLevel::None:
        break;
    }
    return {0, 0, 0, 0};
}

FabricRiskRaster rasterizeFabricRisk(const project_ops::FabricRiskGrid& grid) {
    FabricRiskRaster raster;
    if (grid.cols <= 0 || grid.rows <= 0 ||
        grid.levels.size() != static_cast<std::size_t>(grid.cols) * grid.rows) {
        return raster;
    }
    raster.width = grid.cols;
    raster.height = grid.rows;
    raster.rgba.assign(grid.levels.size() * 4, 0);
    for (std::size_t i = 0; i < grid.levels.size(); ++i) {
        const auto rgba = fabricRiskColor(grid.levels[i]);
        std::copy(rgba.begin(), rgba.end(),
                  raster.rgba.begin() + static_cast<std::ptrdiff_t>(i * 4));
    }
    return raster;
}

FabricRiskCounts countFabricRisk(const project_ops::FabricRiskGrid& grid) {
    FabricRiskCounts counts;
    for (const FabricRiskLevel level : grid.levels) {
        switch (level) {
        case FabricRiskLevel::None:
            ++counts.none;
            break;
        case FabricRiskLevel::Low:
            ++counts.low;
            break;
        case FabricRiskLevel::Moderate:
            ++counts.moderate;
            break;
        case FabricRiskLevel::High:
            ++counts.high;
            break;
        }
    }
    return counts;
}

const char* fabricRiskProfileId(int profile) {
    return profile == 1 ? "jersey" : profile == 2 ? "felt" : "woven";
}

std::uint64_t fabricRiskKey(const stitch::StitchSequence& sequence, int profile) {
    std::uint64_t h = densityKey(sequence, 0.0);
    h = fnv(h, static_cast<std::uint64_t>(profile));
    return fnv(h, 0x66616272ULL); // « fabr » : distingue cette clé de celle de la densité
}

FabricRiskLegend::FabricRiskLegend(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("fabricRiskLegend"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(176, 80);
    setAccessibleName(tr("Légende de la carte de risque tissu"));
    setToolTip(tr("Risque relatif de tirage du tissu (indice heuristique 0 à 100, non calibré sur "
                  "machine) : faible sous 40, modéré de 40 à 70, élevé à partir de 70."));
    if (parent != nullptr) {
        parent->installEventFilter(this);
    }
    place();
    hide();
}

bool FabricRiskLegend::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        place();
    }
    return QWidget::eventFilter(watched, event);
}

void FabricRiskLegend::place() {
    if (parentWidget() != nullptr) {
        move(std::max(0, parentWidget()->width() - width() - 10),
             std::max(0, parentWidget()->height() - height() - 10));
        raise();
    }
}

void FabricRiskLegend::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(255, 255, 255, 225));
    p.setPen(QColor(90, 90, 90));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    QFont f = font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.0));
    p.setFont(f);
    p.setPen(QColor(30, 30, 30));
    p.drawText(QRect(6, 3, width() - 12, 14), Qt::AlignLeft | Qt::AlignVCenter,
               tr("Risque tissu (estimé)"));
    struct Row {
        FabricRiskLevel level;
        QString label;
    };
    const Row rows[] = {{FabricRiskLevel::Low, tr("Faible (< 40)")},
                        {FabricRiskLevel::Moderate, tr("Modéré (40 à 70)")},
                        {FabricRiskLevel::High, tr("Élevé (≥ 70)")}};
    int y = 22;
    for (const Row& row : rows) {
        const auto c = fabricRiskColor(row.level);
        const QRect swatch(8, y, 16, 14);
        p.fillRect(swatch, QColor(255, 255, 255));
        p.fillRect(swatch, QColor(c[0], c[1], c[2], c[3]));
        p.setPen(QColor(60, 60, 60));
        p.drawRect(swatch);
        p.drawText(QRect(30, y, width() - 36, 14), Qt::AlignLeft | Qt::AlignVCenter, row.label);
        y += 18;
    }
}

} // namespace openstitch::desktop
