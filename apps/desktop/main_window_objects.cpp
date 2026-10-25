// SPDX-License-Identifier: Apache-2.0
// Manipulation d'objets (roadmap §9, HP-OBJ-*) : presse-papiers, duplication avec broderie,
// rotation, miroir, transformation du design entier, sélection de tous les objets ou d'une
// couleur, couleur d'un objet de broderie. Aucune géométrie ici : tout est calculé par
// libs/commands (vector_ops.hpp, transform_ops.hpp) et appliqué en UN pas d'annulation.
#include <QAction>
#include <QClipboard>
#include <QColorDialog>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QInputDialog>
#include <QLineF>
#include <QMenu>
#include <QMimeData>
#include <QStandardPaths>
#include <QStatusBar>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/commands/transform_ops.hpp"
#include "openstitch/commands/vector_ops.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "selection_hit_test.hpp"

namespace openstitch::desktop {

namespace {

// Décalage d'un collage (et de chaque collage répété) : 5 mm vers la droite et le bas à l'écran.
constexpr std::int32_t kPasteStepUm = 5'000;

// Active/désactive une action et dit pourquoi dans l'info-bulle et la barre d'état (même
// convention que main_window.cpp).
void setEnabledWithReason(QAction* act, bool enabled, const QString& whyDisabled) {
    if (act == nullptr) {
        return;
    }
    if (!act->property("baseToolTip").isValid()) {
        act->setProperty("baseToolTip", act->toolTip());
        act->setProperty("baseStatusTip", act->statusTip());
    }
    act->setEnabled(enabled);
    const QString baseTip = act->property("baseToolTip").toString();
    if (enabled) {
        act->setToolTip(baseTip);
        act->setStatusTip(act->property("baseStatusTip").toString());
    } else {
        act->setToolTip(baseTip + QStringLiteral("\n") + whyDisabled);
        act->setStatusTip(whyDisabled);
    }
}

} // namespace

void MainWindow::buildObjectEditActions(QMenu* editMenu) {
    const auto add = [this](QMenu* menu, QAction*& member, const QString& text, const QString& name,
                            const QKeySequence& key, const QString& help,
                            const std::function<void()>& run) {
        member = menu->addAction(text);
        member->setObjectName(name);
        if (!key.isEmpty()) {
            member->setShortcut(key);
        }
        member->setToolTip(help);
        connect(member, &QAction::triggered, this, run);
    };
    editMenu->addSeparator();
    add(editMenu, cutAct_, tr("Co&uper"), QStringLiteral("action_cut"), QKeySequence::Cut,
        tr("Copie les formes sélectionnées (avec leur broderie) puis les supprime."),
        [this] { cutSelection(); });
    add(editMenu, copyAct_, tr("Copi&er"), QStringLiteral("action_copy"), QKeySequence::Copy,
        tr("Copie les formes sélectionnées avec leur broderie (réglages, fil, retouches)."),
        [this] { copySelection(); });
    add(editMenu, pasteAct_, tr("C&oller"), QStringLiteral("action_paste"), QKeySequence::Paste,
        tr("Colle les formes copiées, décalées de 5 mm. Fonctionne aussi d'un projet à un autre."),
        [this] { pasteClipboard(false); });
    add(editMenu, pasteInPlaceAct_, tr("Coller &sur place"), QStringLiteral("action_pasteInPlace"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V),
        tr("Colle les formes copiées exactement à leur position d'origine."),
        [this] { pasteClipboard(true); });
    add(editMenu, duplicateWithStitchesAct_, tr("Dupliquer avec la &broderie"),
        QStringLiteral("action_duplicateWithStitches"), QKeySequence(Qt::CTRL | Qt::Key_D),
        tr("Duplique les formes sélectionnées avec leur broderie, décalées de 2 mm."),
        [this] { duplicateSelectionWithStitches(); });
    editMenu->addSeparator();
    add(editMenu, selectAllObjectsAct_, tr("Sélectionner &tous les objets"),
        QStringLiteral("action_selectAllObjects"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A),
        tr("Sélectionne toutes les formes visibles du design."), [this] { selectAllObjects(); });
    add(editMenu, copyStyleAct_, tr("Copier le style"), QStringLiteral("action_copyStyle"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
        tr("Mémorise les réglages de couture de la forme brodée active (espacement, angle, "
           "sous-couche…). Évite de recopier les mêmes valeurs d'un objet à l'autre."),
        [this] { copyStyle(); });
    add(editMenu, pasteStyleAct_, tr("Coller le style"), QStringLiteral("action_pasteStyle"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Y),
        tr("Applique le style copié aux formes brodées sélectionnées de même type de point, en un "
           "seul pas d'annulation."),
        [this] { pasteStyle(); });
    add(editMenu, isolateAct_, tr("Isoler la sélection"), QStringLiteral("action_isolate"),
        QKeySequence(Qt::SHIFT | Qt::Key_I),
        tr("Masque les autres formes et les exclut du clic : protège le reste du design pendant "
           "que vous travaillez. État de session, non enregistré dans le projet (Maj+I)."),
        [this] { toggleIsolation(isolateAct_->isChecked()); });
    isolateAct_->setCheckable(true);
    add(editMenu, selectSameColorObjectsAct_, tr("Sélectionner les objets de &même couleur"),
        QStringLiteral("action_selectSameColorObjects"), QKeySequence(),
        tr("Sélectionne toutes les formes brodées de la même couleur que la forme active."),
        [this] { selectObjectsWithSameColor(); });
    add(editMenu, embroideryColorAct_, tr("Couleur de la broderie…"),
        QStringLiteral("action_embroideryColor"), QKeySequence(),
        tr("Choisit une couleur libre pour la broderie des formes sélectionnées."), [this] {
            const std::vector<ObjectId> targets = targetEmbroideryIds();
            if (targets.empty()) {
                return;
            }
            const auto* first = project_.findEmbroidery(targets.front());
            const QColor initial = first != nullptr
                                       ? QColor(first->rgb[0], first->rgb[1], first->rgb[2])
                                       : QColor(Qt::black);
            const QColor chosen = pickThreadColor(initial, tr("Couleur de la broderie"));
            if (chosen.isValid()) {
                setEmbroideryColorForSelection({static_cast<std::uint8_t>(chosen.red()),
                                                static_cast<std::uint8_t>(chosen.green()),
                                                static_cast<std::uint8_t>(chosen.blue())});
            }
        });

    // Coller s'active dès qu'une autre instance (ou celle-ci) copie des formes.
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this,
            [this] { updateObjectEditActions(); });

    QMenu* transform = editMenu->addMenu(tr("Trans&former"));
    transform->setObjectName(QStringLiteral("menu_transform"));
    add(transform, rotateLeftAct_, tr("Pivoter de 90° à &gauche"),
        QStringLiteral("action_rotateLeft"), QKeySequence(Qt::CTRL | Qt::Key_BracketLeft),
        tr("Tourne la sélection d'un quart de tour dans le sens anti-horaire."),
        [this] { rotateSelection(90.0); });
    add(transform, rotateRightAct_, tr("Pivoter de 90° à &droite"),
        QStringLiteral("action_rotateRight"), QKeySequence(Qt::CTRL | Qt::Key_BracketRight),
        tr("Tourne la sélection d'un quart de tour dans le sens horaire."),
        [this] { rotateSelection(-90.0); });
    add(transform, rotateAngleAct_, tr("&Pivoter d'un angle…"),
        QStringLiteral("action_rotateAngle"), QKeySequence(),
        tr("Tourne la sélection de l'angle saisi (positif : sens anti-horaire)."), [this] {
            bool ok = false;
            const double degrees = QInputDialog::getDouble(
                this, tr("Pivoter"), tr("Angle en degrés (positif : sens anti-horaire) :"),
                lastRotationDeg_, -360.0, 360.0, 1, &ok);
            if (ok && degrees != 0.0) {
                lastRotationDeg_ = degrees;
                rotateSelection(degrees);
            }
        });
    transform->addSeparator();
    add(transform, mirrorHorizontalAct_, tr("Miroir &horizontal"),
        QStringLiteral("action_mirrorHorizontal"), QKeySequence(),
        tr("Retourne la sélection de gauche à droite."), [this] { mirrorSelection(true); });
    add(transform, mirrorVerticalAct_, tr("Miroir &vertical"),
        QStringLiteral("action_mirrorVertical"), QKeySequence(),
        tr("Retourne la sélection de haut en bas."), [this] { mirrorSelection(false); });

    QMenu* design = editMenu->addMenu(tr("Desi&gn entier"));
    design->setObjectName(QStringLiteral("menu_wholeDesign"));
    add(design, designCenterAct_, tr("&Centrer dans le cadre"),
        QStringLiteral("action_designCenter"), QKeySequence(),
        tr("Déplace tout le design pour le centrer dans le cadre de broderie."),
        [this] { centerDesignInHoop(); });
    add(design, designFitAct_, tr("&Ajuster à la zone brodable"),
        QStringLiteral("action_designFit"), QKeySequence(),
        tr("Redimensionne et centre tout le design pour qu'il tienne dans la zone brodable."),
        [this] { fitDesignToHoop(); });
    add(design, designScaleAct_, tr("&Redimensionner le design…"),
        QStringLiteral("action_designScale"), QKeySequence(),
        tr("Agrandit ou réduit tout le design ; les points sont recalculés."), [this] {
            bool ok = false;
            const double percent = QInputDialog::getDouble(
                this, tr("Redimensionner le design"), tr("Taille en % de la taille actuelle :"),
                100.0, 5.0, 1000.0, 1, &ok);
            if (ok && percent != 100.0) {
                scaleDesign(percent / 100.0);
            }
        });
    design->addSeparator();
    add(design, designRotateAct_, tr("&Pivoter le design de 90°"),
        QStringLiteral("action_designRotate"), QKeySequence(),
        tr("Tourne tout le design d'un quart de tour (sens horaire), autour du centre du cadre."),
        [this] { transformDesign(1); });
    add(design, designMirrorHorizontalAct_, tr("Miroir &horizontal du design"),
        QStringLiteral("action_designMirrorHorizontal"), QKeySequence(),
        tr("Retourne tout le design de gauche à droite."), [this] { transformDesign(2); });
    add(design, designMirrorVerticalAct_, tr("Miroir &vertical du design"),
        QStringLiteral("action_designMirrorVertical"), QKeySequence(),
        tr("Retourne tout le design de haut en bas."), [this] { transformDesign(3); });
}

void MainWindow::updateObjectEditActions() {
    const bool anyTarget = !transformTargets().empty();
    const bool anyObject = !project_.vector_objects.empty();
    const QString needShapes = tr("Sélectionnez d'abord une ou plusieurs formes.");
    const QString needDesign = tr("Le design ne contient aucune forme.");
    for (QAction* act :
         {cutAct_, copyAct_, duplicateWithStitchesAct_, rotateLeftAct_, rotateRightAct_,
          rotateAngleAct_, mirrorHorizontalAct_, mirrorVerticalAct_}) {
        setEnabledWithReason(act, anyTarget, needShapes);
    }
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    setEnabledWithReason(pasteAct_,
                         mime != nullptr &&
                             (mime->hasFormat(project_io::kFragmentMimeType) || mime->hasImage()),
                         tr("Copiez d'abord des formes (Édition ▸ Copier)."));
    setEnabledWithReason(pasteInPlaceAct_, pasteAct_ != nullptr && pasteAct_->isEnabled(),
                         tr("Copiez d'abord des formes (Édition ▸ Copier)."));
    setEnabledWithReason(selectAllObjectsAct_, anyObject, needDesign);
    setEnabledWithReason(selectSameColorObjectsAct_, anyTarget, needShapes);
    setEnabledWithReason(copyStyleAct_, !targetEmbroideryIds().empty(),
                         tr("Sélectionnez d'abord une forme brodée."));
    setEnabledWithReason(pasteStyleAct_,
                         styleClipboard_.has_value() && !targetEmbroideryIds().empty(),
                         tr("Copiez d'abord un style (Édition ▸ Copier le style), puis "
                            "sélectionnez les formes brodées à modifier."));
    setEnabledWithReason(isolateAct_, anyTarget || isolating(),
                         tr("Sélectionnez d'abord la ou les formes à isoler."));
    setEnabledWithReason(embroideryColorAct_, !targetEmbroideryIds().empty(),
                         tr("Sélectionnez d'abord une forme brodée."));
    for (QAction* act : {designCenterAct_, designFitAct_, designScaleAct_, designRotateAct_,
                         designMirrorHorizontalAct_, designMirrorVerticalAct_}) {
        setEnabledWithReason(act, anyObject, needDesign);
    }
}

std::vector<ObjectId> MainWindow::transformTargets() const {
    std::vector<ObjectId> ids = selectedObjectIds();
    if (ids.empty() && selectedEmbroidery_) {
        if (const auto* emb = project_.findEmbroidery(*selectedEmbroidery_)) {
            if (project_.findObject(emb->source_vector) != nullptr) {
                ids.push_back(emb->source_vector);
            }
        }
    }
    return ids;
}

std::vector<ObjectId> MainWindow::targetEmbroideryIds() const {
    const std::vector<ObjectId> shapes = transformTargets();
    std::vector<ObjectId> out;
    for (const auto& emb : project_.embroidery_objects) {
        if (std::find(shapes.begin(), shapes.end(), emb.source_vector) != shapes.end()) {
            out.push_back(emb.id);
        }
    }
    if (out.empty() && selectedEmbroidery_ &&
        project_.findEmbroidery(*selectedEmbroidery_) != nullptr) {
        out.push_back(*selectedEmbroidery_);
    }
    return out;
}

void MainWindow::copySelection() {
    const auto fragment = commands::collect_fragment(project_, transformTargets());
    if (fragment.empty()) {
        return;
    }
    auto* mime = new QMimeData;
    mime->setData(project_io::kFragmentMimeType,
                  QByteArray::fromStdString(project_io::fragment_to_json(fragment)));
    QGuiApplication::clipboard()->setMimeData(mime);
    pasteRepeat_ = 0;
    updateActions();
    statusBar()->showMessage(
        tr("%1 forme(s) copiée(s) — Ctrl+V pour coller.").arg(fragment.vectors.size()), 6000);
}

void MainWindow::cutSelection() {
    const std::vector<ObjectId> ids = transformTargets();
    if (ids.empty()) {
        return;
    }
    copySelection();
    auto batch = std::make_unique<commands::CompositeCommand>(tr("Couper").toStdString());
    for (const ObjectId id : ids) {
        batch->add(std::make_unique<commands::RemoveVectorObjectCommand>(id));
    }
    undoStack_.execute(std::move(batch), project_);
    setSelection({});
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("%1 forme(s) coupée(s).").arg(ids.size()), 6000);
}

