// SPDX-License-Identifier: Apache-2.0
// Rendu réaliste des points (façon « TrueView ») : sous-menu Affichage, fenêtre
// de réglages et peinture du pixmap en cache. Aucune logique de rendu ici :
// les brins de fil et l'ombrage viennent de libs/stitch_render (Qt-free,
// testé) ; cette unité ne fait que fournir la séquence EFFECTIVE déjà en cache
// (`sequence_`), choisir la fenêtre de rendu selon le zoom, et poser l'image.
#include <QAction>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QKeySequence>
#include <QMenu>
#include <QPixmap>
#include <QRectF>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/stitch_render/raster.hpp"
#include "realistic_render_dialog.hpp"

namespace openstitch::desktop {

namespace {

// Rafraîchissement différé : laisse passer une rafale de zoom/défilement ou de
// mouvements de curseur avant de recalculer l'image.
constexpr int kRefreshDelayMs = 120;

// Marge de rendu autour de la zone visible (fraction de sa taille) : un
// défilement modéré reste dans l'image déjà calculée.
constexpr double kViewMargin = 0.25;

// Tolérance de résolution avant de recalculer (le pixmap est mis à l'échelle
// par la vue entre-temps).
constexpr double kPpmLow = 0.75;
constexpr double kPpmHigh = 1.35;

// Plafond de mémoire de l'image (pixels) selon la qualité.
constexpr std::size_t kMaxPixelsHigh = 12'000'000;
constexpr std::size_t kMaxPixelsFast = 6'000'000;

std::uint64_t combine(std::uint64_t a, std::uint64_t b) {
    return (a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2))) * 1099511628211ULL;
}

stitch_render::RectMm toRect(const QRectF& r) {
    return {r.left(), r.top(), r.right(), r.bottom()};
}

stitch_render::RectMm intersect(const stitch_render::RectMm& a, const stitch_render::RectMm& b) {
    return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}

} // namespace

void MainWindow::buildRealisticMenu(QMenu* viewMenu) {
    realistic_.prefs = loadRealisticPreferences();

    auto* menu = viewMenu->addMenu(tr("Rend&u réaliste"));
    realisticAct_ = menu->addAction(tr("&Activer"));
    realisticAct_->setObjectName(QStringLiteral("action_realisticView"));
    realisticAct_->setCheckable(true);
    realisticAct_->setChecked(realistic_.prefs.enabled);
    realisticAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    realisticAct_->setToolTip(tr("Dessine chaque point comme un fil texturé (relief, ombre "
                                 "portée). En dézoom fort, retombe sur des lignes."));
    connect(realisticAct_, &QAction::toggled, this, [this](bool on) {
        RealisticPreferences prefs = realistic_.prefs;
        prefs.enabled = on;
        applyRealisticPreferences(prefs);
        // Une bascule se voit tout de suite (les curseurs du dialogue, eux,
        // passent par le rafraîchissement différé).
        if (realistic_.timer != nullptr) {
            realistic_.timer->stop();
        }
        renderStitches();
    });
    auto* settingsAct = menu->addAction(tr("&Réglages…"));
    settingsAct->setObjectName(QStringLiteral("action_realisticSettings"));
    settingsAct->setToolTip(tr("Épaisseur du fil, relief, brillance, torsion, ombre portée, "
                               "tissu et qualité du rendu réaliste."));
    connect(settingsAct, &QAction::triggered, this, &MainWindow::showRealisticDialog);

    realistic_.timer = new QTimer(this);
    realistic_.timer->setSingleShot(true);
    realistic_.timer->setInterval(kRefreshDelayMs);
    connect(realistic_.timer, &QTimer::timeout, this, [this] {
        if (realistic_.prefs.enabled) {
            renderStitches();
        }
    });
    // Zoom ou défilement : le niveau de détail et la zone visible changent.
    connect(view_, &CanvasView::viewChanged, this, [this] {
        if (realistic_.prefs.enabled && realistic_.timer != nullptr) {
            realistic_.timer->start();
        }
    });
}

void MainWindow::applyRealisticPreferences(const RealisticPreferences& prefs) {
    if (prefs == realistic_.prefs) {
        return;
    }
    realistic_.prefs = prefs;
    saveRealisticPreferences(prefs);
    if (realisticAct_ != nullptr && realisticAct_->isChecked() != prefs.enabled) {
        const QSignalBlocker blocker(realisticAct_);
        realisticAct_->setChecked(prefs.enabled);
    }
    if (realistic_.timer != nullptr) {
        // Le cache est invalidé par sa clé (réglages inclus) : un simple
        // rafraîchissement différé suffit.
        realistic_.timer->start();
    }
}

