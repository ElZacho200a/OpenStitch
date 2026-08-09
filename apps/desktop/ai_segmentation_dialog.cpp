// SPDX-License-Identifier: Apache-2.0
#include "ai_segmentation_dialog.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <functional>

#include "openstitch/ai_segmentation/color_refine.hpp"
#include "openstitch/ai_segmentation/label_map.hpp"
#include "openstitch/ai_segmentation/topology_cleanup.hpp"

namespace openstitch::desktop {

namespace {

QTableWidgetItem* makeCheckableItem(Qt::CheckState initial) {
    auto* item = new QTableWidgetItem();
    item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setCheckState(initial);
    return item;
}

}  // namespace

AiSegmentationDialog::AiSegmentationDialog(image::Image sourceImage, Millimeters mmPerPx, AiPreferences prefs,
                                          QWidget* parent)
    : QDialog(parent), sourceImage_(std::move(sourceImage)), mmPerPx_(mmPerPx), prefs_(std::move(prefs)) {
    setupUi();
}

AiSegmentationDialog::~AiSegmentationDialog() {
    if (client_ != nullptr) {
        client_->stop();
    }
    if (!prefs_.keepDiagnosticFiles && !jobId_.isEmpty()) {
        QDir(jobDirPath()).removeRecursively();
    }
}

void AiSegmentationDialog::setupUi() {
    setWindowTitle(tr("Segmenter avec l'IA"));
    resize(900, 640);

    auto* mainLayout = new QVBoxLayout(this);

    auto* topRow = new QHBoxLayout;
    modelCombo_ = new QComboBox(this);
    for (const auto& descriptor : ai_segmentation::all_models()) {
        modelCombo_->addItem(QString::fromUtf8(descriptor.display_name.data(),
                                               static_cast<qsizetype>(descriptor.display_name.size())),
                             static_cast<int>(descriptor.id));
    }
    modelCombo_->setCurrentIndex(static_cast<int>(prefs_.defaultModel));
    topRow->addWidget(new QLabel(tr("Modèle :"), this));
    topRow->addWidget(modelCombo_);

    profileCombo_ = new QComboBox(this);
    profileCombo_->addItem(tr("Formes principales"), QStringLiteral("main_shapes"));
    profileCombo_->addItem(tr("Équilibré (recommandé)"), QStringLiteral("balanced"));
    profileCombo_->addItem(tr("Détails"), QStringLiteral("detail"));
    profileCombo_->setCurrentIndex(1);
    topRow->addWidget(new QLabel(tr("Profil :"), this));
    topRow->addWidget(profileCombo_);

    analyzeButton_ = new QPushButton(tr("Analyser"), this);
    connect(analyzeButton_, &QPushButton::clicked, this, &AiSegmentationDialog::onAnalyzeClicked);
    topRow->addWidget(analyzeButton_);
    cancelButton_ = new QPushButton(tr("Annuler l'analyse"), this);
    cancelButton_->setEnabled(false);
    connect(cancelButton_, &QPushButton::clicked, this, &AiSegmentationDialog::onCancelClicked);
    topRow->addWidget(cancelButton_);
    topRow->addStretch(1);
    mainLayout->addLayout(topRow);

    statusLabel_ = new QLabel(tr("Choisissez un modèle puis cliquez sur Analyser."), this);
    mainLayout->addWidget(statusLabel_);
    progressBar_ = new QProgressBar(this);
    progressBar_->setVisible(false);
    mainLayout->addWidget(progressBar_);

    auto* middleRow = new QHBoxLayout;
    auto* previewColumn = new QVBoxLayout;
    previewLabel_ = new QLabel(this);
    previewLabel_->setMinimumSize(280, 220);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setFrameShape(QFrame::StyledPanel);
    previewColumn->addWidget(new QLabel(tr("Aperçu (tous les masques) :"), this));
    previewColumn->addWidget(previewLabel_, 1);
    selectionPreviewLabel_ = new QLabel(this);
    selectionPreviewLabel_->setMinimumSize(280, 220);
    selectionPreviewLabel_->setAlignment(Qt::AlignCenter);
    selectionPreviewLabel_->setFrameShape(QFrame::StyledPanel);
    previewColumn->addWidget(new QLabel(tr("Masque sélectionné :"), this));
    previewColumn->addWidget(selectionPreviewLabel_, 1);
    middleRow->addLayout(previewColumn, 1);

    auto* tableColumn = new QVBoxLayout;
    maskTable_ = new QTableWidget(0, 6, this);
    maskTable_->setHorizontalHeaderLabels(
        {tr("Garder"), tr("Id"), tr("Aire (mm²)"), tr("IoU"), tr("Stabilité"), tr("Protéger")});
    maskTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    maskTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    connect(maskTable_, &QTableWidget::itemSelectionChanged, this, &AiSegmentationDialog::onTableSelectionChanged);
    tableColumn->addWidget(maskTable_, 1);
    mergeButton_ = new QPushButton(tr("Fusionner la sélection"), this);
    connect(mergeButton_, &QPushButton::clicked, this, &AiSegmentationDialog::onMergeClicked);
    tableColumn->addWidget(mergeButton_);
    middleRow->addLayout(tableColumn, 1);
    mainLayout->addLayout(middleRow, 1);

    auto* thresholdsRow = new QFormLayout;
    minIslandAreaSpin_ = new QDoubleSpinBox(this);
    minIslandAreaSpin_->setRange(0.0, 100.0);
    minIslandAreaSpin_->setDecimals(2);
    minIslandAreaSpin_->setValue(0.3);
    minIslandAreaSpin_->setSuffix(tr(" mm²"));
    thresholdsRow->addRow(tr("Îlots fusionnés sous :"), minIslandAreaSpin_);
    minHoleAreaSpin_ = new QDoubleSpinBox(this);
    minHoleAreaSpin_->setRange(0.0, 100.0);
    minHoleAreaSpin_->setDecimals(2);
    minHoleAreaSpin_->setValue(0.3);
    minHoleAreaSpin_->setSuffix(tr(" mm²"));
    thresholdsRow->addRow(tr("Trous comblés sous :"), minHoleAreaSpin_);
    mainLayout->addLayout(thresholdsRow);

    // SAM 2 découpe par forme, jamais par couleur : pour préparer des blocs
    // de couleur en vue de la numérisation, chaque forme retenue est ici
    // subdivisée par couleur avec l'algorithme de quantification CIELAB déjà
    // utilisé par la segmentation classique (menu Segmentation) — coché par
    // défaut, car c'est l'usage premier de cet outil.
    colorRefineCheck_ = new QCheckBox(tr("Diviser chaque forme retenue par couleur"), this);
    colorRefineCheck_->setChecked(true);
    mainLayout->addWidget(colorRefineCheck_);
    auto* colorRefineRow = new QFormLayout;
    colorRefineColorsSpin_ = new QSpinBox(this);
    colorRefineColorsSpin_->setRange(2, 64);
    colorRefineColorsSpin_->setValue(8);
    colorRefineRow->addRow(tr("Nombre maximal de couleurs par forme :"), colorRefineColorsSpin_);
    colorRefineMinSizeSpin_ = new QSpinBox(this);
    colorRefineMinSizeSpin_->setRange(1, 100'000);
    colorRefineMinSizeSpin_->setValue(16);
    colorRefineMinSizeSpin_->setSuffix(tr(" px"));
    colorRefineRow->addRow(tr("Taille minimale de bloc de couleur :"), colorRefineMinSizeSpin_);
    mainLayout->addLayout(colorRefineRow);
    connect(colorRefineCheck_, &QCheckBox::toggled, colorRefineColorsSpin_, &QWidget::setEnabled);
    connect(colorRefineCheck_, &QCheckBox::toggled, colorRefineMinSizeSpin_, &QWidget::setEnabled);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    validateButton_ = buttons_->addButton(tr("Valider"), QDialogButtonBox::AcceptRole);
    validateButton_->setEnabled(false);
    connect(validateButton_, &QPushButton::clicked, this, &AiSegmentationDialog::onValidateClicked);
    mainLayout->addWidget(buttons_);
}

void AiSegmentationDialog::setStatus(const QString& text) {
    statusLabel_->setText(text);
}

QString AiSegmentationDialog::jobDirPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
          QStringLiteral("/OpenStitch/ai-jobs/") + jobId_;
}

void AiSegmentationDialog::onAnalyzeClicked() {
    analyzeButton_->setEnabled(false);
    cancelButton_->setEnabled(true);
    validateButton_->setEnabled(false);
    maskTable_->setRowCount(0);
    maskPixels_.clear();
    masks_ = {};
    previewLabel_->clear();
    selectionPreviewLabel_->clear();
    progressBar_->setRange(0, 0);
    progressBar_->setVisible(true);
    setStatus(tr("Préparation de l'image…"));

    jobId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(jobDirPath());
    const auto encoded = image::encode_png(sourceImage_);
    bool wrote = false;
    if (encoded) {
        QFile inputFile(jobDirPath() + QStringLiteral("/input.png"));
        if (inputFile.open(QIODevice::WriteOnly)) {
            wrote = inputFile.write(reinterpret_cast<const char*>(encoded->data()),
                                    static_cast<qint64>(encoded->size())) == static_cast<qint64>(encoded->size());
        }
    }
    if (!wrote) {
        setStatus(tr("Échec : impossible de préparer l'image de travail pour le worker."));
        analyzeButton_->setEnabled(true);
        cancelButton_->setEnabled(false);
        progressBar_->setVisible(false);
        return;
    }

    if (client_ == nullptr) {
        client_ = new SamWorkerClient(this);
        connect(client_, &SamWorkerClient::stateChanged, this, &AiSegmentationDialog::onWorkerStateChanged);
        connect(client_, &SamWorkerClient::progress, this, &AiSegmentationDialog::onWorkerProgress);
        connect(client_, &SamWorkerClient::modelReady, this, &AiSegmentationDialog::onModelReady);
        connect(client_, &SamWorkerClient::segmentResult, this, &AiSegmentationDialog::onSegmentResult);
        connect(client_, &SamWorkerClient::workerError, this, &AiSegmentationDialog::onWorkerError);
        connect(client_, &SamWorkerClient::requestCancelled, this, [this](QString requestId) {
            Q_UNUSED(requestId);
            phase_ = Phase::Idle;
            analyzeButton_->setEnabled(true);
            cancelButton_->setEnabled(false);
            progressBar_->setVisible(false);
            setStatus(tr("Analyse annulée."));
        });
        connect(client_, &SamWorkerClient::crashed, this, [this](QString detail) {
            setStatus(tr("Le worker s'est arrêté de façon inattendue (%1). Nouvelle tentative…").arg(detail));
        });
        client_->configure(toWorkerConfig(prefs_));
    }

    pendingModel_ = static_cast<ai_segmentation::ModelId>(modelCombo_->currentData().toInt());

    if (!client_->isConfigured()) {
        setStatus(tr("Configuration IA incomplète : ouvrez Préférences > Intelligence artificielle."));
        analyzeButton_->setEnabled(true);
        cancelButton_->setEnabled(false);
        progressBar_->setVisible(false);
        return;
    }

    if (client_->state() == SamWorkerClient::State::Ready) {
        phase_ = Phase::LoadingModel;
        setStatus(tr("Chargement du modèle…"));
        activeRequestId_ = client_->loadModel(pendingModel_);
    } else {
        phase_ = Phase::StartingWorker;
        setStatus(tr("Démarrage du worker de segmentation…"));
        client_->start();
    }
}

void AiSegmentationDialog::onCancelClicked() {
    if (client_ != nullptr && !activeRequestId_.isEmpty()) {
        client_->cancel(activeRequestId_);
        setStatus(tr("Annulation en cours…"));
    }
}

void AiSegmentationDialog::onWorkerStateChanged(SamWorkerClient::State state) {
    if (state == SamWorkerClient::State::Ready && phase_ == Phase::StartingWorker) {
        phase_ = Phase::LoadingModel;
        setStatus(tr("Chargement du modèle…"));
        activeRequestId_ = client_->loadModel(pendingModel_);
    } else if (state == SamWorkerClient::State::Unavailable && phase_ != Phase::Idle) {
        phase_ = Phase::Idle;
        analyzeButton_->setEnabled(true);
        cancelButton_->setEnabled(false);
        progressBar_->setVisible(false);
        setStatus(tr("Le worker de segmentation IA n'est pas disponible."));
    }
}

void AiSegmentationDialog::onWorkerProgress(QString requestId, QString stage) {
    Q_UNUSED(requestId);
    static const QHash<QString, QString> labels{
        {QStringLiteral("preparing_image"), tr("Préparation de l'image…")},
        {QStringLiteral("loading_model"), tr("Chargement du modèle…")},
        {QStringLiteral("running_inference"), tr("Analyse avec SAM 2…")},
        {QStringLiteral("cleaning_masks"), tr("Nettoyage des masques…")},
        {QStringLiteral("writing_results"), tr("Préparation de l'aperçu…")},
    };
    setStatus(labels.value(stage, stage));
}

void AiSegmentationDialog::onModelReady(QString modelWorkerId, QString device, double loadSeconds) {
    Q_UNUSED(loadSeconds);
    if (phase_ != Phase::LoadingModel) {
        return;
    }
    phase_ = Phase::Segmenting;
    setStatus(tr("Modèle %1 chargé (%2). Analyse avec SAM 2…").arg(modelWorkerId, device));

    SamSegmentParams params;
    params.profile = profileCombo_->currentData().toString();
    const double pxPerMm2 = mmPerPx_.value > 0.0 ? 1.0 / (mmPerPx_.value * mmPerPx_.value) : 0.0;
    params.fillHoleAreaThreshold = static_cast<int>(minHoleAreaSpin_->value() * pxPerMm2);
    params.removeIslandAreaThreshold = static_cast<int>(minIslandAreaSpin_->value() * pxPerMm2);
    params.maxResolution = prefs_.maxAnalysisResolution;
    activeRequestId_ = client_->segmentImage(jobDirPath(), QStringLiteral("input.png"), params);
}

void AiSegmentationDialog::onSegmentResult(QString requestId, QString jobDir, QString masksFile, int maskCount) {
    Q_UNUSED(requestId);
    Q_UNUSED(maskCount);
    phase_ = Phase::Idle;
    analyzeButton_->setEnabled(true);
    cancelButton_->setEnabled(false);
    progressBar_->setVisible(false);

    QFile file(jobDir + QStringLiteral("/") + masksFile);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(tr("Échec : impossible de lire les résultats du worker."));
        return;
    }
    const QByteArray content = file.readAll();
    const auto parsed =
        ai_segmentation::parse_masks_json(std::string_view(content.constData(), static_cast<std::size_t>(content.size())));
    if (!parsed) {
        setStatus(tr("Échec : résultat de segmentation invalide (%1).")
                      .arg(QString::fromStdString(parsed.error().message)));
        return;
    }
    masks_ = *parsed;
    setStatus(tr("%1 masque(s) proposé(s) — cochez ceux à conserver puis validez.").arg(masks_.masks.size()));

