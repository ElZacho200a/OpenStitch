// SPDX-License-Identifier: Apache-2.0
#include "ai_preferences_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <functional>

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

}  // namespace

AiPreferencesDialog::AiPreferencesDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Préférences — Intelligence artificielle"));
    resize(560, 520);

    auto* mainLayout = new QVBoxLayout(this);

    enabledCheck_ = new QCheckBox(tr("Activer la segmentation par IA (SAM 2)"), this);
    mainLayout->addWidget(enabledCheck_);

    auto* runtimeGroup = new QGroupBox(tr("Environnement d'exécution"), this);
    auto* runtimeForm = new QFormLayout(runtimeGroup);
    runtimeCombo_ = new QComboBox(runtimeGroup);
    runtimeCombo_->addItem(tr("WSL"), static_cast<int>(AiRuntimeKind::WslPython));
    runtimeCombo_->addItem(tr("Python natif (sans WSL)"), static_cast<int>(AiRuntimeKind::NativePython));
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
            QString::fromUtf8(descriptor.display_name.data(), static_cast<qsizetype>(descriptor.display_name.size())),
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
    keepDiagnosticsCheck_ = new QCheckBox(tr("Conserver les fichiers de diagnostic"), defaultsGroup);
    defaultsForm->addRow(keepDiagnosticsCheck_);
    logLevelCombo_ = new QComboBox(defaultsGroup);
    logLevelCombo_->addItems({QStringLiteral("DEBUG"), QStringLiteral("INFO"), QStringLiteral("WARNING"),
                              QStringLiteral("ERROR")});
    defaultsForm->addRow(tr("Niveau de journalisation du worker :"), logLevelCombo_);
    mainLayout->addWidget(defaultsGroup);

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
        prefs.defaultDevice == QStringLiteral("cpu") ? 1 : (prefs.defaultDevice == QStringLiteral("cuda") ? 2 : 0));
    maxResolutionSpin_->setValue(prefs.maxAnalysisResolution);
    keepDiagnosticsCheck_->setChecked(prefs.keepDiagnosticFiles);
    logLevelCombo_->setCurrentText(prefs.logLevel);
}

AiPreferences AiPreferencesDialog::preferences() const {
    AiPreferences prefs;
    prefs.enabled = enabledCheck_->isChecked();
    prefs.runtime = runtimeCombo_->currentData().toInt() == static_cast<int>(AiRuntimeKind::NativePython)
                        ? AiRuntimeKind::NativePython
                        : AiRuntimeKind::WslPython;
    prefs.wslDistro = wslDistroEdit_->text();
    prefs.venvPythonPath = venvPythonEdit_->text();
    prefs.workerScriptPath = workerScriptEdit_->text();
    prefs.modelsDir = modelsDirEdit_->text();
    prefs.defaultModel = static_cast<ai_segmentation::ModelId>(defaultModelCombo_->currentData().toInt());
    const int deviceIndex = defaultDeviceCombo_->currentIndex();
    prefs.defaultDevice =
        deviceIndex == 1 ? QStringLiteral("cpu") : (deviceIndex == 2 ? QStringLiteral("cuda") : QStringLiteral("auto"));
    prefs.maxAnalysisResolution = maxResolutionSpin_->value();
    prefs.keepDiagnosticFiles = keepDiagnosticsCheck_->isChecked();
    prefs.logLevel = logLevelCombo_->currentText();
    return prefs;
}

void AiPreferencesDialog::browseVenvPython() {
    const QString picked = QFileDialog::getOpenFileName(this, tr("Sélectionner l'interpréteur Python du venv"));
    if (!picked.isEmpty()) {
        venvPythonEdit_->setText(picked);
    }
}

void AiPreferencesDialog::browseWorkerScript() {
    const QString picked = QFileDialog::getOpenFileName(this, tr("Sélectionner openstitch_sam_worker.py"),
                                                         {}, tr("Script Python (*.py)"));
    if (!picked.isEmpty()) {
        workerScriptEdit_->setText(picked);
    }
}

void AiPreferencesDialog::browseModelsDir() {
    const QString picked = QFileDialog::getExistingDirectory(this, tr("Sélectionner le dossier des modèles"));
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

    connect(testClient_, &SamWorkerClient::stateChanged, this, [this](SamWorkerClient::State state) {
        if (state == SamWorkerClient::State::Ready) {
            appendTestLog(tr("Worker démarré : protocole JSON Lines opérationnel."));
        } else if (state == SamWorkerClient::State::Unavailable) {
            appendTestLog(tr("Échec : le worker n'a pas pu démarrer."));
        }
    });
    connect(testClient_, &SamWorkerClient::workerError, this,
            [this](QString requestId, ai_segmentation::AiErrorCode code, QString message, QString detail) {
                Q_UNUSED(requestId);
                appendTestLog(tr("Erreur [%1] : %2")
                                  .arg(QString::fromStdString(ai_segmentation::ai_error_code_name(code)), message));
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
        appendTestLog(tr("Configuration incomplète : renseignez tous les chemins avant de tester."));
        return;
    }
    testClient_->start();
}

}  // namespace openstitch::desktop
