// SPDX-License-Identifier: Apache-2.0
#include "color_blend_dialog.hpp"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "app_theme.hpp"
#include "wheel_guard.hpp"

namespace openstitch::desktop {
namespace {
QColor color(ColorBlendDialog::Rgb rgb) {
    return QColor(rgb[0], rgb[1], rgb[2]);
}
ColorBlendDialog::Rgb rgb(const QString& text) {
    const QColor c(text);
    return {static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
            static_cast<std::uint8_t>(c.blue())};
}
} // namespace

ColorBlendDialog::ColorBlendDialog(Rgb background, Rgb foreground, bool canAnalyze,
                                   bool mergesZones, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("colorBlendDialog"));
    setWindowTitle(tr("Fondu de couleurs · deux fils"));
    setMinimumWidth(490);
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    auto* explanation = new QLabel(
        mergesZones
            ? tr("Les deux zones seront unies. Le fond sera cousu en premier ; le dessus, à "
                 "densité variable, sera cousu juste après. Annulable en une fois.")
            : tr("Un fond uni et un dessus à densité variable, sur la même forme. "
                 "Choisissez deux couleurs, avec ou sans image. Annulable en une fois."),
        this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* form = new QFormLayout();
    const auto colorRow = [this, form](const QString& label, const QString& name, Rgb value) {
        auto* row = new QWidget(this);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* edit = new QLineEdit(color(value).name(), row);
        edit->setObjectName(name);
        edit->setAccessibleName(label);
        edit->setMaxLength(7);
        edit->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("#[0-9a-fA-F]{6}")), edit));
        edit->setToolTip(tr("Couleur libre au format #RRGGBB. Les fils réels peuvent ensuite "
                            "être associés dans le panneau Fils."));
        auto* choose = new QPushButton(tr("Choisir…"), row);
        choose->setAccessibleName(tr("Choisir : %1").arg(label));
        connect(choose, &QPushButton::clicked, this, [this, edit, label] {
            const QColor picked = QColorDialog::getColor(QColor(edit->text()), this, label);
            if (picked.isValid()) {
                edit->setText(picked.name());
            }
        });
        h->addWidget(edit, 1);
        h->addWidget(choose);
        form->addRow(label, row);
        return edit;
    };
    background_ =
        colorRow(tr("Fond · premier fil :"), QStringLiteral("edit_blendBackground"), background);
    foreground_ =
        colorRow(tr("Dessus · second fil :"), QStringLiteral("edit_blendForeground"), foreground);
    const auto spacing = [this, form](const QString& title, const char* name, double value) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setObjectName(QString::fromLatin1(name));
        spin->setRange(0.1, 4.0);
        spin->setDecimals(2);
        spin->setSingleStep(0.1);
        spin->setSuffix(tr(" mm"));
        spin->setKeyboardTracking(false);
        spin->setValue(value);
        spin->setToolTip(tr("Écart entre lignes du dessus. Petit écart : le dessus domine ; "
                            "grand écart : le fond reste visible. Plage : 0,1 – 4 mm."));
        form->addRow(title, spin);
        return spin;
    };
    start_ = spacing(tr("Écart du dessus au début :"), "spin_blendStart", 4.0);
    end_ = spacing(tr("Écart du dessus à la fin :"), "spin_blendEnd", 0.4);
    angle_ = new QDoubleSpinBox(this);
    angle_->setObjectName(QStringLiteral("spin_blendAngle"));
    angle_->setRange(0.0, 359.9);
    angle_->setDecimals(1);
    angle_->setWrapping(true);
    angle_->setSuffix(tr(" °"));
    angle_->setKeyboardTracking(false);
    angle_->setToolTip(tr("Sens début → fin dans le document : 0° vers la droite, "
                          "90° vers le haut. Les lignes du dessus sont perpendiculaires."));
    form->addRow(tr("Angle du fondu :"), angle_);
    layout->addLayout(form);

    auto* reverse = new QPushButton(tr("Inverser le sens du fondu"), this);
    reverse->setObjectName(QStringLiteral("button_blendReverse"));
    connect(reverse, &QPushButton::clicked, this, [this] {
        const double start = start_->value();
        const QSignalBlocker block(start_);
        start_->setValue(end_->value());
        end_->setValue(start);
        updatePreview();
    });
    layout->addWidget(reverse);
    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("blend_preview"));
    preview_->setMinimumHeight(84);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setAccessibleName(tr("Aperçu schématique du fondu, du début à la fin"));
    layout->addWidget(preview_);
    auto* note = new QLabel(tr("Début → fin · aperçu schématique des couleurs, "
                               "pas une simulation des points ni du tissu."),
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* analyze = new QPushButton(tr("Proposer les couleurs depuis l'image"), this);
    analyze->setObjectName(QStringLiteral("button_blendAnalyze"));
    analyze->setEnabled(canAnalyze);
    analyze->setToolTip(canAnalyze ? tr("Analyse la forme et utilise le nuancier actif du "
                                        "panneau Fils. La proposition reste modifiable.")
                                   : tr("Une image raster sous une forme unique est nécessaire. "
                                        "Le fondu manuel reste disponible."));
    connect(analyze, &QPushButton::clicked, this, &ColorBlendDialog::analysisRequested);
    layout->addWidget(analyze);
    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("blend_message"));
    message_->setWordWrap(true);
    layout->addWidget(message_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    accept_ = buttons->button(QDialogButtonBox::Ok);
    accept_->setText(tr("Créer le fondu"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    for (auto* edit : {background_, foreground_}) {
        connect(edit, &QLineEdit::textChanged, this, &ColorBlendDialog::updatePreview);
    }
    for (auto* spin : {start_, end_, angle_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, &ColorBlendDialog::updatePreview);
    }
    auto* guard = new WheelGuard(this);
    guard->guardAll(this);
    connect(&AppTheme::instance(), &AppTheme::changed, this, &ColorBlendDialog::updatePreview);
    updatePreview();
}

void ColorBlendDialog::updatePreview() {
    const bool valid = background_->hasAcceptableInput() && foreground_->hasAcceptableInput();
    const bool distinct = valid && QColor(background_->text()) != QColor(foreground_->text());
    accept_->setEnabled(distinct);
    if (!distinct) {
        preview_->clear();
        message_->setText(valid ? tr("Choisissez deux couleurs différentes.")
                                : tr("Saisissez deux couleurs valides au format #RRGGBB."));
        return;
    }
    message_->clear();
    QPixmap swatch(440, 84);
    swatch.fill(Qt::transparent);
    QPainter painter(&swatch);
    const QColor base(background_->text());
    const QColor top(foreground_->text());
    QLinearGradient gradient(0, 0, swatch.width(), 0);
    for (int i = 0; i <= 16; ++i) {
        const double t = i / 16.0;
        const double gap = start_->value() * (1.0 - t) + end_->value() * t;
        const double cover = std::clamp(0.4 / gap, 0.0, 1.0);
        gradient.setColorAt(t,
                            QColor::fromRgbF(base.redF() * (1.0 - cover) + top.redF() * cover,
                                             base.greenF() * (1.0 - cover) + top.greenF() * cover,
                                             base.blueF() * (1.0 - cover) + top.blueF() * cover));
    }
    painter.setBrush(gradient);
    painter.setPen(AppTheme::instance().tokens().border);
    painter.drawRoundedRect(swatch.rect().adjusted(0, 0, -1, -1), 8, 8);
    preview_->setPixmap(swatch);
}

void ColorBlendDialog::setProposal(Rgb background, Rgb foreground,
                                   const document::DensityGradient& gradient) {
    const QSignalBlocker b1(background_), b2(foreground_), b3(start_), b4(end_), b5(angle_);
    background_->setText(color(background).name());
    foreground_->setText(color(foreground).name());
    start_->setValue(to_millimeters(gradient.spacing_from).value);
    end_->setValue(to_millimeters(gradient.spacing_to).value);
    const double angle =
        std::atan2(static_cast<double>(gradient.to.y.value - gradient.from.y.value),
                   static_cast<double>(gradient.to.x.value - gradient.from.x.value));
    angle_->setValue(std::fmod(angle * 180.0 / std::numbers::pi + 360.0, 360.0));
    proposal_ = gradient;
    updatePreview();
}

void ColorBlendDialog::setAnalysisMessage(const QString& message) {
    message_->setText(message);
}
ColorBlendDialog::Rgb ColorBlendDialog::backgroundColor() const {
    return rgb(background_->text());
}
ColorBlendDialog::Rgb ColorBlendDialog::foregroundColor() const {
    return rgb(foreground_->text());
}
double ColorBlendDialog::angleDegrees() const {
    return angle_->value();
}
Micrometers ColorBlendDialog::startSpacing() const {
    return to_micrometers(Millimeters{start_->value()});
}
Micrometers ColorBlendDialog::endSpacing() const {
    return to_micrometers(Millimeters{end_->value()});
}
std::optional<document::DensityGradient> ColorBlendDialog::proposedGradient() const {
    if (!proposal_) {
        return std::nullopt;
    }
    const auto& g = *proposal_;
    const double angle = std::fmod(std::atan2(static_cast<double>(g.to.y.value - g.from.y.value),
                                              static_cast<double>(g.to.x.value - g.from.x.value)) *
                                           180.0 / std::numbers::pi +
                                       360.0,
                                   360.0);
    if (std::abs(angle - angle_->value()) > 0.051) {
        return std::nullopt;
    }
    auto out = g;
    out.spacing_from = startSpacing();
    out.spacing_to = endSpacing();
    return out;
}
} // namespace openstitch::desktop
