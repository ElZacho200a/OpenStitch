// SPDX-License-Identifier: Apache-2.0
#include <QAction>
#include <QStatusBar>

#include <algorithm>
#include <numbers>

#include "busy_indicator.hpp"
#include "color_blend_dialog.hpp"
#include "main_window.hpp"
#include "openstitch/autodigitize/two_color_blend.hpp"
#include "openstitch/commands/color_blend_commands.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/document/color_blend.hpp"
#include "openstitch/project_ops/gradient_fill.hpp"
#include "openstitch/stitch_generation/color_blend.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "thread_panel.hpp"

namespace openstitch::desktop {

namespace {

struct BlendLayers {
    document::EmbroideryObject base;
    document::EmbroideryObject top;
};

// Dégradé (axe, écarts, zone, courbe) que décrit le dialogue : source UNIQUE de l'aperçu,
// de la création et de la réédition.
std::optional<document::DensityGradient>
blendGradientFor(const ColorBlendDialog& dialog, const std::vector<geometry::PathSet>& shape) {
    auto gradient = dialog.proposedGradient();
    if (!gradient) {
        gradient = stitch_generation::density_gradient_across(
            shape, Angle{dialog.angleDegrees() * std::numbers::pi / 180.0}, dialog.startSpacing(),
            dialog.endSpacing());
    }
    if (gradient) {
        gradient = stitch_generation::narrow_gradient_zone(*gradient, dialog.transitionStart(),
                                                           dialog.transitionEnd());
    }
    if (gradient) {
        gradient->curve = dialog.curve();
    }
    return gradient;
}

// Les deux couches du fondu telles que le dialogue les décrit : l'aperçu montre ce
// qui sera créé ou modifié (même construction que le service du cœur).
std::optional<BlendLayers> blendLayersFor(const ColorBlendDialog& dialog,
                                          const document::EmbroideryObject& baseBefore,
                                          const document::EmbroideryObject& secondBefore,
                                          const std::vector<geometry::PathSet>& shape) {
    const auto gradient = blendGradientFor(dialog, shape);
    if (!gradient) {
        return std::nullopt;
    }
    auto layers = stitch_generation::make_color_blend_layers(baseBefore, secondBefore, shape,
                                                             *gradient, dialog.complementary());
    if (!layers) {
        return std::nullopt;
    }
    layers->first.rgb = dialog.backgroundColor();
    layers->second.rgb = dialog.foregroundColor();
    return BlendLayers{std::move(layers->first), std::move(layers->second)};
}

} // namespace

// Un fondu se crée sur une seule couche de remplissage, jamais sur un couple déjà créé
// ni sur un réseau de sections ambigu. Les couches existantes restent éditables séparément.
const document::EmbroideryObject* MainWindow::blendFillFor(ObjectId vector) const {
    const document::EmbroideryObject* fill = nullptr;
    for (const auto& e : project_.embroidery_objects) {
        if (e.source_vector == vector && (e.is_tatami() || e.is_directional())) {
            if (fill != nullptr) {
                return nullptr;
            }
            fill = &e;
        }
    }
    return fill;
}

void MainWindow::applyTwoColorBlend() {
    const auto* emb = resolveSelectedEmbroidery();
    if (emb != nullptr && blendFillFor(emb->source_vector) == emb) {
        createColorBlend(emb->id, std::nullopt);
    }
}

void MainWindow::blendSelectedZones() {
    const auto ids = selectedObjectIds();
    if (ids.size() != 2) {
        return;
    }
    const auto* base = blendFillFor(ids[0]);
    const auto* top = blendFillFor(ids[1]);
    if (base != nullptr && top != nullptr) {
        createColorBlend(base->id, top->id);
    }
}

void MainWindow::createColorBlend(ObjectId baseId, std::optional<ObjectId> secondId) {
    const auto* base = project_.findEmbroidery(baseId);
    const auto* second = secondId ? project_.findEmbroidery(*secondId) : base;
    const auto* source = base != nullptr ? project_.findObject(base->source_vector) : nullptr;
    const auto* sourceB = second != nullptr ? project_.findObject(second->source_vector) : nullptr;
    if (base == nullptr || second == nullptr || source == nullptr || sourceB == nullptr) {
        return;
    }
    // Instantanés : aucun pointeur document n'est conservé au travers d'une boucle modale.
    const auto baseBefore = *base;
    const auto secondBefore = *second;
    const auto paths = source->paths;
    auto allPaths = paths;
    if (secondId) {
        allPaths.insert(allPaths.end(), sourceB->paths.begin(), sourceB->paths.end());
    }
    const std::uint64_t generation = documentGeneration_;
    ColorBlendDialog::Rgb proposedTop =
        secondId ? secondBefore.rgb : ColorBlendDialog::Rgb{40, 90, 210};
    if (!secondId && proposedTop == baseBefore.rgb) {
        proposedTop = {240, 180, 40};
    }
    ColorBlendDialog dialog(baseBefore.rgb, proposedTop, !secondId && !project_.original.empty(),
                            secondId.has_value(), this);
    if (secondId) {
        const auto gradient = stitch_generation::density_gradient_between(
            paths, sourceB->paths, Micrometers{4'000}, Micrometers{400});
        if (gradient) {
            dialog.setProposal(baseBefore.rgb, proposedTop, *gradient);
        }
    }
    std::optional<autodigitize::RegionBlend> analyzed;
    connect(&dialog, &ColorBlendDialog::analysisRequested, this, [&] {
        if (generation != documentGeneration_) {
            dialog.reject();
            return;
        }
        std::optional<thread_palette::ThreadChart> chart;
        if (threadPanel_ != nullptr) {
            if (const auto* selected =
                    threadLibrary_.find_chart(threadPanel_->currentChartId().toStdString())) {
                chart = *selected;
            }
        }
        Result<autodigitize::RegionBlend> result = [&] {
            BusyIndicator busy(&dialog, tr("Analyse des couleurs sous la forme…"),
                               {autosaveTimer_, simTimer_, analysisTimer_});
            const auto original = project_.original;
            const auto ops = project_.ops;
            const auto scale = project_.mm_per_px;
            return busy.run([&]() -> Result<autodigitize::RegionBlend> {
                const auto working = image::apply_pipeline(original, ops);
                if (!working) {
                    return std::unexpected(working.error());
                }
                return autodigitize::analyze_region_blend(*working, scale, paths,
                                                          chart ? &*chart : nullptr);
            });
        }();
        if (!result) {
            dialog.setAnalysisMessage(QString::fromStdString(result.error().message));
            return;
        }
        analyzed = *result;
        auto bg = analyzed->blend.background;
        auto fg = analyzed->blend.foreground;
        if (analyzed->threads) {
            const auto first = threadLibrary_.find(analyzed->threads->first);
            const auto last = threadLibrary_.find(analyzed->threads->second);
            if (first && last) {
                bg = first->rgb;
                fg = last->rgb;
            }
        }
        dialog.setProposal(bg, fg, analyzed->blend.foreground_density);
        dialog.setAnalysisMessage(analyzed->threads
                                      ? tr("Proposition associée aux fils du nuancier actif.")
                                      : tr("Proposition en couleurs libres ; associez les fils "
                                           "dans le panneau Fils après création."));
    });
    // Aperçu des points : séquence EFFECTIVE d'un instantané du projet (jamais le
    // document), calculée de façon synchrone avec un indicateur d'attente.
    connect(&dialog, &ColorBlendDialog::previewRequested, this, [&] {
        if (generation != documentGeneration_) {
            dialog.reject();
            return;
        }
        const auto layers = blendLayersFor(dialog, baseBefore, secondBefore, allPaths);
        if (!layers) {
            dialog.setPreviewMessage(
                tr("Aperçu indisponible : la forme ou les réglages sont invalides."));
            return;
        }
        auto snapshot = project_;
        auto base = layers->base;
        auto top = layers->top;
        top.id = snapshot.object_ids.next();
        top.source_vector = baseBefore.source_vector;
        top.thread.reset();
        top.overrides.clear();
        top.edited_fingerprint = 0;
        top.edited_point_count = 0;
        top.color_blend.reset();
        for (auto& vector : snapshot.vector_objects) {
            if (vector.id == baseBefore.source_vector) {
                vector.paths = allPaths; // deux zones : forme réunie (sans l'union exacte)
            }
        }
        const ObjectId topId = top.id;
        const auto rgbBase = base.rgb;
        const auto rgbTop = top.rgb;
        snapshot.embroidery_objects = {std::move(base), std::move(top)};
        BusyIndicator busy(&dialog, tr("Calcul de l'aperçu des points…"),
                           {autosaveTimer_, simTimer_, analysisTimer_}, true);
        const auto sequence = busy.run([&](const TaskContext& task) {
            return stitch_generation::effective_sequence(snapshot, task);
        });
        if (!sequence && sequence.error().category == ErrorCategory::Cancelled) {
            return; // annulé par l'utilisateur : l'aperçu précédent reste affiché
        }
        if (!sequence) {
            dialog.setPreviewMessage(QString::fromStdString(sequence.error().message));
            return;
        }
        dialog.setStitchPreview(*sequence, baseId, topId, rgbBase, rgbTop);
    });
    if (dialog.exec() != QDialog::Accepted || generation != documentGeneration_) {
        return;
    }
    const auto layers = blendLayersFor(dialog, baseBefore, secondBefore, allPaths);
    if (!layers) {
        statusBar()->showMessage(
            tr("Fondu impossible : la forme n'a pas d'étendue ou les réglages sont invalides."),
            8000);
        return;
    }
    std::optional<thread_palette::ThreadKey> baseThread;
    std::optional<thread_palette::ThreadKey> topThread;
    const auto baseRgb = dialog.backgroundColor();
    const auto topRgb = dialog.foregroundColor();
    if (baseRgb == baseBefore.rgb) {
        baseThread = baseBefore.thread;
    }
    if (secondId && topRgb == secondBefore.rgb) {
        topThread = secondBefore.thread;
    }
    if (analyzed && analyzed->threads) {
        const auto first = threadLibrary_.find(analyzed->threads->first);
        const auto last = threadLibrary_.find(analyzed->threads->second);
        if (first && first->rgb == baseRgb) {
            baseThread = first->key;
        }
        if (last && last->rgb == topRgb) {
            topThread = last->key;
        }
    }
    // Construction unique des couches : service du coeur (même verdict que l'aperçu).
    project_ops::GradientFillSpec spec;
    spec.background = baseRgb;
    spec.foreground = topRgb;
    spec.background_thread = baseThread;
    spec.foreground_thread = topThread;
    spec.complementary = dialog.complementary();
    const auto gradient = blendGradientFor(dialog, allPaths);
    if (!gradient) {
        statusBar()->showMessage(
            tr("Fondu impossible : la forme n'a pas d'étendue ou les réglages sont invalides."),
            8000);
        return;
    }
    spec.gradient = *gradient;
    auto created = project_ops::make_create_gradient_fill_command(
        project_, baseId, spec, allPaths, secondId ? &secondBefore : nullptr,
        secondId ? *secondId : ObjectId{});
    if (!created) {
        statusBar()->showMessage(QString::fromStdString(created.error().message), 8000);
        return;
    }
    auto batch = std::make_unique<commands::CompositeCommand>("Fondu de couleurs à deux fils");
    if (secondId) {
        auto merged =
            commands::make_boolean_command(project_, commands::BooleanOp::Union,
                                           {secondBefore.source_vector, baseBefore.source_vector});
        if (!merged.command) {
            statusBar()->showMessage(QString::fromStdString(merged.error), 8000);
            return;
        }
        batch->add(std::move(merged.command));
    }
    batch->add(std::move(*created));
    undoStack_.execute(std::move(batch), project_);
    const auto createdLayers = document::find_color_blend(project_, baseId);
    const ObjectId topId = createdLayers ? createdLayers->foreground : baseId;
    setSelection({.embroidery = topId, .objects = {baseBefore.source_vector}});
    showVectorsAct_->setChecked(true);
    showStitchesAct_->setChecked(true);
    refreshImage();
    syncDocumentSelection();
    updateActions();
    statusBar()->showMessage(tr("Fondu créé : fond puis dessus. Choisissez la couche dans "
                                "Propriétés pour régler sa couleur et sa couture. Ctrl+Z annule "
                                "l'ensemble."),
                             8000);
}

// Réédition globale du couple : même dialogue, mêmes ids, même rang de couture, aucune
// couche créée. Le dialogue est la vue avancée du remplissage « Dégradé ».
void MainWindow::reeditColorBlend(ObjectId layer) {
    const auto spec = project_ops::describe_gradient_fill(project_, layer);
    const auto layers = project_ops::gradient_layers(project_, layer);
    const auto* base = layers ? project_.findEmbroidery(layers->background) : nullptr;
    const auto* top = layers ? project_.findEmbroidery(layers->foreground) : nullptr;
    const auto* source = base != nullptr ? project_.findObject(base->source_vector) : nullptr;
    if (!spec || base == nullptr || top == nullptr || source == nullptr) {
        return;
    }
    const auto baseBefore = *base;
    const auto topBefore = *top;
    const auto shape = source->paths;
    const std::uint64_t generation = documentGeneration_;
    // Le dialogue travaille sur la rampe PLEINE de l'axe courant ; la zone en fractions.
    const auto full = project_ops::gradient_with_zone(shape, spec->gradient, {0.0, 1.0});
    const auto zone = project_ops::gradient_zone(shape, spec->gradient);
    ColorBlendDialog dialog(spec->background, spec->foreground, false, false, this);
    dialog.setEditing(true);
    dialog.setComplementary(spec->complementary);
    dialog.setProposal(spec->background, spec->foreground, full ? *full : spec->gradient);
    if (full && zone) {
        dialog.setTransition(zone->start, zone->end);
    }
    connect(&dialog, &ColorBlendDialog::previewRequested, this, [&] {
        if (generation != documentGeneration_) {
            dialog.reject();
            return;
        }
        const auto built = blendLayersFor(dialog, baseBefore, topBefore, shape);
        if (!built) {
            dialog.setPreviewMessage(
                tr("Aperçu indisponible : la forme ou les réglages sont invalides."));
            return;
        }
        auto snapshot = project_;
        snapshot.embroidery_objects = {built->base, built->top};
        BusyIndicator busy(&dialog, tr("Calcul de l'aperçu des points…"),
                           {autosaveTimer_, simTimer_, analysisTimer_});
        const auto sequence =
            busy.run([&] { return stitch_generation::effective_sequence(snapshot); });
        if (!sequence) {
            dialog.setPreviewMessage(QString::fromStdString(sequence.error().message));
            return;
        }
        dialog.setStitchPreview(*sequence, baseBefore.id, topBefore.id, built->base.rgb,
                                built->top.rgb);
    });
    if (dialog.exec() != QDialog::Accepted || generation != documentGeneration_) {
        return;
    }
    const auto gradient = blendGradientFor(dialog, shape);
    if (!gradient) {
        statusBar()->showMessage(tr("Réglages invalides : le dégradé n'a pas été modifié."), 8000);
        return;
    }
    auto next = *spec;
    next.gradient = *gradient;
    next.complementary = dialog.complementary();
    if (dialog.backgroundColor() != next.background) {
        next.background = dialog.backgroundColor();
        next.background_thread.reset();
    }
    if (dialog.foregroundColor() != next.foreground) {
        next.foreground = dialog.foregroundColor();
        next.foreground_thread.reset();
    }
    applyGradientSpec(layer, next);
}

} // namespace openstitch::desktop
