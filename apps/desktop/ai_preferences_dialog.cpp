// SPDX-License-Identifier: Apache-2.0
#include "ai_preferences_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <functional>

#include "busy_indicator.hpp"
#include "openstitch/ai_segmentation/backend_run.hpp"
#include "responsive.hpp"
#include "sam_worker_client.hpp"

namespace openstitch::desktop {

namespace {

QLineEdit* makePathRow(QFormLayout* form, const QString& label, QWidget* parent,
                       const std::function<void()>& onBrowse) {
    auto* edit = new QLineEdit(parent);
    auto* browse = new QPushButton(QObject::tr("Parcourir…"), parent);
    QObject::connect(browse, &QPushButton::clicked, parent, onBrowse);
    auto* row = new QHBoxLayout;
    row->addWidget(edit);
    row->addWidget(browse);
    form->addRow(label, row);
    return edit;
}

} // namespace

AiPreferencesDialog::AiPreferencesDialog(QWidget* parent)
    : QDialog(parent), transportFactory_(ai_segmentation::process_transport_factory()) {
    setWindowTitle(tr("Préférences — Intelligence artificielle"));
    resize(560, 520);

    auto* mainLayout = new QVBoxLayout(this);

    enabledCheck_ = new QCheckBox(tr("Activer la segmentation par IA (SAM 2)"), this);
    mainLayout->addWidget(enabledCheck_);

    auto* runtimeGroup = new QGroupBox(tr("Environnement d'exécution"), this);
    auto* runtimeForm = new QFormLayout(runtimeGroup);
    runtimeCombo_ = new QComboBox(runtimeGroup);
    runtimeCombo_->addItem(tr("WSL"), static_cast<int>(AiRuntimeKind::WslPython));
    runtimeCombo_->addItem(tr("Python natif (sans WSL)"),
                           static_cast<int>(AiRuntimeKind::NativePython));
    runtimeForm->addRow(tr("Environnement :"), runtimeCombo_);
    wslDistroEdit_ = new QLineEdit(runtimeGroup);
    wslDistroEdit_->setPlaceholderText(tr("ex. Ubuntu"));
    runtimeForm->addRow(tr("Distribution WSL :"), wslDistroEdit_);
    venvPythonEdit_ = makePathRow(runtimeForm, tr("Python du venv worker :"), runtimeGroup,
                                  [this] { browseVenvPython(); });
    workerScriptEdit_ = makePathRow(runtimeForm, tr("Script du worker :"), runtimeGroup,
                                    [this] { browseWorkerScript(); });
    modelsDirEdit_ = makePathRow(runtimeForm, tr("Dossier des modèles :"), runtimeGroup,
                                 [this] { browseModelsDir(); });
    mainLayout->addWidget(runtimeGroup);

    auto* defaultsGroup = new QGroupBox(tr("Valeurs par défaut"), this);
    auto* defaultsForm = new QFormLayout(defaultsGroup);
    defaultModelCombo_ = new QComboBox(defaultsGroup);
    for (const auto& descriptor : ai_segmentation::all_models()) {
        defaultModelCombo_->addItem(
            QString::fromUtf8(descriptor.display_name.data(),
                              static_cast<qsizetype>(descriptor.display_name.size())),
            static_cast<int>(descriptor.id));
    }
    defaultsForm->addRow(tr("Modèle par défaut :"), defaultModelCombo_);
    defaultDeviceCombo_ = new QComboBox(defaultsGroup);
    defaultDeviceCombo_->addItems({tr("Automatique"), tr("CPU"), tr("GPU (CUDA)")});
    defaultsForm->addRow(tr("Processeur :"), defaultDeviceCombo_);
    maxResolutionSpin_ = new QSpinBox(defaultsGroup);
    maxResolutionSpin_->setRange(256, 8192);
    maxResolutionSpin_->setSingleStep(128);
    maxResolutionSpin_->setSuffix(tr(" px"));
    defaultsForm->addRow(tr("Résolution maximale d'analyse :"), maxResolutionSpin_);
    keepDiagnosticsCheck_ =
        new QCheckBox(tr("Conserver les fichiers de diagnostic"), defaultsGroup);
    defaultsForm->addRow(keepDiagnosticsCheck_);
    logLevelCombo_ = new QComboBox(defaultsGroup);
    logLevelCombo_->addItems({QStringLiteral("DEBUG"), QStringLiteral("INFO"),
                              QStringLiteral("WARNING"), QStringLiteral("ERROR")});
    defaultsForm->addRow(tr("Niveau de journalisation du worker :"), logLevelCombo_);
    mainLayout->addWidget(defaultsGroup);

    auto* backendsGroup = new QGroupBox(tr("Backends IA (openstitch.ai.backend/1)"), this);
    backendsGroup->setObjectName(QStringLiteral("aiBackendsGroup"));
    auto* backendsForm = new QFormLayout(backendsGroup);
    auto* backendsNote = new QLabel(
        tr("Les backends (SAM 2.1, HQ-SAM 2, Florence-2…) sont déclarés dans un fichier JSON que "
           "vous maîtrisez : seule la commande qu'il contient est lancée. OpenStitch ne télécharge "
           "ni n'installe aucun modèle ; si un backend est absent, la segmentation couleur "
           "(CIELAB) prend le relais."),
        backendsGroup);
    backendsNote->setWordWrap(true);
    backendsForm->addRow(backendsNote);
    backendsFileEdit_ = makePathRow(backendsForm, tr("Fichier de backends :"), backendsGroup,
                                    [this] { browseBackendsFile(); });
    backendsFileEdit_->setObjectName(QStringLiteral("aiBackendsFileEdit"));
    backendsFileEdit_->setPlaceholderText(tr("vide = variable OPENSTITCH_AI_BACKENDS"));
    engineCombo_ = new QComboBox(backendsGroup);
    engineCombo_->setObjectName(QStringLiteral("aiDefaultEngineCombo"));
    backendsForm->addRow(tr("Moteur par défaut :"), engineCombo_);
    auto* probeButton = new QPushButton(tr("Sonder les backends"), backendsGroup);
    probeButton->setObjectName(QStringLiteral("aiProbeBackendsButton"));
    connect(probeButton, &QPushButton::clicked, this, &AiPreferencesDialog::probeBackends);
    backendsForm->addRow(probeButton);
    connect(backendsFileEdit_, &QLineEdit::editingFinished, this,
            [this] { refreshEngineChoices(engineCombo_->currentData().toString()); });
    mainLayout->addWidget(backendsGroup);

    testButton_ = new QPushButton(tr("Tester la configuration"), this);
    connect(testButton_, &QPushButton::clicked, this, &AiPreferencesDialog::testConfiguration);
    mainLayout->addWidget(testButton_);
    testLog_ = new QPlainTextEdit(this);
    testLog_->setReadOnly(true);
    testLog_->setMaximumBlockCount(200);
    testLog_->setPlaceholderText(tr("Le résultat du test de configuration s'affichera ici…"));
    mainLayout->addWidget(testLog_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
    // Le groupe « Backends IA » allonge le dialogue : le contenu défile, OK / Annuler restent
    // visibles (HP-UX-025).
    scrollDialogBody(this, buttons);
    fitDialogToScreen(this);
}

AiPreferencesDialog::~AiPreferencesDialog() {
    if (testClient_ != nullptr) {
        testClient_->stop();
    }
}

void AiPreferencesDialog::setPreferences(const AiPreferences& prefs) {
    enabledCheck_->setChecked(prefs.enabled);
    runtimeCombo_->setCurrentIndex(prefs.runtime == AiRuntimeKind::NativePython ? 1 : 0);
    wslDistroEdit_->setText(prefs.wslDistro);
    venvPythonEdit_->setText(prefs.venvPythonPath);
    workerScriptEdit_->setText(prefs.workerScriptPath);
    modelsDirEdit_->setText(prefs.modelsDir);
    defaultModelCombo_->setCurrentIndex(static_cast<int>(prefs.defaultModel));
    defaultDeviceCombo_->setCurrentIndex(
        prefs.defaultDevice == QStringLiteral("cpu")
            ? 1
            : (prefs.defaultDevice == QStringLiteral("cuda") ? 2 : 0));
    maxResolutionSpin_->setValue(prefs.maxAnalysisResolution);
    keepDiagnosticsCheck_->setChecked(prefs.keepDiagnosticFiles);
    logLevelCombo_->setCurrentText(prefs.logLevel);
    backendsFileEdit_->setText(prefs.backendsFile);
    refreshEngineChoices(prefs.engine);
}

void AiPreferencesDialog::setTransportFactory(ai_segmentation::TransportFactory factory) {
    transportFactory_ = std::move(factory);
}

void AiPreferencesDialog::refreshEngineChoices(const QString& wanted) {
    AiPreferences current;
    current.backendsFile = backendsFileEdit_->text();
    QString error;
    const auto settings = loadBackendSettings(current, &error);
    engineCombo_->clear();
    engineCombo_->addItem(tr("SAM 2.1 — worker historique (WSL/Python)"),
                          AiEngine{AiEngine::Kind::Legacy, {}}.toString());
    for (const auto& backend : settings.backends) {
        engineCombo_->addItem(
            QString::fromStdString(backend.display_name.empty() ? backend.id
                                                                : backend.display_name),
            AiEngine{AiEngine::Kind::Backend, QString::fromStdString(backend.id)}.toString());
    }
    engineCombo_->addItem(tr("Segmentation couleur (CIELAB, sans IA)"),
                          AiEngine{AiEngine::Kind::Classical, {}}.toString());
    const int index = engineCombo_->findData(wanted);
    engineCombo_->setCurrentIndex(index >= 0 ? index : 0);
    if (!error.isEmpty()) {
        appendTestLog(error);
    }
}

void AiPreferencesDialog::browseBackendsFile() {
    const QString picked = QFileDialog::getOpenFileName(
        this, tr("Sélectionner le fichier de backends IA"), {}, tr("JSON (*.json)"));
    if (!picked.isEmpty()) {
        backendsFileEdit_->setText(picked);
        refreshEngineChoices(engineCombo_->currentData().toString());
    }
}

void AiPreferencesDialog::probeBackends() {
    testLog_->clear();
    AiPreferences current;
    current.backendsFile = backendsFileEdit_->text();
    QString error;
    const auto settings = loadBackendSettings(current, &error);
    if (!error.isEmpty()) {
        appendTestLog(error);
        return;
    }
    if (settings.backends.empty()) {
        appendTestLog(tr("Aucun backend déclaré : seuls le worker historique et la segmentation "
                         "couleur (CIELAB) sont disponibles."));
        return;
    }
    ai_segmentation::SessionOptions options;
    options.start_timeout = std::chrono::seconds{60};
    std::vector<ai_segmentation::BackendStatus> statuses;
    {
        BusyIndicator busy(this, tr("Sonde des backends IA…"));
        statuses = busy.run([&] {
            return ai_segmentation::discover_backends(settings, transportFactory_, options);
        });
    }
    for (const auto& status : statuses) {
        appendTestLog(QString::fromStdString(ai_segmentation::backend_status_line(status)));
    }
    const auto engine = AiEngine::fromString(engineCombo_->currentData().toString());
    if (engine.kind == AiEngine::Kind::Backend) {
        const auto resolution = ai_segmentation::resolve_backend(
            statuses, engine.backendId.toStdString(), ai_segmentation::PromptKind::Automatic);
        appendTestLog(QString::fromStdString(resolution.reason));
    }
}

AiPreferences AiPreferencesDialog::preferences() const {
    AiPreferences prefs;
    prefs.enabled = enabledCheck_->isChecked();
    prefs.runtime =
        runtimeCombo_->currentData().toInt() == static_cast<int>(AiRuntimeKind::NativePython)
            ? AiRuntimeKind::NativePython
            : AiRuntimeKind::WslPython;
    prefs.wslDistro = wslDistroEdit_->text();
    prefs.venvPythonPath = venvPythonEdit_->text();
    prefs.workerScriptPath = workerScriptEdit_->text();
    prefs.modelsDir = modelsDirEdit_->text();
    prefs.defaultModel =
        static_cast<ai_segmentation::ModelId>(defaultModelCombo_->currentData().toInt());
    const int deviceIndex = defaultDeviceCombo_->currentIndex();
    prefs.defaultDevice =
        deviceIndex == 1 ? QStringLiteral("cpu")
                         : (deviceIndex == 2 ? QStringLiteral("cuda") : QStringLiteral("auto"));
    prefs.maxAnalysisResolution = maxResolutionSpin_->value();
    prefs.keepDiagnosticFiles = keepDiagnosticsCheck_->isChecked();
    prefs.logLevel = logLevelCombo_->currentText();
    prefs.backendsFile = backendsFileEdit_->text().trimmed();
    prefs.engine = engineCombo_->currentData().toString();
    return prefs;
}

void AiPreferencesDialog::browseVenvPython() {
    const QString picked =
        QFileDialog::getOpenFileName(this, tr("Sélectionner l'interpréteur Python du venv"));
    if (!picked.isEmpty()) {
        venvPythonEdit_->setText(picked);
    }
}

void AiPreferencesDialog::browseWorkerScript() {
    const QString picked = QFileDialog::getOpenFileName(
        this, tr("Sélectionner openstitch_sam_worker.py"), {}, tr("Script Python (*.py)"));
    if (!picked.isEmpty()) {
        workerScriptEdit_->setText(picked);
    }
}

void AiPreferencesDialog::browseModelsDir() {
    const QString picked =
        QFileDialog::getExistingDirectory(this, tr("Sélectionner le dossier des modèles"));
    if (!picked.isEmpty()) {
        modelsDirEdit_->setText(picked);
    }
}

void AiPreferencesDialog::appendTestLog(const QString& line) {
    testLog_->appendPlainText(line);
}

void AiPreferencesDialog::testConfiguration() {
    if (testClient_ != nullptr) {
        testClient_->stop();
        testClient_->deleteLater();
    }
    testClient_ = new SamWorkerClient(this);
    testLog_->clear();
    appendTestLog(tr("Démarrage du worker de test…"));

    connect(testClient_, &SamWorkerClient::stateChanged, this,
            [this](SamWorkerClient::State state) {
                if (state == SamWorkerClient::State::Ready) {
                    appendTestLog(tr("Worker démarré : protocole JSON Lines opérationnel."));
                } else if (state == SamWorkerClient::State::Unavailable) {
                    appendTestLog(tr("Échec : le worker n'a pas pu démarrer."));
                }
            });
    connect(testClient_, &SamWorkerClient::workerError, this,
            [this](QString requestId, ai_segmentation::AiErrorCode code, QString message,
                   QString detail) {
                Q_UNUSED(requestId);
                appendTestLog(
                    tr("Erreur [%1] : %2")
                        .arg(QString::fromStdString(ai_segmentation::ai_error_code_name(code)),
                             message));
                if (!detail.isEmpty()) {
                    appendTestLog(tr("  détail : %1").arg(detail));
                }
            });
    connect(testClient_, &SamWorkerClient::crashed, this, [this](QString detail) {
        appendTestLog(tr("Le worker de test s'est arrêté de façon inattendue (%1).").arg(detail));
    });

    const AiPreferences prefs = preferences();
    testClient_->configure(toWorkerConfig(prefs));
    if (!testClient_->isConfigured()) {
        appendTestLog(
            tr("Configuration incomplète : renseignez tous les chemins avant de tester."));
        return;
    }
    testClient_->start();
}

} // namespace openstitch::desktop
