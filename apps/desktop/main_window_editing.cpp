// SPDX-License-Identifier: Apache-2.0
// Édition multi-objets et boîte de forme : inspecteur « Appliquer à N objets »,
// champs X/Y/L/H d'un objet vectoriel, alignement, panneau Historique. Membres de
// MainWindow séparés de main_window.cpp pour la lisibilité. Aucune logique métier
// ici : toute mutation du document passe par une commande annulable (un seul pas
// d'annulation par geste).
#include <QAction>
#include <QDockWidget>
#include <QFont>
#include <QInputDialog>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStatusBar>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>

#include <QColorDialog>

#include "main_window.hpp"
#include "openstitch/commands/calque_commands.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"
#include "properties_panel.hpp"
#include "stitch_type_catalog.hpp"
#include "ui_memory.hpp"

namespace openstitch::desktop {

namespace {

// Boîte englobante des nœuds d'un objet, en µm (repère du document, Y vers le haut) : la même
// que celle des poignées de redimensionnement (ScaleVectorObjectCommand).
struct BoundsUm {
    std::int64_t minX{std::numeric_limits<std::int64_t>::max()};
    std::int64_t maxX{std::numeric_limits<std::int64_t>::min()};
    std::int64_t minY{std::numeric_limits<std::int64_t>::max()};
    std::int64_t maxY{std::numeric_limits<std::int64_t>::min()};
    bool valid{false};
};

BoundsUm boundsOf(const document::VectorObject& object) {
    BoundsUm b;
    const auto scan = [&b](const geometry::Path& path) {
        for (const auto& node : path.nodes) {
            b.minX = std::min<std::int64_t>(b.minX, node.pos.x.value);
            b.maxX = std::max<std::int64_t>(b.maxX, node.pos.x.value);
            b.minY = std::min<std::int64_t>(b.minY, node.pos.y.value);
            b.maxY = std::max<std::int64_t>(b.maxY, node.pos.y.value);
            b.valid = true;
        }
    };
    for (const auto& set : object.paths) {
        scan(set.outer);
        for (const auto& hole : set.holes) {
            scan(hole);
        }
    }
    return b;
}

Vec2um vec(std::int64_t x, std::int64_t y) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(x)},
                  Micrometers{static_cast<std::int32_t>(y)}};
}

} // namespace

std::optional<QRectF> MainWindow::vectorBoxMm(const document::VectorObject& object) {
    const BoundsUm b = boundsOf(object);
    if (!b.valid) {
        return std::nullopt;
    }
    return QRectF(static_cast<double>(b.minX) / 1000.0, static_cast<double>(b.minY) / 1000.0,
                  static_cast<double>(b.maxX - b.minX) / 1000.0,
                  static_cast<double>(b.maxY - b.minY) / 1000.0);
}

void MainWindow::connectInspectorEditing() {
    connect(propertiesPanel_, &PropertiesPanel::editTextRequested, this,
            &MainWindow::editSelectedText);
    connect(propertiesPanel_, &PropertiesPanel::fillLayerSelected, this, [this](ObjectId id) {
        if (const auto* layer = project_.findEmbroidery(id)) {
            setSelection({.embroidery = id, .objects = {layer->source_vector}});
            displayImage(processed_);
            syncDocumentSelection();
            updateActions();
        }
    });
    connect(propertiesPanel_, &PropertiesPanel::fillColorRequested, this, [this](ObjectId id) {
        const auto* emb = project_.findEmbroidery(id);
        if (emb == nullptr) {
            return;
        }
        const auto generation = documentGeneration_;
        const QColor chosen = pickThreadColor(QColor(emb->rgb[0], emb->rgb[1], emb->rgb[2]),
                                              tr("Couleur de la couche"));
        if (!chosen.isValid() || generation != documentGeneration_) {
            return;
        }
        const std::array<std::uint8_t, 3> rgb{static_cast<std::uint8_t>(chosen.red()),
                                              static_cast<std::uint8_t>(chosen.green()),
                                              static_cast<std::uint8_t>(chosen.blue())};
        const auto* current = project_.findEmbroidery(id);
        if (current == nullptr || rgb == current->rgb) {
            return;
        }
        undoStack_.execute(
            std::make_unique<commands::SetObjectThreadCommand>(
                std::vector<ObjectId>{id}, std::nullopt, rgb, "Couleur de la couche"),
            project_);
        refreshImage();
        updateActions();
    });
    connect(propertiesPanel_, &PropertiesPanel::vectorBoxEdited, this,
            [this](ObjectId id, QRectF want) { applyVectorBox(id, want); });
    connect(propertiesPanel_, &PropertiesPanel::stitchTypeRequested, this,
            [this](int type) { setStitchTypeForSelection(type); });
    connect(propertiesPanel_, &PropertiesPanel::reapplyOverridesRequested, this,
            [this](ObjectId id) {
                // HP-UX-029 : rétablit les paramètres d'avant l'édition (nouveau pas annulable) ;
                // les retouches, dont l'empreinte redevient valide, sont de nouveau appliquées.
                if (!overrideWarning_ || overrideWarning_->id != id ||
                    project_.findEmbroidery(id) == nullptr) {
                    return;
                }
                undoStack_.execute(std::make_unique<commands::SetStitchParamsCommand>(
                                       id, overrideWarning_->before,
                                       tr("Réappliquer les retouches").toStdString()),
                                   project_);
                scheduleParamsRefresh();
                refreshOverrideWarning();
            });
    connect(propertiesPanel_, &PropertiesPanel::applyToSelectionRequested, this,
            &MainWindow::applyToSelection);
    connect(propertiesPanel_, &PropertiesPanel::groupParamsEdited, this,
            &MainWindow::applyGroupParams);
    connect(propertiesPanel_, &PropertiesPanel::groupActionRequested, this,
            &MainWindow::applyGroupAction);
    connect(propertiesPanel_, &PropertiesPanel::groupGuideAngleRequested, this,
            &MainWindow::applyGroupGuideAngle);
}

