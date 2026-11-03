// SPDX-License-Identifier: Apache-2.0
#include "colorway_dialog.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include <iterator>

namespace openstitch::desktop {

namespace {

struct FabricPreset {
    const char* label;
    int r;
    int g;
    int b;
};

// Tissus courants (couleurs indicatives, réglables ensuite à la main).
constexpr FabricPreset kPresets[] = {
    {QT_TRANSLATE_NOOP("openstitch::desktop::ColorwayDialog", "Blanc"), 255, 255, 255},
    {QT_TRANSLATE_NOOP("openstitch::desktop::ColorwayDialog", "Écru"), 240, 234, 214},
    {QT_TRANSLATE_NOOP("openstitch::desktop::ColorwayDialog", "Gris clair"), 200, 200, 200},
    {QT_TRANSLATE_NOOP("openstitch::desktop::ColorwayDialog", "Noir"), 25, 25, 28},
    {QT_TRANSLATE_NOOP("openstitch::desktop::ColorwayDialog", "Bleu marine"), 24, 38, 82},
    {QT_TRANSLATE_NOOP("openstitch::desktop::ColorwayDialog", "Rouge bordeaux"), 110, 24, 40},
    {QT_TRANSLATE_NOOP("openstitch::desktop::ColorwayDialog", "Vert sapin"), 28, 70, 48},
    {QT_TRANSLATE_NOOP("openstitch::desktop::ColorwayDialog", "Jean"), 70, 100, 150},
};

} // namespace

ColorwayDialog::ColorwayDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("dialog_colorways"));
    setWindowTitle(tr("Coloris et tissu"));
    setModal(false);

    auto* root = new QVBoxLayout(this);

    auto* colorwayBox = new QGroupBox(tr("Coloris"), this);
    auto* colorwayLayout = new QVBoxLayout(colorwayBox);
    auto* hint = new QLabel(tr("Un coloris est un jeu de fils complet du même dessin. Double-clic "
                               "pour renommer ; l'export et la fiche de production suivent le "
                               "coloris actif."),
                            colorwayBox);
    hint->setWordWrap(true);
    colorwayLayout->addWidget(hint);
    list_ = new QListWidget(colorwayBox);
    list_->setObjectName(QStringLiteral("list_colorways"));
    colorwayLayout->addWidget(list_);
    auto* buttons = new QHBoxLayout();
    addButton_ = new QPushButton(tr("Nouveau (copie du courant)"), colorwayBox);
    addButton_->setObjectName(QStringLiteral("btn_colorway_add"));
    activateButton_ = new QPushButton(tr("Activer"), colorwayBox);
    activateButton_->setObjectName(QStringLiteral("btn_colorway_activate"));
    removeButton_ = new QPushButton(tr("Supprimer"), colorwayBox);
    removeButton_->setObjectName(QStringLiteral("btn_colorway_remove"));
    buttons->addWidget(addButton_);
    buttons->addWidget(activateButton_);
    buttons->addWidget(removeButton_);
    colorwayLayout->addLayout(buttons);
    root->addWidget(colorwayBox);

    auto* fabricBox = new QGroupBox(tr("Tissu de fond"), this);
    auto* fabricRoot = new QVBoxLayout(fabricBox);
    auto* fabricLayout = new QHBoxLayout();
    presetCombo_ = new QComboBox(fabricBox);
    presetCombo_->setObjectName(QStringLiteral("combo_fabric_preset"));
    presetCombo_->addItem(tr("Tissu courant…"));
    for (const auto& preset : kPresets) {
        QPixmap swatch(14, 14);
        swatch.fill(QColor(preset.r, preset.g, preset.b));
        presetCombo_->addItem(QIcon(swatch), tr(preset.label));
    }
    fabricButton_ = new QPushButton(tr("Couleur…"), fabricBox);
    fabricButton_->setObjectName(QStringLiteral("btn_fabric_color"));
    fabricName_ = new QLineEdit(fabricBox);
    fabricName_->setObjectName(QStringLiteral("edit_fabric_name"));
    fabricName_->setPlaceholderText(tr("Nom du tissu (facultatif)"));
    fabricLayout->addWidget(presetCombo_);
    fabricLayout->addWidget(fabricButton_);
    fabricLayout->addWidget(fabricName_, 1);
    fabricRoot->addLayout(fabricLayout);
    auto* profileLayout = new QHBoxLayout();
    profileLayout->addWidget(new QLabel(tr("Matière :"), fabricBox));
    materialCombo_ = new QComboBox(fabricBox);
    materialCombo_->setObjectName(QStringLiteral("combo_fabric_material"));
    materialCombo_->addItem(tr("Non précisée"), QString());
    materialCombo_->addItem(tr("Coton tissé"), QStringLiteral("woven"));
    materialCombo_->addItem(tr("Jersey"), QStringLiteral("jersey"));
    materialCombo_->addItem(tr("Éponge"), QStringLiteral("terry"));
    materialCombo_->addItem(tr("Feutrine"), QStringLiteral("felt"));
    materialCombo_->addItem(tr("Jean"), QStringLiteral("denim"));
    profileLayout->addWidget(materialCombo_);
    stabilizer_ = new QLineEdit(fabricBox);
    stabilizer_->setObjectName(QStringLiteral("edit_fabric_stabilizer"));
    stabilizer_->setPlaceholderText(tr("Stabilisateur (ex. cut-away)"));
    profileLayout->addWidget(stabilizer_, 1);
    fabricRoot->addLayout(profileLayout);
    // Sens du tissu (prédiction du tirage) : direction des fils de chaîne et souplesse de trame.
    auto* grainLayout = new QHBoxLayout();
    grainLayout->addWidget(new QLabel(tr("Sens de la chaîne :"), fabricBox));
    grainAngle_ = new QDoubleSpinBox(fabricBox);
    grainAngle_->setObjectName(QStringLiteral("spin_fabric_grain_angle"));
    grainAngle_->setRange(-90.0, 90.0);
    grainAngle_->setDecimals(1);
    grainAngle_->setSuffix(QStringLiteral(" °"));
    grainAngle_->setToolTip(tr("Direction des fils de chaîne (0° = horizontal, 90° = vertical)."));
    grainLayout->addWidget(grainAngle_);
    grainLayout->addWidget(new QLabel(tr("Souplesse de trame :"), fabricBox));
    weftSoftness_ = new QDoubleSpinBox(fabricBox);
    weftSoftness_->setObjectName(QStringLiteral("spin_fabric_weft_softness"));
    weftSoftness_->setRange(0.25, 4.0);
    weftSoftness_->setDecimals(2);
    weftSoftness_->setSingleStep(0.1);
    weftSoftness_->setValue(1.0);
    weftSoftness_->setToolTip(
        tr("1 = tissu isotrope ; plus de 1 = la trame cède davantage que la chaîne."));
    grainLayout->addWidget(weftSoftness_);
    grainLayout->addStretch(1);
    fabricRoot->addLayout(grainLayout);
    root->addWidget(fabricBox);

    connect(list_, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!populating_ && item != nullptr) {
            emit renameRequested(list_->row(item), item->text().trimmed());
        }
    });
    connect(addButton_, &QPushButton::clicked, this,
            [this] { emit addRequested(tr("Coloris %1").arg(list_->count() + 1)); });
    connect(activateButton_, &QPushButton::clicked, this, [this] {
        if (list_->currentRow() >= 0) {
            emit activateRequested(list_->currentRow());
        }
    });
    connect(removeButton_, &QPushButton::clicked, this, [this] {
        if (list_->currentRow() >= 0) {
            emit removeRequested(list_->currentRow());
        }
    });
    connect(fabricButton_, &QPushButton::clicked, this, [this] { pickFabricColor(); });
    connect(presetCombo_, &QComboBox::activated, this, [this](int index) { applyPreset(index); });
    connect(fabricName_, &QLineEdit::editingFinished, this, [this] {
        if (!populating_) {
            emitFabric();
        }
    });
    connect(materialCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!populating_) {
            emitFabric();
        }
    });
    connect(stabilizer_, &QLineEdit::editingFinished, this, [this] {
        if (!populating_) {
            emitFabric();
        }
    });
    connect(grainAngle_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!populating_) {
            emitFabric();
        }
    });
    connect(weftSoftness_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!populating_) {
            emitFabric();
        }
    });
    paintSwatch();
    updateButtons();
}

