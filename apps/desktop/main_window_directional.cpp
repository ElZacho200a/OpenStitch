// SPDX-License-Identifier: Apache-2.0
// Interface du remplissage directionnel : conversion, mode « Guides de
// direction » (aperçu du champ, tracé/déplacement/suppression des guides et
// des lignes de rupture). Membres de MainWindow séparés de main_window.cpp
// pour la lisibilité. Aucune logique métier ici : le champ, la conversion et
// le lissage des guides viennent du cœur, et toute mutation du document passe
// par une commande annulable.
#include <QAction>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QMenu>
#include <QMessageBox>
#include <QPainterPath>
#include <QPen>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <iterator>

#include "app_theme.hpp"
#include "main_window.hpp"
#include "node_handle.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"

namespace openstitch::desktop {

namespace {

// Scène en mm, Y vers le bas ; modèle en µm, Y vers le haut (ADR-003).
QPointF toScene(Vec2um p) {
    return QPointF(to_millimeters(p.x).value, -to_millimeters(p.y).value);
}

Vec2um toModel(QPointF s) {
    return Vec2um{to_micrometers(Millimeters{s.x()}), to_micrometers(Millimeters{-s.y()})};
}

// Chemin Qt d'un guide (ouvert, courbes de Bézier réelles).
QPainterPath openPainterPath(const geometry::Path& path) {
    QPainterPath out;
    const std::size_t n = path.nodes.size();
    if (n == 0) {
        return out;
    }
    out.moveTo(toScene(path.nodes[0].pos));
    for (std::size_t e = 0; e + 1 < n; ++e) {
        const auto& a = path.nodes[e];
        const auto& b = path.nodes[e + 1];
        if (a.tan_out || b.tan_in) {
            const QPointF c1 = a.tan_out ? toScene(a.pos + *a.tan_out) : toScene(a.pos);
            const QPointF c2 = b.tan_in ? toScene(b.pos + *b.tan_in) : toScene(b.pos);
            out.cubicTo(c1, c2, toScene(b.pos));
        } else {
            out.lineTo(toScene(b.pos));
        }
    }
    return out;
}

} // namespace

std::optional<document::DirectionalFillParams>
MainWindow::directionalParamsFor(const document::EmbroideryObject& emb) const {
    const auto* source = project_.findObject(emb.source_vector);
    if (source == nullptr || source->paths.empty()) {
        return std::nullopt;
    }
    document::TatamiParams base;
    if (const auto* tatami = std::get_if<document::TatamiParams>(&emb.params)) {
        base = *tatami;
    }
    return stitch_generation::directional_from_tatami(base, source->paths,
                                                      static_cast<std::uint32_t>(emb.id.value));
}

void MainWindow::convertToDirectional(ObjectId embroideryId) {
    const auto* emb = project_.findEmbroidery(embroideryId);
    if (emb == nullptr || emb->is_directional()) {
        return;
    }
    auto params = directionalParamsFor(*emb);
    if (!params) {
        QMessageBox::warning(this, tr("Conversion impossible"),
                             tr("Aucun contour source pour le remplissage directionnel."));
        return;
    }
    // Même commande que « Type de points » : un réseau satin en plusieurs
    // sections partageant la source est converti d'un bloc (cf. setStitchType).
    undoStack_.execute(std::make_unique<commands::ConvertFillGroupCommand>(
                           embroideryId, std::move(*params), "Type : remplissage directionnel"),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Remplissage directionnel — « Guides de direction » (D) pour tracer des courbes "
           "guides et des lignes de rupture."));
}

