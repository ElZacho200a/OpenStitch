// SPDX-License-Identifier: Apache-2.0
// Interface de l'auto-satin par squelette et traversées orientées (spec
// specs/plans/satin-squelette-traversees.md) : création, aperçu de couverture,
// guides d'orientation, visualisation du squelette et des traversées. Membres de
// MainWindow séparés de main_window.cpp pour la lisibilité. Aucune logique métier
// ici : le squelette, l'orientation et les traversées viennent de libs/auto_satin,
// et toute mutation du document passe par une commande annulable.
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainterPath>
#include <QPen>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "app_theme.hpp"
#include "main_window.hpp"
#include "node_handle.hpp"
#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/vectorization/vectorize.hpp"

namespace openstitch::desktop {

namespace {

// Scène en mm, Y vers le bas ; modèle en µm, Y vers le haut (ADR-003).
QPointF toScene(Vec2um p) {
    return QPointF(to_millimeters(p.x).value, -to_millimeters(p.y).value);
}

Vec2um toModel(QPointF s) {
    return Vec2um{to_micrometers(Millimeters{s.x()}), to_micrometers(Millimeters{-s.y()})};
}

auto_satin::SkeletonSatinParameters engine_parameters(const document::AutoSatinParams& p,
                                                      bool measureCoverage) {
    auto_satin::SkeletonSatinParameters e;
    e.spacing = p.spacing;
    e.measure_coverage = measureCoverage;
    for (const auto& g : p.guides) {
        e.guides.push_back({g.anchor, g.angle.radians, g.absolute});
    }
    return e;
}

// Empreinte (FNV-1a) de ce qui détermine le résultat du moteur : forme source,
// guides et espacement. Sert de clé au cache du résumé de diagnostic.
std::uint64_t summary_key(const document::VectorObject& source,
                          const document::AutoSatinParams& p) {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::int64_t v) {
        h ^= static_cast<std::uint64_t>(v);
        h *= 1099511628211ull;
    };
    const auto path = [&](const geometry::Path& pa) {
        mix(static_cast<std::int64_t>(pa.nodes.size()));
        for (const auto& n : pa.nodes) {
            mix(n.pos.x.value);
            mix(n.pos.y.value);
        }
    };
    for (const auto& set : source.paths) {
        path(set.outer);
        for (const auto& hole : set.holes) {
            path(hole);
        }
    }
    mix(p.spacing.value);
    for (const auto& g : p.guides) {
        mix(g.anchor.x.value);
        mix(g.anchor.y.value);
        mix(static_cast<std::int64_t>(std::llround(g.angle.radians * 1e6)));
        mix(g.absolute ? 1 : 0);
    }
    return h;
}

// Angle d'une droite, ramené dans (-π/2, π/2].
double wrap_half_pi(double a) {
    a = std::fmod(a, std::numbers::pi);
    if (a > std::numbers::pi / 2.0) {
        a -= std::numbers::pi;
    } else if (a <= -std::numbers::pi / 2.0) {
        a += std::numbers::pi;
    }
    return a;
}

} // namespace

std::optional<std::vector<geometry::PathSet>>
MainWindow::pristineSatinContour(const document::VectorObject& vector) const {
    if (!vector.source_region || !project_.segmentation) {
        return std::nullopt;
    }
    vectorization::VectorizeOptions options;
    options.mm_per_px = project_.mm_per_px;
    const auto raw =
        vectorization::vectorize_region(*project_.segmentation, *vector.source_region, options);
    if (!raw || raw->empty()) {
        return std::nullopt;
    }
    double current = 0.0;
    for (const auto& set : vector.paths) {
        current += geometry::path_set_area_um2(set);
    }
    double pristine = 0.0;
    for (const auto& set : *raw) {
        pristine += geometry::path_set_area_um2(set);
    }
    // Moins de 3 % de plus : le contour est déjà celui de la région (ou retouché à la main).
    if (pristine <= 0.0 || current <= pristine * 1.03) {
        return std::nullopt;
    }
    return *raw;
}