void MainWindow::noteOverridesInvalidated(ObjectId id, const document::StitchParams& before) {
    // Seule la PREMIÈRE modification d'un objet « ✎ » (retouches encore appliquées) compte : un
    // objet propre n'a rien à avertir, et des retouches déjà obsolètes (⚠) ne sont plus
    // désactivées une seconde fois.
    const auto* emb = project_.findEmbroidery(id);
    if (emb == nullptr || emb->overrides.empty()) {
        return;
    }
    if (overrideWarning_ && overrideWarning_->id == id) {
        return; // bandeau déjà posé pour cet objet
    }
    if (editStateOf(id) != stitch_generation::ObjectEditState::ManuallyEdited) {
        return;
    }
    overrideWarning_ = OverrideWarning{id, static_cast<int>(emb->overrides.size()),
                                       document::StitchParams{before}};
}

void MainWindow::refreshOverrideWarning() {
    if (propertiesPanel_ == nullptr) {
        return;
    }
    std::optional<ObjectId> shownId;
    int count = 0;
    if (overrideWarning_) {
        const auto* emb = project_.findEmbroidery(overrideWarning_->id);
        if (emb == nullptr || emb->overrides.empty()) {
            overrideWarning_.reset(); // objet supprimé ou retouches abandonnées
        } else {
            const auto* selected = resolveSelectedEmbroidery();
            // Visible tant que les paramètres diffèrent de ceux d'avant l'édition : annuler
            // (Ctrl+Z) ou « Réappliquer » les rétablit et retire le bandeau.
            if (selected != nullptr && selected->id == emb->id &&
                !(emb->params == overrideWarning_->before)) {
                shownId = emb->id;
                count = overrideWarning_->count;
            }
        }
    }
    propertiesPanel_->setOverrideWarning(shownId, count);
}

MainWindow::EmbroideryGroup MainWindow::selectedEmbroideryGroup() const {
    EmbroideryGroup group;
    std::optional<std::size_t> kind;
    group.sameType = true;
    for (const ObjectId vectorId : selectedObjectIds()) {
        for (const auto& emb : project_.embroidery_objects) {
            if (emb.source_vector != vectorId) {
                continue;
            }
            group.ids.push_back(emb.id);
            if (!kind) {
                kind = emb.params.index();
            } else if (*kind != emb.params.index()) {
                group.sameType = false;
            }
        }
    }
    if (group.ids.size() < 2) {
        group.sameType = false;
    }
    return group;
}

void MainWindow::applyGroupParams(const QString& field,
                                  const std::function<void(document::StitchParams&)>& apply) {
    const EmbroideryGroup group = selectedEmbroideryGroup();
    if (!group.sameType) {
        return;
    }
    auto composite = std::make_unique<commands::CompositeCommand>(
        tr("Modifier : %1 (%2 objets)").arg(field).arg(group.ids.size()).toStdString());
    for (const ObjectId id : group.ids) {
        const auto* emb = project_.findEmbroidery(id);
        if (emb == nullptr) {
            continue;
        }
        document::StitchParams params = emb->params;
        apply(params);
        if (params == emb->params) {
            continue;
        }
        composite->add(std::make_unique<commands::SetStitchParamsCommand>(id, std::move(params),
                                                                          field.toStdString()));
    }
    if (composite->empty()) {
        return;
    }
    // Une rafale (flèches, molette) sur le même champ du même groupe = un seul pas d'annulation.
    std::string mergeKey = "groupparams:" + field.toStdString();
    for (const ObjectId id : group.ids) {
        mergeKey += ":" + std::to_string(id.value);
    }
    composite->setMergeKey(std::move(mergeKey));
    undoStack_.execute(std::move(composite), project_);
    // Le formulaire montre le représentant : il reste cohérent sans reconstruction. La
    // régénération des points est différée de 150 ms (HP-PERF-009), comme pour un seul objet.
    scheduleParamsRefresh();
}

