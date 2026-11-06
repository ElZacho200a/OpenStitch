// SPDX-License-Identifier: Apache-2.0
#include "render_quality_dialog.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace openstitch::desktop_v2 {

namespace {
QSlider* makeSlider(const QString& name, QWidget* parent) {
    auto* s = new QSlider(Qt::Horizontal, parent);
    s->setObjectName(name);
    s->setRange(0, 100);
    s->setAccessibleName(name);
    return s;
}
} // namespace

RenderQualityDialog::RenderQualityDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("renderQualityDialog"));
    setWindowTitle(tr("Qualité du rendu réaliste"));
    setModal(false);
    auto* column = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    column->addLayout(form);

    preset_ = new QComboBox(this);
    preset_->setObjectName(QStringLiteral("renderPreset"));
    for (const char* id : {"draft", "balanced", "high", "photo"}) {
        const auto label = renderPresetLabel(id);
        preset_->addItem(QString::fromUtf8(label.data(), static_cast<qsizetype>(label.size())),
                         QString::fromLatin1(id));
    }
    preset_->addItem(tr("Personnalisé"), QString());
    form->addRow(tr("Préréglage"), preset_);

    resolution_ = new QDoubleSpinBox(this);
    resolution_->setObjectName(QStringLiteral("renderResolution"));
    resolution_->setRange(kMinPxPerMm, kMaxPxPerMm);
    resolution_->setSingleStep(0.5);
    resolution_->setDecimals(1);
    resolution_->setSuffix(tr(" px/mm"));
    resolution_->setToolTip(tr("Netteté de l'aperçu. Plus haut = plus net mais plus lent ; un très "
                               "grand motif est ramené automatiquement à la limite mémoire."));
    form->addRow(tr("Résolution"), resolution_);

    thread_ = new QDoubleSpinBox(this);
    thread_->setObjectName(QStringLiteral("renderThread"));
    thread_->setRange(0.1, 1.0);
    thread_->setSingleStep(0.05);
    thread_->setDecimals(2);
    thread_->setSuffix(tr(" mm"));
    form->addRow(tr("Épaisseur du fil"), thread_);

    relief_ = makeSlider(QStringLiteral("renderRelief"), this);
    sheen_ = makeSlider(QStringLiteral("renderSheen"), this);
    twist_ = makeSlider(QStringLiteral("renderTwist"), this);
    shadow_ = makeSlider(QStringLiteral("renderShadow"), this);
    form->addRow(tr("Relief du fil"), relief_);
    form->addRow(tr("Brillance"), sheen_);
    form->addRow(tr("Torsion"), twist_);
    form->addRow(tr("Ombre portée"), shadow_);

    fabric_ = new QPushButton(this);
    fabric_->setObjectName(QStringLiteral("renderFabricColour"));
    form->addRow(tr("Couleur du tissu"), fabric_);
    weave_ = new QCheckBox(tr("Armure du tissu visible"), this);
    weave_->setObjectName(QStringLiteral("renderWeave"));
    form->addRow(QString(), weave_);
    fabricRelief_ = makeSlider(QStringLiteral("renderFabricRelief"), this);
    form->addRow(tr("Relief du tissu"), fabricRelief_);
    high_ = new QCheckBox(tr("Moteur haute qualité"), this);
    high_->setObjectName(QStringLiteral("renderHigh"));
    high_->setToolTip(tr("Décoché : rendu brouillon, beaucoup plus rapide."));
    form->addRow(QString(), high_);

    hint_ = new QLabel(this);
    hint_->setObjectName(QStringLiteral("renderHint"));
    hint_->setWordWrap(true);
    column->addWidget(hint_);

    auto* buttons = new QDialogButtonBox(this);
    auto* reset = buttons->addButton(tr("Valeurs par défaut"), QDialogButtonBox::ResetRole);
    reset->setObjectName(QStringLiteral("renderReset"));
    buttons->addButton(QDialogButtonBox::Close);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(reset, &QPushButton::clicked, this, [this] { apply(*renderPreset("balanced"), true); });

    connect(preset_, &QComboBox::activated, this, [this](int index) {
        const auto id = preset_->itemData(index).toString().toStdString();
        if (const auto p = renderPreset(id)) {
            apply(*p, true);
        }
    });
    connect(resolution_, &QDoubleSpinBox::valueChanged, this, [this](double) { edited(); });
    connect(thread_, &QDoubleSpinBox::valueChanged, this, [this](double) { edited(); });
    for (auto* s : {relief_, sheen_, twist_, shadow_, fabricRelief_}) {
        connect(s, &QSlider::valueChanged, this, [this](int) { edited(); });
    }
    connect(weave_, &QCheckBox::toggled, this, [this](bool) { edited(); });
    connect(high_, &QCheckBox::toggled, this, [this](bool) { edited(); });
    connect(fabric_, &QPushButton::clicked, this, [this] {
        const QColor initial(current_.fabricRgb[0], current_.fabricRgb[1], current_.fabricRgb[2]);
        const QColor c = QColorDialog::getColor(initial, this, tr("Couleur du tissu"));
        if (c.isValid()) {
            RenderSettings next = current_;
            next.fabricRgb = {static_cast<std::uint8_t>(c.red()),
                              static_cast<std::uint8_t>(c.green()),
                              static_cast<std::uint8_t>(c.blue())};
            apply(next, true);
        }
    });
    refreshWidgets();
}

