// SPDX-License-Identifier: Apache-2.0
// Outils de tracé du menu Forme : Lisser (HP-VEC-007), Convertir en courbes (HP-VEC-008) et
// formes prédéfinies (HP-VEC-014). Aucune géométrie ici : tout vient de libs/geometry
// (smooth.hpp, to_curves.hpp, shapes.hpp) et de commands/vector_path_commands.hpp ; la
// sélection entière est traitée en UN pas d'annulation.
#include <QAction>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/commands/vector_path_commands.hpp"
#include "openstitch/geometry/shapes.hpp"

namespace openstitch::desktop {

namespace {

constexpr double kDefaultPresetSizeMm = 40.0;

Micrometers mmToUm(double mm) {
    return Micrometers{static_cast<std::int32_t>(std::lround(mm * 1000.0))};
}

// Repère scène (mm, Y vers le bas) -> modèle (µm, Y vers le haut).
Vec2um sceneToModelUm(QPointF sceneMm) {
    return Vec2um{mmToUm(sceneMm.x()), mmToUm(-sceneMm.y())};
}

Vec2um offset(Vec2um c, std::int32_t dx, std::int32_t dy) {
    return Vec2um{Micrometers{c.x.value + dx}, Micrometers{c.y.value + dy}};
}

} // namespace

void MainWindow::buildVectorToolsMenu() {
    if (shapeMenu_ == nullptr) {
        return;
    }
    shapeMenu_->addSeparator();
    smoothAct_ = shapeMenu_->addAction(tr("&Lisser…"));
    smoothAct_->setObjectName(QStringLiteral("action_shapeSmooth"));
    smoothAct_->setToolTip(tr("Réduit le nombre de nœuds des formes sélectionnées et remplace les "
                              "parties arrondies par des courbes, en restant à moins de la "
                              "tolérance du tracé d'origine (0,2 mm par défaut)."));
    connect(smoothAct_, &QAction::triggered, this, [this] {
        bool ok = false;
        const double mm = QInputDialog::getDouble(
            this, tr("Lisser"), tr("Écart maximal avec le tracé d'origine (mm) :"),
            commands::kDefaultSmoothTolerance.value / 1000.0, 0.01, 5.0, 2, &ok);
        if (ok) {
            smoothSelection(mmToUm(mm));
        }
    });
    convertToCurvesAct_ = shapeMenu_->addAction(tr("Convertir en &courbes"));
    convertToCurvesAct_->setObjectName(QStringLiteral("action_shapeConvertToCurves"));
    convertToCurvesAct_->setToolTip(
        tr("Arrondit les sommets doux des polygones sélectionnés en courbes de Bézier qui passent "
           "par les mêmes points ; les coins francs sont conservés."));
    connect(convertToCurvesAct_, &QAction::triggered, this, [this] { convertSelectionToCurves(); });

    presetShapesMenu_ = shapeMenu_->addMenu(tr("Formes &prédéfinies"));
    presetShapesMenu_->setObjectName(QStringLiteral("menu_presetShapes"));
    const auto addPreset = [this](PresetShape shape, const QString& text, const QString& name) {
        QAction* act = presetShapesMenu_->addAction(text);
        act->setObjectName(name);
        act->setToolTip(tr("Insère la forme au centre de la vue ; ses dimensions sont demandées."));
        connect(act, &QAction::triggered, this, [this, shape] { promptPresetShape(shape); });
    };
    addPreset(PresetShape::Star, tr("É&toile…"), QStringLiteral("action_presetStar"));
    addPreset(PresetShape::Heart, tr("&Cœur…"), QStringLiteral("action_presetHeart"));
    addPreset(PresetShape::Arrow, tr("&Flèche…"), QStringLiteral("action_presetArrow"));
    addPreset(PresetShape::RoundedRectangle, tr("&Rectangle arrondi…"),
              QStringLiteral("action_presetRoundedRectangle"));
    addPreset(PresetShape::Arc, tr("&Arc…"), QStringLiteral("action_presetArc"));
    addPreset(PresetShape::Spiral, tr("&Spirale…"), QStringLiteral("action_presetSpiral"));
    updateVectorToolActions();
}

void MainWindow::updateVectorToolActions() {
    const bool any = !selectedObjectIds().empty();
    const QString why = tr("Sélectionnez au moins une forme vectorielle.");
    for (QAction* act : {smoothAct_, convertToCurvesAct_}) {
        if (act == nullptr) {
            continue;
        }
        if (!act->property("baseToolTip").isValid()) {
            act->setProperty("baseToolTip", act->toolTip());
        }
        act->setEnabled(any);
        const QString base = act->property("baseToolTip").toString();
        act->setToolTip(any ? base : base + QStringLiteral("\n") + why);
    }
}

void MainWindow::smoothSelection(Micrometers tolerance) {
    const std::vector<ObjectId> ids = selectedObjectIds();
    auto result = commands::make_smooth_command(project_, ids, tolerance);
    if (!result.command) {
        statusBar()->showMessage(ids.empty() ? tr("Sélectionnez au moins une forme à lisser.")
                                             : QString::fromUtf8(result.error.c_str()),
                                 8000);
        return;
    }
    const std::size_t count = result.resulting_objects;
    undoStack_.execute(std::move(result.command), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("%1 forme(s) lissée(s). Ctrl+Z pour annuler.").arg(count), 8000);
}

void MainWindow::convertSelectionToCurves() {
    const std::vector<ObjectId> ids = selectedObjectIds();
    auto result = commands::make_convert_to_curves_command(project_, ids);
    if (!result.command) {
        statusBar()->showMessage(ids.empty() ? tr("Sélectionnez au moins une forme à convertir.")
                                             : QString::fromUtf8(result.error.c_str()),
                                 8000);
        return;
    }
    const std::size_t count = result.resulting_objects;
    undoStack_.execute(std::move(result.command), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("%1 forme(s) convertie(s) en courbes. Ctrl+Z pour annuler.").arg(count), 8000);
}

void MainWindow::promptPresetShape(PresetShape shape) {
    bool ok = false;
    const double sizeMm = QInputDialog::getDouble(this, tr("Forme prédéfinie"), tr("Taille (mm) :"),
                                                  kDefaultPresetSizeMm, 2.0, 1000.0, 1, &ok);
    if (!ok) {
        return;
    }
    int points = 5;
    if (shape == PresetShape::Star) {
        points =
            QInputDialog::getInt(this, tr("Étoile"), tr("Nombre de branches :"), 5, 3, 24, 1, &ok);
        if (!ok) {
            return;
        }
    }
    insertPresetShape(shape, sizeMm, points);
}

void MainWindow::insertPresetShape(PresetShape shape, double sizeMm, int starPoints) {
    // Centre de la vue ; repli sur l'origine si la vue n'a pas encore de géométrie.
    Vec2um center{};
    if (view_ != nullptr && view_->viewport() != nullptr) {
        center = sceneToModelUm(view_->mapToScene(view_->viewport()->rect().center()));
    }
    const std::int32_t size = std::max<std::int32_t>(mmToUm(sizeMm).value, 1000);
    const std::int32_t half = size / 2;
    geometry::Path path;
    QString name;
    switch (shape) {
    case PresetShape::Star:
        path = geometry::star_path(center, Micrometers{half},
                                   Micrometers{static_cast<std::int32_t>(half * 0.4)}, starPoints);
        name = tr("Étoile");
        break;
    case PresetShape::Heart:
        path = geometry::heart_path(offset(center, -half, -half), offset(center, half, half));
        name = tr("Cœur");
        break;
    case PresetShape::Arrow:
        path = geometry::arrow_path(offset(center, -half, 0), offset(center, half, 0),
                                    Micrometers{static_cast<std::int32_t>(size * 0.3)},
                                    Micrometers{static_cast<std::int32_t>(size * 0.6)},
                                    Micrometers{static_cast<std::int32_t>(size * 0.4)});
        name = tr("Flèche");
        break;
    case PresetShape::RoundedRectangle: {
        const std::int32_t h = static_cast<std::int32_t>(size / 3);
        path = geometry::rounded_rectangle_path(offset(center, -half, -h), offset(center, half, h),
                                                Micrometers{size / 8});
        name = tr("Rectangle arrondi");
        break;
    }
    case PresetShape::Arc:
        path = geometry::arc_path(center, Micrometers{half}, 0.0, std::numbers::pi);
        name = tr("Arc");
        break;
    case PresetShape::Spiral:
        path = geometry::spiral_path(center, Micrometers{size / 20}, Micrometers{half}, 3.0);
        name = tr("Spirale");
        break;
    }
    if (path.nodes.empty()) {
        statusBar()->showMessage(tr("Forme impossible avec ces dimensions."), 6000);
        return;
    }
    addVectorPrimitive(std::move(path), name);
}

} // namespace openstitch::desktop
