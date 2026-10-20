// SPDX-License-Identifier: Apache-2.0
#include "density_overlay.hpp"

#include <QEvent>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QSettings>

#include <algorithm>
#include <bit>
#include <cmath>

namespace openstitch::desktop {

namespace {

constexpr std::uint8_t kAlphaNormal = 140;
constexpr std::uint8_t kAlphaHot = 205;

struct Stop {
    double t;
    std::array<std::uint8_t, 3> rgb;
};

// Bleu -> cyan -> vert -> jaune -> rouge.
constexpr std::array<Stop, 5> kStops{{{0.00, {40, 70, 200}},
                                      {0.25, {0, 170, 220}},
                                      {0.50, {60, 190, 80}},
                                      {0.75, {240, 200, 40}},
                                      {1.00, {220, 40, 30}}}};

std::uint64_t fnv(std::uint64_t h, std::uint64_t v) {
    constexpr std::uint64_t prime = 1099511628211ULL;
    for (int i = 0; i < 8; ++i) {
        h = (h ^ ((v >> (8 * i)) & 0xffU)) * prime;
    }
    return h;
}

} // namespace

std::array<std::uint8_t, 3> densityColor(double t) {
    t = std::clamp(std::isfinite(t) ? t : 0.0, 0.0, 1.0);
    for (std::size_t i = 1; i < kStops.size(); ++i) {
        if (t <= kStops[i].t) {
            const double u = (t - kStops[i - 1].t) / (kStops[i].t - kStops[i - 1].t);
            std::array<std::uint8_t, 3> out{};
            for (std::size_t c = 0; c < 3; ++c) {
                const double a = kStops[i - 1].rgb[c];
                const double b = kStops[i].rgb[c];
                out[c] = static_cast<std::uint8_t>(std::lround(a + (b - a) * u));
            }
            return out;
        }
    }
    return kStops.back().rgb;
}

DensityRaster rasterizeDensity(const stitch_analysis::DensityMap& map, double threshold) {
    DensityRaster raster;
    if (map.empty() || map.columns == 0 || map.rows == 0) {
        return raster;
    }
    raster.width = static_cast<int>(map.columns);
    raster.height = static_cast<int>(map.rows);
    raster.scale_max = std::max({map.max_density_per_mm2(), threshold, 1.0});
    raster.rgba.assign(static_cast<std::size_t>(raster.width) * raster.height * 4, 0);
    for (std::size_t row = 0; row < map.rows; ++row) {
        const std::size_t imageRow = map.rows - 1 - row; // Y vers le haut -> ligne 0 en haut
        for (std::size_t col = 0; col < map.columns; ++col) {
            const double d = map.density_per_mm2(col, row);
            if (d <= 0.0) {
                continue;
            }
            const auto rgb = densityColor(d / raster.scale_max);
            const std::size_t at = (imageRow * map.columns + col) * 4;
            raster.rgba[at] = rgb[0];
            raster.rgba[at + 1] = rgb[1];
            raster.rgba[at + 2] = rgb[2];
            raster.rgba[at + 3] = (threshold > 0.0 && d > threshold) ? kAlphaHot : kAlphaNormal;
        }
    }
    return raster;
}

DensityResult computeDensity(const stitch::StitchSequence& sequence, double threshold) {
    stitch_analysis::DensityOptions options;
    options.max_penetrations_per_mm2 = std::max(0.0, threshold);
    DensityResult result;
    const stitch_analysis::DensityMap map = stitch_analysis::compute_density_map(sequence, options);
    result.origin = map.origin;
    result.cell = map.cell;
    result.max_density = map.max_density_per_mm2();
    result.raster = rasterizeDensity(map, options.max_penetrations_per_mm2);
    result.hotspots = stitch_analysis::find_density_hotspots(sequence, options);
    return result;
}

DensityRaster rasterizeThickness(const stitch_analysis::DensityMap& map,
                                 const stitch_analysis::ThicknessOptions& options) {
    DensityRaster raster;
    if (map.empty() || map.columns == 0 || map.rows == 0) {
        return raster;
    }
    raster.width = static_cast<int>(map.columns);
    raster.height = static_cast<int>(map.rows);
    // Sommet de la palette : le plus grand de l'épaisseur maximale mesurée et du seuil, jamais en
    // dessous de 0,1 mm (une carte presque vide ne doit pas s'échelonner sur du bruit).
    double maxLayers = 0.0;
    for (std::size_t row = 0; row < map.rows; ++row) {
        for (std::size_t col = 0; col < map.columns; ++col) {
            maxLayers = std::max(maxLayers, map.thickness_layers(col, row, options.thread_width));
        }
    }
    const double limitMm = stitch_analysis::layers_to_mm(options.max_layers, options);
    raster.scale_max = std::max({stitch_analysis::layers_to_mm(maxLayers, options), limitMm, 0.1});
    raster.rgba.assign(static_cast<std::size_t>(raster.width) * raster.height * 4, 0);
    for (std::size_t row = 0; row < map.rows; ++row) {
        const std::size_t imageRow = map.rows - 1 - row; // Y vers le haut -> ligne 0 en haut
        for (std::size_t col = 0; col < map.columns; ++col) {
            const double layers = map.thickness_layers(col, row, options.thread_width);
            if (layers <= 0.0) {
                continue;
            }
            const double mm = stitch_analysis::layers_to_mm(layers, options);
            const auto rgb = densityColor(mm / raster.scale_max);
            const std::size_t at = (imageRow * map.columns + col) * 4;
            raster.rgba[at] = rgb[0];
            raster.rgba[at + 1] = rgb[1];
            raster.rgba[at + 2] = rgb[2];
            raster.rgba[at + 3] = (options.max_layers > 0.0 && layers > options.max_layers)
                                      ? kAlphaHot
                                      : kAlphaNormal;
        }
    }
    return raster;
}

ThicknessResult computeThickness(const stitch::StitchSequence& sequence,
                                 const stitch_analysis::ThicknessOptions& options) {
    ThicknessResult result;
    const stitch_analysis::DensityMap grid =
        stitch_analysis::compute_thickness_grid(sequence, options);
    result.origin = grid.origin;
    result.cell = grid.cell;
    result.raster = rasterizeThickness(grid, options);
    result.peak = stitch_analysis::find_thickness_peak(sequence, grid, options);
    return result;
}

std::uint64_t thicknessKey(const stitch::StitchSequence& sequence,
                           const stitch_analysis::ThicknessOptions& options) {
    std::uint64_t h = densityKey(sequence, 0.0);
    h = fnv(h, static_cast<std::uint64_t>(options.cell.value));
    h = fnv(h, static_cast<std::uint64_t>(options.thread_width.value));
    h = fnv(h, static_cast<std::uint64_t>(options.layer_thickness.value));
    h = fnv(h, std::bit_cast<std::uint64_t>(options.max_layers));
    return fnv(h, 0x7468696bULL); // « thik » : distingue cette clé de celle de la densité
}

ThicknessSettings ThicknessSettings::load() {
    ThicknessSettings s;
    const QSettings q;
    bool ok = false;
    const double layer = q.value(QStringLiteral("ui/thickness/layerMm"), s.layerMm).toDouble(&ok);
    s.layerMm = (ok && layer >= 0.05 && layer <= 0.6) ? layer : s.layerMm;
    const double maxLayers =
        q.value(QStringLiteral("ui/thickness/maxLayers"), s.maxLayers).toDouble(&ok);
    s.maxLayers = (ok && maxLayers >= 0.5 && maxLayers <= 20.0) ? maxLayers : s.maxLayers;
    return s;
}

void ThicknessSettings::save() const {
    QSettings q;
    q.setValue(QStringLiteral("ui/thickness/layerMm"), layerMm);
    q.setValue(QStringLiteral("ui/thickness/maxLayers"), maxLayers);
}

stitch_analysis::ThicknessOptions ThicknessSettings::options(Micrometers cell) const {
    stitch_analysis::ThicknessOptions o;
    o.cell = cell;
    o.layer_thickness = Micrometers{static_cast<std::int32_t>(std::lround(layerMm * 1000.0))};
    o.max_layers = maxLayers;
    return o;
}

std::uint64_t densityKey(const stitch::StitchSequence& sequence, double threshold) {
    std::uint64_t h = 14695981039346656037ULL;
    h = fnv(h, std::bit_cast<std::uint64_t>(threshold));
    h = fnv(h, sequence.commands.size());
    for (const auto& c : sequence.commands) {
        h = fnv(h, static_cast<std::uint32_t>(c.pos.x.value));
        h = fnv(h, static_cast<std::uint32_t>(c.pos.y.value));
        h = fnv(h, static_cast<std::uint64_t>(c.type) | (static_cast<std::uint64_t>(c.pass) << 8));
        h = fnv(h, c.source.value);
    }
    return h;
}

ExtraRuleSettings ExtraRuleSettings::load() {
    ExtraRuleSettings s;
    const QSettings q;
    s.trimsEnabled = q.value(QStringLiteral("ui/analysis/trimsEnabled"), false).toBool();
    s.maxTrims =
        std::clamp(q.value(QStringLiteral("ui/analysis/maxTrims"), s.maxTrims).toInt(), 1, 100'000);
    s.densityEnabled = q.value(QStringLiteral("ui/analysis/densityEnabled"), false).toBool();
    bool ok = false;
    const double d = q.value(QStringLiteral("ui/analysis/maxDensity"), s.maxDensity).toDouble(&ok);
    s.maxDensity = (ok && d > 0.0 && d <= 1000.0) ? d : s.maxDensity;
    return s;
}

void ExtraRuleSettings::save() const {
    QSettings q;
    q.setValue(QStringLiteral("ui/analysis/trimsEnabled"), trimsEnabled);
    q.setValue(QStringLiteral("ui/analysis/maxTrims"), maxTrims);
    q.setValue(QStringLiteral("ui/analysis/densityEnabled"), densityEnabled);
    q.setValue(QStringLiteral("ui/analysis/maxDensity"), maxDensity);
}

double loadDensityThreshold() {
    const double fallback = stitch_analysis::DensityOptions{}.max_penetrations_per_mm2;
    bool ok = false;
    const double v =
        QSettings().value(QStringLiteral("ui/density/threshold"), fallback).toDouble(&ok);
    return (ok && v > 0.0 && v <= 1000.0) ? v : fallback;
}

void saveDensityThreshold(double value) {
    QSettings().setValue(QStringLiteral("ui/density/threshold"), value);
}

DensityLegend::DensityLegend(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("densityLegend"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(190, 62);
    setAccessibleName(tr("Légende de la carte de densité"));
    if (parent != nullptr) {
        parent->installEventFilter(this);
    }
    place();
    hide();
}

void DensityLegend::setRange(double scaleMax, double threshold) {
    // Densité : au moins 1 pén./mm² ; épaisseur : au moins 0,1 mm (échelle en millimètres).
    scaleMax_ = std::max(scaleMax, thicknessMode_ ? 0.1 : 1.0);
    threshold_ = threshold;
    if (thicknessMode_) {
        setToolTip(tr("Épaisseur estimée du fil en mm (0 à %1 ; seuil %2). Estimation à partir de "
                      "la longueur de fil par zone, non mesurée sur machine.")
                       .arg(scaleMax_, 0, 'f', 2)
                       .arg(threshold_, 0, 'f', 2));
    } else {
        setToolTip(tr("Densité en pénétrations d'aiguille par mm² (0 à %1 ; seuil %2)")
                       .arg(scaleMax_, 0, 'f', 1)
                       .arg(threshold_, 0, 'f', 1));
    }
    update();
}

void DensityLegend::setThicknessMode(bool thickness) {
    thicknessMode_ = thickness;
    setAccessibleName(thickness ? tr("Légende de la carte d'épaisseur")
                                : tr("Légende de la carte de densité"));
    update();
}

bool DensityLegend::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        place();
    }
    return QWidget::eventFilter(watched, event);
}

void DensityLegend::place() {
    if (parentWidget() != nullptr) {
        move(10, std::max(0, parentWidget()->height() - height() - 10));
        raise();
    }
}

void DensityLegend::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), QColor(255, 255, 255, 215));
    p.setPen(QColor(90, 90, 90));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    QFont f = font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.0));
    p.setFont(f);
    p.setPen(QColor(30, 30, 30));
    p.drawText(QRect(6, 3, width() - 12, 14), Qt::AlignLeft | Qt::AlignVCenter,
               thicknessMode_ ? tr("Épaisseur estimée (mm)") : tr("Densité (pén./mm²)"));
    const QRect bar(8, 22, width() - 16, 14);
    QLinearGradient grad(bar.left(), 0, bar.right(), 0);
    for (const auto& stop : kStops) {
        grad.setColorAt(stop.t, QColor(stop.rgb[0], stop.rgb[1], stop.rgb[2]));
    }
    p.fillRect(bar, grad);
    p.setPen(QColor(60, 60, 60));
    p.drawRect(bar);
    if (threshold_ > 0.0 && threshold_ <= scaleMax_) {
        const int x =
            bar.left() + static_cast<int>(std::lround(bar.width() * threshold_ / scaleMax_));
        p.setPen(QPen(QColor(0, 0, 0), 2));
        p.drawLine(x, bar.top() - 3, x, bar.bottom() + 3);
    }
    p.setPen(QColor(30, 30, 30));
    p.drawText(QRect(bar.left(), 40, 50, 14), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("0"));
    p.drawText(QRect(bar.right() - 60, 40, 60, 14), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(scaleMax_, 'f', thicknessMode_ ? 2 : 1));
    if (threshold_ > 0.0 && threshold_ <= scaleMax_) {
        p.drawText(QRect(bar.left() + 50, 40, bar.width() - 110, 14), Qt::AlignCenter,
                   tr("seuil %1").arg(threshold_, 0, 'f', thicknessMode_ ? 2 : 1));
    }
}

} // namespace openstitch::desktop