void MainWindow::buildDirectionalActions(QMenu* embMenu) {
    directionGuideModeAct_ = embMenu->addAction(tr("Guides de &direction…"));
    directionGuideModeAct_->setObjectName(QStringLiteral("action_directionGuideMode"));
    directionGuideModeAct_->setCheckable(true);
    directionGuideModeAct_->setShortcut(QKeySequence(Qt::Key_D));
    directionGuideModeAct_->setToolTip(
        tr("Remplissage directionnel sélectionné : affiche le champ de directions (petits "
           "traits), les guides et les lignes de rupture ; glisser un point pour le déplacer, "
           "clic droit pour supprimer (D)."));
    connect(directionGuideModeAct_, &QAction::toggled, this,
            &MainWindow::onDirectionGuideModeToggled);
    autoDirectionGuideAct_ = embMenu->addAction(tr("Generer un guide depuis la forme"));
    autoDirectionGuideAct_->setObjectName(QStringLiteral("action_autoDirectionGuide"));
    autoDirectionGuideAct_->setToolTip(
        tr("Calcule un guide de direction editable depuis l'axe medial de la forme."));
    connect(autoDirectionGuideAct_, &QAction::triggered, this,
            &MainWindow::generateDirectionGuideFromShape);
    const auto startDrawing = [this](Tool tool) {
        if (!directionGuideModeAct_->isChecked()) {
            directionGuideModeAct_->setChecked(true);
        }
        if (directionGuideTarget_) {
            setTool(tool);
        }
    };
    drawDirectionGuideAct_ = embMenu->addAction(tr("Tracer un guide de direction"));
    drawDirectionGuideAct_->setObjectName(QStringLiteral("action_drawDirectionGuide"));
    drawDirectionGuideAct_->setToolTip(
        tr("Cliquez les points de la courbe que le fil doit suivre — Entrée/double-clic pour "
           "terminer."));
    connect(drawDirectionGuideAct_, &QAction::triggered, this,
            [startDrawing] { startDrawing(Tool::DrawDirectionGuide); });
    drawBreakLineAct_ = embMenu->addAction(tr("Tracer une ligne de rupture"));
    drawBreakLineAct_->setObjectName(QStringLiteral("action_drawBreakLine"));
    drawBreakLineAct_->setToolTip(
        tr("Sépare la forme en secteurs aux directions indépendantes (chevrons). La ligne "
           "doit traverser la forme d'un bord à l'autre."));
    connect(drawBreakLineAct_, &QAction::triggered, this,
            [startDrawing] { startDrawing(Tool::DrawBreakLine); });
}

void MainWindow::onDirectionGuideModeToggled(bool on) {
    directionGuideTarget_.reset();
    if (on) {
        // Mode exclusif : un seul jeu de poignées éditables à la fois.
        for (QAction* other :
             {stitchEditModeAct_, satinEditModeAct_, satinGuideModeAct_, railEditModeAct_}) {
            if (other != nullptr && other->isChecked()) {
                other->setChecked(false);
            }
        }
        if (const auto* emb = resolveSelectedEmbroidery();
            emb != nullptr && emb->is_directional()) {
            directionGuideTarget_ = emb->id;
        }
        if (!directionGuideTarget_) {
            QSignalBlocker block(directionGuideModeAct_);
            directionGuideModeAct_->setChecked(false);
        } else {
            statusBar()->showMessage(
                tr("Guides de direction : glissez un point pour le déplacer, clic droit pour "
                   "supprimer ; « Tracer un guide » / « Tracer une ligne de rupture » pour en "
                   "ajouter. Échap pour quitter."));
        }
    } else if (drawingDirectionGuide()) {
        setTool(Tool::Select);
    }
    displayImage(processed_);
    updateActions();
}

