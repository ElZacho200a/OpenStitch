// SPDX-License-Identifier: Apache-2.0
#include "export_options_dialog.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "export_settings.hpp"

namespace openstitch::desktop_v2 {

namespace {
QString fromUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

QDoubleSpinBox* mmSpin(QWidget* parent, double lo, double hi, double step, int decimals = 2) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(lo, hi);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setSuffix(QObject::tr(" mm"));
    return spin;
}
} // namespace

ExportOptionsDialog::ExportOptionsDialog(DocumentSession& session,
                                         std::filesystem::path hoopStoreFile, QWidget* parent)
    : QDialog(parent), session_(session), hoopStoreFile_(std::move(hoopStoreFile)) {
    if (!hoopStoreFile_.empty()) {
        userHoops_ = loadUserHoops(hoopStoreFile_);
    }
    setObjectName("exportOptionsDialog");
    setWindowTitle(tr("Options machine"));
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    root->addLayout(form);

    machine_ = new QComboBox(this);
    machine_->setObjectName("exportMachine");
    machine_->addItem(tr("Aucune (pas de vérification)"), QString());
    for (const auto& m : machineChoices()) {
        machine_->addItem(fromUtf8(m.name), fromUtf8(m.id));
    }
    form->addRow(tr("Machine :"), machine_);

    hoop_ = new QComboBox(this);
    hoop_->setObjectName("exportHoop");
    rebuildHoopChoices();
    form->addRow(tr("Cadre :"), hoop_);
    auto* mine = new QHBoxLayout;
    saveHoop_ = new QPushButton(tr("Enregistrer le cadre actuel…"), this);
    saveHoop_->setObjectName("exportSaveHoop");
    removeHoop_ = new QPushButton(tr("Supprimer ce cadre perso"), this);
    removeHoop_->setObjectName("exportRemoveHoop");
    saveHoop_->setEnabled(!hoopStoreFile_.empty());
    removeHoop_->setEnabled(false);
    mine->addWidget(saveHoop_);
    mine->addWidget(removeHoop_);
    form->addRow(QString(), mine);
    margin_ = mmSpin(this, 0.0, 100.0, 0.5, 1);
    form->addRow(tr("Marge de sécurité :"), margin_);

    trim_ = mmSpin(this, 0.5, 100.0, 0.5, 1);
    form->addRow(tr("Coupe au-delà d'un saut de :"), trim_);
    trimColor_ = new QCheckBox(tr("Couper avant chaque changement de fil"), this);
    form->addRow(trimColor_);
    lock_ = new QComboBox(this);
    for (int k = 0; k <= 3; ++k) {
        lock_->addItem(fromUtf8(lockKindLabel(static_cast<LockKind>(k))));
    }
    form->addRow(tr("Points d'arrêt :"), lock_);
    lockLength_ = mmSpin(this, 0.2, 3.0, 0.1, 1);
    form->addRow(tr("Longueur des points d'arrêt :"), lockLength_);
    lockPasses_ = new QSpinBox(this);
    lockPasses_->setRange(1, 6);
    form->addRow(tr("Passes des points d'arrêt :"), lockPasses_);
    filterShort_ = new QCheckBox(tr("Fusionner les points trop courts"), this);
    form->addRow(filterShort_);
    minStitch_ = mmSpin(this, 0.1, 2.0, 0.1, 1);
    form->addRow(tr("Point minimal :"), minStitch_);
    splitLong_ = new QCheckBox(tr("Découper les points trop longs"), this);
    form->addRow(splitLong_);
    maxStitch_ = mmSpin(this, 2.0, 12.1, 0.5, 1);
    form->addRow(tr("Point maximal :"), maxStitch_);
    autoJoin_ = new QCheckBox(tr("Choisir automatiquement entrée et sortie des objets"), this);
    form->addRow(autoJoin_);

    hasStart_ = new QCheckBox(tr("Point de départ imposé"), this);
    startX_ = mmSpin(this, -1000.0, 1000.0, 1.0, 1);
    startY_ = mmSpin(this, -1000.0, 1000.0, 1.0, 1);
    form->addRow(hasStart_);
    form->addRow(tr("Départ X / Y :"), startX_);
    form->addRow(QString(), startY_);
    hasEnd_ = new QCheckBox(tr("Point d'arrivée imposé"), this);
    endX_ = mmSpin(this, -1000.0, 1000.0, 1.0, 1);
    endY_ = mmSpin(this, -1000.0, 1000.0, 1.0, 1);
    form->addRow(hasEnd_);
    form->addRow(tr("Arrivée X / Y :"), endX_);
    form->addRow(QString(), endY_);

    check_ = new QLabel(this);
    check_->setObjectName("exportCheck");
    check_->setWordWrap(true);
    feedback_ = new QLabel(this);
    feedback_->setObjectName("exportFeedback");
    feedback_->setWordWrap(true);
    root->addWidget(check_);
    root->addWidget(feedback_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    connect(hoop_, &QComboBox::currentIndexChanged, this, [this] {
        removeHoop_->setEnabled(hoop_->currentData().toString().startsWith(QLatin1String("user:")));
    });
    connect(saveHoop_, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Mes cadres"), tr("Nom du cadre :"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok) {
            const QString refusal = saveCurrentHoopAs(name);
            feedback_->setText(refusal.isEmpty() ? tr("Cadre « %1 » enregistré.").arg(name)
                                                 : refusal);
        }
    });
    connect(removeHoop_, &QPushButton::clicked, this, [this] {
        const QString key = hoop_->currentData().toString();
        if (key.startsWith(QLatin1String("user:"))) {
            const QString refusal = removeUserHoopNamed(key.mid(5));
            feedback_->setText(refusal.isEmpty() ? tr("Cadre supprimé de « Mes cadres ».")
                                                 : refusal);
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
        const QString refusal = apply();
        feedback_->setText(refusal.isEmpty() ? tr("Options appliquées (Ctrl+Z pour annuler).")
                                             : refusal);
    });
    root->addWidget(buttons);
    load();
}

