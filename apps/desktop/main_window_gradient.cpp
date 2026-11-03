// SPDX-License-Identifier: Apache-2.0
// Remplissage « Uni / Dégradé » : câblage du bureau. Le dégradé est UN remplissage
// logique ; ses deux couches de couture (fond + dessus, reliées par ColorBlendLink)
// sont reconstruites par le cœur (project_ops::gradient_fill) à chaque modification,
// donc aucun calcul de dégradé n'a lieu ici : seulement des commandes annulables.
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QMenu>
#include <QPen>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "app_theme.hpp"
#include "color_blend_dialog.hpp"
#include "gradient_fill_panel.hpp"
#include "main_window.hpp"
#include "node_handle.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/project_ops/gradient_fill.hpp"
#include "properties_panel.hpp"

namespace openstitch::desktop {

namespace {

QPointF toScene(Vec2um p) {
    return QPointF(to_millimeters(p.x).value, -to_millimeters(p.y).value);
}

Vec2um toModel(QPointF s) {
    return Vec2um{to_micrometers(Millimeters{s.x()}), to_micrometers(Millimeters{-s.y()})};
}

QColor toColor(const std::array<std::uint8_t, 3>& rgb) {
    return QColor(rgb[0], rgb[1], rgb[2]);
}

std::array<std::uint8_t, 3> toRgb(const QColor& color) {
    return {static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
            static_cast<std::uint8_t>(color.blue())};
}

// Choisir un VRAI fil avec identifiant de nuancier (plutôt qu'un simple RGB).
// Les couleurs génériques restent disponibles mais sont explicitement nommées.
std::optional<thread_palette::Thread>
pickGradientThread(QWidget* parent, const thread_palette::ThreadLibrary& library,
                   std::array<std::uint8_t, 3> wanted) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("dialog_gradientThreadPicker"));
    dialog.setWindowTitle(QObject::tr("Choisir une référence de fil"));
    dialog.resize(450, 480);
    auto* layout = new QVBoxLayout(&dialog);
    auto* note = new QLabel(QObject::tr(
        "Choisissez une référence du nuancier. Les couleurs RGB ne garantissent "
        "pas un résultat identique sur tissu."), &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* chart = new QComboBox(&dialog);
    chart->setObjectName(QStringLiteral("combo_gradientThreadChart"));
    chart->addItem(QObject::tr("Tous les nuanciers"), QString());
    for (const auto& item : library.charts()) {
        chart->addItem(QString::fromStdString(item.display_name),
                       QString::fromStdString(item.chart_id));
    }
    layout->addWidget(chart);
    auto* search = new QLineEdit(&dialog);
    search->setObjectName(QStringLiteral("edit_gradientThreadSearch"));
    search->setPlaceholderText(QObject::tr("Chercher par marque, gamme, nom ou code…"));
    layout->addWidget(search);
    auto* all = new QCheckBox(QObject::tr("Parcourir tous les fils (au lieu des plus proches)"),
                              &dialog);
    all->setObjectName(QStringLiteral("check_gradientThreadAll"));
    layout->addWidget(all);
    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("list_gradientThreads"));
    list->setAccessibleName(QObject::tr("Références de fils disponibles"));
    layout->addWidget(list, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    auto* ok = buttons->button(QDialogButtonBox::Ok);
    ok->setText(QObject::tr("Choisir ce fil"));
    ok->setEnabled(false);

    std::vector<thread_palette::Thread> visible;
    const auto refresh = [&] {
        visible.clear();
        const auto chartId = chart->currentData().toString().toStdString();
        const auto query = search->text().trimmed().toStdString();
        if (query.empty() && !all->isChecked()) {
            for (const auto& match : library.nearest(wanted, chartId, 30)) {
                if (const auto thread = library.find(match.key)) {
                    visible.push_back(*thread);
                }
            }
        } else {
            visible = library.search(query, chartId, 500);
        }
        list->clear();
        for (const auto& t : visible) {
            QPixmap swatch(20, 20);
            swatch.fill(QColor(t.rgb[0], t.rgb[1], t.rgb[2]));
            const QString label = QString::fromStdString(t.brand) + " · " +
                                  QString::fromStdString(t.key.code) + " · " +
                                  QString::fromStdString(t.name);
            list->addItem(new QListWidgetItem(QIcon(swatch), label));
        }
        ok->setEnabled(false);
    };
    QObject::connect(chart, &QComboBox::currentIndexChanged, &dialog,
                     [&](int) { refresh(); });
    QObject::connect(search, &QLineEdit::textChanged, &dialog,
                     [&](const QString&) { refresh(); });
    QObject::connect(all, &QCheckBox::toggled, &dialog, [&](bool) { refresh(); });
    QObject::connect(list, &QListWidget::currentRowChanged, &dialog,
                     [&](int row) { ok->setEnabled(row >= 0); });
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog,
                     [&](QListWidgetItem*) { dialog.accept(); });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refresh();
    if (dialog.exec() != QDialog::Accepted || list->currentRow() < 0 ||
        static_cast<std::size_t>(list->currentRow()) >= visible.size()) {
        return std::nullopt;
    }
    return visible[static_cast<std::size_t>(list->currentRow())];
}