void MainWindow::pasteClipboard(bool inPlace) {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    // HP-FMT-016 : une image du presse-papiers (capture, copie depuis un navigateur) s'ouvre comme
    // une image importée, via un PNG temporaire.
    if (mime != nullptr && !mime->hasFormat(project_io::kFragmentMimeType) && mime->hasImage()) {
        const QImage clip = qvariant_cast<QImage>(mime->imageData());
        const QString tmp = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                .filePath(QStringLiteral("openstitch-presse-papiers.png"));
        if (!clip.isNull() && clip.save(tmp, "PNG")) {
            openPath(tmp);
        } else {
            statusBar()->showMessage(tr("L'image du presse-papiers n'a pas pu être lue."), 6000);
        }
        return;
    }
    if (mime == nullptr || !mime->hasFormat(project_io::kFragmentMimeType)) {
        statusBar()->showMessage(
            tr("Rien à coller : copiez d'abord des formes (Édition ▸ Copier)."), 6000);
        return;
    }
    const QByteArray bytes = mime->data(project_io::kFragmentMimeType);
    const auto fragment =
        project_io::fragment_from_json(std::string_view(bytes.constData(), bytes.size()));
    if (!fragment) {
        statusBar()->showMessage(QString::fromStdString(fragment.error().message), 8000);
        return;
    }
    Vec2um delta{};
    if (!inPlace) {
        ++pasteRepeat_;
        delta = Vec2um{Micrometers{kPasteStepUm * pasteRepeat_},
                       Micrometers{-kPasteStepUm * pasteRepeat_}};
    }
    applyPastedFragment(*fragment, delta, tr("Coller"),
                        tr("%1 forme(s) collée(s). Ctrl+Z pour annuler."));
}