void MainWindow::applyGroupGuideAngle(double angleDeg, bool absolute) {
    const EmbroideryGroup group = selectedEmbroideryGroup();
    auto composite = std::make_unique<commands::CompositeCommand>(
        tr("Poser un guide d'orientation (%1 objets)").arg(group.ids.size()).toStdString());
    const Angle angle{angleDeg * std::numbers::pi / 180.0};
    for (const ObjectId id : group.ids) {
        const auto* emb = project_.findEmbroidery(id);
        const auto* sat =
            emb != nullptr ? std::get_if<document::AutoSatinParams>(&emb->params) : nullptr;
        const auto* source = emb != nullptr ? project_.findObject(emb->source_vector) : nullptr;
        if (sat == nullptr || source == nullptr) {
            continue;
        }
        const BoundsUm b = boundsOf(*source);
        if (!b.valid) {
            continue;
        }
        document::AutoSatinParams params = *sat;
        // Guide unique, ancré au centre de la forme : angle relatif à l'axe ou absolu.
        params.guides = {document::AutoSatinGuide{vec((b.minX + b.maxX) / 2, (b.minY + b.maxY) / 2),
                                                  angle, absolute}};
        if (params == *sat) {
            continue;
        }
        composite->add(std::make_unique<commands::EditAutoSatinCommand>(
            id, std::move(params), "Poser un guide d'orientation"));
    }
    if (composite->empty()) {
        statusBar()->showMessage(tr("Rien à modifier : ces guides sont déjà posés."));
        return;
    }
    undoStack_.execute(std::move(composite), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Guide posé sur %1 forme(s), en une seule étape annulable.").arg(group.ids.size()),
        8000);
}

void MainWindow::applyGroupAction(const QString& action) {
    const EmbroideryGroup group = selectedEmbroideryGroup();
    auto composite = std::make_unique<commands::CompositeCommand>(
        tr("Guides (%1 objets)").arg(group.ids.size()).toStdString());
    int touched = 0;
    for (const ObjectId id : group.ids) {
        const auto* emb = project_.findEmbroidery(id);
        if (emb == nullptr) {
            continue;
        }
        if (action == QLatin1String("clearSatinGuides")) {
            const auto* sat = std::get_if<document::AutoSatinParams>(&emb->params);
            if (sat == nullptr || sat->guides.empty()) {
                continue;
            }
            document::AutoSatinParams params = *sat;
            params.guides.clear();
            composite->add(std::make_unique<commands::EditAutoSatinCommand>(
                id, std::move(params), "Retirer les guides d'orientation"));
            ++touched;
        } else if (action == QLatin1String("clearDirectionGuides")) {
            const auto* dir = std::get_if<document::DirectionalFillParams>(&emb->params);
            if (dir == nullptr || (dir->guides.empty() && dir->break_lines.empty())) {
                continue;
            }
            document::DirectionalFillParams params = *dir;
            params.guides.clear();
            params.break_lines.clear();
            composite->add(std::make_unique<commands::EditDirectionalFillCommand>(
                id, std::move(params), "Retirer guides et ruptures"));
            ++touched;
        } else if (action == QLatin1String("autoDirectionGuides")) {
            const auto* dir = std::get_if<document::DirectionalFillParams>(&emb->params);
            const auto* source = project_.findObject(emb->source_vector);
            if (dir == nullptr || source == nullptr) {
                continue;
            }
            std::vector<geometry::Path> generated;
            for (const auto& set : source->paths) {
                auto guides = stitch_generation::directional_guides_from_region(set);
                generated.insert(generated.end(), std::make_move_iterator(guides.begin()),
                                 std::make_move_iterator(guides.end()));
            }
            if (generated.empty()) {
                continue;
            }
            document::DirectionalFillParams params = *dir;
            params.guides = std::move(generated); // remplace : un lot reproductible
            composite->add(std::make_unique<commands::EditDirectionalFillCommand>(
                id, std::move(params), "Générer les guides de direction"));
            ++touched;
        }
    }
    if (composite->empty()) {
        statusBar()->showMessage(tr("Rien à modifier pour cette action."));
        return;
    }
    undoStack_.execute(std::move(composite), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("%1 forme(s) modifiée(s), en une seule étape annulable.").arg(touched), 8000);
}

