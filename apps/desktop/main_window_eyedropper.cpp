// SPDX-License-Identifier: Apache-2.0
// Outil Pipette (HP-THR-012) : un clic sur l'image échantillonne la couleur
// (thread_palette::pick_thread), propose les fils les plus proches du nuancier courant dans un
// petit menu et assigne le choix à la sélection par la commande existante SetObjectThreadCommand
// (annulable). Aucune règle de choix de fil ici : seulement du câblage.

#include <QAction>
#include <QColor>
#include <QIcon>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>

#include <algorithm>
#include <cmath>
#include <vector>

#include "app_theme.hpp"
#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/thread_palette/chart_import.hpp"
#include "openstitch/thread_palette/eyedropper.hpp"
#include "thread_panel.hpp"

namespace openstitch::desktop {

namespace {

constexpr int kSampleRadiusPx = 2;     // moyenne sur 5 x 5 pixels (lisse le bruit de l'image)
constexpr std::size_t kTopThreads = 5; // fils proposés

QIcon swatch(std::array<std::uint8_t, 3> rgb) {
    QPixmap pm(14, 14);
    pm.fill(QColor(rgb[0], rgb[1], rgb[2]));
    QPainter p(&pm);
    p.setPen(QColor(0, 0, 0, 90));
    p.drawRect(0, 0, 13, 13);
    p.end();
    return QIcon(pm);
}

QIcon dropperIcon() {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(AppTheme::instance().tokens().textSecondary);
    pen.setWidthF(2.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    // Une pipette : bulbe, tube, pointe.
    p.drawLine(QPointF(8, 32), QPointF(22, 18));
    p.drawLine(QPointF(8, 32), QPointF(7, 34));
    p.save();
    p.translate(27, 13);
    p.rotate(45);
    p.drawRoundedRect(QRectF(-4, -9, 8, 18), 3, 3);
    p.restore();
    p.end();
    return QIcon(pm);
}

} // namespace

void MainWindow::buildEyedropperTool() {
    toolEyedropperAct_ = new QAction(dropperIcon(), tr("Pipette"), this);
    toolEyedropperAct_->setObjectName(QStringLiteral("tool_eyedropper"));
    toolEyedropperAct_->setCheckable(true);
    toolEyedropperAct_->setShortcut(QKeySequence(Qt::Key_I));
    toolEyedropperAct_->setToolTip(
        tr("Pipette (I)\nCliquez sur l'image pour échantillonner une couleur : les fils les plus "
           "proches du nuancier courant sont proposés, puis assignés à la sélection."));
    connect(toolEyedropperAct_, &QAction::triggered, this, [this] { setTool(Tool::Eyedropper); });
}

void MainWindow::assignEyedropperChoice(const std::optional<thread_palette::ThreadKey>& key,
                                        std::array<std::uint8_t, 3> rgb, const QString& label) {
    const auto ids = selectedEmbroideryIds();
    if (ids.empty()) {
        statusBar()->showMessage(tr("Sélectionnez d'abord un ou plusieurs objets de broderie."),
                                 5000);
        return;
    }
    undoStack_.execute(std::make_unique<commands::SetObjectThreadCommand>(
                           ids, key, rgb, tr("Pipette : %1").arg(label).toStdString()),
                       project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Pipette : %1 assigné à %2 objet(s).").arg(label).arg(ids.size()),
                             5000);
}

void MainWindow::eyedropperAt(QPointF posMm) {
    if (processed_.empty()) {
        statusBar()->showMessage(tr("Pipette : aucune image à échantillonner (importez-en une)."),
                                 5000);
        return;
    }
    // Position dans l'image affichée : centrée sur l'origine, mm_per_px par pixel, Y vers le bas
    // (cf. renderBase).
    const double mmPerPx = project_.mm_per_px.value;
    if (!(mmPerPx > 0.0)) {
        return;
    }
    const double wMm = processed_.width * mmPerPx;
    const double hMm = processed_.height * mmPerPx;
    const int px = static_cast<int>(std::floor((posMm.x() + wMm / 2.0) / mmPerPx));
    const int py = static_cast<int>(std::floor((posMm.y() + hMm / 2.0) / mmPerPx));
    if (px < 0 || py < 0 || px >= processed_.width || py >= processed_.height) {
        statusBar()->showMessage(tr("Pipette : cliquez sur l'image pour échantillonner."), 4000);
        return;
    }

    // Fenêtre RGB entrelacée autour du pixel (le cœur ne connaît pas l'image RGBA du bureau) :
    // assez large pour la moyenne, rognée aux bords de l'image.
    const int x0 = std::max(0, px - kSampleRadiusPx);
    const int y0 = std::max(0, py - kSampleRadiusPx);
    const int x1 = std::min(processed_.width - 1, px + kSampleRadiusPx);
    const int y1 = std::min(processed_.height - 1, py + kSampleRadiusPx);
    const int w = x1 - x0 + 1;
    const int h = y1 - y0 + 1;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3U);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t src =
                (static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(processed_.width) +
                 static_cast<std::size_t>(x0 + x)) *
                4U;
            const std::size_t dst = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                     static_cast<std::size_t>(x)) *
                                    3U;
            rgb[dst] = processed_.rgba[src];
            rgb[dst + 1] = processed_.rgba[src + 1];
            rgb[dst + 2] = processed_.rgba[src + 2];
        }
    }
    const thread_palette::RgbImageView view{std::span<const std::uint8_t>(rgb), w, h};
    const std::string chart =
        threadPanel_ != nullptr ? threadPanel_->currentChartId().toStdString() : std::string{};
    // « Limiter à mes fils » (HP-THR-007) : recherche dans le stock possédé ; sinon dans le
    // nuancier courant.
    const thread_palette::ThreadStock* stock =
        (threadPanel_ != nullptr && threadPanel_->limitToStock()) ? &threadPanel_->stock()
                                                                  : nullptr;
    const auto picked = thread_palette::pick_thread(view, px - x0, py - y0, kSampleRadiusPx,
                                                    threadLibrary_, chart, stock, kTopThreads);
    if (!picked) {
        statusBar()->showMessage(tr("Pipette : échantillonnage impossible (%1).")
                                     .arg(QString::fromStdString(picked.error().message)),
                                 5000);
        return;
    }
    eyedropperRgb_ = picked->sampled;
    const QString hex = QString::fromStdString(thread_palette::to_hex_color(picked->sampled));

    if (eyedropperMenu_ != nullptr) {
        eyedropperMenu_->close();
        eyedropperMenu_->deleteLater();
    }
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("menu_eyedropper"));
    menu->setAttribute(Qt::WA_DeleteOnClose, true);
    eyedropperMenu_ = menu;

    const bool haveTarget = !selectedEmbroideryIds().empty();
    QAction* header =
        menu->addAction(swatch(picked->sampled), tr("Couleur échantillonnée %1").arg(hex));
    header->setEnabled(false);
    if (!haveTarget) {
        menu->addAction(tr("(sélectionnez un objet de broderie pour assigner)"))->setEnabled(false);
    }
    menu->addSeparator();

    for (const auto& match : picked->matches) {
        const auto thread = threadLibrary_.find(match.key);
        if (!thread) {
            continue;
        }
        const QString code = QString::fromStdString(thread->key.code);
        const QString text = tr("%1 %2 — %3 (écart %4)")
                                 .arg(QString::fromStdString(thread->brand), code,
                                      QString::fromStdString(thread->name),
                                      QLocale(QLocale::French).toString(match.distance, 'f', 1));
        QAction* act = menu->addAction(swatch(thread->rgb), text.simplified());
        act->setEnabled(haveTarget);
        const thread_palette::Thread chosen = *thread;
        connect(act, &QAction::triggered, this, [this, chosen] {
            assignEyedropperChoice(chosen.key, chosen.rgb,
                                   tr("fil %1").arg(QString::fromStdString(chosen.key.code)));
        });
    }

    // « Reprendre le fil d'un objet de broderie » : si le clic tombe sur une forme déjà brodée.
    if (const auto vectorId = objectAt(posMm)) {
        for (const auto& emb : project_.embroidery_objects) {
            if (emb.source_vector != *vectorId) {
                continue;
            }
            menu->addSeparator();
            const QString what =
                emb.thread
                    ? tr("fil %1").arg(QString::fromStdString(emb.thread->code))
                    : tr("couleur %1")
                          .arg(QString::fromStdString(thread_palette::to_hex_color(emb.rgb)));
            QAction* take =
                menu->addAction(swatch(emb.rgb), tr("Reprendre le %1 de l'objet cliqué").arg(what));
            take->setEnabled(haveTarget);
            const auto key = emb.thread;
            const auto objRgb = emb.rgb;
            connect(take, &QAction::triggered, this,
                    [this, key, objRgb, what] { assignEyedropperChoice(key, objRgb, what); });
            break;
        }
    }

    menu->addSeparator();
    QAction* freeColor =
        menu->addAction(swatch(picked->sampled), tr("Couleur libre %1 (sans fil)").arg(hex));
    freeColor->setEnabled(haveTarget);
    const auto sampled = picked->sampled;
    connect(freeColor, &QAction::triggered, this, [this, sampled, hex] {
        assignEyedropperChoice(std::nullopt, sampled, tr("couleur libre %1").arg(hex));
    });

    statusBar()->showMessage(tr("Pipette : %1 — choisissez un fil dans le menu.").arg(hex), 8000);
    const QPoint global = view_->viewport()->mapToGlobal(view_->mapFromScene(posMm));
    menu->popup(global);
}

} // namespace openstitch::desktop
