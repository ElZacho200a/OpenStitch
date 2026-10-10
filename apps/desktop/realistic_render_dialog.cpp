// SPDX-License-Identifier: Apache-2.0
#include "realistic_render_dialog.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace openstitch::desktop {

namespace {

int toPercent(double v) {
    return static_cast<int>(std::lround(v * 100.0));
}

double fromPercent(int v) {
    return static_cast<double>(v) / 100.0;
}

// Curseur 0-100 % accompagné de sa valeur.
QWidget* sliderRow(QSlider* slider, QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* label = new QLabel(row);
    label->setMinimumWidth(40);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    const auto update = [label](int v) { label->setText(QStringLiteral("%1 %").arg(v)); };
    QObject::connect(slider, &QSlider::valueChanged, label, update);
    update(slider->value());
    layout->addWidget(slider, 1);
    layout->addWidget(label);
    return row;
}

} // namespace

QSlider* RealisticRenderDialog::makeSlider(QWidget* parent, const char* name) {
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setObjectName(QString::fromLatin1(name));
    slider->setRange(0, 100);
    slider->setPageStep(10);
    return slider;
}

RealisticRenderDialog::RealisticRenderDialog(const RealisticPreferences& initial, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("realisticRenderDialog"));
    setWindowTitle(tr("Rendu réaliste"));
    auto* root = new QVBoxLayout(this);

    enabled_ = new QCheckBox(tr("Activer le rendu réaliste des points"), this);
    enabled_->setObjectName(QStringLiteral("realisticEnabled"));
    enabled_->setToolTip(tr("Dessine chaque point comme un fil avec épaisseur, relief et ombre "
                            "portée. En dézoom fort, retombe sur des lignes colorées."));
    root->addWidget(enabled_);

    auto* threadBox = new QGroupBox(tr("Fil"), this);
    auto* threadForm = new QFormLayout(threadBox);
    threadWidth_ = new QDoubleSpinBox(threadBox);
    threadWidth_->setObjectName(QStringLiteral("realisticThreadWidth"));
    threadWidth_->setRange(stitch_render::kMinThreadWidthMm, stitch_render::kMaxThreadWidthMm);
    threadWidth_->setDecimals(2);
    threadWidth_->setSingleStep(0.05);
    threadWidth_->setSuffix(tr(" mm"));
    threadWidth_->setToolTip(tr("Épaisseur apparente du fil (un fil 40 wt fait environ 0,3 mm)."));
    threadForm->addRow(tr("Épaisseur"), threadWidth_);
    relief_ = makeSlider(threadBox, "realisticRelief");
    relief_->setToolTip(tr("Intensité du relief : ombrage cylindrique sur la largeur du fil."));
    threadForm->addRow(tr("Relief"), sliderRow(relief_, threadBox));
    sheen_ = makeSlider(threadBox, "realisticSheen");
    sheen_->setToolTip(tr("Brillance du fil (reflet de la lumière)."));
    threadForm->addRow(tr("Brillance"), sliderRow(sheen_, threadBox));
    twist_ = makeSlider(threadBox, "realisticTwist");
    twist_->setToolTip(tr("Visibilité de la torsion du fil (stries obliques)."));
    threadForm->addRow(tr("Torsion"), sliderRow(twist_, threadBox));
    shadow_ = makeSlider(threadBox, "realisticShadow");
    shadow_->setToolTip(tr("Ombre portée du fil sur le tissu et sur les points dessous."));
    threadForm->addRow(tr("Ombre portée"), sliderRow(shadow_, threadBox));
    root->addWidget(threadBox);

    auto* fabricBox = new QGroupBox(tr("Tissu"), this);
    auto* fabricForm = new QFormLayout(fabricBox);
    fabricOpaque_ = new QCheckBox(tr("Afficher le tissu (fond opaque)"), fabricBox);
    fabricOpaque_->setObjectName(QStringLiteral("realisticFabricOpaque"));
    fabricOpaque_->setToolTip(
        tr("Décoché : seuls les fils sont dessinés, l'image reste visible dessous."));
    fabricForm->addRow(QString(), fabricOpaque_);
    fabricColorButton_ = new QPushButton(fabricBox);
    fabricColorButton_->setObjectName(QStringLiteral("realisticFabricColor"));
    fabricForm->addRow(tr("Couleur"), fabricColorButton_);
    fabricTexture_ = new QComboBox(fabricBox);
    fabricTexture_->setObjectName(QStringLiteral("realisticFabricTexture"));
    fabricTexture_->addItem(tr("Uni"), static_cast<int>(stitch_render::FabricTexture::Plain));
    fabricTexture_->addItem(tr("Toile tissée"),
                            static_cast<int>(stitch_render::FabricTexture::Weave));
    fabricTexture_->addItem(tr("Feutrine"), static_cast<int>(stitch_render::FabricTexture::Felt));
    fabricForm->addRow(tr("Texture"), fabricTexture_);
    fabricRelief_ = makeSlider(fabricBox, "realisticFabricRelief");
    fabricRelief_->setToolTip(tr("Relief de la texture du tissu."));
    fabricForm->addRow(tr("Relief du tissu"), sliderRow(fabricRelief_, fabricBox));
    root->addWidget(fabricBox);

    auto* qualityForm = new QFormLayout;
    quality_ = new QComboBox(this);
    quality_->setObjectName(QStringLiteral("realisticQuality"));
    quality_->addItem(tr("Rapide"), static_cast<int>(stitch_render::Quality::Fast));
    quality_->addItem(tr("Haute"), static_cast<int>(stitch_render::Quality::High));
    quality_->setToolTip(tr("Rapide : sans torsion, ombre portée ni texture fine du tissu "
                            "(recommandé pour les très gros motifs)."));
    qualityForm->addRow(tr("Qualité"), quality_);
    root->addLayout(qualityForm);

    auto* buttons = new QDialogButtonBox(this);
    auto* reset = buttons->addButton(tr("Réinitialiser"), QDialogButtonBox::ResetRole);
    reset->setObjectName(QStringLiteral("realisticReset"));
    auto* close = buttons->addButton(QDialogButtonBox::Close);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(reset, &QPushButton::clicked, this, [this] {
        setPreferences(RealisticPreferences{enabled_->isChecked(), {}});
        emitChanged();
    });
    root->addWidget(buttons);

    setPreferences(initial);

    connect(enabled_, &QCheckBox::toggled, this, [this] { emitChanged(); });
    connect(threadWidth_, &QDoubleSpinBox::valueChanged, this, [this] { emitChanged(); });
    for (QSlider* s : {relief_, sheen_, twist_, shadow_, fabricRelief_}) {
        connect(s, &QSlider::valueChanged, this, [this] { emitChanged(); });
    }
    connect(quality_, &QComboBox::currentIndexChanged, this, [this] { emitChanged(); });
    connect(fabricOpaque_, &QCheckBox::toggled, this, [this] {
        updateFabricControls();
        emitChanged();
    });
    connect(fabricTexture_, &QComboBox::currentIndexChanged, this, [this] {
        updateFabricControls();
        emitChanged();
    });
    connect(fabricColorButton_, &QPushButton::clicked, this,
            &RealisticRenderDialog::chooseFabricColor);
}