QString gradientThreadLabel(const thread_palette::ThreadLibrary& library,
                            const std::optional<thread_palette::ThreadKey>& key) {
    if (!key) return {};
    if (const auto thread = library.find(*key)) {
        return QString::fromStdString(thread->brand) + " · " +
               QString::fromStdString(thread->key.code) + " · " +
               QString::fromStdString(thread->name);
    }
    return QString::fromStdString(key->chart_id) + " · " +
           QString::fromStdString(key->code) + QObject::tr(" (nuancier absent)");
}

} // namespace

void MainWindow::buildGradientActions(QMenu* embMenu) {
    gradientEditModeAct_ = embMenu->addAction(tr("Poig&nées du dégradé"));
    gradientEditModeAct_->setObjectName(QStringLiteral("action_gradientEditMode"));
    gradientEditModeAct_->setCheckable(true);
    gradientEditModeAct_->setToolTip(
        tr("Dégradé sélectionné : affiche l'axe et ses poignées (début, fin, orientation, "
           "déplacement). Un glissement = une annulation."));
    connect(gradientEditModeAct_, &QAction::toggled, this, [this](bool on) {
        if (on) {
            const auto* emb = resolveSelectedEmbroidery();
            if (emb == nullptr || !project_ops::gradient_layers(project_, emb->id)) {
                QSignalBlocker block(gradientEditModeAct_);
                gradientEditModeAct_->setChecked(false);
                return;
            }
            // Un seul mode d'édition à la fois.
            for (QAction* other : {directionGuideModeAct_, stitchEditModeAct_, satinEditModeAct_,
                                   satinGuideModeAct_, railEditModeAct_}) {
                if (other != nullptr && other->isChecked()) {
                    other->setChecked(false);
                }
            }
            gradientEditTarget_ = emb->id;
        } else {
            gradientEditTarget_.reset();
        }
        displayImage(processed_);
        updateInspector();
    });
}