void ExportOptionsDialog::load() {
    loading_ = true;
    const auto& project = session_.project();
    const auto o = exportOptionsOf(project);
    machine_->setCurrentIndex(
        std::max(0, machine_->findData(QString::fromStdString(project.machine_id))));
    hoop_->setCurrentIndex(std::max(0, hoop_->findData(fromUtf8(currentHoopId(project)))));
    margin_->setValue(currentMarginMm(project));
    trim_->setValue(o.trimThresholdMm);
    trimColor_->setChecked(o.trimBeforeColorChange);
    lock_->setCurrentIndex(static_cast<int>(o.lockKind));
    lockLength_->setValue(o.lockLengthMm);
    lockPasses_->setValue(o.lockPasses);
    filterShort_->setChecked(o.filterShortStitches);
    minStitch_->setValue(o.minStitchMm);
    splitLong_->setChecked(o.splitLongStitches);
    maxStitch_->setValue(o.maxStitchMm);
    autoJoin_->setChecked(o.autoJoin);
    hasStart_->setChecked(o.designStart.has_value());
    startX_->setValue(o.designStart ? o.designStart->x : 0.0);
    startY_->setValue(o.designStart ? o.designStart->y : 0.0);
    hasEnd_->setChecked(o.designEnd.has_value());
    endX_->setValue(o.designEnd ? o.designEnd->x : 0.0);
    endY_->setValue(o.designEnd ? o.designEnd->y : 0.0);
    loading_ = false;
    refreshCheck();
}

void ExportOptionsDialog::refreshCheck() {
    check_->setText(fromUtf8(checkExport(session_.project()).summary()));
}

QString ExportOptionsDialog::apply() {
    // Machine d'abord, puis cadre, puis finitions : chaque groupe est UNE commande annulable ;
    // un groupe inchangé ne laisse aucune trace. Les refus de « déjà réglé » ne sont pas des
    // erreurs pour l'utilisateur ; les autres remontent.
    QString problem;
    const auto note = [&problem](const ServiceResult& r) {
        if (!r.ok && problem.isEmpty() && r.message.find("déjà") == std::string::npos) {
            problem = QString::fromStdString(r.message);
        }
    };
    note(applyMachine(session_, machine_->currentData().toString().toStdString()));
    if (const QString hoopId = hoop_->currentData().toString();
        hoopId.startsWith(QLatin1String("user:"))) {
        if (const auto* mine = findUserHoop(userHoops_, hoopId.mid(5).toStdString())) {
            note(applyUserHoop(session_, *mine)); // sa propre marge : celle enregistrée
        }
    } else if (!hoopId.isEmpty()) {
        note(applyHoop(session_, hoopId.toStdString(), margin_->value()));
    }
    ExportOptions o = exportOptionsOf(session_.project());
    o.trimThresholdMm = trim_->value();
    o.trimBeforeColorChange = trimColor_->isChecked();
    o.lockKind = static_cast<LockKind>(lock_->currentIndex());
    o.lockLengthMm = lockLength_->value();
    o.lockPasses = lockPasses_->value();
    o.filterShortStitches = filterShort_->isChecked();
    o.minStitchMm = minStitch_->value();
    o.splitLongStitches = splitLong_->isChecked();
    o.maxStitchMm = maxStitch_->value();
    o.autoJoin = autoJoin_->isChecked();
    o.designStart = hasStart_->isChecked()
                        ? std::optional<PointMm>{{startX_->value(), startY_->value()}}
                        : std::nullopt;
    o.designEnd = hasEnd_->isChecked() ? std::optional<PointMm>{{endX_->value(), endY_->value()}}
                                       : std::nullopt;
    note(setExportOptions(session_, o));
    refreshCheck();
    return problem;
}

