// SPDX-License-Identifier: Apache-2.0
#include "ai_segmentation_dialog.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScopeGuard>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>

#include "app_theme.hpp"
#include "openstitch/ai_segmentation/color_refine.hpp"
#include "openstitch/ai_segmentation/label_map.hpp"
#include "openstitch/ai_segmentation/topology_cleanup.hpp"

namespace openstitch::desktop {

namespace {

// Cellule triée par sa valeur numérique (« 10 » après « 9 », pas avant « 2 »).
class NumericItem : public QTableWidgetItem {
public:
    NumericItem(const QString& text, double key) : QTableWidgetItem(text), key_(key) {}
    bool operator<(const QTableWidgetItem& other) const override {
        if (const auto* o = dynamic_cast<const NumericItem*>(&other)) {
            return key_ < o->key_;
        }
        return QTableWidgetItem::operator<(other);
    }

private:
    double key_;
};

// Case à cocher triée par son état (décochées d'abord).
class CheckItem : public QTableWidgetItem {
public:
    bool operator<(const QTableWidgetItem& other) const override {
        return checkState() < other.checkState();
    }
};

QTableWidgetItem* makeCheckableItem(Qt::CheckState initial) {
    auto* item = new CheckItem();
    item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setCheckState(initial);
    return item;
}

QTableWidgetItem* makeNumberItem(double value, int decimals) {
    return new NumericItem(QString::number(value, 'f', decimals), value);
}

// Erreurs dont la cause se règle dans Préférences > Intelligence artificielle.
bool isConfigurationIssue(ai_segmentation::AiErrorCode code) {
    using Code = ai_segmentation::AiErrorCode;
    switch (code) {
    case Code::WorkerNotConfigured:
    case Code::WorkerStartFailed:
    case Code::PythonNotFound:
    case Code::WslNotFound:
    case Code::VenvNotFound:
    case Code::Sam2NotInstalled:
    case Code::ModelNotInstalled:
    case Code::ConfigNotFound:
    case Code::CheckpointNotFound:
    case Code::ModelCheckpointMismatch:
        return true;
    default:
        return false;
    }
}

constexpr int kColId = 1;
const QString kSettingsPrefix = QStringLiteral("ui/aiSegmentation/");

} // namespace

AiSegmentationDialog::AiSegmentationDialog(image::Image sourceImage, Millimeters mmPerPx,
                                           AiPreferences prefs, QWidget* parent)
    : QDialog(parent), sourceImage_(std::move(sourceImage)), mmPerPx_(mmPerPx),
      prefs_(std::move(prefs)) {
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
    resize(900, 700);

    auto* mainLayout = new QVBoxLayout(this);

    // Confusion réelle rencontrée à l'usage : l'IA détecte des FORMES/OBJETS
    // (contours, textures), jamais des couleurs — deux zones de la même
    // couleur mais appartenant à des formes différentes seront séparées, et
    // inversement. Pour diviser par couleur (préparation de blocs de
    // couleur), le menu Segmentation classique est le bon outil.
    auto* explainer =
        new QLabel(tr("Détecte des formes/objets (contours), pas des couleurs — deux zones "
                      "de même couleur mais de formes différentes seront séparées. Pour "
                      "diviser par couleur, utilisez plutôt le menu Segmentation."),
                   this);
    explainer->setWordWrap(true);
    markSecondaryText(explainer); // texte atténué : information, pas une alerte
    mainLayout->addWidget(explainer);

    auto* topRow = new QHBoxLayout;
    modelCombo_ = new QComboBox(this);
    for (const auto& descriptor : ai_segmentation::all_models()) {
        modelCombo_->addItem(
            QString::fromUtf8(descriptor.display_name.data(),
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

    auto* statusRow = new QHBoxLayout;
    statusLabel_ = new QLabel(tr("Choisissez un modèle puis cliquez sur Analyser."), this);
    statusLabel_->setWordWrap(true);
    statusRow->addWidget(statusLabel_, 1);
    detailButton_ = new QPushButton(tr("Afficher le détail"), this);
    detailButton_->setObjectName(QStringLiteral("aiErrorDetailButton"));
    detailButton_->setVisible(false);
    connect(detailButton_, &QPushButton::clicked, this, [this] {
        QMessageBox box(QMessageBox::Information, tr("Détail de l'erreur"), statusLabel_->text(),
                        QMessageBox::Ok, this);
        QString detail = errorDetail_;
        if (detail.isEmpty()) {
            detail = tr("Aucun détail technique n'a été fourni par le worker.");
        }
        box.setDetailedText(detail);
        box.exec();
    });
    statusRow->addWidget(detailButton_);
    preferencesButton_ = new QPushButton(tr("Ouvrir les préférences…"), this);
    preferencesButton_->setObjectName(QStringLiteral("aiOpenPreferencesButton"));
    preferencesButton_->setVisible(false);
    connect(preferencesButton_, &QPushButton::clicked, this,
            &AiSegmentationDialog::openPreferencesRequested);
    statusRow->addWidget(preferencesButton_);
    mainLayout->addLayout(statusRow);
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
    // En-têtes expliqués : « IoU », « Stabilité » et « Protéger » ne veulent rien dire sans
    // eux. Les infobulles sont posées sur les éléments d'en-tête (survol de la colonne).
    const QStringList headerTips = {
        tr("Cochée : ce masque est conservé à la validation."),
        tr("Numéro du masque (les fusions en créent un nouveau)."),
        tr("Surface du masque en mm², d'après l'échelle de l'image."),
        tr("Confiance du modèle dans la qualité du contour, de 0 à 1 (plus haut = plus sûr)."),
        tr("Robustesse du masque aux variations du seuil, de 0 à 1 (plus haut = plus net)."),
        tr("Protéger : ce masque n'est jamais absorbé par le nettoyage des îlots et des "
           "trous.")};
    for (int column = 0; column < headerTips.size(); ++column) {
        if (auto* header = maskTable_->horizontalHeaderItem(column)) {
            header->setToolTip(headerTips.at(column));
        }
    }
    maskTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    maskTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Tri par colonne (clic sur l'en-tête) : trier par aire, par confiance… pour trouver vite
    // les masques douteux. Départ : par numéro, l'ordre naturel de l'analyse.
    maskTable_->horizontalHeader()->setSortIndicator(kColId, Qt::AscendingOrder);
    maskTable_->setSortingEnabled(true);
    connect(maskTable_, &QTableWidget::itemSelectionChanged, this,
            &AiSegmentationDialog::onTableSelectionChanged);
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

    // Ce que « Valider » produit : des régions éditables (la suite du flux normal) ou
    // directement les objets de broderie. Annoncé ici, pas découvert après coup.
    auto* outputBox = new QGroupBox(tr("Après validation, créer"), this);
    auto* outputLayout = new QVBoxLayout(outputBox);
    editableRegionsRadio_ =
        new QRadioButton(tr("Des régions éditables (puis fusionner, recolorer, vectoriser)"),
                         outputBox);
    editableRegionsRadio_->setObjectName(QStringLiteral("aiOutputRegionsRadio"));
    editableRegionsRadio_->setChecked(true);
    embroideryObjectsRadio_ =
        new QRadioButton(tr("Directement les objets de broderie (numérisation automatique)"),
                         outputBox);
    embroideryObjectsRadio_->setObjectName(QStringLiteral("aiOutputObjectsRadio"));
    outputLayout->addWidget(editableRegionsRadio_);
    outputLayout->addWidget(embroideryObjectsRadio_);
    auto* directOptions = new QWidget(outputBox);
    directOptions->setObjectName(QStringLiteral("aiDirectOptions"));
    auto* directLayout = new QVBoxLayout(directOptions);
    directLayout->setContentsMargins(24, 0, 0, 0);
    skipBackgroundCheck_ =
        new QCheckBox(tr("Ignorer la plus grande région (probablement le fond)"), directOptions);
    skipBackgroundCheck_->setObjectName(QStringLiteral("aiSkipBackgroundCheck"));
    directLayout->addWidget(skipBackgroundCheck_);
    auto* detailRow = new QHBoxLayout;
    detailRow->addWidget(new QLabel(tr("Détail vectorisation :"), directOptions));
    detailRow->addWidget(new QLabel(tr("Faible"), directOptions));
    vectorDetailSlider_ = new QSlider(Qt::Horizontal, directOptions);
    vectorDetailSlider_->setObjectName(QStringLiteral("aiVectorDetailSlider"));
    vectorDetailSlider_->setRange(0, 100);
    vectorDetailSlider_->setValue(50);
    vectorDetailSlider_->setToolTip(
        tr("Niveau de détail des formes vectorisées : bas = contours lissés ; haut = plus "
           "fidèle aux pixels segmentés."));
    detailRow->addWidget(vectorDetailSlider_, 1);
    detailRow->addWidget(new QLabel(tr("Élevé"), directOptions));
    directLayout->addLayout(detailRow);
    outputLayout->addWidget(directOptions);
    directOptions->setEnabled(false);
    connect(embroideryObjectsRadio_, &QRadioButton::toggled, directOptions, &QWidget::setEnabled);
    mainLayout->addWidget(outputBox);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    validateButton_ = buttons_->addButton(tr("Valider"), QDialogButtonBox::AcceptRole);
    validateButton_->setEnabled(false);
    connect(validateButton_, &QPushButton::clicked, this, &AiSegmentationDialog::onValidateClicked);
    mainLayout->addWidget(buttons_);

    restoreChoices();
}

void AiSegmentationDialog::restoreChoices() {
    // Les réglages d'une analyse à l'autre : on ne repart pas des défauts à chaque ouverture.
    QSettings settings;
    const QString profile = settings.value(kSettingsPrefix + QStringLiteral("profile")).toString();
    if (const int index = profileCombo_->findData(profile); index >= 0) {
        profileCombo_->setCurrentIndex(index);
    }
    minIslandAreaSpin_->setValue(
        settings.value(kSettingsPrefix + QStringLiteral("minIsland"), minIslandAreaSpin_->value())
            .toDouble());
    minHoleAreaSpin_->setValue(
        settings.value(kSettingsPrefix + QStringLiteral("minHole"), minHoleAreaSpin_->value())
            .toDouble());
    colorRefineCheck_->setChecked(
        settings.value(kSettingsPrefix + QStringLiteral("colorRefine"), true).toBool());
    colorRefineColorsSpin_->setValue(
        settings.value(kSettingsPrefix + QStringLiteral("refineColors"), 8).toInt());
    colorRefineMinSizeSpin_->setValue(
        settings.value(kSettingsPrefix + QStringLiteral("refineMinSize"), 16).toInt());
    colorRefineColorsSpin_->setEnabled(colorRefineCheck_->isChecked());
    colorRefineMinSizeSpin_->setEnabled(colorRefineCheck_->isChecked());
    skipBackgroundCheck_->setChecked(
        settings.value(kSettingsPrefix + QStringLiteral("skipBackground"), false).toBool());
    vectorDetailSlider_->setValue(
        settings.value(kSettingsPrefix + QStringLiteral("vectorDetail"), 50).toInt());
    const bool direct =
        settings.value(kSettingsPrefix + QStringLiteral("output"), QStringLiteral("regions"))
            .toString() == QLatin1String("objects");
    embroideryObjectsRadio_->setChecked(direct);
    editableRegionsRadio_->setChecked(!direct);
}

void AiSegmentationDialog::saveChoices() const {
    QSettings settings;
    settings.setValue(kSettingsPrefix + QStringLiteral("profile"),
                      profileCombo_->currentData().toString());
    settings.setValue(kSettingsPrefix + QStringLiteral("minIsland"), minIslandAreaSpin_->value());
    settings.setValue(kSettingsPrefix + QStringLiteral("minHole"), minHoleAreaSpin_->value());
    settings.setValue(kSettingsPrefix + QStringLiteral("colorRefine"),
                      colorRefineCheck_->isChecked());
    settings.setValue(kSettingsPrefix + QStringLiteral("refineColors"),
                      colorRefineColorsSpin_->value());
    settings.setValue(kSettingsPrefix + QStringLiteral("refineMinSize"),
                      colorRefineMinSizeSpin_->value());
    settings.setValue(kSettingsPrefix + QStringLiteral("skipBackground"),
                      skipBackgroundCheck_->isChecked());
    settings.setValue(kSettingsPrefix + QStringLiteral("vectorDetail"),
                      vectorDetailSlider_->value());
    settings.setValue(kSettingsPrefix + QStringLiteral("output"),
                      embroideryObjectsRadio_->isChecked() ? QStringLiteral("objects")
                                                           : QStringLiteral("regions"));
}

AiSegmentationDialog::Output AiSegmentationDialog::output() const {
    return embroideryObjectsRadio_ != nullptr && embroideryObjectsRadio_->isChecked()
               ? Output::EmbroideryObjects
               : Output::EditableRegions;
}

bool AiSegmentationDialog::skipBackground() const {
    return skipBackgroundCheck_ != nullptr && skipBackgroundCheck_->isChecked();
}

int AiSegmentationDialog::vectorDetail() const {
    return vectorDetailSlider_ != nullptr ? vectorDetailSlider_->value() : 50;
}

void AiSegmentationDialog::setPreferences(AiPreferences prefs) {
    prefs_ = std::move(prefs);
    if (client_ != nullptr) {
        client_->configure(toWorkerConfig(prefs_));
    }
    clearError();
    setStatus(tr("Préférences mises à jour — cliquez sur Analyser."));
}

void AiSegmentationDialog::setStatus(const QString& text) {
    statusLabel_->setStyleSheet(QString());
    statusLabel_->setText(text);
}

void AiSegmentationDialog::setError(const QString& message, const QString& detail,
                                    bool configurationIssue) {
    errorDetail_ = detail;
    statusLabel_->setStyleSheet(
        QStringLiteral("color:%1;").arg(AppTheme::instance().tokens().error.name()));
    statusLabel_->setText(message);
    statusLabel_->setToolTip(detail);
    detailButton_->setVisible(true);
    preferencesButton_->setVisible(configurationIssue);
}

void AiSegmentationDialog::clearError() {
    errorDetail_.clear();
    statusLabel_->setStyleSheet(QString());
    statusLabel_->setToolTip(QString());
    detailButton_->setVisible(false);
    preferencesButton_->setVisible(false);
}

QString AiSegmentationDialog::jobDirPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
           QStringLiteral("/OpenStitch/ai-jobs/") + jobId_;
}

void AiSegmentationDialog::onAnalyzeClicked() {
    if (maskTable_->rowCount() > 0) {
        // Une nouvelle analyse remplace les masques : choix décochés, protections et fusions
        // seraient perdus sans le dire.
        const auto answer = QMessageBox::question(
            this, tr("Nouvelle analyse"),
            tr("Relancer l'analyse remplace les masques actuels : vos choix (masques décochés, "
               "protections, fusions) seront perdus. Continuer ?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    saveChoices();
    clearError();
    // Un identifiant de requête périmé (analyse précédente) ne doit jamais être celui que
    // « Annuler l'analyse » vise pendant le démarrage du worker.
    activeRequestId_.clear();
    analyzeButton_->setEnabled(false);
    cancelButton_->setEnabled(true);
    validateButton_->setEnabled(false);
    maskTable_->setRowCount(0);
    maskPixels_.clear();
    masks_ = {};
    previewLabel_->clear();
    selectionPreviewLabel_->clear();
    previewPixmap_ = QPixmap();
    selectionPreviewPixmap_ = QPixmap();
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
                                    static_cast<qint64>(encoded->size())) ==
                    static_cast<qint64>(encoded->size());
        }
    }
    if (!wrote) {
        setError(tr("Échec : impossible de préparer l'image de travail pour le worker."),
                 tr("Le dossier temporaire %1 n'est pas accessible en écriture, ou le disque est "
                    "plein.")
                     .arg(QDir::toNativeSeparators(jobDirPath())),
                 false);
        analyzeButton_->setEnabled(true);
        cancelButton_->setEnabled(false);
        progressBar_->setVisible(false);
        return;
    }

    if (client_ == nullptr) {
        client_ = new SamWorkerClient(this);
        connect(client_, &SamWorkerClient::stateChanged, this,
                &AiSegmentationDialog::onWorkerStateChanged);
        connect(client_, &SamWorkerClient::progress, this, &AiSegmentationDialog::onWorkerProgress);
        connect(client_, &SamWorkerClient::modelReady, this, &AiSegmentationDialog::onModelReady);
        connect(client_, &SamWorkerClient::segmentResult, this,
                &AiSegmentationDialog::onSegmentResult);
        connect(client_, &SamWorkerClient::workerError, this, &AiSegmentationDialog::onWorkerError);
        connect(client_, &SamWorkerClient::requestCancelled, this, [this](QString requestId) {
            Q_UNUSED(requestId);
            phase_ = Phase::Idle;
            activeRequestId_.clear();
            analyzeButton_->setEnabled(true);
            cancelButton_->setEnabled(false);
            progressBar_->setVisible(false);
            setStatus(tr("Analyse annulée."));
        });
        connect(client_, &SamWorkerClient::crashed, this, [this](QString detail) {
            setStatus(tr("Le worker s'est arrêté de façon inattendue (%1). Nouvelle tentative…")
                          .arg(detail));
        });
        client_->configure(toWorkerConfig(prefs_));
    }

    pendingModel_ = static_cast<ai_segmentation::ModelId>(modelCombo_->currentData().toInt());

    if (!client_->isConfigured()) {
        setError(tr("Configuration IA incomplète : il manque le chemin de Python, du worker ou "
                    "des modèles (ou la distribution WSL)."),
                 tr("Renseignez-les dans Préférences > Intelligence artificielle."), true);
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
    if (phase_ == Phase::StartingWorker) {
        // Pas encore de requête à annuler : le worker démarre (WSL peut mettre plusieurs
        // secondes). On abandonne l'analyse ; il finira de démarrer en arrière-plan, prêt pour
        // la suivante, et son état « prêt » ne lancera plus rien (la phase n'est plus
        // StartingWorker).
        phase_ = Phase::Idle;
        activeRequestId_.clear();
        analyzeButton_->setEnabled(true);
        cancelButton_->setEnabled(false);
        progressBar_->setVisible(false);
        setStatus(tr("Analyse annulée."));
        return;
    }
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
        activeRequestId_.clear();
        analyzeButton_->setEnabled(true);
        cancelButton_->setEnabled(false);
        progressBar_->setVisible(false);
        setError(tr("Le worker de segmentation IA n'est pas disponible : il n'a pas pu démarrer "
                    "ou s'est arrêté. Vérifiez les chemins (Python, worker, modèles) et la "
                    "distribution WSL."),
                 client_ != nullptr ? client_->stderrLog() : QString(), true);
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

void AiSegmentationDialog::onSegmentResult(QString requestId, QString jobDir, QString masksFile,
                                           int maskCount) {
    Q_UNUSED(requestId);
    Q_UNUSED(maskCount);
    phase_ = Phase::Idle;
    activeRequestId_.clear();
    analyzeButton_->setEnabled(true);
    cancelButton_->setEnabled(false);
    progressBar_->setVisible(false);

    QFile file(jobDir + QStringLiteral("/") + masksFile);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(tr("Échec : impossible de lire les résultats du worker."),
                 tr("Fichier introuvable ou illisible : %1")
                     .arg(QDir::toNativeSeparators(file.fileName())),
                 false);
        return;
    }
    const QByteArray content = file.readAll();
    const auto parsed = ai_segmentation::parse_masks_json(
        std::string_view(content.constData(), static_cast<std::size_t>(content.size())));
    if (!parsed) {
        setError(tr("Échec : résultat de segmentation invalide."),
                 QString::fromStdString(parsed.error().message), false);
        return;
    }
    masks_ = *parsed;
    setStatus(tr("%1 masque(s) proposé(s) — cochez ceux à conserver puis validez.")
                  .arg(masks_.masks.size()));

    const QPixmap preview(jobDir + QStringLiteral("/preview.png"));
    if (!preview.isNull()) {
        previewPixmap_ = preview;
    }
    loadMasksIntoTable();
    rescalePreviews();
}

void AiSegmentationDialog::onWorkerError(QString requestId, ai_segmentation::AiErrorCode code,
                                         QString message, QString detail) {
    Q_UNUSED(requestId);
    phase_ = Phase::Idle;
    activeRequestId_.clear();
    analyzeButton_->setEnabled(true);
    cancelButton_->setEnabled(false);
    progressBar_->setVisible(false);
    setError(message, detail, isConfigurationIssue(code));
}

QVector<std::uint8_t>
AiSegmentationDialog::loadMaskPixels(const ai_segmentation::MaskEntry& entry) const {
    const std::filesystem::path path(
        (jobDirPath() + QStringLiteral("/") + QString::fromStdString(entry.file)).toStdString());
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
    // Tri suspendu pendant le remplissage : une ligne insérée serait sinon déplacée avant que
    // ses autres cellules soient posées.
    maskTable_->setSortingEnabled(false);
    maskTable_->setRowCount(0);
    maskPixels_.clear();
    const double mm2PerPx = mmPerPx_.value * mmPerPx_.value;
    for (const auto& entry : masks_.masks) {
        maskPixels_.insert(entry.id, loadMaskPixels(entry));

        const int r = maskTable_->rowCount();
        maskTable_->insertRow(r);
        maskTable_->setItem(r, 0, makeCheckableItem(Qt::Checked));
        maskTable_->setItem(r, 1, makeNumberItem(entry.id, 0));
        maskTable_->setItem(
            r, 2, makeNumberItem(static_cast<double>(entry.area_pixels) * mm2PerPx, 2));
        maskTable_->setItem(r, 3, makeNumberItem(entry.predicted_iou, 2));
        maskTable_->setItem(r, 4, makeNumberItem(entry.stability_score, 2));
        maskTable_->setItem(r, 5, makeCheckableItem(Qt::Unchecked));
    }
    maskTable_->setSortingEnabled(true);
    validateButton_->setEnabled(maskTable_->rowCount() > 0);
}

void AiSegmentationDialog::onTableSelectionChanged() {
    updateSelectedMaskPreview();
}

void AiSegmentationDialog::updateSelectedMaskPreview() {
    const auto selectedRows = maskTable_->selectionModel() != nullptr
                                  ? maskTable_->selectionModel()->selectedRows()
                                  : QModelIndexList{};
    if (selectedRows.isEmpty()) {
        selectionPreviewLabel_->clear();
        selectionPreviewPixmap_ = QPixmap();
        return;
    }
    const int id = maskTable_->item(selectedRows.first().row(), kColId)->text().toInt();
    const auto pixelsIt = maskPixels_.constFind(id);
    if (pixelsIt == maskPixels_.constEnd() || pixelsIt->isEmpty()) {
        selectionPreviewLabel_->clear();
        selectionPreviewPixmap_ = QPixmap();
        return;
    }
    const int width = masks_.image_width;
    const int height = masks_.image_height;
    if (width <= 0 || height <= 0 || sourceImage_.width != width || sourceImage_.height != height) {
        selectionPreviewLabel_->clear();
        selectionPreviewPixmap_ = QPixmap();
        return;
    }
    // Écriture directe ligne par ligne : un setPixelColor par pixel coûtait plusieurs secondes
    // à chaque changement de sélection sur une image de 2000 x 2000.
    QImage image(width, height, QImage::Format_RGB888);
    const std::uint8_t* mask = pixelsIt->constData();
    for (int y = 0; y < height; ++y) {
        uchar* line = image.scanLine(y);
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        const std::uint8_t* src = sourceImage_.rgba.data() + row * 4;
        for (int x = 0; x < width; ++x, line += 3, src += 4) {
            if (mask[row + static_cast<std::size_t>(x)] != 0) {
                line[0] = 255;
                line[1] = 90;
                line[2] = 0;
            } else {
                const auto gray = static_cast<uchar>((src[0] + src[1] + src[2]) / 6);
                line[0] = gray;
                line[1] = gray;
                line[2] = gray;
            }
        }
    }
    selectionPreviewPixmap_ = QPixmap::fromImage(image);
    rescalePreviews();
}

void AiSegmentationDialog::rescalePreviews() {
    // Les aperçus suivent la taille de la fenêtre au lieu de rester à celle du premier calcul.
    const auto fit = [](QLabel* label, const QPixmap& source) {
        if (source.isNull()) {
            return;
        }
        label->setPixmap(
            source.scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };
    fit(previewLabel_, previewPixmap_);
    fit(selectionPreviewLabel_, selectionPreviewPixmap_);
}

void AiSegmentationDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    rescalePreviews();
}

void AiSegmentationDialog::reject() {
    // Fermer (bouton, Échap, croix) avec des masques en cours de revue perd les choix : le dire.
    if (!validating_ && maskTable_ != nullptr && maskTable_->rowCount() > 0 &&
        result() != QDialog::Accepted) {
        const auto answer = QMessageBox::question(
            this, tr("Fermer sans valider"),
            tr("Fermer maintenant abandonne les masques et vos choix (masques décochés, "
               "protections, fusions). Fermer quand même ?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    saveChoices();
    QDialog::reject();
}

void AiSegmentationDialog::onMergeClicked() {
    const auto selectedRows = maskTable_->selectionModel() != nullptr
                                  ? maskTable_->selectionModel()->selectedRows()
                                  : QModelIndexList{};
    if (selectedRows.size() < 2) {
        setStatus(tr("Sélectionnez au moins deux masques dans la liste pour les fusionner."));
        return;
    }

    QVector<int> ids;
    for (const auto& index : selectedRows) {
        ids.push_back(maskTable_->item(index.row(), kColId)->text().toInt());
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
    const auto mergedArea =
        static_cast<std::size_t>(std::count(merged.begin(), merged.end(), std::uint8_t{1}));

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
        masks_.masks.erase(std::remove_if(masks_.masks.begin(), masks_.masks.end(),
                                          [id](const auto& e) { return e.id == id; }),
                           masks_.masks.end());
        maskPixels_.remove(id);
    }

    QVector<int> rowIndices;
    for (const auto& index : selectedRows) {
        rowIndices.push_back(index.row());
    }
    std::sort(rowIndices.begin(), rowIndices.end(), std::greater<>());
    // Tri suspendu : les suppressions/insertions de lignes doivent viser les lignes d'origine.
    maskTable_->setSortingEnabled(false);
    for (const int r : rowIndices) {
        maskTable_->removeRow(r);
    }

    const int r = maskTable_->rowCount();
    maskTable_->insertRow(r);
    maskTable_->setItem(r, 0, makeCheckableItem(Qt::Checked));
    maskTable_->setItem(r, 1, makeNumberItem(newId, 0));
    const double mm2PerPx = mmPerPx_.value * mmPerPx_.value;
    maskTable_->setItem(r, 2, makeNumberItem(static_cast<double>(mergedArea) * mm2PerPx, 2));
    maskTable_->setItem(r, 3, makeNumberItem(maxIou, 2));
    maskTable_->setItem(r, 4, makeNumberItem(maxStability, 2));
    maskTable_->setItem(r, 5, makeCheckableItem(Qt::Unchecked));
    maskTable_->setSortingEnabled(true);

    setStatus(tr("%1 masques fusionnés en un seul (id %2).").arg(ids.size()).arg(newId));
}

void AiSegmentationDialog::onValidateClicked() {
    if (validating_) {
        return; // double-clic : un seul calcul à la fois
    }
    // Le calcul tourne sur le fil de l'interface : bouton grisé, curseur d'attente et message
    // AVANT de commencer (sinon la fenêtre semble gelée, et un second clic relance le calcul).
    validating_ = true;
    validateButton_->setEnabled(false);
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    setStatus(tr("Construction des régions…"));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const bool ok = buildValidatedSegmentation();
    QGuiApplication::restoreOverrideCursor();
    validating_ = false;
    if (ok) {
        saveChoices();
        accept();
        return;
    }
    validateButton_->setEnabled(maskTable_->rowCount() > 0); // échec : on peut corriger et retenter
}

bool AiSegmentationDialog::buildValidatedSegmentation() {
    std::vector<ai_segmentation::LabelMaskInput> inputs;
    for (int r = 0; r < maskTable_->rowCount(); ++r) {
        if (maskTable_->item(r, 0)->checkState() != Qt::Checked) {
            continue;
        }
        const int id = maskTable_->item(r, kColId)->text().toInt();
        const auto pixelsIt = maskPixels_.constFind(id);
        if (pixelsIt == maskPixels_.constEnd() || pixelsIt->isEmpty()) {
            continue;
        }

        ai_segmentation::LabelMaskInput input;
        input.mask_id = id;
        input.pixels.assign(pixelsIt->begin(), pixelsIt->end());
        input.rgb = {static_cast<std::uint8_t>((id * 53) % 180 + 60),
                     static_cast<std::uint8_t>((id * 97) % 180 + 60),
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
    // Ordre par numéro de masque, jamais par ordre d'affichage : trier le tableau ne doit pas
    // changer le résultat (départage des recouvrements, identifiants de régions).
    std::sort(inputs.begin(), inputs.end(),
              [](const auto& a, const auto& b) { return a.mask_id < b.mask_id; });

    if (inputs.empty()) {
        setStatus(tr("Cochez au moins un masque à conserver avant de valider."));
        return false;
    }

    const ai_segmentation::LabelMapOptions labelMapOptions{masks_.image_width, masks_.image_height};
    auto labelMap = ai_segmentation::build_label_map(inputs, labelMapOptions);
    if (!labelMap) {
        setError(tr("Échec de la construction de la carte de labels."),
                 QString::fromStdString(labelMap.error().message), false);
        return false;
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
        setError(tr("Échec du nettoyage topologique."),
                 QString::fromStdString(report.error().message), false);
        return false;
    }

    validationReport_ = *report;
    clearError();

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
        auto refined =
            ai_segmentation::refine_label_map_by_color(*labelMap, sourceImage_, colorOptions);
        if (refined) {
            validatedSegmentation_ = std::move(*refined);
            return true;
        }
        setStatus(tr("Découpage par couleur impossible (%1) : formes IA conservées telles quelles.")
                      .arg(QString::fromStdString(refined.error().message)));
    }

    validatedSegmentation_ = std::move(*labelMap);
    return true;
}

std::optional<segmentation::Segmentation> AiSegmentationDialog::takeSegmentation() {
    return std::move(validatedSegmentation_);
}

} // namespace openstitch::desktop