void MainWindow::connectGradientPanel() {
    auto* panel = propertiesPanel_->gradientPanel();
    connect(panel, &GradientFillPanel::modeRequested, this, &MainWindow::setFillMode);
    connect(panel, &GradientFillPanel::edited, this, &MainWindow::applyGradientState);
    connect(panel, &GradientFillPanel::handlesToggled, this,
            [this](bool shown) { gradientEditModeAct_->setChecked(shown); });
    connect(panel, &GradientFillPanel::densityRequested, this, [this] {
        if (densityMapAct_ != nullptr && !densityMapAct_->isChecked()) {
            densityMapAct_->trigger();
        }
        if (analysisDock_ != nullptr) {
            analysisDock_->show();
            analysisDock_->raise();
        }
    });
    connect(panel, &GradientFillPanel::threadRequested, this, [this](bool start) {
        const auto* emb = resolveSelectedEmbroidery();
        if (emb == nullptr) return;
        const ObjectId id = emb->id;
        const auto current = project_ops::describe_gradient_fill(project_, id);
        if (!current) return;
        const auto wanted = start ? current->background : current->foreground;
        const auto selected = pickGradientThread(this, threadLibrary_, wanted);
        if (!selected) return;
        // La commande conserve explicitement la référence catalogue et le RGB.
        auto changed = *current;
        if (start) {
            changed.background = selected->rgb;
            changed.background_thread = selected->key;
        } else {
            changed.foreground = selected->rgb;
            changed.foreground_thread = selected->key;
        }
        applyGradientSpec(id, changed);
    });
    connect(panel, &GradientFillPanel::assistantRequested, this, [this] {
        // L'assistant garde ses propositions image/aperçu : il réutilise le même chemin
        // (couches reconstruites par le cœur) et ne crée jamais de couche de plus.
        if (const auto* emb = resolveSelectedEmbroidery()) {
            if (project_ops::gradient_layers(project_, emb->id)) {
                editGradientWithAssistant(emb->id);
            } else if (blendFillFor(emb->source_vector) == emb) {
                createColorBlend(emb->id, std::nullopt);
            }
        }
    });
}

// Section « Remplissage » : relue du document à chaque rafraîchissement de l'inspecteur.
void MainWindow::updateGradientPanel() {
    if (propertiesPanel_ == nullptr || propertiesPanel_->gradientPanel() == nullptr) {
        return;
    }
    GradientFillPanel::State state;
    const auto* emb = resolveSelectedEmbroidery();
    if (emb != nullptr && (emb->is_tatami() || emb->is_directional())) {
        state.visible = true;
        if (const auto spec = project_ops::describe_gradient_fill(project_, emb->id)) {
            state.gradient = true;
            state.available = true;
            state.start = toColor(spec->background);
            state.end = toColor(spec->foreground);
            state.startThread = gradientThreadLabel(threadLibrary_, spec->background_thread);
            state.endThread = gradientThreadLabel(threadLibrary_, spec->foreground_thread);
            state.curve = static_cast<int>(spec->gradient.curve);
            state.spacingStartMm = to_millimeters(spec->gradient.spacing_from).value;
            state.spacingEndMm = to_millimeters(spec->gradient.spacing_to).value;
            state.complementary = spec->complementary;
            state.handles = gradientEditModeAct_ != nullptr && gradientEditModeAct_->isChecked();
            if (const auto* source = project_.findObject(emb->source_vector)) {
                if (const auto zone = project_ops::gradient_zone(source->paths, spec->gradient)) {
                    state.zoneStartPercent = std::clamp(zone->start * 100.0, 0.0, 100.0);
                    state.zoneEndPercent = std::clamp(zone->end * 100.0, 0.0, 100.0);
                }
            }
        } else {
            const std::string reason = project_ops::gradient_incompatibility(project_, emb->id);
            state.available = reason.empty();
            state.reason = QString::fromStdString(reason);
        }
    }
    propertiesPanel_->gradientPanel()->setState(state);
}

void MainWindow::updateGradientActions() {
    if (gradientEditModeAct_ == nullptr) {
        return;
    }
    const auto* emb = resolveSelectedEmbroidery();
    const bool context = emb != nullptr && project_ops::gradient_layers(project_, emb->id);
    const bool sameTarget = gradientEditTarget_.has_value() && context &&
                            project_ops::gradient_layers(project_, *gradientEditTarget_) ==
                                project_ops::gradient_layers(project_, emb->id);
    if (gradientEditModeAct_->isChecked() && (!context || !sameTarget)) {
        QSignalBlocker block(gradientEditModeAct_);
        gradientEditModeAct_->setChecked(false);
        gradientEditTarget_.reset();
        displayImage(processed_);
    }
    gradientEditModeAct_->setEnabled(context);
}