void ColorwayDialog::setState(const QStringList& names, int active, const QColor& fabric,
                              const QString& fabricName, const QString& material,
                              const QString& stabilizer, double grainAngleDeg,
                              double weftSoftness) {
    populating_ = true;
    const int keep = list_->currentRow();
    list_->clear();
    for (int i = 0; i < names.size(); ++i) {
        auto* item = new QListWidgetItem(names[i], list_);
        item->setFlags(item->flags() | Qt::ItemIsEditable); // double-clic = renommer
        if (i == active) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
            item->setToolTip(tr("Coloris actif"));
        }
    }
    active_ = active;
    list_->setCurrentRow(keep >= 0 && keep < names.size() ? keep : (active >= 0 ? active : -1));
    fabric_ = fabric;
    fabricName_->setText(fabricName);
    const int materialIndex = materialCombo_->findData(material);
    materialCombo_->setCurrentIndex(materialIndex >= 0 ? materialIndex : 0);
    stabilizer_->setText(stabilizer);
    grainAngle_->setValue(grainAngleDeg);
    weftSoftness_->setValue(weftSoftness);
    presetCombo_->setCurrentIndex(0);
    populating_ = false;
    paintSwatch();
    updateButtons();
}

void ColorwayDialog::updateButtons() {
    const int row = list_->currentRow();
    activateButton_->setEnabled(row >= 0 && row != active_);
    removeButton_->setEnabled(row >= 0 && row != active_);
}

void ColorwayDialog::paintSwatch() {
    QPixmap pm(14, 14);
    pm.fill(fabric_);
    fabricButton_->setIcon(QIcon(pm));
}

void ColorwayDialog::pickFabricColor() {
    const QColor chosen = QColorDialog::getColor(fabric_, this, tr("Couleur du tissu"));
    if (chosen.isValid() && chosen != fabric_) {
        fabric_ = chosen;
        paintSwatch();
        emitFabric();
    }
}

void ColorwayDialog::applyPreset(int presetIndex) {
    const int i = presetIndex - 1; // l'entrée 0 est l'invite
    if (i < 0 || i >= static_cast<int>(std::size(kPresets))) {
        return;
    }
    fabric_ = QColor(kPresets[i].r, kPresets[i].g, kPresets[i].b);
    fabricName_->setText(tr(kPresets[i].label));
    paintSwatch();
    emitFabric();
}

void ColorwayDialog::emitFabric() {
    emit fabricChangeRequested(
        fabric_, fabricName_->text().trimmed(), materialCombo_->currentData().toString(),
        stabilizer_->text().trimmed(), grainAngle_->value(), weftSoftness_->value());
}

} // namespace openstitch::desktop