MainWindow::AutoSatinPreview
MainWindow::previewAutoSatin(const document::VectorObject& source,
                             const document::AutoSatinParams& params) const {
    AutoSatinPreview out;
    double areaSum = 0.0;
    double coveredSum = 0.0;
    double overlapSum = 0.0;
    for (const auto& set : source.paths) {
        const auto res = auto_satin::generate_skeleton_satin(set, engine_parameters(params, true));
        if (!res) {
            out.messages << QString::fromStdString(res.error().message);
            continue;
        }
        out.columns += res->columns.size();
        out.orphanGuides += res->diagnostics.orphan_guides;
        for (const auto& m : res->diagnostics.messages) {
            const QString q = QString::fromStdString(m);
            if (!out.messages.contains(q)) {
                out.messages << q;
            }
        }
        if (res->diagnostics.coverage_measured) {
            double area = std::abs(geometry::signed_area_um2(set.outer));
            for (const auto& hole : set.holes) {
                area -= std::abs(geometry::signed_area_um2(hole));
            }
            area = std::max(area, 0.0) / 1e6;
            areaSum += area;
            coveredSum += area * res->diagnostics.coverage_ratio;
            overlapSum += area * res->diagnostics.overlap_ratio;
            out.uncoveredMm2 += res->diagnostics.uncovered_area_mm2;
        }
    }
    if (areaSum > 0.0) {
        out.measured = true;
        out.coverage = coveredSum / areaSum;
        out.overlap = overlapSum / areaSum;
    }
    return out;
}

QString MainWindow::describeAutoSatinPreview(const AutoSatinPreview& preview) const {
    QString text = tr("%n colonne(s)", nullptr, static_cast<int>(preview.columns));
    if (preview.measured) {
        text += tr(" · couverture estimée %1 % · fil en double ×%2")
                    .arg(preview.coverage * 100.0, 0, 'f', 1)
                    .arg(preview.overlap, 0, 'f', 2);
        if (preview.uncoveredMm2 >= 1.0) {
            text += tr("\n⚠ %1 mm² resteraient sans point.").arg(preview.uncoveredMm2, 0, 'f', 1);
        }
    }
    if (preview.orphanGuides > 0) {
        text += tr("\n⚠ %1 guide(s) trop loin de l'axe, ignoré(s).").arg(preview.orphanGuides);
    }
    for (const auto& m : preview.messages) {
        text += tr("\nAttention : %1").arg(m);
    }
    return text;
}

QString MainWindow::autoSatinSummary(const document::EmbroideryObject& emb) {
    const auto* params = std::get_if<document::AutoSatinParams>(&emb.params);
    const auto* source = project_.findObject(emb.source_vector);
    if (params == nullptr || source == nullptr) {
        return {};
    }
    const std::uint64_t key = summary_key(*source, *params);
    if (autoSatinSummaryCache_.id == emb.id && autoSatinSummaryCache_.key == key &&
        autoSatinSummaryCache_.valid) {
        return autoSatinSummaryCache_.text;
    }
    autoSatinSummaryCache_ = {emb.id, key,
                              describeAutoSatinPreview(previewAutoSatin(*source, *params)), true};
    return autoSatinSummaryCache_.text;
}