// Supprimer une couche d'un dégradé supprime le dégradé entier (une annulation) : jamais
// de couche orpheline qui garderait seule une rampe de densité.
std::unique_ptr<commands::ICommand> MainWindow::removeEmbroideryCommand(ObjectId id) const {
    if (const auto layers = project_ops::gradient_layers(project_, id)) {
        auto both = std::make_unique<commands::CompositeCommand>("Supprimer le dégradé");
        both->add(std::make_unique<commands::RemoveEmbroideryObjectCommand>(layers->foreground));
        both->add(std::make_unique<commands::RemoveEmbroideryObjectCommand>(layers->background));
        return both;
    }
    return std::make_unique<commands::RemoveEmbroideryObjectCommand>(id);
}

void MainWindow::setFillMode(bool gradient) {
    const auto* emb = resolveSelectedEmbroidery();
    if (emb == nullptr) {
        return;
    }
    const ObjectId layer = emb->id;
    const bool isGradient = project_ops::gradient_layers(project_, layer).has_value();
    if (gradient == isGradient) {
        return;
    }
    if (gradient) {
        const std::string reason = project_ops::gradient_incompatibility(project_, layer);
        const auto spec = project_ops::default_gradient_fill(project_, layer);
        const auto* source = project_.findObject(emb->source_vector);
        if (!reason.empty() || !spec || source == nullptr) {
            statusBar()->showMessage(QString::fromStdString(reason), 8000);
            updateGradientPanel();
            return;
        }
        const ObjectId sourceId = emb->source_vector;
        auto command =
            project_ops::make_create_gradient_fill_command(project_, layer, *spec, source->paths);
        if (!command) {
            statusBar()->showMessage(QString::fromStdString(command.error().message), 8000);
            updateGradientPanel();
            return;
        }
        undoStack_.execute(std::move(*command), project_);
        setSelection({.embroidery = layer, .objects = {sourceId}});
        showVectorsAct_->setChecked(true);
        showStitchesAct_->setChecked(true);
        statusBar()->showMessage(
            tr("Dégradé créé. Réglez les couleurs et la transition dans Propriétés, ou "
               "activez les poignées. Ctrl+Z revient au remplissage uni."),
            8000);
    } else {
        const auto layers = project_ops::gradient_layers(project_, layer);
        const ObjectId sourceId = emb->source_vector;
        auto command = project_ops::make_remove_gradient_fill_command(project_, layer);
        if (!command) {
            statusBar()->showMessage(QString::fromStdString(command.error().message), 8000);
            return;
        }
        undoStack_.execute(std::move(*command), project_);
        setSelection({.embroidery = layers->background, .objects = {sourceId}});
        statusBar()->showMessage(tr("Remplissage uni (couleur de départ conservée)."), 8000);
    }
    refreshImage();
    syncDocumentSelection();
    updateActions();
}

void MainWindow::applyGradientSpec(ObjectId layer, const project_ops::GradientFillSpec& spec) {
    if (project_ops::describe_gradient_fill(project_, layer) == spec) {
        return; // aucun changement : aucun pas d'historique
    }
    auto command = project_ops::make_edit_gradient_fill_command(project_, layer, spec);
    if (!command) {
        statusBar()->showMessage(QString::fromStdString(command.error().message), 8000);
        updateGradientPanel(); // remet l'inspecteur dans l'état réel du document
        return;
    }
    undoStack_.execute(std::move(*command), project_);
    refreshImage();
    updateActions();
}