void MainWindow::duplicateSelectionWithStitches() {
    const auto fragment = commands::collect_fragment(project_, transformTargets());
    applyPastedFragment(fragment, Vec2um{Micrometers{2'000}, Micrometers{-2'000}}, tr("Dupliquer"),
                        tr("%1 forme(s) dupliquée(s) avec leur broderie."));
}

void MainWindow::applyPastedFragment(const document::ObjectFragment& fragment, Vec2um delta,
                                     const QString& label, const QString& done) {
    auto pasted = commands::make_paste_command(project_, fragment, delta, label.toStdString());
    if (!pasted.command) {
        return;
    }
    undoStack_.execute(std::move(pasted.command), project_);
    setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = pasted.pasted});
    showVectorsAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(done.arg(pasted.pasted.size()), 6000);
}

std::vector<ObjectId> MainWindow::selectableObjectsAt(QPointF posMm) const {
    // Tolérance : 0,4 mm ou 4 px écran, le plus grand (un tracé ouvert reste cliquable).
    const double pxPerMm = std::max(view_->pixelsPerMm(), 1e-6);
    const double toleranceMm = std::max(0.4, 4.0 / pxPerMm);
    return objectsAtPointMm(
        project_, posMm, toleranceMm,
        [this](const document::VectorObject& object) { return vectorPassesFilter(object); });
}