void MainWindow::createAutoSatin(bool askParameters) {
    if (!selectedObject_ || hasMultiSelection()) {
        return;
    }
    const auto* current = project_.findObject(*selectedObject_);
    if (current == nullptr || current->paths.empty()) {
        return;
    }
    // Le satin suit la région telle que segmentée, pas le contour agrandi par le recouvrement
    // des tatamis voisins (sinon il déborde de sa zone).
    const auto restored = pristineSatinContour(*current);
    document::VectorObject restoredCopy;
    const document::VectorObject* source = current;
    if (restored) {
        restoredCopy = *current;
        restoredCopy.paths = *restored;
        source = &restoredCopy;
    }

    document::AutoSatinParams params;
    const AutoSatinPreview preview = previewAutoSatin(*source, params);

    if (preview.columns == 0) {
        // Le moteur n'a produit aucune colonne : refus nommé, jamais contourné par
        // une géométrie de moindre qualité. Seul choix actionnable : un tatami.
        const auto answer = QMessageBox::question(
            this, tr("Satin impossible"),
            tr("Aucune colonne satin n'a pu être construite pour cette région :\n%1\n\n"
               "Utiliser un remplissage tatami à la place ?")
                .arg(preview.messages.isEmpty() ? tr("forme non exploitable")
                                                : preview.messages.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
        document::EmbroideryObject tatami;
        tatami.id = project_.object_ids.next();
        tatami.name = tr("Tatami de %1").arg(QString::fromStdString(source->name)).toStdString();
        tatami.source_vector = source->id;
        tatami.rgb = source->rgb;
        tatami.params = document::TatamiParams{};
        tatami.intent = document::EmbroideryIntent::ForcedUserChoice;
        undoStack_.execute(
            std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(tatami)), project_);
        showStitchesAct_->setChecked(true);
        refreshImage();
        updateActions();
        statusBar()->showMessage(tr("Remplissage tatami créé (satin impossible)."));
        return;
    }

    if (askParameters) {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Satin"));
        auto* layout = new QFormLayout(&dialog);
        auto* info = new QLabel(describeAutoSatinPreview(preview), &dialog);
        info->setWordWrap(true);
        layout->addRow(info);
        auto* warn = new QLabel(tr("⚠ Le satin automatique n'a pas été validé sur machine. "
                                   "Vérifiez le résultat (densité, virages, jonctions) avant "
                                   "broderie."),
                                &dialog);
        warn->setWordWrap(true);
        warn->setStyleSheet(
            QStringLiteral("color:%1;").arg(AppTheme::instance().tokens().warning.name()));
        layout->addRow(warn);
        auto* spacingSpin = new QDoubleSpinBox(&dialog);
        spacingSpin->setRange(0.1, 1.5);
        spacingSpin->setDecimals(2);
        spacingSpin->setSuffix(tr(" mm"));
        spacingSpin->setValue(to_millimeters(params.spacing).value);
        auto* compSpin = new QDoubleSpinBox(&dialog);
        compSpin->setRange(0.0, 1.0);
        compSpin->setDecimals(2);
        compSpin->setSuffix(tr(" mm"));
        compSpin->setValue(0.0);
        auto* underlayCheck = new QCheckBox(tr("Sous-couche centrale"), &dialog);
        underlayCheck->setChecked(params.center_underlay);
        auto* splitCheck =
            new QCheckBox(tr("Fractionner les traversées longues (> 7 mm)"), &dialog);
        splitCheck->setChecked(params.split_stitch != document::SatinSplit::Disabled);
        layout->addRow(tr("Espacement :"), spacingSpin);
        layout->addRow(tr("Compensation de tirage :"), compSpin);
        layout->addRow(underlayCheck);
        layout->addRow(splitCheck);
        auto* buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addRow(buttons);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        params.spacing = to_micrometers(Millimeters{spacingSpin->value()});
        params.pull_compensation = to_micrometers(Millimeters{compSpin->value()});
        params.center_underlay = underlayCheck->isChecked();
        params.split_stitch = splitCheck->isChecked() ? document::SatinSplit::Staggered
                                                      : document::SatinSplit::Disabled;
    } else {
        const auto answer = QMessageBox::question(
            this, tr("Convertir en satin"),
            tr("%1\n\nCréer le satin ? (annulable)").arg(describeAutoSatinPreview(preview)));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    document::EmbroideryObject object;
    object.id = project_.object_ids.next();
    object.name = tr("Satin de %1").arg(QString::fromStdString(source->name)).toStdString();
    object.source_vector = source->id;
    object.rgb = source->rgb;
    object.params = params;
    // L'utilisateur a explicitement demandé un satin : jamais une classification
    // automatique (§21/§24 du plan de refonte satin).
    object.intent = document::EmbroideryIntent::ForcedUserChoice;
    if (restored) {
        auto group = std::make_unique<commands::CompositeCommand>("Créer un satin");
        group->add(std::make_unique<commands::SetVectorPathsCommand>(source->id, *restored,
                                                                     "Contour brut de la région"));
        group->add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(object)));
        undoStack_.execute(std::move(group), project_);
    } else {
        undoStack_.execute(
            std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(object)), project_);
    }
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Satin créé : %1.").arg(describeAutoSatinPreview(preview).section('\n', 0, 0)) +
        (restored ? tr(" Contour ramené à celui de la région (recouvrement tatami retiré).")
                  : QString()));
}

void MainWindow::applyAutoSatinEdit(ObjectId id, document::AutoSatinParams params,
                                    const QString& label, const QString& mergeTag) {
    auto cmd = std::make_unique<commands::EditAutoSatinCommand>(id, std::move(params),
                                                                label.toStdString());
    cmd->setMergeTag(mergeTag.toStdString());
    undoStack_.execute(std::move(cmd), project_);
    refreshImage();
    updateActions();
}

void MainWindow::addAutoSatinGuideFromStroke(ObjectId id, Vec2um from, Vec2um to) {
    const auto* emb = project_.findEmbroidery(id);
    const auto* sat =
        emb != nullptr ? std::get_if<document::AutoSatinParams>(&emb->params) : nullptr;
    if (sat == nullptr) {
        return;
    }
    const double dx = static_cast<double>(to.x.value - from.x.value);
    const double dy = static_cast<double>(to.y.value - from.y.value);
    if (std::hypot(dx, dy) < 300.0) {
        statusBar()->showMessage(tr("Trait trop court pour fixer une direction."));
        return;
    }
    document::AutoSatinParams params = *sat;
    // Trait tracé = direction des fils voulue, en absolu (repère du dessin).
    params.guides.push_back({from, Angle{wrap_half_pi(std::atan2(dy, dx))}, true});
    applyAutoSatinEdit(id, std::move(params), tr("Ajouter un guide d'orientation"));
}