void MainWindow::applyGradientState(const GradientFillPanel::State& state) {
    const auto* emb = resolveSelectedEmbroidery();
    const auto current =
        emb != nullptr ? project_ops::describe_gradient_fill(project_, emb->id) : std::nullopt;
    const auto* source = emb != nullptr ? project_.findObject(emb->source_vector) : nullptr;
    if (!current || source == nullptr) {
        return;
    }
    auto spec = *current;
    const auto adopt = [](const QColor& wanted, std::array<std::uint8_t, 3>& rgb,
                          std::optional<thread_palette::ThreadKey>& thread) {
        if (toRgb(wanted) != rgb) {
            rgb = toRgb(wanted);
            thread.reset(); // une couleur choisie à la main est libre
        }
    };
    const bool inverted = toRgb(state.start) == current->foreground &&
                          toRgb(state.end) == current->background &&
                          current->foreground != current->background;
    if (inverted) {
        std::swap(spec.background, spec.foreground);
        std::swap(spec.background_thread, spec.foreground_thread);
    } else {
        adopt(state.start, spec.background, spec.background_thread);
        adopt(state.end, spec.foreground, spec.foreground_thread);
    }
    spec.gradient.curve = static_cast<document::DensityCurve>(state.curve);
    spec.complementary = state.complementary;
    const auto mm = [](double value) { return to_micrometers(Millimeters{value}); };
    spec.gradient.spacing_from = mm(state.spacingStartMm);
    spec.gradient.spacing_to = mm(state.spacingEndMm);
    if (spec.gradient.spacing_from == current->gradient.spacing_from) {
        spec.gradient.spacing_from = current->gradient.spacing_from;
    }
    const auto zone = project_ops::gradient_zone(source->paths, current->gradient);
    const double wantedStart = state.zoneStartPercent / 100.0;
    const double wantedEnd = state.zoneEndPercent / 100.0;
    if (zone &&
        (std::abs(zone->start - wantedStart) > 5e-4 || std::abs(zone->end - wantedEnd) > 5e-4)) {
        // Un champ inchangé n'est jamais recalculé : pas de dérive d'arrondi.
        const auto moved = project_ops::gradient_with_zone(source->paths, current->gradient,
                                                           {wantedStart, wantedEnd});
        if (!moved) {
            statusBar()->showMessage(
                tr("Transition invalide : le début doit précéder la fin (0 à 100 %)."), 8000);
            updateGradientPanel();
            return;
        }
        spec.gradient = *moved;
        spec.gradient.curve = static_cast<document::DensityCurve>(state.curve);
        spec.gradient.spacing_from = mm(state.spacingStartMm);
        spec.gradient.spacing_to = mm(state.spacingEndMm);
    }
    applyGradientSpec(emb->id, spec);
}

void MainWindow::editGradientWithAssistant(ObjectId layer) {
    const auto spec = project_ops::describe_gradient_fill(project_, layer);
    if (!spec) {
        return;
    }
    // Le dialogue d'origine reste l'assistant avancé (aperçu des points, proposition
    // depuis l'image) ; en réédition il modifie les MÊMES couches.
    reeditColorBlend(layer);
}