ObjectId MainWindow::cycleTarget(const std::vector<ObjectId>& under, QPointF posMm) {
    const double radiusMm = 2.0 / std::max(view_->pixelsPerMm(), 1e-6);
    const Selection current = currentSelection();
    const bool sameSpot = under.size() > 1 && cycleIds_ == under &&
                          QLineF(cycleAnchorMm_, posMm).length() <= radiusMm &&
                          current.objects.size() == 1 && cycleIndex_ >= 0 &&
                          static_cast<std::size_t>(cycleIndex_) < under.size() &&
                          current.objects.front() == under[static_cast<std::size_t>(cycleIndex_)];
    cycleIndex_ =
        sameSpot ? static_cast<int>((static_cast<std::size_t>(cycleIndex_) + 1) % under.size()) : 0;
    cycleIds_ = under;
    cycleAnchorMm_ = posMm;
    return under[static_cast<std::size_t>(cycleIndex_)];
}

void MainWindow::selectAllInActiveLayer() {
    const bool anyVisibleObject =
        std::any_of(project_.vector_objects.begin(), project_.vector_objects.end(),
                    [](const document::VectorObject& o) { return o.visible; });
    if (showVectorsAct_->isChecked() && anyVisibleObject) {
        selectAllObjects();
    } else {
        selectAllRegions();
    }
}