void MainWindow::changeAutoSatinGuide(ObjectId id, int index, double angleDeg, bool absolute) {
    const auto* emb = project_.findEmbroidery(id);
    const auto* sat =
        emb != nullptr ? std::get_if<document::AutoSatinParams>(&emb->params) : nullptr;
    if (sat == nullptr || index < 0 || static_cast<std::size_t>(index) >= sat->guides.size()) {
        return;
    }
    document::AutoSatinParams params = *sat;
    auto& guide = params.guides[static_cast<std::size_t>(index)];
    const Angle next{wrap_half_pi(angleDeg * std::numbers::pi / 180.0)};
    if (guide.angle.radians == next.radians && guide.absolute == absolute) {
        return;
    }
    guide.angle = next;
    guide.absolute = absolute;
    applyAutoSatinEdit(id, std::move(params), tr("Modifier un guide d'orientation"),
                       QStringLiteral("guide-%1").arg(index));
}

void MainWindow::highlightAutoSatinGuide(ObjectId id, int index) {
    const auto* emb = project_.findEmbroidery(id);
    const auto* sat =
        emb != nullptr ? std::get_if<document::AutoSatinParams>(&emb->params) : nullptr;
    if (sat == nullptr || index < 0 || static_cast<std::size_t>(index) >= sat->guides.size()) {
        return;
    }
    if (guideHighlight_ != nullptr) {
        baseItems_.removeAll(guideHighlight_);
        delete guideHighlight_;
        guideHighlight_ = nullptr;
    }
    const auto& tokens = AppTheme::instance().tokens();
    const QPointF c = toScene(sat->guides[static_cast<std::size_t>(index)].anchor);
    auto* ring = new QGraphicsEllipseItem(-13.0, -13.0, 26.0, 26.0);
    ring->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    QPen pen(tokens.warning, 3.0);
    ring->setPen(pen);
    ring->setBrush(Qt::NoBrush);
    ring->setPos(c);
    ring->setZValue(102);
    ring->setToolTip(tr("Guide d'orientation #%1").arg(index + 1));
    scene_->addItem(ring);
    baseItems_.append(ring);
    guideHighlight_ = ring;
    statusBar()->showMessage(tr("Guide #%1 mis en évidence sur le canevas.").arg(index + 1), 3000);
}

void MainWindow::removeAutoSatinGuide(ObjectId id, int index) {
    const auto* emb = project_.findEmbroidery(id);
    const auto* sat =
        emb != nullptr ? std::get_if<document::AutoSatinParams>(&emb->params) : nullptr;
    if (sat == nullptr || index < 0 || static_cast<std::size_t>(index) >= sat->guides.size()) {
        return;
    }
    document::AutoSatinParams params = *sat;
    params.guides.erase(params.guides.begin() + index);
    applyAutoSatinEdit(id, std::move(params), tr("Supprimer un guide d'orientation"));
}