// Poignées de l'axe : un glissement = UNE commande (au relâchement). Pendant le glisser,
// seule la géométrie de l'axe est redessinée (aperçu réactif), les points sont recalculés
// par le chemin habituel après validation.
void MainWindow::renderGradientHandles() {
    if (gradientEditModeAct_ == nullptr || !gradientEditModeAct_->isChecked() ||
        !gradientEditTarget_) {
        return;
    }
    const auto spec = project_ops::describe_gradient_fill(project_, *gradientEditTarget_);
    const auto layers = project_ops::gradient_layers(project_, *gradientEditTarget_);
    const auto* base = layers ? project_.findEmbroidery(layers->background) : nullptr;
    const auto* source = base != nullptr ? project_.findObject(base->source_vector) : nullptr;
    if (!spec || source == nullptr) {
        return;
    }
    const auto& tokens = AppTheme::instance().tokens();
    const auto shape = source->paths;
    const auto gradient = spec->gradient;
    const ObjectId target = *gradientEditTarget_;
    const std::uint64_t generation = documentGeneration_;

    QPen axisPen(tokens.accent);
    axisPen.setCosmetic(true);
    axisPen.setWidthF(2.0);
    axisPen.setStyle(Qt::DashLine);
    const QPointF from = toScene(gradient.from);
    const QPointF to = toScene(gradient.to);
    auto* axis = scene_->addLine(QLineF(from, to), axisPen);
    axis->setZValue(98);
    baseItems_.append(axis);

    // Limites de la transition : traits perpendiculaires à l'axe, sur toute la forme.
    QPen limitPen(tokens.accent);
    limitPen.setCosmetic(true);
    limitPen.setWidthF(1.0);
    QLineF dir(from, to);
    QLineF normal = dir.normalVector();
    double reach = 0.0;
    for (const auto& set : shape) {
        for (const auto& node : set.outer.nodes) {
            reach = std::max(
                reach, std::hypot(to_millimeters(node.pos.x).value - (from.x() + to.x()) / 2,
                                  -to_millimeters(node.pos.y).value - (from.y() + to.y()) / 2));
        }
    }
    const auto limit = [&](QPointF at) {
        QLineF line = normal;
        line.setLength(reach + 2.0);
        line.translate(at - line.p1());
        QLineF mirrored(at, at * 2 - line.p2());
        auto* a = scene_->addLine(QLineF(line.p2(), mirrored.p2()), limitPen);
        a->setZValue(97);
        baseItems_.append(a);
        return a;
    };
    limit(from);
    limit(to);

    const auto commit = [this, target, generation, shape,
                         gradient](project_ops::GradientHandle kind, QPointF released,
                                   Qt::KeyboardModifiers modifiers) {
        QTimer::singleShot(
            0, this, [this, target, generation, shape, gradient, kind, released, modifiers] {
                if (generation != documentGeneration_) {
                    return;
                }
                const auto current = project_ops::describe_gradient_fill(project_, target);
                if (!current || current->gradient != gradient) {
                    return;
                }
                const auto moved = project_ops::drag_gradient_handle(
                    shape, gradient, kind, toModel(released), (modifiers & Qt::ShiftModifier) != 0);
                if (!moved || *moved == gradient) {
                    refreshImage(); // glissement sans effet : la poignée revient en place
                    return;
                }
                auto next = *current;
                next.gradient = *moved;
                applyGradientSpec(target, next);
            });
    };
    const auto preview = [axis, shape, gradient](project_ops::GradientHandle kind, QPointF at) {
        if (const auto moved =
                project_ops::drag_gradient_handle(shape, gradient, kind, toModel(at))) {
            axis->setLine(QLineF(toScene(moved->from), toScene(moved->to)));
        }
    };
    const auto addHandle = [&](QPointF at, project_ops::GradientHandle kind, const QString& tip,
                               const QColor& fill) {
        auto* handle = new NodeHandleItem(
            at, [](QPointF) {}, [preview, kind](QPointF now) { preview(kind, now); });
        handle->setReleasedWithModifiers([commit, kind](QPointF released, Qt::KeyboardModifiers m) {
            commit(kind, released, m);
        });
        handle->setCancelled([this] { refreshImage(); });
        handle->setPen(QPen(tokens.accent, 1.5));
        handle->setBrush(QBrush(fill));
        handle->setToolTip(tip);
        scene_->addItem(handle);
        baseItems_.append(handle);
    };
    const QPointF center = (from + to) / 2.0;
    QLineF perp(center, to);
    perp.setAngle(perp.angle() + 90.0);
    perp.setLength(std::max(6.0, QLineF(from, to).length() * 0.3));
    addHandle(from, project_ops::GradientHandle::Start,
              tr("Début de la transition (Maj : symétrique)"), toColor(spec->background));
    addHandle(to, project_ops::GradientHandle::End, tr("Fin de la transition (Maj : symétrique)"),
              toColor(spec->foreground));
    addHandle(center, project_ops::GradientHandle::Move, tr("Déplacer l'axe du dégradé"),
              tokens.canvasSelectionHalo);
    addHandle(perp.p2(), project_ops::GradientHandle::Rotate, tr("Tourner l'axe du dégradé"),
              tokens.canvasSelectionHalo);
    auto* stem = scene_->addLine(QLineF(center, perp.p2()), limitPen);
    stem->setZValue(97);
    baseItems_.append(stem);
}

} // namespace openstitch::desktop