void MainWindow::copyStyle() {
    const std::vector<ObjectId> targets = targetEmbroideryIds();
    const auto* source = targets.empty() ? nullptr : project_.findEmbroidery(targets.back());
    if (source == nullptr) {
        statusBar()->showMessage(tr("Sélectionnez d'abord une forme brodée."), 5000);
        return;
    }
    styleClipboard_ = source->params;
    updateActions();
    statusBar()->showMessage(
        tr("Style de « %1 » copié : sélectionnez d'autres formes puis Coller le style.")
            .arg(QString::fromStdString(source->name)),
        6000);
}

void MainWindow::pasteStyle() {
    if (!styleClipboard_) {
        statusBar()->showMessage(tr("Aucun style copié."), 5000);
        return;
    }
    auto group = std::make_unique<commands::CompositeCommand>(tr("Coller le style").toStdString());
    int applied = 0;
    int skipped = 0;
    for (const ObjectId id : targetEmbroideryIds()) {
        const auto* object = project_.findEmbroidery(id);
        if (object == nullptr) {
            continue;
        }
        if (object->params.index() != styleClipboard_->index()) {
            ++skipped; // autre type de point : le style ne s'y applique pas
            continue;
        }
        if (object->params == *styleClipboard_) {
            continue; // déjà identique : aucune commande parasite
        }
        group->add(std::make_unique<commands::SetStitchParamsCommand>(id, *styleClipboard_,
                                                                      tr("Style").toStdString()));
        ++applied;
    }
    if (applied == 0) {
        statusBar()->showMessage(skipped > 0 ? tr("Style non collé : aucun objet sélectionné n'a "
                                                  "le même type de point.")
                                             : tr("Rien à changer : style déjà identique."),
                                 6000);
        return;
    }
    undoStack_.execute(std::move(group), project_);
    refreshImage();
    updateActions();
    updateInspector();
    QString message = tr("Style collé sur %1 objet(s).").arg(applied);
    if (skipped > 0) {
        message += tr(" %1 ignoré(s) (autre type de point).").arg(skipped);
    }
    statusBar()->showMessage(message, 6000);
}