void MainWindow::generateDirectionGuideFromShape() {
    auto* emb = resolveSelectedEmbroidery();
    const auto* source = emb != nullptr ? project_.findObject(emb->source_vector) : nullptr;
    if (emb == nullptr || source == nullptr || source->paths.empty() ||
        !(emb->is_tatami() || emb->is_directional())) {
        return;
    }

    std::vector<geometry::Path> generated;
    for (const auto& set : source->paths) {
        auto guides = stitch_generation::directional_guides_from_region(set);
        generated.insert(generated.end(), std::make_move_iterator(guides.begin()),
                         std::make_move_iterator(guides.end()));
    }
    if (generated.empty()) {
        QMessageBox::information(
            this, tr("Guide impossible"),
            tr("Aucun axe exploitable n'a pu etre construit pour cette forme."));
        return;
    }

    document::DirectionalFillParams params;
    if (const auto* directional = std::get_if<document::DirectionalFillParams>(&emb->params)) {
        params = *directional;
    } else {
        auto converted = directionalParamsFor(*emb);
        if (!converted) {
            QMessageBox::warning(this, tr("Conversion impossible"),
                                 tr("Aucun contour source pour le remplissage directionnel."));
            return;
        }
        params = std::move(*converted);
        params.guides.clear();
    }
    params.guides.insert(params.guides.end(), std::make_move_iterator(generated.begin()),
                         std::make_move_iterator(generated.end()));

    const ObjectId target = emb->id;
    if (emb->is_directional()) {
        undoStack_.execute(std::make_unique<commands::EditDirectionalFillCommand>(
                               target, std::move(params), "Generer un guide de direction"),
                           project_);
    } else {
        undoStack_.execute(std::make_unique<commands::ConvertFillGroupCommand>(
                               target, std::move(params), "Generer un guide de direction"),
                           project_);
    }
    showStitchesAct_->setChecked(true);
    refreshImage();
    if (const auto* updated = project_.findEmbroidery(target);
        updated != nullptr && updated->is_directional() && !directionGuideModeAct_->isChecked()) {
        directionGuideModeAct_->setChecked(true);
    }
    updateActions();
    statusBar()->showMessage(tr("Guide de direction genere depuis la forme."));
}

void MainWindow::addDirectionGuidePoint(QPointF posMm) {
    const Vec2um v = toModel(posMm);
    if (!pendingGuidePoints_.empty()) {
        // Même garde anti-doublon que le polygone : la séquence Qt d'un
        // double-clic émet aussi un clic simple au même endroit.
        const Vec2um d = v - pendingGuidePoints_.back();
        const double dx = static_cast<double>(d.x.value);
        const double dy = static_cast<double>(d.y.value);
        if (dx * dx + dy * dy < 4.0) {
            return;
        }
    }
    pendingGuidePoints_.push_back(v);
    updateDirectionGuidePreview(posMm);
    updateDrawActionsState();
    statusBar()->showMessage(
        (currentTool_ == Tool::DrawBreakLine ? tr("Ligne de rupture : %1 point(s)")
                                             : tr("Guide : %1 point(s)"))
            .arg(pendingGuidePoints_.size()) +
        tr(" — Entrée/double-clic pour terminer (2 points min.), Retour arrière pour retirer "
           "le dernier point, Échap pour annuler."));
}

void MainWindow::updateDirectionGuidePreview(QPointF cursorSceneMm) {
    if (pendingGuidePoints_.empty()) {
        return;
    }
    if (guidePreviewItem_ == nullptr) {
        guidePreviewItem_ = new QGraphicsPathItem();
        guidePreviewItem_->setZValue(1000.0);
        scene_->addItem(guidePreviewItem_);
    }
    const bool isBreak = currentTool_ == Tool::DrawBreakLine;
    QPen pen(isBreak ? AppTheme::instance().tokens().warning
                     : AppTheme::instance().tokens().accent);
    pen.setCosmetic(true);
    pen.setWidthF(2.0);
    pen.setStyle(Qt::DashLine);
    guidePreviewItem_->setPen(pen);
    // Aperçu fidèle au résultat : courbe lisse pour un guide, polyligne pour
    // une rupture (même fonction du cœur qu'à la validation).
    std::vector<Vec2um> pts = pendingGuidePoints_;
    pts.push_back(toModel(cursorSceneMm));
    const geometry::Path path = isBreak ? geometry::Path{} : geometry::smooth_open_path(pts);
    QPainterPath painter;
    if (isBreak) {
        painter.moveTo(toScene(pts.front()));
        for (std::size_t i = 1; i < pts.size(); ++i) {
            painter.lineTo(toScene(pts[i]));
        }
    } else {
        painter = openPainterPath(path);
    }
    guidePreviewItem_->setPath(painter);
}