void RenderQualityDialog::setSettings(const RenderSettings& settings) {
    apply(settings, false);
}

void RenderQualityDialog::apply(const RenderSettings& settings, bool emitChange) {
    const RenderSettings next = clamped(settings);
    const bool changed = !(next == current_);
    current_ = next;
    refreshWidgets();
    if (emitChange && changed) {
        emit settingsChanged(current_);
    }
}

void RenderQualityDialog::refreshWidgets() {
    refreshing_ = true;
    resolution_->setValue(current_.pxPerMm);
    thread_->setValue(current_.threadWidthMm);
    relief_->setValue(static_cast<int>(current_.relief * 100.0 + 0.5));
    sheen_->setValue(static_cast<int>(current_.sheen * 100.0 + 0.5));
    twist_->setValue(static_cast<int>(current_.twist * 100.0 + 0.5));
    shadow_->setValue(static_cast<int>(current_.shadow * 100.0 + 0.5));
    fabricRelief_->setValue(static_cast<int>(current_.fabricRelief * 100.0 + 0.5));
    weave_->setChecked(current_.fabricWeave);
    fabricRelief_->setEnabled(current_.fabricWeave);
    high_->setChecked(current_.highQuality);
    const QColor c(current_.fabricRgb[0], current_.fabricRgb[1], current_.fabricRgb[2]);
    fabric_->setText(c.name().toUpper());
    fabric_->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
                               .arg(c.name(), c.lightness() < 128 ? "white" : "black"));
    const auto id = matchingRenderPreset(current_);
    const int index =
        id.empty() ? preset_->count() - 1 : preset_->findData(QString::fromStdString(id));
    preset_->setCurrentIndex(index);
    hint_->setText(current_.pxPerMm >= 8.0 || !current_.highQuality
                       ? (current_.highQuality
                              ? tr("Résolution élevée : l'aperçu d'un grand motif peut prendre "
                                   "plusieurs secondes.")
                              : tr("Brouillon : rendu rapide, texture du fil simplifiée."))
                       : QString());
    refreshing_ = false;
}

void RenderQualityDialog::edited() {
    if (refreshing_) {
        return;
    }
    RenderSettings next = current_;
    next.pxPerMm = resolution_->value();
    next.threadWidthMm = thread_->value();
    next.relief = relief_->value() / 100.0;
    next.sheen = sheen_->value() / 100.0;
    next.twist = twist_->value() / 100.0;
    next.shadow = shadow_->value() / 100.0;
    next.fabricRelief = fabricRelief_->value() / 100.0;
    next.fabricWeave = weave_->isChecked();
    next.highQuality = high_->isChecked();
    apply(next, true);
}

} // namespace openstitch::desktop_v2