    const QPixmap preview(jobDir + QStringLiteral("/preview.png"));
    if (!preview.isNull()) {
        previewLabel_->setPixmap(
            preview.scaled(previewLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    loadMasksIntoTable();
}

void AiSegmentationDialog::onWorkerError(QString requestId, ai_segmentation::AiErrorCode code, QString message,
                                        QString detail) {
    Q_UNUSED(requestId);
    Q_UNUSED(code);
    phase_ = Phase::Idle;
    analyzeButton_->setEnabled(true);
    cancelButton_->setEnabled(false);
    progressBar_->setVisible(false);
    setStatus(detail.isEmpty() ? message : QStringLiteral("%1 (%2)").arg(message, detail));
}

QVector<std::uint8_t> AiSegmentationDialog::loadMaskPixels(const ai_segmentation::MaskEntry& entry) const {
    const std::filesystem::path path((jobDirPath() + QStringLiteral("/") + QString::fromStdString(entry.file)).toStdString());
    const auto loaded = image::load_image(path);
    if (!loaded) {
        return {};
    }
    QVector<std::uint8_t> pixels(loaded->width * loaded->height, 0);
    for (int i = 0; i < loaded->width * loaded->height; ++i) {
        pixels[i] = loaded->rgba[static_cast<std::size_t>(i) * 4] > 127 ? 1 : 0;
    }
    return pixels;
}

void AiSegmentationDialog::loadMasksIntoTable() {
    maskTable_->setRowCount(0);
    maskPixels_.clear();
    const double mm2PerPx = mmPerPx_.value * mmPerPx_.value;
    for (const auto& entry : masks_.masks) {
        maskPixels_.insert(entry.id, loadMaskPixels(entry));

        const int r = maskTable_->rowCount();
        maskTable_->insertRow(r);
        maskTable_->setItem(r, 0, makeCheckableItem(Qt::Checked));
        maskTable_->setItem(r, 1, new QTableWidgetItem(QString::number(entry.id)));
        maskTable_->setItem(r, 2,
                            new QTableWidgetItem(QString::number(static_cast<double>(entry.area_pixels) * mm2PerPx, 'f', 2)));
        maskTable_->setItem(r, 3, new QTableWidgetItem(QString::number(entry.predicted_iou, 'f', 2)));
        maskTable_->setItem(r, 4, new QTableWidgetItem(QString::number(entry.stability_score, 'f', 2)));
        maskTable_->setItem(r, 5, makeCheckableItem(Qt::Unchecked));
    }
    validateButton_->setEnabled(maskTable_->rowCount() > 0);
}

void AiSegmentationDialog::onTableSelectionChanged() {
    updateSelectedMaskPreview();
}

void AiSegmentationDialog::updateSelectedMaskPreview() {
    const auto selectedRows = maskTable_->selectionModel() != nullptr ? maskTable_->selectionModel()->selectedRows()
                                                                      : QModelIndexList{};
    if (selectedRows.isEmpty()) {
        selectionPreviewLabel_->clear();
        return;
    }
    const int id = maskTable_->item(selectedRows.first().row(), 1)->text().toInt();
    const auto pixelsIt = maskPixels_.constFind(id);
    if (pixelsIt == maskPixels_.constEnd() || pixelsIt->isEmpty()) {
        selectionPreviewLabel_->clear();
        return;
    }
    const int width = masks_.image_width;
    const int height = masks_.image_height;
    if (width <= 0 || height <= 0 || sourceImage_.width != width || sourceImage_.height != height) {
        selectionPreviewLabel_->clear();
        return;
    }
    QImage image(width, height, QImage::Format_RGB888);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int idx = y * width + x;
            const std::uint8_t* src = sourceImage_.rgba.data() + static_cast<std::size_t>(idx) * 4;
            if ((*pixelsIt)[idx] != 0) {
                image.setPixelColor(x, y, QColor(255, 90, 0));
            } else {
                const int gray = (src[0] + src[1] + src[2]) / 6;
                image.setPixelColor(x, y, QColor(gray, gray, gray));
            }
        }
    }
    selectionPreviewLabel_->setPixmap(
        QPixmap::fromImage(image).scaled(selectionPreviewLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void AiSegmentationDialog::onMergeClicked() {
    const auto selectedRows = maskTable_->selectionModel() != nullptr ? maskTable_->selectionModel()->selectedRows()
                                                                      : QModelIndexList{};
    if (selectedRows.size() < 2) {
        setStatus(tr("Sélectionnez au moins deux masques dans la liste pour les fusionner."));
        return;
    }

    QVector<int> ids;
    for (const auto& index : selectedRows) {
        ids.push_back(maskTable_->item(index.row(), 1)->text().toInt());
    }

    const int width = masks_.image_width;
    const int height = masks_.image_height;
    QVector<std::uint8_t> merged(width * height, 0);
    double maxIou = 0.0;
    double maxStability = 0.0;
    for (const int id : ids) {
        const auto pixelsIt = maskPixels_.constFind(id);
        if (pixelsIt != maskPixels_.constEnd()) {
            for (int i = 0; i < merged.size() && i < pixelsIt->size(); ++i) {
                if ((*pixelsIt)[i] != 0) {
                    merged[i] = 1;
                }
            }
        }
        for (const auto& entry : masks_.masks) {
            if (entry.id == id) {
                maxIou = std::max(maxIou, entry.predicted_iou);
                maxStability = std::max(maxStability, entry.stability_score);
            }
        }
    }
    const auto mergedArea = static_cast<std::size_t>(std::count(merged.begin(), merged.end(), std::uint8_t{1}));

    int newId = 0;
    for (const auto& entry : masks_.masks) {
        newId = std::max(newId, entry.id + 1);
    }

    ai_segmentation::MaskEntry newEntry;
    newEntry.id = newId;
    newEntry.area_pixels = mergedArea;
    newEntry.predicted_iou = maxIou;
    newEntry.stability_score = maxStability;
    masks_.masks.push_back(newEntry);
    maskPixels_.insert(newId, merged);

    for (const int id : ids) {
        masks_.masks.erase(
            std::remove_if(masks_.masks.begin(), masks_.masks.end(), [id](const auto& e) { return e.id == id; }),
            masks_.masks.end());
        maskPixels_.remove(id);
    }

    QVector<int> rowIndices;
    for (const auto& index : selectedRows) {
        rowIndices.push_back(index.row());
    }
    std::sort(rowIndices.begin(), rowIndices.end(), std::greater<>());
    for (const int r : rowIndices) {
        maskTable_->removeRow(r);
    }

    const int r = maskTable_->rowCount();
    maskTable_->insertRow(r);
    maskTable_->setItem(r, 0, makeCheckableItem(Qt::Checked));
    maskTable_->setItem(r, 1, new QTableWidgetItem(QString::number(newId)));
    const double mm2PerPx = mmPerPx_.value * mmPerPx_.value;
    maskTable_->setItem(r, 2, new QTableWidgetItem(QString::number(static_cast<double>(mergedArea) * mm2PerPx, 'f', 2)));
    maskTable_->setItem(r, 3, new QTableWidgetItem(QString::number(maxIou, 'f', 2)));
    maskTable_->setItem(r, 4, new QTableWidgetItem(QString::number(maxStability, 'f', 2)));
    maskTable_->setItem(r, 5, makeCheckableItem(Qt::Unchecked));

    setStatus(tr("%1 masques fusionnés en un seul (id %2).").arg(ids.size()).arg(newId));
}

void AiSegmentationDialog::onValidateClicked() {
    std::vector<ai_segmentation::LabelMaskInput> inputs;
    for (int r = 0; r < maskTable_->rowCount(); ++r) {
        if (maskTable_->item(r, 0)->checkState() != Qt::Checked) {
            continue;
        }
        const int id = maskTable_->item(r, 1)->text().toInt();
        const auto pixelsIt = maskPixels_.constFind(id);
        if (pixelsIt == maskPixels_.constEnd() || pixelsIt->isEmpty()) {
            continue;
        }

        ai_segmentation::LabelMaskInput input;
        input.mask_id = id;
        input.pixels.assign(pixelsIt->begin(), pixelsIt->end());
        input.rgb = {static_cast<std::uint8_t>((id * 53) % 180 + 60), static_cast<std::uint8_t>((id * 97) % 180 + 60),
                    static_cast<std::uint8_t>((id * 151) % 180 + 60)};
        input.is_protected = maskTable_->item(r, 5)->checkState() == Qt::Checked;
        for (const auto& entry : masks_.masks) {
            if (entry.id == id) {
                input.predicted_iou = entry.predicted_iou;
                input.stability_score = entry.stability_score;
                break;
            }
        }
        inputs.push_back(std::move(input));
    }

    if (inputs.empty()) {
        setStatus(tr("Cochez au moins un masque à conserver avant de valider."));
        return;
    }

    const ai_segmentation::LabelMapOptions labelMapOptions{masks_.image_width, masks_.image_height};
    auto labelMap = ai_segmentation::build_label_map(inputs, labelMapOptions);
    if (!labelMap) {
        setStatus(tr("Échec de la construction de la carte de labels : %1")
                      .arg(QString::fromStdString(labelMap.error().message)));
        return;
    }

    std::vector<RegionId> protectedIds;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (inputs[i].is_protected) {
            protectedIds.push_back(RegionId{i + 1});
        }
    }

    ai_segmentation::TopologyCleanupOptions cleanupOptions;
    cleanupOptions.mm_per_px = mmPerPx_.value;
    cleanupOptions.min_island_area_mm2 = minIslandAreaSpin_->value();
    cleanupOptions.min_hole_area_mm2 = minHoleAreaSpin_->value();
    cleanupOptions.protected_regions = std::move(protectedIds);

    auto report = ai_segmentation::cleanup_topology(*labelMap, cleanupOptions);
    if (!report) {
        setStatus(
            tr("Échec du nettoyage topologique : %1").arg(QString::fromStdString(report.error().message)));
        return;
    }

    validationReport_ = *report;

    // SAM 2 a trouvé des FORMES ; l'usage réel de cet outil est de préparer
    // des blocs de COULEUR pour la numérisation. Chaque forme retenue est
    // donc, par défaut, encore subdivisée par couleur (même algorithme que
    // la segmentation classique). Un échec ici (forme trop petite/uniforme)
    // ne doit pas bloquer la validation : on retombe sur les formes IA
    // telles quelles plutôt que de perdre tout le travail de revue.
    if (colorRefineCheck_->isChecked()) {
        ai_segmentation::ColorRefineOptions colorOptions;
        colorOptions.max_colors = colorRefineColorsSpin_->value();
        colorOptions.min_region_px = colorRefineMinSizeSpin_->value();
        auto refined = ai_segmentation::refine_label_map_by_color(*labelMap, sourceImage_, colorOptions);
        if (refined) {
            validatedSegmentation_ = std::move(*refined);
            accept();
            return;
        }
        setStatus(tr("Découpage par couleur impossible (%1) : formes IA conservées telles quelles.")
                      .arg(QString::fromStdString(refined.error().message)));
    }

    validatedSegmentation_ = std::move(*labelMap);
    accept();
}

std::optional<segmentation::Segmentation> AiSegmentationDialog::takeSegmentation() {
    return std::move(validatedSegmentation_);
}

}  // namespace openstitch::desktop
