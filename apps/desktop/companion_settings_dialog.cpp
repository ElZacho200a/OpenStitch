// SPDX-License-Identifier: Apache-2.0
#include "companion_settings_dialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "claude_backend.hpp"

namespace openstitch::desktop {

CompanionSettingsDialog::CompanionSettingsDialog(agent::ISecretStore& secrets, QWidget* parent)
    : QDialog(parent), secrets_(secrets) {
    setObjectName(QStringLiteral("dialog_companionSettings"));
    setWindowTitle(tr("Compagnon IA"));
    auto* layout = new QVBoxLayout(this);

    auto* notice = new QLabel(
        tr("Données : si vous activez Claude, vos messages et les résultats des outils "
           "(statistiques "
           "du projet, listes d'objets, aperçus de rendu) sont envoyés à api.anthropic.com avec "
           "votre clé API. Rien n'est envoyé tant que vous n'écrivez pas de message. Un abonnement "
           "Claude grand public n'est pas utilisable ici : il faut une clé API. Sans réseau ni "
           "clé, OpenStitch fonctionne normalement."),
        this);
    notice->setObjectName(QStringLiteral("label_companionNotice"));
    notice->setWordWrap(true);
    layout->addWidget(notice);

    auto* form = new QFormLayout;
    fullyOff_ =
        new QCheckBox(tr("Désactiver entièrement le compagnon (pont local et fournisseur)"), this);
    fullyOff_->setObjectName(QStringLiteral("check_companionOff"));
    fullyOff_->setChecked(companion_settings::fullyDisabled());
    enableProvider_ = new QCheckBox(tr("Activer le fournisseur Claude (réseau requis)"), this);
    enableProvider_->setObjectName(QStringLiteral("check_companionProvider"));
    enableProvider_->setChecked(companion_settings::providerEnabled());
    model_ = new QLineEdit(companion_settings::model(), this);
    model_->setObjectName(QStringLiteral("edit_companionModel"));
    model_->setAccessibleName(tr("Modèle Claude"));
    key_ = new QLineEdit(this);
    key_->setObjectName(QStringLiteral("edit_companionKey"));
    key_->setAccessibleName(tr("Clé API Anthropic"));
    key_->setEchoMode(QLineEdit::Password);
    key_->setPlaceholderText(tr("sk-ant-…"));
    keyStatus_ = new QLabel(this);
    keyStatus_->setObjectName(QStringLiteral("label_companionKeyStatus"));
    keyStatus_->setWordWrap(true);
    saveKey_ = new QPushButton(tr("Enregistrer la clé"), this);
    saveKey_->setObjectName(QStringLiteral("button_companionSaveKey"));
    removeKey_ = new QPushButton(tr("Retirer la clé"), this);
    removeKey_->setObjectName(QStringLiteral("button_companionRemoveKey"));
    form->addRow(fullyOff_);
    form->addRow(enableProvider_);
    form->addRow(tr("Modèle"), model_);
    form->addRow(tr("Clé API"), key_);
    form->addRow(keyStatus_);
    layout->addLayout(form);
    auto* keyButtons = new QHBoxLayout;
    keyButtons->addWidget(saveKey_);
    keyButtons->addWidget(removeKey_);
    layout->addLayout(keyButtons);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        applyToSettings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(saveKey_, &QPushButton::clicked, this, &CompanionSettingsDialog::saveKey);
    connect(removeKey_, &QPushButton::clicked, this, &CompanionSettingsDialog::removeKey);
    refreshKeyStatus();
}

void CompanionSettingsDialog::applyToSettings() {
    companion_settings::setFullyDisabled(fullyOff_->isChecked());
    companion_settings::setProviderEnabled(enableProvider_->isChecked());
    const QString model = model_->text().trimmed();
    if (!model.isEmpty()) {
        companion_settings::setModel(model);
    }
}

void CompanionSettingsDialog::refreshKeyStatus() {
    if (!secrets_.available()) {
        keyStatus_->setText(tr("Stockage sécurisé indisponible sur ce système : la clé ne sera "
                               "jamais écrite sur le disque, le fournisseur reste inactif."));
        saveKey_->setEnabled(false);
        removeKey_->setEnabled(false);
        return;
    }
    const bool present = secrets_.read(companion_settings::kSecretKey).has_value();
    keyStatus_->setText(present ? tr("Une clé est enregistrée dans le gestionnaire d'identifiants "
                                     "du système.")
                                : tr("Aucune clé enregistrée."));
    removeKey_->setEnabled(present);
}

void CompanionSettingsDialog::saveKey() {
    const QString typed = key_->text().trimmed();
    if (typed.isEmpty()) {
        return;
    }
    if (secrets_.write(companion_settings::kSecretKey, typed.toStdString())) {
        key_->clear(); // la clé n'est plus affichée une fois enregistrée
    }
    refreshKeyStatus();
}

void CompanionSettingsDialog::removeKey() {
    secrets_.remove(companion_settings::kSecretKey);
    refreshKeyStatus();
}

} // namespace openstitch::desktop