void MainWindow::applyVectorBox(ObjectId id, QRectF want) {
    const auto* object = project_.findObject(id);
    if (object == nullptr) {
        return;
    }
    const BoundsUm b = boundsOf(*object);
    if (!b.valid || b.maxX == b.minX || b.maxY == b.minY) {
        return;
    }
    const double curW = static_cast<double>(b.maxX - b.minX) / 1000.0;
    const double curH = static_cast<double>(b.maxY - b.minY) / 1000.0;
    const bool resize =
        std::abs(want.width() - curW) >= 0.005 || std::abs(want.height() - curH) >= 0.005;
    const std::int64_t dx = std::llround(want.x() * 1000.0) - b.minX;
    const std::int64_t dy = std::llround(want.y() * 1000.0) - b.minY;
    const bool move = dx != 0 || dy != 0;
    if (!resize && !move) {
        return;
    }
    auto composite = std::make_unique<commands::CompositeCommand>(
        resize ? (move ? tr("Modifier la taille et la position de la forme")
                       : tr("Modifier la taille de la forme"))
                     .toStdString()
               : tr("Modifier la position de la forme").toStdString());
    if (resize) {
        // Ancrage au coin bas-gauche : la position reste celle de la boîte jusqu'au déplacement.
        composite->add(std::make_unique<commands::ScaleVectorObjectCommand>(
            id, vec(b.minX, b.minY), want.width() / curW, want.height() / curH));
    }
    if (move) {
        composite->add(std::make_unique<commands::TranslateVectorObjectCommand>(id, vec(dx, dy)));
    }
    undoStack_.execute(std::move(composite), project_);
    refreshImage();
    updateActions();
}

void MainWindow::setStitchTypeForSelection(
    int type, const std::function<void(document::StitchParams&)>& tweak) {
    const std::vector<ObjectId> ids = selectedObjectIds();
    if (ids.empty()) {
        if (selectedEmbroidery_) {
            setStitchType(*selectedEmbroidery_, type);
        }
        return;
    }
    const QString typeName =
        type >= 0 && type < kStitchTypeCount ? stitchTypeLabel(type) : tr("Type de points");
    auto group = std::make_unique<commands::CompositeCommand>(
        tr("Type : %1 (%2 formes)").arg(typeName).arg(ids.size()).toStdString());
    QStringList skipped;
    QString firstError;
    int done = 0;
    for (const ObjectId id : ids) {
        const auto* vec = project_.findObject(id);
        if (vec == nullptr) {
            continue;
        }
        const document::EmbroideryObject* existing = nullptr;
        if (selectedEmbroidery_) {
            const auto* selected = project_.findEmbroidery(*selectedEmbroidery_);
            if (selected != nullptr && selected->source_vector == id) {
                existing = selected; // couche explicite de l'inspecteur, pas le premier fond trouvé
            }
        }
        for (const auto& emb : project_.embroidery_objects) {
            if (existing == nullptr && emb.source_vector == id) {
                existing = &emb;
                break;
            }
        }
        // Confirmer le type déjà présent ne réinitialise aucun réglage et ne
        // fusionne pas les sections d'une même forme. Une demande explicite
        // d'espacement/angle (tweak) garde son effet, même avec le même type.
        if (existing != nullptr && !tweak &&
            ((selectedEmbroidery_ && existing->id == *selectedEmbroidery_ &&
              stitchTypeIndex(*existing) == type) ||
             std::all_of(project_.embroidery_objects.begin(), project_.embroidery_objects.end(),
                         [id, type](const auto& emb) {
                             return emb.source_vector != id || stitchTypeIndex(emb) == type;
                         }))) {
            continue;
        }
        // Forme sans couture : un objet provisoire donne sa forme et ses couleurs au calcul.
        document::EmbroideryObject provisional;
        if (existing == nullptr) {
            provisional.source_vector = id;
            provisional.rgb = vec->rgb;
            provisional.params = document::TatamiParams{};
        }
        document::StitchParams params;
        std::string label;
        std::optional<std::vector<geometry::PathSet>> restored;
        QString error;
        if (!stitchParamsForType(existing != nullptr ? *existing : provisional, type, params, label,
                                 restored, error)) {
            skipped << QString::fromStdString(vec->name);
            if (firstError.isEmpty()) {
                firstError = error;
            }
            continue;
        }
        if (existing == nullptr) {
            ui_memory::creation::apply(params); // HP-UX-002 : forme neuve = dernier réglage
        }
        if (tweak) {
            tweak(params);
        }
        if (restored) {
            group->add(std::make_unique<commands::SetVectorPathsCommand>(
                id, *restored, "Contour brut de la région"));
        }
        if (existing != nullptr) {
            group->add(std::make_unique<commands::ConvertFillGroupCommand>(
                existing->id, std::move(params), std::move(label)));
        } else {
            document::EmbroideryObject object;
            object.id = project_.object_ids.next();
            object.name =
                tr("%1 de %2").arg(typeName, QString::fromStdString(vec->name)).toStdString();
            object.source_vector = id;
            object.rgb = vec->rgb;
            object.params = std::move(params);
            object.intent = document::EmbroideryIntent::ForcedUserChoice;
            group->add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(object)));
        }
        ++done;
    }
    if (group->empty()) {
        if (skipped.isEmpty()) {
            return; // tout est déjà du type demandé : pas de dialogue ni d'étape undo
        }
        QMessageBox::warning(this, tr("Changement de type impossible"),
                             firstError.isEmpty() ? tr("Aucune forme à modifier.") : firstError);
        return;
    }
    undoStack_.execute(std::move(group), project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    QString message =
        tr("%1 : appliqué à %2 forme(s), en une seule étape annulable.").arg(typeName).arg(done);
    if (!skipped.isEmpty()) {
        message +=
            tr(" Ignorées (%1) : %2.").arg(skipped.size()).arg(skipped.join(QStringLiteral(", ")));
    }
    statusBar()->showMessage(message, 12000);
}