void MainWindow::cancelDirectionGuideDraw() {
    pendingGuidePoints_.clear();
    if (guidePreviewItem_ != nullptr) {
        scene_->removeItem(guidePreviewItem_);
        delete guidePreviewItem_;
        guidePreviewItem_ = nullptr;
    }
    updateDrawActionsState();
}

void MainWindow::removeLastDirectionGuidePoint() {
    if (pendingGuidePoints_.empty()) {
        return;
    }
    pendingGuidePoints_.pop_back();
    if (pendingGuidePoints_.empty()) {
        cancelDirectionGuideDraw();
        return;
    }
    updateDirectionGuidePreview(toScene(pendingGuidePoints_.back()));
    updateDrawActionsState();
}

void MainWindow::finishDirectionGuide() {
    const bool isBreak = currentTool_ == Tool::DrawBreakLine;
    std::vector<Vec2um> pts = std::move(pendingGuidePoints_);
    cancelDirectionGuideDraw();
    if (!directionGuideTarget_) {
        return;
    }
    const auto* emb = project_.findEmbroidery(*directionGuideTarget_);
    const auto* dir =
        emb != nullptr ? std::get_if<document::DirectionalFillParams>(&emb->params) : nullptr;
    if (dir == nullptr) {
        return;
    }
    if (pts.size() < 2) {
        statusBar()->showMessage(tr("Au moins 2 points sont nécessaires."));
        return;
    }
    document::DirectionalFillParams params = *dir;
    if (isBreak) {
        geometry::Path line;
        line.closed = false;
        for (const Vec2um p : pts) {
            line.nodes.push_back({p, geometry::NodeType::Corner, {}, {}});
        }
        params.break_lines.push_back(std::move(line));
    } else {
        params.guides.push_back(geometry::smooth_open_path(pts));
    }
    // L'outil reste actif : on enchaîne souvent plusieurs guides.
    applyDirectionalEdit(emb->id, std::move(params),
                         isBreak ? tr("Tracer une ligne de rupture")
                                 : tr("Tracer un guide de direction"));
}

void MainWindow::applyDirectionalEdit(ObjectId id, document::DirectionalFillParams params,
                                      const QString& label) {
    undoStack_.execute(std::make_unique<commands::EditDirectionalFillCommand>(id, std::move(params),
                                                                              label.toStdString()),
                       project_);
    refreshImage();
    updateActions();
}

void MainWindow::updateDirectionGuideActions() {
    if (directionGuideModeAct_ == nullptr) {
        return;
    }
    const auto* emb = resolveSelectedEmbroidery();
    const bool context = emb != nullptr && emb->is_directional();
    const bool sameTarget =
        directionGuideTarget_.has_value() && emb != nullptr && *directionGuideTarget_ == emb->id;
    if (directionGuideModeAct_->isChecked() && (!context || !sameTarget)) {
        // Sélection changée, objet supprimé ou reconverti (undo) : sortie propre.
        QSignalBlocker block(directionGuideModeAct_);
        directionGuideModeAct_->setChecked(false);
        directionGuideTarget_.reset();
        if (drawingDirectionGuide()) {
            setTool(Tool::Select);
        }
        displayImage(processed_);
    }
    directionGuideModeAct_->setEnabled(context);
    if (autoDirectionGuideAct_ != nullptr) {
        const bool canGenerate = emb != nullptr && (emb->is_tatami() || emb->is_directional()) &&
                                 project_.findObject(emb->source_vector) != nullptr;
        autoDirectionGuideAct_->setEnabled(canGenerate);
    }
    drawDirectionGuideAct_->setEnabled(context);
    drawBreakLineAct_->setEnabled(context);
}

