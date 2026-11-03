// SPDX-License-Identifier: Apache-2.0
#include "gradient_fill_panel.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace openstitch::desktop {

namespace {

QDoubleSpinBox* percentSpin(const QString& name, const QString& tip) {
    auto* spin = new QDoubleSpinBox;
    spin->setObjectName(name);
    spin->setRange(0.0, 100.0);
    spin->setDecimals(1);
    spin->setSuffix(QStringLiteral(" %"));
    spin->setKeyboardTracking(false);
    spin->setToolTip(tip);
    return spin;
}

QDoubleSpinBox* spacingSpin(const QString& name, const QString& tip) {
    auto* spin = new QDoubleSpinBox;
    spin->setObjectName(name);
    spin->setRange(0.1, 4.0);
    spin->setDecimals(2);
    spin->setSingleStep(0.05);
    spin->setSuffix(QStringLiteral(" mm"));
    spin->setKeyboardTracking(false);
    spin->setToolTip(tip);
    return spin;
}

} // namespace

GradientFillPanel::GradientFillPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("panel_gradientFill"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel(tr("Remplissage")));
    mode_ = new QComboBox;
    mode_->setObjectName(QStringLiteral("combo_fillMode"));
    mode_->addItem(tr("Uni"), false);
    mode_->addItem(tr("Dégradé"), true);
    mode_->setAccessibleName(tr("Apparence du remplissage"));
    mode_->setToolTip(tr("Uni : une seule couleur. Dégradé : deux couleurs qui se fondent ; "
                         "deux couches de couture sont gérées pour vous."));
    modeRow->addWidget(mode_, 1);
    root->addLayout(modeRow);

    reason_ = new QLabel;
    reason_->setObjectName(QStringLiteral("label_gradientReason"));
    reason_->setWordWrap(true);
    root->addWidget(reason_);

    gradientBox_ = new QWidget;
    gradientBox_->setObjectName(QStringLiteral("box_gradient"));
    root->addWidget(gradientBox_);

    connect(mode_, &QComboBox::activated, this, [this](int index) {
        if (!updating_) {
            emit modeRequested(mode_->itemData(index).toBool());
        }
    });
    setState({});
}

// Les réglages ne sont construits qu'au premier passage en dégradé : un remplissage uni
// n'ajoute aucun champ à l'inspecteur.
void GradientFillPanel::buildGradientControls() {
    if (built_) {
        return;
    }
    built_ = true;
    auto* box = new QVBoxLayout(gradientBox_);
    box->setContentsMargins(0, 0, 0, 0);

    auto* colors = new QHBoxLayout;
    startButton_ = colorButton(QStringLiteral("button_gradientStart"));
    startButton_->setToolTip(tr("Couleur de départ du dégradé"));
    endButton_ = colorButton(QStringLiteral("button_gradientEnd"));
    endButton_->setToolTip(tr("Couleur d'arrivée du dégradé"));
    invert_ = new QPushButton(tr("⇄ Inverser"));
    invert_->setObjectName(QStringLiteral("button_gradientInvert"));
    invert_->setToolTip(tr("Échange les deux couleurs (la transition reste en place)"));
    colors->addWidget(startButton_);
    colors->addWidget(invert_);
    colors->addWidget(endButton_);
    box->addLayout(colors);

    auto* form = new QFormLayout;
    zoneStart_ = percentSpin(QStringLiteral("spin_gradientZoneStart"),
                             tr("Début de la transition, en % de la forme le long de l'axe"));
    zoneEnd_ = percentSpin(QStringLiteral("spin_gradientZoneEnd"),
                           tr("Fin de la transition, en % de la forme le long de l'axe"));
    curve_ = new QComboBox;
    curve_->setObjectName(QStringLiteral("combo_gradientCurve"));
    curve_->addItem(tr("Linéaire"), 0);
    curve_->addItem(tr("Douce"), 1);
    curve_->addItem(tr("Marquée"), 2);
    form->addRow(tr("Début"), zoneStart_);
    form->addRow(tr("Fin"), zoneEnd_);
    form->addRow(tr("Courbe"), curve_);
    box->addLayout(form);

    handles_ = new QPushButton(tr("Poignées sur le canevas"));
    handles_->setObjectName(QStringLiteral("button_gradientHandles"));
    handles_->setCheckable(true);
    handles_->setToolTip(
        tr("Affiche des poignées pour déplacer le début et la fin de la transition, tourner "
           "l'axe et le déplacer. Maj : début et fin symétriques. Échap annule un glissement."));
    box->addWidget(handles_);

    advancedToggle_ = new QToolButton;
    advancedToggle_->setObjectName(QStringLiteral("button_gradientAdvanced"));
    advancedToggle_->setText(tr("Avancé"));
    advancedToggle_->setCheckable(true);
    advancedToggle_->setArrowType(Qt::RightArrow);
    advancedToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    advancedToggle_->setToolTip(tr("Espacements, profil des densités, assistant avec aperçu"));
    box->addWidget(advancedToggle_);
    advanced_ = new QWidget;
    advanced_->setObjectName(QStringLiteral("group_gradientAdvanced"));
    advanced_->setVisible(false);
    auto* advancedForm = new QFormLayout(advanced_);
    advancedForm->setContentsMargins(0, 0, 0, 0);
    spacingStart_ = spacingSpin(QStringLiteral("spin_gradientSpacingStart"),
                                tr("Écart entre rangées de la couche de dessus au début"));
    spacingEnd_ = spacingSpin(QStringLiteral("spin_gradientSpacingEnd"),
                              tr("Écart entre rangées de la couche de dessus à la fin"));
    profile_ = new QComboBox;
    profile_->setObjectName(QStringLiteral("combo_gradientProfile"));
    profile_->addItem(tr("Densités complémentaires"), true);
    profile_->addItem(tr("Fond uniforme"), false);
    auto* assistant = new QPushButton(tr("Assistant (aperçu, proposition depuis l'image)…"));
    assistant->setObjectName(QStringLiteral("button_gradientAssistant"));
    advancedForm->addRow(tr("Écart début"), spacingStart_);
    advancedForm->addRow(tr("Écart fin"), spacingEnd_);
    advancedForm->addRow(tr("Profil"), profile_);
    advancedForm->addRow(assistant);
    layers_ = new QLabel;
    layers_->setObjectName(QStringLiteral("label_gradientLayers"));
    layers_->setWordWrap(true);
    advancedForm->addRow(layers_);
    connect(advancedToggle_, &QToolButton::toggled, this, [this](bool open) {
        advancedToggle_->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        advanced_->setVisible(open);
    });
    box->addWidget(advanced_);
    connect(startButton_, &QPushButton::clicked, this, [this] { pickColor(true); });
    connect(endButton_, &QPushButton::clicked, this, [this] { pickColor(false); });
    connect(invert_, &QPushButton::clicked, this, [this] {
        std::swap(state_.start, state_.end);
        emitEdited();
    });
    for (auto* spin : {zoneStart_, zoneEnd_, spacingStart_, spacingEnd_}) {
        connect(spin, &QDoubleSpinBox::editingFinished, this, [this] { emitEdited(); });
    }
    connect(curve_, &QComboBox::activated, this, [this] { emitEdited(); });
    connect(profile_, &QComboBox::activated, this, [this] { emitEdited(); });
    connect(handles_, &QPushButton::toggled, this, [this](bool shown) {
        if (!updating_) {
            state_.handles = shown;
            emit handlesToggled(shown);
        }
    });
    connect(assistant, &QPushButton::clicked, this, &GradientFillPanel::assistantRequested);
}