void MainWindow::applyToSelection(int stitchType, bool setSpacing, double spacingMm, bool setAngle,
                                  double angleDeg) {
    const std::vector<ObjectId> ids = selectedObjectIds();
    const Micrometers spacing = to_micrometers(Millimeters{spacingMm});
    const Angle angle{angleDeg * std::numbers::pi / 180.0};
    if (stitchType >= 0) {
        // Nouveau type pour toutes les formes (0 contour, 1 tatami, 2 satin, 3 directionnel),
        // avec l'espacement / l'angle cochés appliqués aux paramètres de départ.
        setStitchTypeForSelection(stitchType, [&](document::StitchParams& params) {
            if (auto* t = std::get_if<document::TatamiParams>(&params)) {
                if (setSpacing) {
                    t->row_spacing = spacing;
                }
                if (setAngle) {
                    t->angle = angle;
                }
            } else if (auto* d = std::get_if<document::DirectionalFillParams>(&params)) {
                if (setSpacing) {
                    d->row_spacing = spacing;
                }
            } else if (auto* a = std::get_if<document::AutoSatinParams>(&params)) {
                if (setSpacing) {
                    a->spacing = spacing;
                }
            }
        });
        return;
    }
    auto composite = std::make_unique<commands::CompositeCommand>(
        tr("Appliquer les réglages à %1 objets").arg(ids.size()).toStdString());
    for (const ObjectId vectorId : ids) {
        std::vector<const document::EmbroideryObject*> targets;
        for (const auto& emb : project_.embroidery_objects) {
            if (emb.source_vector == vectorId) {
                targets.push_back(&emb);
            }
        }
        if (targets.empty()) {
            continue;
        }
        for (const auto* emb : targets) {
            document::StitchParams params = emb->params;
            bool changed = false;
            if (auto* t = std::get_if<document::TatamiParams>(&params)) {
                if (setSpacing) {
                    changed = changed || t->row_spacing != spacing;
                    t->row_spacing = spacing;
                }
                if (setAngle) {
                    changed = changed || t->angle.radians != angle.radians;
                    t->angle = angle;
                }
            } else if (auto* d = std::get_if<document::DirectionalFillParams>(&params)) {
                if (setSpacing) {
                    changed = d->row_spacing != spacing;
                    d->row_spacing = spacing;
                }
            } else if (auto* a = std::get_if<document::AutoSatinParams>(&params)) {
                if (setSpacing) {
                    changed = a->spacing != spacing;
                    a->spacing = spacing;
                }
            } else if (auto* s = std::get_if<document::SatinParams>(&params)) {
                if (setSpacing) {
                    changed = s->density != spacing;
                    s->density = spacing;
                }
            }
            if (changed) {
                composite->add(std::make_unique<commands::SetStitchParamsCommand>(
                    emb->id, std::move(params), tr("Réglages multiples").toStdString()));
            }
        }
    }
    if (composite->empty()) {
        statusBar()->showMessage(tr("Rien à modifier : les réglages demandés sont déjà "
                                    "appliqués ou ne concernent pas ces objets."));
        return;
    }
    std::string mergeKey =
        std::string("applysel:") + (setSpacing ? "s" : "-") + (setAngle ? "a" : "-");
    for (const ObjectId id : ids) {
        mergeKey += ":" + std::to_string(id.value);
    }
    composite->setMergeKey(std::move(mergeKey)); // rafale = un seul pas d'annulation
    undoStack_.execute(std::move(composite), project_);
    scheduleParamsRefresh(); // champs en direct : une rafale = une régénération (HP-PERF-009)
    statusBar()->showMessage(
        tr("Réglages appliqués à %1 objets (une seule étape annulable).").arg(ids.size()));
}

// ---------------------------------------------------------------------------------------------
// Alignement (Édition > Aligner) : range les objets sélectionnés sur la boîte de la sélection.
// ---------------------------------------------------------------------------------------------