void MainWindow::showRealisticDialog() {
    if (realistic_.dialog != nullptr) {
        realistic_.dialog->setPreferences(realistic_.prefs);
        realistic_.dialog->show();
        realistic_.dialog->raise();
        realistic_.dialog->activateWindow();
        return;
    }
    auto* dialog = new RealisticRenderDialog(realistic_.prefs, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    realistic_.dialog = dialog;
    connect(dialog, &RealisticRenderDialog::preferencesChanged, this,
            [this](const RealisticPreferences& prefs) { applyRealisticPreferences(prefs); });
    dialog->show();
}

bool MainWindow::renderRealisticStitches() {
    if (!realistic_.prefs.enabled || !sequence_ || simulating() || view_ == nullptr) {
        return false;
    }

    // Couleur de fil par objet ; un objet masqué par les filtres n'est pas peint.
    std::unordered_map<std::uint64_t, std::array<std::uint8_t, 3>> colorOf;
    for (const auto& emb : project_.embroidery_objects) {
        if (objectPassesFilter(emb)) {
            colorOf[emb.id.value] = emb.rgb;
        }
    }
    // Design importé (source == 0) : toujours visible, couleur du premier bloc
    // (noir par défaut), comme l'affichage en lignes.
    if (project_.imported_design) {
        const auto& blocks = project_.imported_design->color_blocks;
        colorOf[ObjectId{}.value] =
            blocks.empty() ? std::array<std::uint8_t, 3>{0, 0, 0} : blocks.front().rgb;
    }
    const auto segments = stitch_render::build_thread_segments(
        *sequence_, sequence_->commands.size(),
        [&colorOf](ObjectId id) -> std::optional<std::array<std::uint8_t, 3>> {
            const auto it = colorOf.find(id.value);
            if (it == colorOf.end()) {
                return std::nullopt;
            }
            return it->second;
        });
    if (segments.empty()) {
        return false;
    }

    const stitch_render::RenderParams params = stitch_render::sanitized(realistic_.prefs.params);
    const double ppm = view_->pixelsPerMm() * view_->devicePixelRatioF();
    if (stitch_render::choose_detail(ppm, params) == stitch_render::Detail::Lines) {
        return false; // dézoom fort : lignes épaisses colorées
    }

    // Zone à peindre : partie visible (avec marge) du motif.
    const QRectF visibleQ = view_->mapToScene(view_->viewport()->rect()).boundingRect();
    stitch_render::RectMm design = stitch_render::segments_bounds(segments);
    const double pad = params.thread_width_mm;
    design = {design.x0 - pad, design.y0 - pad, design.x1 + pad, design.y1 + pad};
    const stitch_render::RectMm visible = intersect(toRect(visibleQ), design);
    if (visible.empty()) {
        return true; // motif hors champ : rien à peindre
    }

    const std::uint64_t key =
        combine(stitch_render::hash_segments(segments), stitch_render::hash_params(params));
    const double ratio = realistic_.requestedPpm > 0.0 ? ppm / realistic_.requestedPpm : 0.0;
    const bool reusable = realistic_.valid && realistic_.key == key &&
                          realistic_.rect.contains(visible) && ratio >= kPpmLow &&
                          ratio <= kPpmHigh;
    if (!reusable) {
        const double mx = (visible.width()) * kViewMargin;
        const double my = (visible.height()) * kViewMargin;
        const stitch_render::RectMm region =
            intersect({visible.x0 - mx, visible.y0 - my, visible.x1 + mx, visible.y1 + my}, design);
        const std::size_t maxPixels =
            params.quality == stitch_render::Quality::High ? kMaxPixelsHigh : kMaxPixelsFast;
        const stitch_render::RasterView rasterView =
            stitch_render::plan_view(region, ppm, maxPixels);
        if (rasterView.width <= 0) {
            return true;
        }
        const stitch_render::RasterImage image =
            stitch_render::render_threads(segments, params, rasterView);
        const QImage qimage(image.rgba.data(), image.width, image.height, image.width * 4,
                            QImage::Format_RGBA8888_Premultiplied);
        realistic_.pixmap = QPixmap::fromImage(qimage); // copie : `image` est locale
        realistic_.rect = rasterView.rect;
        realistic_.pixmapPpm = rasterView.px_per_mm;
        realistic_.requestedPpm = ppm;
        realistic_.key = key;
        realistic_.valid = true;
        ++realistic_.renderCount;
    }

    auto* item = scene_->addPixmap(realistic_.pixmap);
    item->setTransformationMode(Qt::SmoothTransformation);
    item->setPos(realistic_.rect.x0, realistic_.rect.y0);
    item->setScale(1.0 / realistic_.pixmapPpm);
    item->setZValue(20);
    stitchItems_.append(item);
    return true;
}

} // namespace openstitch::desktop