QPushButton* GradientFillPanel::colorButton(const QString& objectName) {
    auto* button = new QPushButton;
    button->setObjectName(objectName);
    button->setMinimumHeight(28);
    return button;
}

void GradientFillPanel::paintSwatch(QPushButton* button, const QColor& color) {
    button->setText(color.name().toUpper());
    // Texte lisible sur n'importe quelle pastille, quel que soit le thème.
    const QColor text = color.lightnessF() > 0.55 ? QColor(20, 20, 20) : QColor(245, 245, 245);
    button->setStyleSheet(QStringLiteral("QPushButton { background:%1; color:%2; "
                                         "border:1px solid palette(mid); border-radius:4px; }")
                              .arg(color.name(), text.name()));
}

void GradientFillPanel::setState(const State& state) {
    state_ = state;
    updating_ = true;
    setVisible(state.visible);
    {
        const QSignalBlocker block(mode_);
        mode_->setCurrentIndex(state.gradient ? 1 : 0);
    }
    mode_->setEnabled(state.available || state.gradient);
    reason_->setText(state.reason);
    reason_->setVisible(!state.reason.isEmpty() && !state.gradient);
    if (state.gradient) {
        buildGradientControls();
    }
    gradientBox_->setVisible(state.gradient);
    if (state.gradient) {
        paintSwatch(startButton_, state.start);
        paintSwatch(endButton_, state.end);
        zoneStart_->setValue(state.zoneStartPercent);
        zoneEnd_->setValue(state.zoneEndPercent);
        curve_->setCurrentIndex(std::max(0, curve_->findData(state.curve)));
        spacingStart_->setValue(state.spacingStartMm);
        spacingEnd_->setValue(state.spacingEndMm);
        profile_->setCurrentIndex(state.complementary ? 0 : 1);
        handles_->setChecked(state.handles);
        layers_->setText(tr("%1 couches de couture, fond puis dessus (ordre réel dans la liste "
                            "« Couche »). Elles se modifient ensemble.")
                             .arg(state.layerCount));
    }
    updating_ = false;
}

void GradientFillPanel::pickColor(bool start) {
    const QColor chosen =
        QColorDialog::getColor(start ? state_.start : state_.end, this,
                               start ? tr("Couleur de départ") : tr("Couleur d'arrivée"));
    if (chosen.isValid()) {
        (start ? state_.start : state_.end) = chosen;
        emitEdited();
    }
}

void GradientFillPanel::emitEdited() {
    if (updating_) {
        return;
    }
    State next = state_;
    next.zoneStartPercent = zoneStart_->value();
    next.zoneEndPercent = zoneEnd_->value();
    next.curve = curve_->currentData().toInt();
    next.spacingStartMm = spacingStart_->value();
    next.spacingEndMm = spacingEnd_->value();
    next.complementary = profile_->currentData().toBool();
    emit edited(next);
}

} // namespace openstitch::desktop