void MainWindow::toggleIsolation(bool on) {
    if (!on) {
        isolatedIds_.clear();
        statusBar()->showMessage(tr("Isolation terminée : toutes les formes sont à nouveau "
                                    "visibles."),
                                 5000);
    } else {
        const std::vector<ObjectId> targets = transformTargets();
        if (targets.empty()) {
            QSignalBlocker block(isolateAct_);
            isolateAct_->setChecked(false);
            statusBar()->showMessage(tr("Sélectionnez d'abord la ou les formes à isoler."), 5000);
            return;
        }
        isolatedIds_ = targets;
        statusBar()->showMessage(
            tr("%1 forme(s) isolée(s) — Maj+I pour quitter l'isolation.").arg(isolatedIds_.size()),
            8000);
    }
    displayImage(processed_);
    updateActions();
}

void MainWindow::zoomToSelection() {
    const Selection current = currentSelection();
    std::vector<ObjectId> sources = current.objects;
    if (current.embroidery) {
        if (const auto* emb = project_.findEmbroidery(*current.embroidery)) {
            sources.push_back(emb->source_vector);
        }
    }
    QRectF box;
    for (const ObjectId id : sources) {
        if (const auto* object = project_.findObject(id)) {
            box = box.united(objectScenePath(*object).boundingRect());
        }
    }
    if (!box.isValid() || box.isNull()) {
        statusBar()->showMessage(tr("Rien à cadrer : sélectionnez un objet (Z)."), 5000);
        return;
    }
    view_->zoomToRectMm(box);
}