void ExportOptionsDialog::rebuildHoopChoices() {
    const QString keep = hoop_->currentData().toString();
    hoop_->blockSignals(true);
    hoop_->clear();
    hoop_->addItem(tr("Personnalisé (inchangé)"), QString());
    for (const auto& h : hoopChoices()) {
        hoop_->addItem(fromUtf8(h.family) + " — " + fromUtf8(h.name), fromUtf8(h.id));
    }
    for (const auto& h : userHoops_) {
        hoop_->addItem(tr("Mes cadres — %1").arg(fromUtf8(h.name)), "user:" + fromUtf8(h.name));
    }
    hoop_->setCurrentIndex(std::max(0, hoop_->findData(keep)));
    hoop_->blockSignals(false);
}

bool ExportOptionsDialog::persistUserHoops() {
    return hoopStoreFile_.empty() || saveUserHoops(hoopStoreFile_, userHoops_);
}

QString ExportOptionsDialog::saveCurrentHoopAs(const QString& name) {
    if (hoopStoreFile_.empty()) {
        return tr("Les cadres personnels ne sont pas disponibles (dossier de préférences absent).");
    }
    const QString trimmed = name.trimmed();
    auto next = userHoops_;
    const auto r =
        addUserHoop(next, userHoopFromProject(session_.project(), trimmed.toStdString()));
    if (!r.ok) {
        return fromUtf8(r.message);
    }
    const auto previous = std::exchange(userHoops_, std::move(next));
    if (!persistUserHoops()) {
        userHoops_ = previous; // rien n'est promis si le fichier n'a pas pu être écrit
        return tr("Écriture impossible : « Mes cadres » n'a pas été modifié.");
    }
    rebuildHoopChoices();
    hoop_->setCurrentIndex(std::max(0, hoop_->findData("user:" + trimmed)));
    return {};
}

QString ExportOptionsDialog::removeUserHoopNamed(const QString& name) {
    auto next = userHoops_;
    if (!removeUserHoop(next, name.toStdString())) {
        return tr("Ce cadre n'est pas dans « Mes cadres ».");
    }
    const auto previous = std::exchange(userHoops_, std::move(next));
    if (!persistUserHoops()) {
        userHoops_ = previous;
        return tr("Écriture impossible : « Mes cadres » n'a pas été modifié.");
    }
    rebuildHoopChoices();
    return {};
}

void ExportOptionsDialog::chooseHoop(const QString& hoopId, double marginMm) {
    hoop_->setCurrentIndex(std::max(0, hoop_->findData(hoopId)));
    margin_->setValue(marginMm);
}

void ExportOptionsDialog::chooseMachine(const QString& machineId) {
    machine_->setCurrentIndex(std::max(0, machine_->findData(machineId)));
}

void ExportOptionsDialog::setTrimThresholdMm(double mm) {
    trim_->setValue(mm);
}

void ExportOptionsDialog::setLockKind(int index) {
    lock_->setCurrentIndex(index);
}

void ExportOptionsDialog::setDesignStart(bool enabled, double xMm, double yMm) {
    hasStart_->setChecked(enabled);
    startX_->setValue(xMm);
    startY_->setValue(yMm);
}

QString ExportOptionsDialog::checkText() const {
    return check_->text();
}

int ExportOptionsDialog::hoopCount() const {
    return hoop_->count() - 1;
}

int ExportOptionsDialog::machineCount() const {
    return machine_->count() - 1;
}

} // namespace openstitch::desktop_v2