void MainWindow::renderAutoSatinOverlay(const document::EmbroideryObject& obj,
                                        const document::AutoSatinParams& params,
                                        const document::VectorObject& source) {
    const auto& tokens = AppTheme::instance().tokens();

    QPainterPath chords;
    QPainterPath axes;
    std::vector<std::vector<Vec2um>> axisList;
    for (const auto& set : source.paths) {
        const auto res = auto_satin::generate_skeleton_satin(set, engine_parameters(params, false));
        if (!res) {
            continue;
        }
        for (const auto& column : res->columns) {
            for (const auto& c : column.crossings) {
                chords.moveTo(toScene(c.a));
                chords.lineTo(toScene(c.b));
            }
        }
        for (const auto& axis : res->axes) {
            if (axis.size() < 2) {
                continue;
            }
            axes.moveTo(toScene(axis.front()));
            for (std::size_t i = 1; i < axis.size(); ++i) {
                axes.lineTo(toScene(axis[i]));
            }
            axisList.push_back(axis);
        }
    }
    // 1) Traversées calculées (aperçu fidèle de l'orientation des fils).
    QColor chordColor = tokens.canvasHandle;
    chordColor.setAlpha(110);
    QPen chordPen(chordColor);
    chordPen.setCosmetic(true);
    chordPen.setWidthF(1.0);
    auto* chordItem = scene_->addPath(chords, chordPen, Qt::NoBrush);
    chordItem->setZValue(96);
    baseItems_.append(chordItem);
    // 2) Squelette (axe de référence de chaque colonne).
    QPen axisPen(tokens.accent);
    axisPen.setCosmetic(true);
    axisPen.setWidthF(1.6);
    axisPen.setStyle(Qt::DashLine);
    auto* axisItem = scene_->addPath(axes, axisPen, Qt::NoBrush);
    axisItem->setZValue(97);
    axisItem->setToolTip(tr("Squelette (axe de référence des traversées)"));
    baseItems_.append(axisItem);

    // 3) Guides : un trait indiquant la direction des fils et une poignée
    //    (glisser = déplacer l'ancre, clic droit = supprimer).
    const ObjectId targetId = obj.id;
    const std::uint64_t generation = documentGeneration_;
    for (std::size_t gi = 0; gi < params.guides.size(); ++gi) {
        const auto& guide = params.guides[gi];
        double direction = guide.angle.radians;
        if (!guide.absolute) {
            // Relatif : perpendiculaire à la tangente de l'axe le plus proche + écart.
            double best = std::numeric_limits<double>::max();
            double alpha = 0.0;
            for (const auto& axis : axisList) {
                for (std::size_t i = 0; i + 1 < axis.size(); ++i) {
                    const QPointF a = toScene(axis[i]);
                    const QPointF b = toScene(axis[i + 1]);
                    const QPointF p = toScene(guide.anchor);
                    const QPointF ab = b - a;
                    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
                    const double t =
                        len2 > 1e-12
                            ? std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) /
                                             len2,
                                         0.0, 1.0)
                            : 0.0;
                    const QPointF q = a + ab * t;
                    const double d = std::hypot(p.x() - q.x(), p.y() - q.y());
                    if (d < best) {
                        best = d;
                        alpha = std::atan2(-ab.y(), ab.x()); // scène Y bas -> repère modèle
                    }
                }
            }
            direction = alpha + std::numbers::pi / 2.0 + guide.angle.radians;
        }
        const QPointF c = toScene(guide.anchor);
        const double half = 5.0; // mm
        const QPointF d(std::cos(direction) * half, -std::sin(direction) * half);
        QPainterPath arrow;
        arrow.moveTo(c - d);
        arrow.lineTo(c + d);
        QPen pen(tokens.warning);
        pen.setCosmetic(true);
        pen.setWidthF(2.4);
        auto* line = scene_->addPath(arrow, pen, Qt::NoBrush);
        line->setZValue(98);
        line->setToolTip(tr("Guide d'orientation #%1 (%2)")
                             .arg(gi + 1)
                             .arg(guide.absolute ? tr("absolu") : tr("relatif")));
        baseItems_.append(line);

        const auto onReleased = [this, targetId, generation, gi](QPointF released) {
            QTimer::singleShot(0, this, [this, targetId, generation, gi, released] {
                if (generation != documentGeneration_) {
                    return;
                }
                const auto* e = project_.findEmbroidery(targetId);
                const auto* s =
                    e != nullptr ? std::get_if<document::AutoSatinParams>(&e->params) : nullptr;
                if (s == nullptr || gi >= s->guides.size()) {
                    return;
                }
                const Vec2um to = toModel(released);
                if (s->guides[gi].anchor == to) {
                    return; // clic sans déplacement : aucun historique fantôme
                }
                auto next = *s;
                next.guides[gi].anchor = to;
                applyAutoSatinEdit(targetId, std::move(next),
                                   tr("Déplacer un guide d'orientation"));
            });
        };
        const auto onContextMenu = [this, targetId, generation, gi](QPoint globalPos) {
            QTimer::singleShot(0, this, [this, targetId, generation, gi, globalPos] {
                if (generation != documentGeneration_) {
                    return;
                }
                QMenu menu(this);
                QAction* remove = menu.addAction(tr("Supprimer le guide"));
                if (menu.exec(globalPos) == remove) {
                    removeAutoSatinGuide(targetId, static_cast<int>(gi));
                }
            });
        };
        auto* handle = new NodeHandleItem(c, onReleased, {}, onContextMenu);
        handle->setPen(QPen(tokens.warning, 1.5));
        handle->setBrush(QBrush(tokens.warning.lighter(160)));
        handle->setToolTip(tr("Glisser pour déplacer — clic droit pour supprimer"));
        scene_->addItem(handle);
        baseItems_.append(handle);
    }
}

} // namespace openstitch::desktop