void MainWindow::selectAllObjects() {
    // Mode « Éditer les points » : Ctrl+A sélectionne les points de l'objet, pas les objets.
    if (stitchPointEditActive()) {
        selectAllStitchPoints();
        return;
    }
    std::vector<ObjectId> ids;
    for (const auto& object : project_.vector_objects) {
        if (object.visible) {
            ids.push_back(object.id);
        }
    }
    if (ids.empty()) {
        return;
    }
    showVectorsAct_->setChecked(true);
    setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = ids});
    displayImage(processed_);
    updateActions();
    statusBar()->showMessage(tr("%1 forme(s) sélectionnée(s).").arg(ids.size()), 6000);
}

void MainWindow::selectObjectsWithSameColor() {
    const std::vector<ObjectId> current = transformTargets();
    if (current.empty()) {
        return;
    }
    // Couleur d'une forme : celle de sa broderie si elle en a une, sinon la sienne.
    const auto colorOf = [this](const document::VectorObject& object) {
        for (const auto& emb : project_.embroidery_objects) {
            if (emb.source_vector == object.id) {
                return emb.rgb;
            }
        }
        return object.rgb;
    };
    const auto* active = project_.findObject(current.back());
    if (active == nullptr) {
        return;
    }
    const auto wanted = colorOf(*active);
    std::vector<ObjectId> ids;
    for (const auto& object : project_.vector_objects) {
        if (object.visible && object.id != active->id && colorOf(object) == wanted) {
            ids.push_back(object.id);
        }
    }
    ids.push_back(active->id); // la forme active reste l'active (dernière)
    setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = ids});
    displayImage(processed_);
    updateActions();
    statusBar()->showMessage(tr("%1 forme(s) de cette couleur sélectionnée(s).").arg(ids.size()),
                             6000);
}

void MainWindow::setEmbroideryColorForSelection(std::array<std::uint8_t, 3> rgb) {
    const std::vector<ObjectId> targets = targetEmbroideryIds();
    if (targets.empty()) {
        return;
    }
    // Couleur libre : aucun fil de nuancier associé (HP-OBJ-018).
    undoStack_.execute(std::make_unique<commands::SetObjectThreadCommand>(
                           targets, std::nullopt, rgb, tr("Couleur de la broderie").toStdString()),
                       project_);
    refreshImage();
    updateActions();
    updateInspector(); // la pastille de l'inspecteur suit la nouvelle couleur
    statusBar()->showMessage(tr("Couleur appliquée à %1 objet(s) de broderie.").arg(targets.size()),
                             6000);
}

