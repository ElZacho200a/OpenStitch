// SPDX-License-Identifier: Apache-2.0
// Flux import -> segmentation -> vectorisation : vectorisation de la sélection de régions,
// segmentation par IA, quantification avec aperçu, panneau Workflow et actions grisées avec
// raison. Membres de MainWindow séparés de main_window.cpp pour la lisibilité. Aucune logique
// métier ici : les calculs viennent de libs/vectorization, libs/autodigitize et
// libs/segmentation, et toute mutation du document passe par une commande annulable.
#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>

#include "ai_preferences.hpp"
#include "ai_segmentation_dialog.hpp"
#include "main_window.hpp"
#include "ui_memory.hpp"

#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/segmentation/segmentation.hpp"
#include "openstitch/vectorization/vectorize.hpp"

namespace openstitch::desktop {

// Défini dans main_window.cpp (curseur « détail » -> tolérance de simplification).
Micrometers vectorize_tolerance_from_detail(int detail);

namespace {

// Calcul synchrone long : curseur d'attente et fenêtre « en cours » indéterminée (sans bouton
// Annuler : ces calculs ne sont pas interruptibles).
class WorkIndicator {
public:
    WorkIndicator(QWidget* parent, const QString& text) : dialog_(text, QString(), 0, 0, parent) {
        dialog_.setWindowTitle(QObject::tr("Veuillez patienter"));
        dialog_.setWindowModality(Qt::WindowModal);
        dialog_.setCancelButton(nullptr);
        dialog_.setMinimumDuration(0);
        dialog_.show();
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    ~WorkIndicator() {
        QGuiApplication::restoreOverrideCursor();
        dialog_.close();
    }
    WorkIndicator(const WorkIndicator&) = delete;
    WorkIndicator& operator=(const WorkIndicator&) = delete;

private:
    QProgressDialog dialog_;
};

// Même principe que setEnabledWithReason (main_window.cpp, mêmes propriétés mémorisées) : l'action
// grisée dit, dans son info-bulle et son texte d'état, ce qu'il faut faire pour l'activer.
void setActionEnabledWithReason(QAction* act, bool enabled, const QString& whyDisabled) {
    if (act == nullptr) {
        return;
    }
    if (!act->property("baseToolTip").isValid()) {
        act->setProperty("baseToolTip", act->toolTip());
        act->setProperty("baseStatusTip", act->statusTip());
    }
    act->setEnabled(enabled);
    if (enabled) {
        act->setToolTip(act->property("baseToolTip").toString());
        act->setStatusTip(act->property("baseStatusTip").toString());
    } else {
        act->setToolTip(act->property("baseToolTip").toString() + QStringLiteral("\n") +
                        whyDisabled);
        act->setStatusTip(whyDisabled);
    }
}

} // namespace

void MainWindow::vectorizeSelectedRegion() {
    if (!selectedRegion_ || !project_.segmentation) {
        return;
    }
    // Toutes les régions sélectionnées sont vectorisées (la barre contextuelle annonce « 6
    // régions »), en un seul pas d'annulation. L'active seule ne comptait pas toute la sélection.
    const std::vector<RegionId> picked = selectedRegionIds();
    std::vector<RegionId> fresh;
    std::vector<RegionId> already;
    std::vector<ObjectId> existing; // objets déjà issus de ces régions
    for (const RegionId id : picked) {
        const auto it =
            std::find_if(project_.vector_objects.begin(), project_.vector_objects.end(),
                         [id](const document::VectorObject& o) { return o.source_region == id; });
        if (it == project_.vector_objects.end()) {
            fresh.push_back(id);
        } else {
            already.push_back(id);
            existing.push_back(it->id);
        }
    }

    bool replace = false;
    if (!already.empty()) {
        // Vectoriser deux fois la même région empilait un doublon (double broderie) sans rien dire.
        QString question;
        if (already.size() == 1) {
            question = tr("La région %1 a déjà un objet vectoriel.").arg(already.front().value);
        } else {
            question =
                tr("%1 des régions sélectionnées ont déjà un objet vectoriel.").arg(already.size());
        }
        QMessageBox box(QMessageBox::Question, tr("Région déjà vectorisée"), question,
                        QMessageBox::NoButton, this);
        box.setObjectName(QStringLiteral("alreadyVectorizedBox"));
        box.setInformativeText(
            tr("Remplacer recrée l'objet depuis la région (les objets de broderie qui en "
               "dépendent sont supprimés avec lui, annulable)."));
        QAbstractButton* replaceButton =
            box.addButton(tr("Remplacer"), QMessageBox::DestructiveRole);
        const QString skipText =
            fresh.empty() ? tr("Sélectionner l'objet existant") : tr("Ignorer ces régions");
        QAbstractButton* skipButton = box.addButton(skipText, QMessageBox::AcceptRole);
        QAbstractButton* cancelButton = box.addButton(tr("Annuler"), QMessageBox::RejectRole);
        box.setDefaultButton(qobject_cast<QPushButton*>(skipButton));
        box.setEscapeButton(cancelButton);
        box.exec();
        if (box.clickedButton() == replaceButton) {
            replace = true;
        } else if (box.clickedButton() == skipButton) {
            if (fresh.empty()) {
                showVectorsAct_->setChecked(true);
                setSelection({.region = std::nullopt,
                              .embroidery = std::nullopt,
                              .objects = existing,
                              .extraRegions = {}});
                displayImage(processed_);
                updateActions();
                QString message;
                if (already.size() == 1) {
                    message = tr("Région %1 déjà vectorisée — objet sélectionné.")
                                  .arg(already.front().value);
                } else {
                    message = tr("%1 régions déjà vectorisées — objets sélectionnés.")
                                  .arg(already.size());
                }
                statusBar()->showMessage(message);
                return;
            }
        } else {
            return; // Annuler
        }
    }
    const std::vector<RegionId>& todo = replace ? picked : fresh;
    if (todo.empty()) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Vectorisation"));
    auto* layout = new QFormLayout(&dialog);
    if (todo.size() > 1) {
        layout->addRow(new QLabel(
            tr("%1 régions seront vectorisées avec ce réglage.").arg(todo.size()), &dialog));
    }
    auto* detailSlider = new QSlider(Qt::Horizontal, &dialog);
    detailSlider->setObjectName("vectorizeDetailSlider");
    detailSlider->setRange(0, 100);
    detailSlider->setValue(ui_memory::intValue(QStringLiteral("vectorize/detail"), 50, 0, 100));
    detailSlider->setToolTip(tr("Niveau de détail conservé dans le contour vectoriel."));
    auto* detailValue = new QLabel(QString::number(detailSlider->value()), &dialog);
    detailValue->setObjectName("vectorizeDetailValue");
    connect(detailSlider, &QSlider::valueChanged, detailValue,
            [detailValue](int value) { detailValue->setText(QString::number(value)); });
    auto* detailRow = new QWidget(&dialog);
    auto* detailLayout = new QHBoxLayout(detailRow);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addWidget(new QLabel(tr("Faible"), detailRow));
    detailLayout->addWidget(detailSlider, 1);
    detailLayout->addWidget(new QLabel(tr("Élevé"), detailRow));
    detailLayout->addWidget(detailValue);
    layout->addRow(tr("Détail :"), detailRow);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    ui_memory::setIntValue(QStringLiteral("vectorize/detail"), detailSlider->value());

    const Micrometers simplifyTolerance = vectorize_tolerance_from_detail(detailSlider->value());

    std::vector<document::VectorObject> objects;
    QStringList failures;
    {
        WorkIndicator busy(this, tr("Vectorisation en cours…"));
        for (const RegionId id : todo) {
            const auto* region = project_.segmentation->find(id);
            if (region == nullptr) {
                continue;
            }
            auto sets = vectorization::vectorize_region(
                *project_.segmentation, id,
                {.mm_per_px = project_.mm_per_px, .simplify_tolerance = simplifyTolerance});
            if (!sets) {
                failures << tr("Région %1 : %2")
                                .arg(id.value)
                                .arg(QString::fromStdString(sets.error().message));
                continue;
            }
            document::VectorObject object;
            object.id = project_.object_ids.next();
            object.name = tr("Région %1").arg(id.value).toStdString();
            object.source_region = id;
            object.rgb = region->rgb;
            object.paths = std::move(*sets);
            objects.push_back(std::move(object));
        }
    }
    if (objects.empty()) {
        QString reason = tr("Aucune région à vectoriser.");
        if (!failures.isEmpty()) {
            reason = failures.join(QLatin1Char('\n'));
        }
        QMessageBox::warning(this, tr("Vectorisation impossible"), reason);
        return;
    }

    std::vector<ObjectId> created;
    for (const auto& object : objects) {
        created.push_back(object.id);
    }
    if (objects.size() == 1 && !replace) {
        undoStack_.execute(
            std::make_unique<commands::AddVectorObjectCommand>(std::move(objects.front())),
            project_);
    } else {
        auto group = std::make_unique<commands::CompositeCommand>(
            tr("Vectoriser %1 régions").arg(objects.size()).toStdString());
        if (replace) {
            // Seuls les objets des régions effectivement recréées sont remplacés : une région
            // dont la vectorisation a échoué garde son objet.
            for (const auto& old : project_.vector_objects) {
                const bool rebuilt =
                    std::any_of(objects.begin(), objects.end(), [&old](const auto& fresh) {
                        return old.source_region && old.source_region == fresh.source_region;
                    });
                if (rebuilt) {
                    group->add(std::make_unique<commands::RemoveVectorObjectCommand>(old.id));
                }
            }
        }
        for (auto& object : objects) {
            group->add(std::make_unique<commands::AddVectorObjectCommand>(std::move(object)));
        }
        undoStack_.execute(std::move(group), project_);
    }
    if (created.size() == 1) {
        editSelection([id = created.front()](Selection& sel) { sel.objects = {id}; });
    } else {
        setSelection({.region = std::nullopt,
                      .embroidery = std::nullopt,
                      .objects = created,
                      .extraRegions = {}});
    }
    showVectorsAct_->setChecked(true);
    refreshImage();
    updateActions();
    if (!failures.isEmpty()) {
        QMessageBox::warning(this, tr("Vectorisation partielle"),
                             tr("%1 région(s) n'ont pas pu être vectorisées :\n\n%2")
                                 .arg(failures.size())
                                 .arg(failures.join(QLatin1Char('\n'))));
    }
    QString message = tr("Objet vectoriel créé — cliquez-le pour éditer ses nœuds");
    if (created.size() > 1) {
        message = tr("%1 objets vectoriels créés (un seul pas d'annulation) — cliquez-en un "
                     "pour éditer ses nœuds")
                      .arg(created.size());
    }
    statusBar()->showMessage(message);
}

void MainWindow::segmentWithAi() {
    if (!project_.hasImage() || processed_.empty()) {
        QMessageBox::information(this, tr("Segmenter avec l'IA"),
                                 tr("Importez d'abord une image."));
        return;
    }
    AiPreferences prefs = loadAiPreferences();
    if (!prefs.enabled) {
        const auto answer = QMessageBox::question(
            this, tr("Segmenter avec l'IA"),
            tr("La segmentation par IA n'est pas activée. Ouvrir les préférences maintenant ?"));
        if (answer != QMessageBox::Yes) {
            return;
        }
        openAiPreferences();
        // Activée à l'instant : on enchaîne au lieu de faire relancer l'action à la main.
        prefs = loadAiPreferences();
        if (!prefs.enabled) {
            return;
        }
    }

    AiSegmentationDialog dialog(processed_, project_.mm_per_px, prefs, this);
    connect(&dialog, &AiSegmentationDialog::openPreferencesRequested, &dialog, [this, &dialog] {
        openAiPreferences();
        dialog.setPreferences(loadAiPreferences());
    });
    if (dialog.exec() != QDialog::Accepted || !dialog.hasResult()) {
        return;
    }
    auto seg = dialog.takeSegmentation();
    if (!seg) {
        return;
    }

    if (dialog.output() == AiSegmentationDialog::Output::EditableRegions) {
        // Le résultat devient la segmentation du document : fusion, recoloration, sélection
        // multiple et vectorisation s'appliquent comme pour la segmentation classique. Les
        // régions reprennent leur couleur moyenne dans l'image (les couleurs de masque sont
        // arbitraires).
        if (project_.segmentation) {
            const auto answer = QMessageBox::question(
                this, tr("Segmenter avec l'IA"),
                tr("Remplacer la segmentation actuelle ? Vos fusions et couleurs modifiées seront "
                   "perdues (annulable avec Ctrl+Z)."),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                return;
            }
        }
        if (seg->width == processed_.width && seg->height == processed_.height) {
            for (auto& slot : seg->region_slots) {
                if (!slot) {
                    continue;
                }
                if (const auto mean = segmentation::region_mean_color(*seg, processed_, slot->id)) {
                    slot->rgb = *mean;
                }
            }
        }
        const auto regionCount = seg->region_count();
        undoStack_.execute(std::make_unique<commands::SetSegmentationCommand>(std::move(*seg)),
                           project_);
        editSelection([](Selection& sel) { sel.region.reset(); });
        showSegAct_->setChecked(true);
        refreshImage();
        updateActions();
        statusBar()->showMessage(
            tr("Segmentation IA : %1 régions éditables — fusionnez ou recolorez-les, puis "
               "vectorisez (F7) ou lancez la numérisation automatique (F8).")
                .arg(regionCount));
        return;
    }

    if (!project_.embroidery_objects.empty() || !project_.vector_objects.empty()) {
        const auto answer = QMessageBox::question(
            this, tr("Segmenter avec l'IA"),
            tr("Des objets existent déjà ; la numérisation directe ajoute de nouveaux objets "
               "(ils seront superposés aux existants). Continuer ?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    autodigitize::AutoOptions opts;
    opts.mm_per_px = project_.mm_per_px;
    opts.skip_largest_region = dialog.skipBackground();
    opts.simplify_tolerance = vectorize_tolerance_from_detail(dialog.vectorDetail());
    std::optional<WorkIndicator> busy;
    busy.emplace(this, tr("Numérisation automatique en cours…"));
    auto result = autodigitize::auto_digitize(*seg, project_.object_ids, opts);
    busy.reset();
    if (!result) {
        QMessageBox box(QMessageBox::Warning, tr("Numérisation impossible"),
                        tr("La numérisation des formes retenues a échoué."), QMessageBox::Ok, this);
        box.setDetailedText(QString::fromStdString(result.error().message));
        box.exec();
        return;
    }
    const std::size_t vecCount = result->vectors.size();
    const std::size_t embCount = result->embroideries.size();
    const std::vector<std::string> warnings = std::move(result->warnings);

    undoStack_.execute(
        std::make_unique<commands::AddObjectBatchCommand>(
            std::move(result->vectors), std::move(result->embroideries), "Segmentation IA"),
        project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Segmentation IA : %1 objet(s) vectoriel(s), %2 objet(s) de broderie créés.")
            .arg(vecCount)
            .arg(embCount));
    warnAboutSkippedAutoSatinBranches(warnings);
}

void MainWindow::quantizeColors() {
    if (!project_.hasImage()) {
        return;
    }
    // Aperçu en direct comme pour Luminosité/Contraste : choisir un nombre de couleurs à
    // l'aveugle obligeait à essayer, annuler, recommencer. Le calcul est différé de 150 ms
    // après la dernière frappe (une quantification sur une grande image n'est pas instantanée).
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Quantifier les couleurs"));
    auto* layout = new QFormLayout(&dialog);
    auto* colorsSpin = new QSpinBox(&dialog);
    colorsSpin->setObjectName(QStringLiteral("quantizeColorsSpin"));
    colorsSpin->setRange(2, 64);
    colorsSpin->setValue(ui_memory::intValue(QStringLiteral("quantize/colors"), 8, 2, 64));
    layout->addRow(tr("Nombre maximal de couleurs :"), colorsSpin);
    auto* hint =
        new QLabel(tr("L'aperçu s'affiche sur le canevas ; Annuler rétablit l'image."), &dialog);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    layout->addRow(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);

    const auto showPreview = [this, colorsSpin] {
        if (const auto img = image::apply_op(processed_, image::QuantizeOp{colorsSpin->value()})) {
            displayImage(*img);
        }
    };
    auto* debounce = new QTimer(&dialog);
    debounce->setSingleShot(true);
    debounce->setInterval(150);
    connect(debounce, &QTimer::timeout, &dialog, showPreview);
    connect(colorsSpin, &QSpinBox::valueChanged, debounce, qOverload<>(&QTimer::start));
    QTimer::singleShot(0, &dialog, showPreview); // aperçu initial, dialogue déjà affiché

    const bool accepted = dialog.exec() == QDialog::Accepted;
    displayImage(processed_); // retire l'aperçu dans tous les cas
    if (accepted) {
        ui_memory::setIntValue(QStringLiteral("quantize/colors"), colorsSpin->value());
        executeOp(image::QuantizeOp{colorsSpin->value()});
    }
}

void MainWindow::updateSegmentationWorkflowActions() {
    const bool hasSegmentation = project_.segmentation.has_value();
    // Les deux actions ouvraient une boîte « impossible » quand elles ne pouvaient pas servir ;
    // elles sont maintenant grisées avec la raison (les gardes des slots restent en repli).
    setActionEnabledWithReason(
        autoDigitizeAct_, hasSegmentation || !project_.vector_objects.empty(),
        tr("Segmentez d'abord l'image (menu Segmentation, F6) ou importez un fichier SVG."));
    setActionEnabledWithReason(aiSegmentAct_, project_.hasImage(), tr("Ouvrez d'abord une image."));

    if (regionCountLabel_ == nullptr) {
        regionCountLabel_ = new QLabel(this);
        regionCountLabel_->setObjectName(QStringLiteral("regionCountLabel"));
        statusBar()->addPermanentWidget(regionCountLabel_);
    }
    if (hasSegmentation) {
        const auto count = project_.segmentation->region_count();
        regionCountLabel_->setText(tr("%n région(s)", "", static_cast<int>(count)));
        regionCountLabel_->setToolTip(
            tr("Nombre de régions de la segmentation. Trop de petites régions : fusionnez-les "
               "dans leur voisine principale (Ctrl+Maj+M) ou segmentez avec moins de couleurs."));
        regionCountLabel_->show();
    } else {
        regionCountLabel_->clear();
        regionCountLabel_->hide();
    }
}

void MainWindow::onWorkflowStepClicked(int step) {
    // Une étape du panneau lance l'action correspondante si elle est disponible ; sinon la barre
    // d'état dit pourquoi (raison portée par l'action grisée). Les textes citent les vrais noms
    // des menus.
    struct Step {
        QAction* action;
        QString hint;
    };
    const std::array<Step, 6> steps{{
        {openImageAct_, tr("Fichier ▸ Ouvrir une image… pour commencer.")},
        {segmentAct_, tr("Segmentation ▸ Segmenter l'image… (F6), ou Segmentation ▸ Segmenter "
                         "avec l'IA…")},
        {vectorizeRegionAct_, tr("Sélectionnez une ou plusieurs régions (Ctrl+clic, ou glisser un "
                                 "cadre), puis Segmentation ▸ Vectoriser la sélection (F7).")},
        {autoDigitizeAct_, tr("Broderie ▸ Numérisation automatique (F8), ou créez un objet de "
                              "broderie depuis une forme.")},
        {analyzeAct_, tr("Analyse ▸ Analyser le motif (F5) pour vérifier le motif.")},
        {exportDstAct_, tr("Fichier ▸ Exporter en DST… (Ctrl+E).")},
    }};
    if (step < 0 || step >= static_cast<int>(steps.size())) {
        return;
    }
    const Step& entry = steps[static_cast<std::size_t>(step)];
    if (entry.action != nullptr && entry.action->isEnabled()) {
        statusBar()->showMessage(entry.hint, 6000);
        entry.action->trigger();
        return;
    }
    const QString reason = entry.action != nullptr ? entry.action->statusTip() : QString();
    statusBar()->showMessage(
        reason.isEmpty() ? entry.hint : reason + QStringLiteral(" — ") + entry.hint, 6000);
}

} // namespace openstitch::desktop