void MainWindow::buildAlignMenu(QMenu* editMenu) {
    auto* menu = editMenu->addMenu(tr("A&ligner la sélection"));
    menu->setObjectName(QStringLiteral("menu_align"));
    struct Entry {
        const char* name;
        QString text;
        int mode;
    };
    const Entry entries[] = {
        {"action_alignLeft", tr("À &gauche"), 0},
        {"action_alignCenter", tr("Centrés &horizontalement"), 1},
        {"action_alignRight", tr("À &droite"), 2},
        {"action_alignTop", tr("En &haut"), 3},
        {"action_alignMiddle", tr("Centrés &verticalement"), 4},
        {"action_alignBottom", tr("En &bas"), 5},
    };
    for (const auto& e : entries) {
        auto* act = menu->addAction(e.text);
        act->setObjectName(QString::fromLatin1(e.name));
        const int mode = e.mode;
        connect(act, &QAction::triggered, this, [this, mode] { alignSelection(mode); });
        alignActs_.append(act);
    }
}

void MainWindow::updateAlignActions() {
    const bool enabled = hasMultiSelection() && selectedObjectIds().size() >= 2;
    for (QAction* act : alignActs_) {
        act->setEnabled(enabled);
        act->setStatusTip(enabled ? QString()
                                  : tr("Sélectionnez au moins deux formes (Maj+clic)."));
    }
}

void MainWindow::alignSelection(int mode) {
    alignShapes(mode); // référence d'alignement et commande : main_window_arrange.cpp
}

// ---------------------------------------------------------------------------------------------
// Panneau Historique : liste des pas d'annulation, un clic saute à cet état.
// ---------------------------------------------------------------------------------------------

void MainWindow::buildHistoryPanel() {
    historyDock_ = new QDockWidget(tr("Historique"), this);
    historyDock_->setObjectName(QStringLiteral("historyDock"));
    historyDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    historyList_ = new QListWidget(historyDock_);
    historyList_->setObjectName(QStringLiteral("list_history"));
    historyList_->setAccessibleName(tr("Historique des modifications"));
    historyList_->setToolTip(tr("Cliquez sur une ligne pour revenir à cet état (les étapes "
                                "suivantes restent rétablissables)."));
    historyDock_->setWidget(historyList_);
    addDockWidget(Qt::RightDockWidgetArea, historyDock_);
    historyDock_->setAccessibleName(historyDock_->windowTitle());
    historyDock_->hide();
    connect(historyList_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) { jumpToHistory(historyList_->row(item)); });
}

void MainWindow::refreshHistoryPanel() {
    if (historyList_ == nullptr || historyDock_ == nullptr || !historyDock_->isVisible()) {
        return;
    }
    const auto undoNames = undoStack_.undoNames();
    const auto redoNames = undoStack_.redoNames();
    const QSignalBlocker block(historyList_);
    historyList_->clear();
    historyList_->addItem(tr("État initial"));
    for (const auto& name : undoNames) {
        historyList_->addItem(QString::fromStdString(name));
    }
    QFont dim = historyList_->font();
    dim.setItalic(true);
    for (const auto& name : redoNames) {
        auto* item = new QListWidgetItem(QString::fromStdString(name));
        item->setFont(dim);
        item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        item->setToolTip(tr("Étape annulée : cliquez pour la rétablir."));
        historyList_->addItem(item);
    }
    historyList_->setCurrentRow(static_cast<int>(undoNames.size()));
}

void MainWindow::jumpToHistory(int row) {
    if (row < 0) {
        return;
    }
    const int applied = static_cast<int>(undoStack_.undoNames().size());
    if (row == applied) {
        return;
    }
    int steps = 0;
    while (static_cast<int>(undoStack_.undoNames().size()) > row && undoStack_.undo(project_)) {
        ++steps;
    }
    while (static_cast<int>(undoStack_.undoNames().size()) < row && undoStack_.redo(project_)) {
        ++steps;
    }
    if (steps > 0) {
        refreshImage();
        updateActions();
    }
}

std::array<std::uint8_t, 3> MainWindow::defaultLayerColor(const std::array<std::uint8_t, 3>& base) {
    // Complémentaire de la base (teinte opposée) ; les gris, sans teinte, prennent la
    // luminosité opposée. Toujours distinct de la base.
    const QColor src(base[0], base[1], base[2]);
    QColor out = src;
    if (src.hslHue() < 0 || src.hslSaturation() < 20) {
        out = QColor::fromHsl(0, 0, src.lightness() < 128 ? 200 : 55);
    } else {
        out = QColor::fromHsl((src.hslHue() + 180) % 360, src.hslSaturation(), src.lightness());
    }
    std::array<std::uint8_t, 3> rgb{static_cast<std::uint8_t>(out.red()),
                                    static_cast<std::uint8_t>(out.green()),
                                    static_cast<std::uint8_t>(out.blue())};
    if (rgb == base) {
        rgb = {static_cast<std::uint8_t>(255 - base[0]), static_cast<std::uint8_t>(255 - base[1]),
               static_cast<std::uint8_t>(255 - base[2])};
        if (rgb == base) { // gris moyen exact
            rgb = {static_cast<std::uint8_t>(base[0] ^ 0x40), base[1], base[2]};
        }
    }
    return rgb;
}