void MainWindow::renderDirectionGuides() {
    if (directionGuideModeAct_ == nullptr || !directionGuideModeAct_->isChecked() ||
        !directionGuideTarget_) {
        return;
    }
    const auto* obj = project_.findEmbroidery(*directionGuideTarget_);
    const auto* dir =
        obj != nullptr ? std::get_if<document::DirectionalFillParams>(&obj->params) : nullptr;
    const auto* source = obj != nullptr ? project_.findObject(obj->source_vector) : nullptr;
    if (dir == nullptr || source == nullptr) {
        return;
    }
    const auto& tokens = AppTheme::instance().tokens();

    // 1) Aperçu du champ : un petit trait par échantillon, orienté comme le
    //    fil — visible AVANT génération, recalculé à chaque édition de guide.
    QPainterPath ticks;
    for (const auto& set : source->paths) {
        if (set.outer.nodes.empty()) {
            continue;
        }
        std::int32_t minX = set.outer.nodes.front().pos.x.value;
        std::int32_t maxX = minX;
        std::int32_t minY = set.outer.nodes.front().pos.y.value;
        std::int32_t maxY = minY;
        for (const auto& n : set.outer.nodes) {
            minX = std::min(minX, n.pos.x.value);
            maxX = std::max(maxX, n.pos.x.value);
            minY = std::min(minY, n.pos.y.value);
            maxY = std::max(maxY, n.pos.y.value);
        }
        // ~35 traits sur la plus grande dimension, entre 1,2 et 6 mm.
        const double extent = static_cast<double>(std::max(maxX - minX, maxY - minY));
        const double step = std::clamp(extent / 35.0, 1'200.0, 6'000.0);
        const double half = 0.35 * step / 1000.0; // mm
        for (const auto& tick : stitch_generation::directional_field_preview(
                 set, *dir, Micrometers{static_cast<std::int32_t>(std::lround(step))})) {
            const QPointF c = toScene(tick.pos);
            const QPointF d(std::cos(tick.angle.radians) * half,
                            -std::sin(tick.angle.radians) * half);
            ticks.moveTo(c - d);
            ticks.lineTo(c + d);
        }
    }
    QColor tickColor = tokens.canvasHandle;
    tickColor.setAlpha(180);
    QPen tickPen(tickColor);
    tickPen.setCosmetic(true);
    tickPen.setWidthF(1.2);
    auto* tickItem = scene_->addPath(ticks, tickPen, Qt::NoBrush);
    tickItem->setZValue(97);
    baseItems_.append(tickItem);

    // 2) Guides (courbes pleines) et lignes de rupture (tirets), avec une
    //    poignée par nœud : glisser = déplacer, clic droit = supprimer.
    const ObjectId targetId = obj->id;
    const std::uint64_t generation = documentGeneration_;
    const auto drawKind = [&](bool isBreak) {
        const auto& paths = isBreak ? dir->break_lines : dir->guides;
        for (std::size_t gi = 0; gi < paths.size(); ++gi) {
            QPen pen(isBreak ? tokens.warning : tokens.accent);
            pen.setCosmetic(true);
            pen.setWidthF(2.0);
            if (isBreak) {
                pen.setStyle(Qt::DashLine);
            }
            auto* item = scene_->addPath(openPainterPath(paths[gi]), pen, Qt::NoBrush);
            item->setZValue(98);
            item->setToolTip(isBreak ? tr("Ligne de rupture #%1").arg(gi + 1)
                                     : tr("Guide de direction #%1").arg(gi + 1));
            baseItems_.append(item);
            for (std::size_t ni = 0; ni < paths[gi].nodes.size(); ++ni) {
                // Toute mutation est DIFFÉRÉE : refreshImage() détruit la
                // poignée pendant son propre évènement souris sinon.
                const auto onReleased = [this, targetId, generation, isBreak, gi,
                                         ni](QPointF released) {
                    QTimer::singleShot(
                        0, this, [this, targetId, generation, isBreak, gi, ni, released] {
                            if (generation != documentGeneration_) {
                                return;
                            }
                            const auto* e = project_.findEmbroidery(targetId);
                            const auto* d =
                                e != nullptr
                                    ? std::get_if<document::DirectionalFillParams>(&e->params)
                                    : nullptr;
                            if (d == nullptr) {
                                return;
                            }
                            auto params = *d;
                            auto& list = isBreak ? params.break_lines : params.guides;
                            if (gi >= list.size() || ni >= list[gi].nodes.size()) {
                                return;
                            }
                            const Vec2um to = toModel(released);
                            if (list[gi].nodes[ni].pos == to) {
                                return; // clic sans déplacement : aucun historique fantôme
                            }
                            list[gi].nodes[ni].pos = to; // tangentes relatives : suivent le nœud
                            applyDirectionalEdit(targetId, std::move(params),
                                                 isBreak ? tr("Déplacer un point de rupture")
                                                         : tr("Déplacer un point de guide"));
                        });
                };
                const auto onContextMenu = [this, targetId, generation, isBreak, gi,
                                            ni](QPoint globalPos) {
                    QTimer::singleShot(
                        0, this, [this, targetId, generation, isBreak, gi, ni, globalPos] {
                            if (generation != documentGeneration_) {
                                return;
                            }
                            const auto* e = project_.findEmbroidery(targetId);
                            const auto* d =
                                e != nullptr
                                    ? std::get_if<document::DirectionalFillParams>(&e->params)
                                    : nullptr;
                            if (d == nullptr) {
                                return;
                            }
                            const auto& list = isBreak ? d->break_lines : d->guides;
                            if (gi >= list.size()) {
                                return;
                            }
                            QMenu menu(this);
                            QAction* removeNode = nullptr;
                            if (list[gi].nodes.size() > 2) {
                                removeNode = menu.addAction(tr("Supprimer ce point"));
                            }
                            QAction* removePath =
                                menu.addAction(isBreak ? tr("Supprimer la ligne de rupture")
                                                       : tr("Supprimer le guide"));
                            QAction* chosen = menu.exec(globalPos);
                            if (chosen == nullptr) {
                                return;
                            }
                            auto params = *d;
                            auto& target = isBreak ? params.break_lines : params.guides;
                            if (chosen == removeNode && ni < target[gi].nodes.size()) {
                                target[gi].nodes.erase(target[gi].nodes.begin() +
                                                       static_cast<std::ptrdiff_t>(ni));
                                applyDirectionalEdit(targetId, std::move(params),
                                                     tr("Supprimer un point de guide"));
                            } else if (chosen == removePath) {
                                target.erase(target.begin() + static_cast<std::ptrdiff_t>(gi));
                                applyDirectionalEdit(targetId, std::move(params),
                                                     isBreak ? tr("Supprimer une ligne de rupture")
                                                             : tr("Supprimer un guide"));
                            }
                        });
                };
                auto* handle = new NodeHandleItem(toScene(paths[gi].nodes[ni].pos), onReleased, {},
                                                  onContextMenu);
                handle->setPen(QPen(isBreak ? tokens.warning : tokens.accent, 1.5));
                handle->setBrush(QBrush((isBreak ? tokens.warning : tokens.accent).lighter(160)));
                handle->setToolTip(tr("Glisser pour déplacer — clic droit pour supprimer"));
                scene_->addItem(handle);
                baseItems_.append(handle);
            }
        }
    };
    drawKind(false);
    drawKind(true);
}

} // namespace openstitch::desktop
