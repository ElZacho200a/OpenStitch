// SPDX-License-Identifier: Apache-2.0
#include "import_dialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "app_theme.hpp"

namespace openstitch::desktop {

namespace {

constexpr double kMinMm = 1.0;
constexpr double kMaxMm = 1000.0;

} // namespace

ImportDialog::ImportDialog(int widthPx, int heightPx, const QImage& preview, QSizeF hoopMm,
                           QWidget* parent)
    : QDialog(parent), widthPx_(widthPx), heightPx_(heightPx), hoopMm_(hoopMm) {
    setWindowTitle(tr("Importer une image"));

    auto* root = new QVBoxLayout(this);

    // Aperçu + informations pixel.
    auto* preview_label = new QLabel(this);
    preview_label->setAlignment(Qt::AlignCenter);
    if (!preview.isNull()) {
        preview_label->setPixmap(QPixmap::fromImage(preview).scaled(220, 160, Qt::KeepAspectRatio,
                                                                    Qt::SmoothTransformation));
    }
    root->addWidget(preview_label);

    QString info = tr("%1 × %2 pixels").arg(widthPx_).arg(heightPx_);
    if (preview.hasAlphaChannel()) {
        info += tr("  ·  transparence");
    }
    auto* infoLabel = new QLabel(info, this);
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setEnabled(false);
    root->addWidget(infoLabel);

    auto* form = new QFormLayout();
    const auto makeSpin = [this] {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(kMinMm, kMaxMm);
        spin->setDecimals(1);
        spin->setSuffix(tr(" mm"));
        return spin;
    };
    widthMm_ = makeSpin();
    widthMm_->setObjectName(QStringLiteral("importWidthSpin"));
    heightMm_ = makeSpin();
    heightMm_->setObjectName(QStringLiteral("importHeightSpin"));
    keepRatio_ = new QCheckBox(tr("Conserver les proportions"), this);
    keepRatio_->setObjectName(QStringLiteral("importKeepRatioCheck"));
    keepRatio_->setChecked(true);
    applyRatioRanges();

    // Valeur par défaut : 96 dpi (métier : libs/document), mais jamais plus grande que le cadre
    // — une photo de 3000 px donnait ~ 800 mm, que l'utilisateur devait ramener à la main.
    if (const auto def = document::placement_from_dpi(widthPx_, heightPx_, 96.0)) {
        widthMm_->setValue(to_millimeters(def->width).value);
        heightMm_->setValue(to_millimeters(def->height).value);
    }
    if (widthMm_->value() > hoopMm_.width() + 1e-6 ||
        heightMm_->value() > hoopMm_.height() + 1e-6) {
        fitToHoop();
    }

    form->addRow(tr("Largeur :"), widthMm_);
    form->addRow(tr("Hauteur :"), heightMm_);
    form->addRow(keepRatio_);
    root->addLayout(form);

    fitButton_ = new QPushButton(tr("Ajuster au cadre"), this);
    fitButton_->setObjectName(QStringLiteral("importFitButton"));
    fitButton_->setToolTip(tr("Ramène l'image à la plus grande taille qui tient dans le cadre, "
                              "proportions conservées."));
    connect(fitButton_, &QPushButton::clicked, this, &ImportDialog::fitToHoop);
    root->addWidget(fitButton_, 0, Qt::AlignLeft);

    resolutionLabel_ = new QLabel(this);
    resolutionLabel_->setEnabled(false);
    root->addWidget(resolutionLabel_);

    const QString warningStyle =
        QStringLiteral("color:%1;").arg(AppTheme::instance().tokens().warning.name());
    warningLabel_ = new QLabel(this);
    warningLabel_->setWordWrap(true);
    warningLabel_->setStyleSheet(warningStyle);
    root->addWidget(warningLabel_);

    ratioLabel_ = new QLabel(this);
    ratioLabel_->setObjectName(QStringLiteral("importRatioLabel"));
    ratioLabel_->setWordWrap(true);
    ratioLabel_->setStyleSheet(warningStyle);
    root->addWidget(ratioLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(widthMm_, &QDoubleSpinBox::valueChanged, this, &ImportDialog::syncFromWidth);
    connect(heightMm_, &QDoubleSpinBox::valueChanged, this, &ImportDialog::syncFromHeight);
    connect(keepRatio_, &QCheckBox::toggled, this, [this] {
        applyRatioRanges();
        syncFromWidth();
    });
    recompute();
}

void ImportDialog::applyRatioRanges() {
    const bool locked = keepRatio_ != nullptr && keepRatio_->isChecked();
    double widthLo = kMinMm;
    double widthHi = kMaxMm;
    double heightLo = kMinMm;
    double heightHi = kMaxMm;
    if (locked && widthPx_ > 0 && heightPx_ > 0) {
        const double ratio = static_cast<double>(widthPx_) / static_cast<double>(heightPx_);
        widthLo = std::max(kMinMm, kMinMm * ratio);
        widthHi = std::min(kMaxMm, kMaxMm * ratio);
        heightLo = std::max(kMinMm, kMinMm / ratio);
        heightHi = std::min(kMaxMm, kMaxMm / ratio);
        // Image extrême (bandeau de 1000 : 1) : aucune taille ne satisfait les deux bornes ;
        // on garde au moins un intervalle non vide plutôt qu'un champ bloqué.
        widthHi = std::max(widthHi, widthLo);
        heightHi = std::max(heightHi, heightLo);
    }
    const bool previous = syncing_;
    syncing_ = true; // l'écrêtage éventuel de la valeur courante n'est pas une saisie
    widthMm_->setRange(widthLo, widthHi);
    heightMm_->setRange(heightLo, heightHi);
    syncing_ = previous;
}

void ImportDialog::fitToHoop() {
    if (hoopMm_.width() <= 0.0 || hoopMm_.height() <= 0.0 || widthPx_ <= 0 || heightPx_ <= 0) {
        return;
    }
    // Plus grande taille, proportions conservées, qui tient dans le cadre.
    const double fitWidth =
        std::min(hoopMm_.width(), hoopMm_.height() * static_cast<double>(widthPx_) / heightPx_);
    const auto p = document::placement_from_width(widthPx_, heightPx_, Millimeters{fitWidth});
    if (!p) {
        return;
    }
    syncing_ = true;
    keepRatio_->setChecked(true);
    applyRatioRanges();
    widthMm_->setValue(to_millimeters(p->width).value);
    heightMm_->setValue(to_millimeters(p->height).value);
    syncing_ = false;
    if (resolutionLabel_ != nullptr) { // appelée aussi par le constructeur, avant les libellés
        recompute();
    }
}

void ImportDialog::syncFromWidth() {
    if (!syncing_ && keepRatio_->isChecked()) {
        syncing_ = true;
        if (const auto p = document::placement_from_width(widthPx_, heightPx_,
                                                          Millimeters{widthMm_->value()})) {
            heightMm_->setValue(to_millimeters(p->height).value);
        }
        syncing_ = false;
    }
    recompute();
}

void ImportDialog::syncFromHeight() {
    if (!syncing_ && keepRatio_->isChecked()) {
        syncing_ = true;
        // Ratio conservé depuis la hauteur : symétrique du cas largeur.
        if (const auto p = document::placement_from_width(heightPx_, widthPx_,
                                                          Millimeters{heightMm_->value()})) {
            widthMm_->setValue(to_millimeters(p->height).value);
        }
        syncing_ = false;
    }
    recompute();
}

void ImportDialog::recompute() {
    const double wMm = widthMm_->value();
    const double hMm = heightMm_->value();
    const double mmPerPx = widthPx_ > 0 ? wMm / widthPx_ : 0.0;
    const double dpi = mmPerPx > 0.0 ? 25.4 / mmPerPx : 0.0;
    resolutionLabel_->setText(tr("Résolution : %1 mm/pixel (%2 dpi)   ·   cadre %3 × %4 mm")
                                  .arg(mmPerPx, 0, 'f', 3)
                                  .arg(dpi, 0, 'f', 0)
                                  .arg(hoopMm_.width(), 0, 'f', 0)
                                  .arg(hoopMm_.height(), 0, 'f', 0));

    if (wMm > hoopMm_.width() + 1e-6 || hMm > hoopMm_.height() + 1e-6) {
        warningLabel_->setText(
            tr("L'image dépasse le cadre (%1 × %2 mm > %3 × %4 mm). « Ajuster au cadre » la "
               "ramène à sa taille ; vous pourrez aussi la recadrer ou agrandir le cadre.")
                .arg(wMm, 0, 'f', 1)
                .arg(hMm, 0, 'f', 1)
                .arg(hoopMm_.width(), 0, 'f', 0)
                .arg(hoopMm_.height(), 0, 'f', 0));
        warningLabel_->show();
    } else {
        warningLabel_->clear();
        warningLabel_->hide();
    }

    // Proportions : indiquées dès qu'elles ne sont plus celles de l'image (case décochée, ou
    // largeur et hauteur saisies indépendamment).
    const double imageRatio = heightPx_ > 0 ? static_cast<double>(widthPx_) / heightPx_ : 1.0;
    const double chosenRatio = hMm > 0.0 ? wMm / hMm : imageRatio;
    const double distortion = imageRatio > 0.0 ? chosenRatio / imageRatio - 1.0 : 0.0;
    if (!keepRatio_->isChecked() && std::abs(distortion) > 0.01) {
        ratioLabel_->setText(tr("Les proportions de l'image sont modifiées : elle sera étirée de "
                                "%1 %.")
                                 .arg(std::abs(distortion) * 100.0, 0, 'f', 0));
        ratioLabel_->show();
    } else {
        ratioLabel_->clear();
        ratioLabel_->hide();
    }
}

std::optional<document::ImagePlacement> ImportDialog::placement() const {
    const auto p = document::placement_from_size(
        widthPx_, heightPx_, Millimeters{widthMm_->value()}, Millimeters{heightMm_->value()});
    if (!p) {
        return std::nullopt;
    }
    return *p;
}

} // namespace openstitch::desktop