QString MainWindow::nextLayerName(const document::EmbroideryObject& base) const {
    // Racine = nom de la base sans son éventuel suffixe « – couche N » (ajouter une couche
    // depuis une couche ne doit pas empiler les suffixes) ; N = premier numéro libre parmi les
    // couches de la même forme, donc stable après une suppression.
    const QRegularExpression suffix(QRegularExpression::escape(tr(" – couche ")) +
                                    QStringLiteral("\\d+$"));
    QString root = QString::fromStdString(base.name);
    root.remove(suffix);
    std::set<QString> used;
    for (const auto& e : project_.embroidery_objects) {
        if (e.source_vector == base.source_vector) {
            used.insert(QString::fromStdString(e.name));
        }
    }
    for (int n = 2;; ++n) {
        const QString candidate = tr("%1 – couche %2").arg(root).arg(n);
        if (used.count(candidate) == 0) {
            return candidate;
        }
    }
}

bool MainWindow::addLayerQuick(ObjectId baseId) {
    const auto* base = project_.findEmbroidery(baseId);
    if (base == nullptr) {
        return false;
    }
    // Un geste, aucun dialogue : même famille de points que la couche visée, fil complémentaire.
    return addLayer(baseId, stitchTypeIndex(*base), defaultLayerColor(base->rgb));
}

bool MainWindow::duplicateLayer(ObjectId id) {
    const auto* source = project_.findEmbroidery(id);
    if (source == nullptr) {
        return false;
    }
    document::EmbroideryObject copy = *source;
    copy.id = project_.object_ids.next();
    copy.name = nextLayerName(*source).toStdString();
    const ObjectId copyId = copy.id;
    const ObjectId sourceId = source->source_vector;
    undoStack_.execute(std::make_unique<commands::AddLayerCommand>(id, std::move(copy)), project_);
    setSelection({.embroidery = copyId, .objects = {sourceId}});
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Couche dupliquée."));
    return true;
}

void MainWindow::addCalque() {
    // Premier calque ajouté : le calque implicite du fond est matérialisé d'abord (« Calque 1 »),
    // toutes les formes existantes y restent.
    const ObjectId firstId = project_.calques.empty() ? project_.object_ids.next() : ObjectId{};
    const ObjectId newId = project_.object_ids.next();
    const auto number = static_cast<int>(std::max<std::size_t>(project_.calques.size(), 1)) + 1;
    undoStack_.execute(std::make_unique<commands::AddCalqueCommand>(
                           newId, tr("Calque %1").arg(number).toStdString(), firstId,
                           tr("Calque 1").toStdString()),
                       project_);
    project_.active_calque = newId; // le nouveau calque reçoit les prochaines formes
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Calque ajouté : il reçoit les formes créées."));
}

void MainWindow::removeCalque(ObjectId id) {
    const document::Calque* calque = project_.findCalque(id);
    if (calque == nullptr || project_.calques.size() <= 1) {
        return;
    }
    const QString name = QString::fromStdString(calque->name);
    undoStack_.execute(std::make_unique<commands::RemoveCalqueCommand>(id), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Calque « %1 » supprimé : ses formes sont passées dans le calque en dessous.")
            .arg(name));
}

void MainWindow::renameCalqueInteractive(ObjectId id) {
    const document::Calque* calque = project_.findCalque(id);
    if (calque == nullptr) {
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Renommer le calque"), tr("Nom du calque :"),
                              QLineEdit::Normal, QString::fromStdString(calque->name), &ok)
            .trimmed();
    if (!ok || name.isEmpty() || project_.findCalque(id) == nullptr) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::RenameCalqueCommand>(id, name.toStdString()),
                       project_);
    refreshImage();
    updateActions();
}

void MainWindow::moveSelectionToCalque(ObjectId id) {
    if (project_.findCalque(id) == nullptr) {
        return;
    }
    // Les formes sélectionnées au canevas, ou à défaut la forme de la couche choisie : dans les
    // deux cas toutes leurs couches de couture suivent.
    std::vector<ObjectId> shapes = selectedObjectIds();
    if (shapes.empty() && selectedEmbroidery_) {
        if (const auto* emb = project_.findEmbroidery(*selectedEmbroidery_)) {
            shapes.push_back(emb->source_vector);
        }
    }
    shapes.erase(std::remove_if(shapes.begin(), shapes.end(),
                                [this](ObjectId s) { return project_.findObject(s) == nullptr; }),
                 shapes.end());
    if (shapes.empty()) {
        statusBar()->showMessage(tr("Sélectionnez d'abord une forme à déplacer."));
        return;
    }
    undoStack_.execute(std::make_unique<commands::SetShapesCalqueCommand>(shapes, id), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("%n forme(s) déplacée(s) dans le calque.", nullptr, static_cast<int>(shapes.size())));
}