RealisticPreferences RealisticRenderDialog::preferences() const {
    RealisticPreferences prefs;
    prefs.enabled = enabled_->isChecked();
    stitch_render::RenderParams& p = prefs.params;
    p.thread_width_mm = threadWidth_->value();
    p.relief = fromPercent(relief_->value());
    p.sheen = fromPercent(sheen_->value());
    p.twist = fromPercent(twist_->value());
    p.shadow = fromPercent(shadow_->value());
    p.fabric_rgb = {static_cast<std::uint8_t>(fabricColor_.red()),
                    static_cast<std::uint8_t>(fabricColor_.green()),
                    static_cast<std::uint8_t>(fabricColor_.blue())};
    p.fabric_opaque = fabricOpaque_->isChecked();
    p.fabric_texture =
        static_cast<stitch_render::FabricTexture>(fabricTexture_->currentData().toInt());
    p.fabric_relief = fromPercent(fabricRelief_->value());
    p.quality = static_cast<stitch_render::Quality>(quality_->currentData().toInt());
    return prefs;
}

void RealisticRenderDialog::setPreferences(const RealisticPreferences& prefs) {
    const stitch_render::RenderParams p = stitch_render::sanitized(prefs.params);
    updating_ = true;
    enabled_->setChecked(prefs.enabled);
    threadWidth_->setValue(p.thread_width_mm);
    relief_->setValue(toPercent(p.relief));
    sheen_->setValue(toPercent(p.sheen));
    twist_->setValue(toPercent(p.twist));
    shadow_->setValue(toPercent(p.shadow));
    quality_->setCurrentIndex(quality_->findData(static_cast<int>(p.quality)));
    fabricOpaque_->setChecked(p.fabric_opaque);
    fabricTexture_->setCurrentIndex(fabricTexture_->findData(static_cast<int>(p.fabric_texture)));
    fabricRelief_->setValue(toPercent(p.fabric_relief));
    fabricColor_ = QColor(p.fabric_rgb[0], p.fabric_rgb[1], p.fabric_rgb[2]);
    updateColorSwatch();
    updateFabricControls();
    updating_ = false;
}

void RealisticRenderDialog::setFabricColor(const QColor& color) {
    if (!color.isValid() || color == fabricColor_) {
        return;
    }
    fabricColor_ = color;
    updateColorSwatch();
    emitChanged();
}

void RealisticRenderDialog::emitChanged() {
    // Pas d'émission pendant une synchronisation depuis l'extérieur.
    if (updating_) {
        return;
    }
    emit preferencesChanged(preferences());
}

void RealisticRenderDialog::chooseFabricColor() {
    const QColor chosen = QColorDialog::getColor(fabricColor_, this, tr("Couleur du tissu"));
    if (chosen.isValid()) {
        setFabricColor(chosen);
    }
}

void RealisticRenderDialog::updateFabricControls() {
    const bool opaque = fabricOpaque_->isChecked();
    fabricColorButton_->setEnabled(opaque);
    fabricTexture_->setEnabled(opaque);
    fabricRelief_->setEnabled(opaque && fabricTexture_->currentData().toInt() !=
                                            static_cast<int>(stitch_render::FabricTexture::Plain));
}

void RealisticRenderDialog::updateColorSwatch() {
    fabricColorButton_->setText(fabricColor_.name().toUpper());
    fabricColorButton_->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: %2; }")
            .arg(fabricColor_.name(), fabricColor_.lightnessF() > 0.55
                                          ? QStringLiteral("#000000")
                                          : QStringLiteral("#ffffff")));
}

} // namespace openstitch::desktop