void MainWindow::runTransform(const std::vector<ObjectId>& ids,
                              const commands::AffineTransform& transform, const QString& label,
                              const QString& done) {
    auto command = commands::make_transform_command(project_, ids, transform, label.toStdString());
    if (!command) {
        return;
    }
    undoStack_.execute(std::move(command), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(done, 6000);
}

void MainWindow::rotateSelection(double degrees) {
    rotateObjects(transformTargets(), degrees);
}

double MainWindow::rotationDragDegrees(QPointF centerScene, QPointF handleScene,
                                       QPointF releasedScene, bool snap) {
    // Scène : Y vers le bas ; modèle : Y vers le haut. On inverse Y pour un angle anti-horaire.
    const double before =
        std::atan2(-(handleScene.y() - centerScene.y()), handleScene.x() - centerScene.x());
    const double after =
        std::atan2(-(releasedScene.y() - centerScene.y()), releasedScene.x() - centerScene.x());
    double degrees = (after - before) * 180.0 / std::numbers::pi;
    // Ramène dans ]-180 ; 180] : le plus court chemin angulaire.
    while (degrees > 180.0) {
        degrees -= 360.0;
    }
    while (degrees <= -180.0) {
        degrees += 360.0;
    }
    if (snap) {
        degrees = std::round(degrees / 15.0) * 15.0;
    }
    return std::abs(degrees) < 0.05 ? 0.0 : degrees;
}

void MainWindow::rotateObjects(const std::vector<ObjectId>& ids, double degrees,
                               std::optional<Vec2um> pivot) {
    const auto box = commands::objects_bounds(project_, ids);
    if (!box) {
        return;
    }
    runTransform(ids,
                 commands::AffineTransform::rotation(pivot.value_or(box->center()),
                                                     Angle{degrees * std::numbers::pi / 180.0}),
                 tr("Pivoter"), tr("Sélection pivotée de %1°.").arg(degrees, 0, 'f', 1));
}

void MainWindow::mirrorSelection(bool horizontal) {
    const std::vector<ObjectId> ids = transformTargets();
    const auto box = commands::objects_bounds(project_, ids);
    if (!box) {
        return;
    }
    runTransform(ids, commands::AffineTransform::mirror(box->center(), horizontal),
                 horizontal ? tr("Miroir horizontal") : tr("Miroir vertical"),
                 tr("Sélection retournée."));
}

void MainWindow::centerDesignInHoop() {
    const std::vector<ObjectId> ids = commands::all_object_ids(project_);
    const auto box = commands::objects_bounds(project_, ids);
    if (!box) {
        return;
    }
    const Vec2um c = box->center();
    if (c.x.value == 0 && c.y.value == 0) {
        statusBar()->showMessage(tr("Le design est déjà centré dans le cadre."), 6000);
        return;
    }
    // Le cadre est centré sur l'origine du repère.
    runTransform(ids,
                 commands::AffineTransform::translate(
                     Vec2um{Micrometers{-c.x.value}, Micrometers{-c.y.value}}),
                 tr("Centrer le design"), tr("Design centré dans le cadre."));
}

void MainWindow::scaleDesign(double factor) {
    const std::vector<ObjectId> ids = commands::all_object_ids(project_);
    const auto box = commands::objects_bounds(project_, ids);
    if (!box || !(factor > 0.0)) {
        return;
    }
    runTransform(ids, commands::AffineTransform::scale(box->center(), factor),
                 tr("Redimensionner le design"),
                 tr("Design redimensionné à %1 %.").arg(factor * 100.0, 0, 'f', 1));
}

void MainWindow::fitDesignToHoop() {
    const std::vector<ObjectId> ids = commands::all_object_ids(project_);
    const auto box = commands::objects_bounds(project_, ids);
    if (!box) {
        return;
    }
    const double w = static_cast<double>(box->max.x.value) - box->min.x.value;
    const double h = static_cast<double>(box->max.y.value) - box->min.y.value;
    const double availW = static_cast<double>(project_.canvas.usable_width().value);
    const double availH = static_cast<double>(project_.canvas.usable_height().value);
    if (w <= 0.0 || h <= 0.0 || availW <= 0.0 || availH <= 0.0) {
        return;
    }
    // Mise à l'échelle autour du centre du design, puis recentrage sur le cadre.
    const double factor = std::min(availW / w, availH / h);
    commands::AffineTransform t = commands::AffineTransform::scale(box->center(), factor);
    const Vec2um c = box->center();
    t.translation = Vec2um{Micrometers{-c.x.value}, Micrometers{-c.y.value}};
    runTransform(ids, t, tr("Ajuster le design au cadre"),
                 tr("Design ajusté à la zone brodable (%1 %).").arg(factor * 100.0, 0, 'f', 1));
}

void MainWindow::transformDesign(int kind) {
    const std::vector<ObjectId> ids = commands::all_object_ids(project_);
    if (ids.empty()) {
        return;
    }
    // Autour du centre du CADRE (l'origine) : un design centré le reste.
    const Vec2um pivot{};
    if (kind == 1) {
        runTransform(ids,
                     commands::AffineTransform::rotation(pivot, Angle{-std::numbers::pi / 2.0}),
                     tr("Pivoter le design"), tr("Design pivoté de 90°."));
    } else {
        const bool horizontal = kind == 2;
        runTransform(ids, commands::AffineTransform::mirror(pivot, horizontal),
                     horizontal ? tr("Miroir horizontal du design")
                                : tr("Miroir vertical du design"),
                     tr("Design retourné."));
    }
}

} // namespace openstitch::desktop