bool MainWindow::moveLayer(ObjectId id, int direction) {
    auto& all = project_.embroidery_objects;
    const auto it = std::find_if(all.begin(), all.end(), [&](const auto& e) { return e.id == id; });
    if (it == all.end() || direction == 0) {
        return false;
    }
    // Voisin = la couche de la MÊME forme la plus proche dans le sens demandé : on échange leurs
    // rangs de couture, les objets des autres formes ne bougent pas.
    const auto index = static_cast<std::ptrdiff_t>(it - all.begin());
    const auto step = direction > 0 ? 1 : -1;
    auto j = index + step;
    while (j >= 0 && j < static_cast<std::ptrdiff_t>(all.size()) &&
           all[static_cast<std::size_t>(j)].source_vector != it->source_vector) {
        j += step;
    }
    if (j < 0 || j >= static_cast<std::ptrdiff_t>(all.size())) {
        return false; // déjà au bout de la pile
    }
    std::vector<ObjectId> order;
    order.reserve(all.size());
    for (const auto& e : all) {
        order.push_back(e.id);
    }
    std::swap(order[static_cast<std::size_t>(index)], order[static_cast<std::size_t>(j)]);
    undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order), project_);
    refreshImage();
    refreshOrderPanel();
    updateActions();
    statusBar()->showMessage(direction > 0 ? tr("Couche montée.") : tr("Couche descendue."));
    return true;
}

void MainWindow::renameLayerInteractive(ObjectId id) {
    const auto* layer = project_.findEmbroidery(id);
    if (layer == nullptr) {
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Renommer la couche"), tr("Nom de la couche :"),
                              QLineEdit::Normal, QString::fromStdString(layer->name), &ok)
            .trimmed();
    // Les dialogues pompent la boucle d'événements : l'objet est re-résolu avant d'agir.
    if (!ok || name.isEmpty() || project_.findEmbroidery(id) == nullptr) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::RenameEmbroideryCommand>(id, name.toStdString()),
                       project_);
    refreshImage();
    updateActions();
}

bool MainWindow::addLayer(ObjectId baseId, int type, const std::array<std::uint8_t, 3>& rgb) {
    const auto* base = project_.findEmbroidery(baseId);
    if (base == nullptr) {
        return false;
    }
    document::StitchParams params;
    std::string label;
    std::optional<std::vector<geometry::PathSet>> restoredContour; // satin : ignoré pour une couche
    QString error;
    if (!stitchParamsForType(*base, type, params, label, restoredContour, error)) {
        QMessageBox::warning(this, tr("Couche impossible"), error);
        return false;
    }
    ui_memory::creation::apply(params); // dernier réglage de création, comme une forme neuve
    document::EmbroideryObject layer;
    layer.id = project_.object_ids.next();
    layer.name = nextLayerName(*base).toStdString();
    layer.source_vector = base->source_vector;
    layer.rgb = rgb;
    layer.params = std::move(params);
    layer.intent = document::EmbroideryIntent::ForcedUserChoice;
    const ObjectId layerId = layer.id;
    const ObjectId sourceId = base->source_vector;
    const QString baseName = QString::fromStdString(base->name);
    undoStack_.execute(std::make_unique<commands::AddLayerCommand>(baseId, std::move(layer)),
                       project_); // `base` est invalidé par l'insertion
    setSelection({.embroidery = layerId, .objects = {sourceId}});
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Couche ajoutée à « %1 ».").arg(baseName));
    return true;
}

void MainWindow::addLayerInteractive(ObjectId baseId) {
    const auto* base = project_.findEmbroidery(baseId);
    if (base == nullptr) {
        return;
    }
    const QStringList types{tr("Remplissage tatami"), tr("Remplissage directionnel"),
                            tr("Contour cousu (running)"), tr("Colonne satin")};
    constexpr int typeIndex[] = {1, 3, 0, 2}; // même numérotation que setStitchType
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, tr("Ajouter une couche"),
                                                 tr("Type de la couche :"), types, 0, false, &ok);
    if (!ok) {
        return;
    }
    const auto def = defaultLayerColor(base->rgb);
    const QColor color = pickThreadColor(QColor(def[0], def[1], def[2]), tr("Couleur du fil"));
    if (!color.isValid()) {
        return;
    }
    // `base` peut être invalidé par la boucle d'événements des dialogues : on le re-résout.
    if (project_.findEmbroidery(baseId) == nullptr) {
        return;
    }
    addLayer(baseId, typeIndex[std::max(0, static_cast<int>(types.indexOf(chosen)))],
             {static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
              static_cast<std::uint8_t>(color.blue())});
}

} // namespace openstitch::desktop
