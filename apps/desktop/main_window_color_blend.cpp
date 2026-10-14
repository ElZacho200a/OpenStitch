// SPDX-License-Identifier: Apache-2.0
#include <QAction>
#include <QStatusBar>

#include <algorithm>
#include <numbers>

#include "busy_indicator.hpp"
#include "color_blend_dialog.hpp"
#include "main_window.hpp"
#include "openstitch/autodigitize/two_color_blend.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"
#include "thread_panel.hpp"

namespace openstitch::desktop {

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
    if (dialog.exec() != QDialog::Accepted || generation != documentGeneration_) {
        return;
    }
    auto gradient = dialog.proposedGradient();
    if (!gradient) {
        gradient = stitch_generation::density_gradient_across(
            allPaths, Angle{dialog.angleDegrees() * std::numbers::pi / 180.0},
            dialog.startSpacing(), dialog.endSpacing());
    }
    if (!gradient) {
        statusBar()->showMessage(tr("Fondu impossible : la forme n'a pas d'étendue."), 8000);
        return;
    }
    document::DirectionalFillParams topParams;
    if (const auto* dir = std::get_if<document::DirectionalFillParams>(&secondBefore.params)) {
        topParams = *dir;
    } else if (auto converted = directionalParamsFor(secondBefore)) {
        topParams = std::move(*converted);
    } else {
        return;
    }
    topParams.row_spacing = gradient->spacing_from;
    topParams.density_gradient = gradient;
    topParams.underlay_mode = document::UnderlayMode::Manual;
    topParams.underlay_edge = false;
    topParams.underlay_parallel = false;
    stitch_generation::align_guides_across_density_gradient(topParams, allPaths);
    document::EmbroideryObject top = secondBefore;
    top.id = project_.object_ids.next();
    top.source_vector = baseBefore.source_vector;
    top.name = secondBefore.name + " (dessus du fondu)";
    top.params = topParams;
    top.rgb = dialog.foregroundColor();
    top.thread.reset();
    top.overrides.clear();
    top.edited_fingerprint = 0;
    top.edited_point_count = 0;
    std::optional<thread_palette::ThreadKey> baseThread;
    const auto baseRgb = dialog.backgroundColor();
    if (baseRgb == baseBefore.rgb) {
        baseThread = baseBefore.thread;
    }
    if (secondId && top.rgb == secondBefore.rgb) {
        top.thread = secondBefore.thread;
    }
    if (analyzed && analyzed->threads) {
        const auto first = threadLibrary_.find(analyzed->threads->first);
        const auto last = threadLibrary_.find(analyzed->threads->second);
        if (first && first->rgb == baseRgb) {
            baseThread = first->key;
        }
        if (last && last->rgb == top.rgb) {
            top.thread = last->key;
        }
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
    if (const auto* dir = std::get_if<document::DirectionalFillParams>(&baseBefore.params);
        dir != nullptr && dir->density_gradient) {
        auto uniform = *dir;
        uniform.density_gradient.reset();
        batch->add(std::make_unique<commands::SetStitchParamsCommand>(baseId, uniform, "Fond uni"));
    }
    batch->add(std::make_unique<commands::SetObjectThreadCommand>(std::vector<ObjectId>{baseId},
                                                                  baseThread, baseRgb));
    const ObjectId topId = top.id;
    batch->add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(top)));
    std::vector<ObjectId> order;
    order.reserve(project_.embroidery_objects.size() + 1);
    for (const auto& e : project_.embroidery_objects) {
        if (secondId && e.source_vector == secondBefore.source_vector) {
            continue; // supprimé par l'union
        }
        order.push_back(e.id);
        if (e.id == baseId) {
            order.push_back(topId); // pas en fin de motif, après d'éventuels objets recouvrants
        }
    }
    batch->add(std::make_unique<commands::ReorderEmbroideryCommand>(std::move(order)));
    undoStack_.execute(std::move(batch), project_);
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
} // namespace openstitch::desktop
