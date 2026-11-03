// SPDX-License-Identifier: Apache-2.0
#include "gradient_fill_panel.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QSizePolicy>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

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

// Diagramme conceptuel du mélange des couches, distinct des points réels.
class GradientProfilePreview final : public QWidget {
public:
    explicit GradientProfilePreview(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("widget_gradientProfile"));
        setAccessibleName(tr("Profil théorique du fondu"));
        setMinimumHeight(104);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void setState(const GradientFillPanel::State& state) {
        state_ = state;
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF area = QRectF(rect()).adjusted(9, 9, -9, -9);
        if (area.width() < 20 || area.height() < 20) return;
        p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
        p.drawRoundedRect(area, 4, 4);
        const QRectF graph = area.adjusted(5, 5, -5, -5);
        const double start = std::clamp(state_.zoneStartPercent / 100.0, 0.0, 0.999);
        const double end = std::clamp(state_.zoneEndPercent / 100.0, start + 0.001, 1.0);
        const auto blend = [&](double t) {
            const double v = std::clamp((t - start) / (end - start), 0.0, 1.0);
            if (state_.curve == 1) return v * v * (3.0 - 2.0 * v);
            if (state_.curve == 2)
                return v < 0.5 ? 2.0 * v * v : 1.0 - 2.0 * (1.0 - v) * (1.0 - v);
            return v;
        };
        QPainterPath base, top;
        for (int i = 0; i <= 100; ++i) {
            const double t = static_cast<double>(i) / 100.0;
            const double f = blend(t);
            const double a = state_.complementary ? 1.0 - f : 1.0;
            const double x = graph.left() + graph.width() * t;
            const QPointF one(x, graph.bottom() - graph.height() * a);
            const QPointF two(x, graph.bottom() - graph.height() * f);
            if (i == 0) { base.moveTo(one); top.moveTo(two); }
            else { base.lineTo(one); top.lineTo(two); }
        }
        p.setClipRect(area);
        p.setPen(QPen(state_.start, 2.5));
        p.drawPath(base);
        p.setPen(QPen(state_.end, 2.5));
        p.drawPath(top);
        p.setPen(QPen(palette().color(QPalette::Text), 1, Qt::DashLine));
        for (const double t : {start, end}) {
            const double x = graph.left() + graph.width() * t;
            p.drawLine(QPointF(x, graph.top()), QPointF(x, graph.bottom()));
        }
    }
private:
    GradientFillPanel::State state_;
};

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

    auto* threadForm = new QFormLayout;
    startThreadButton_ = new QPushButton(tr("Choisir dans les nuanciers…"));
    startThreadButton_->setObjectName(QStringLiteral("button_gradientStartThread"));
    endThreadButton_ = new QPushButton(tr("Choisir dans les nuanciers…"));
    endThreadButton_->setObjectName(QStringLiteral("button_gradientEndThread"));
    startThreadLabel_ = new QLabel;
    startThreadLabel_->setObjectName(QStringLiteral("label_gradientStartThread"));
    startThreadLabel_->setWordWrap(true);
    endThreadLabel_ = new QLabel;
    endThreadLabel_->setObjectName(QStringLiteral("label_gradientEndThread"));
    endThreadLabel_->setWordWrap(true);
    auto* a = new QWidget(gradientBox_);
    auto* ar = new QVBoxLayout(a);
    ar->setContentsMargins(0, 0, 0, 0);
    ar->addWidget(startThreadButton_);
    ar->addWidget(startThreadLabel_);
    auto* b = new QWidget(gradientBox_);
    auto* br = new QVBoxLayout(b);
    br->setContentsMargins(0, 0, 0, 0);
    br->addWidget(endThreadButton_);
    br->addWidget(endThreadLabel_);
    threadForm->addRow(tr("Fil A"), a);
    threadForm->addRow(tr("Fil B"), b);
    box->addLayout(threadForm);
    connect(startThreadButton_, &QPushButton::clicked, this,
            [this] { emit threadRequested(true); });
    connect(endThreadButton_, &QPushButton::clicked, this,
            [this] { emit threadRequested(false); });

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

    profilePreview_ = new GradientProfilePreview(gradientBox_);
    box->addWidget(profilePreview_);
    auto* profileDescription = new QLabel(
        tr("Profil théorique des couches ; ce schéma n'est pas un rendu de points."));
    profileDescription->setObjectName(QStringLiteral("label_gradientProfileDescription"));
    profileDescription->setWordWrap(true);
    box->addWidget(profileDescription);
    previewButton_ = new QPushButton(tr("Voir les points réels…"), gradientBox_);
    previewButton_->setObjectName(QStringLiteral("button_gradientLivePreview"));
    previewButton_->setToolTip(tr("Ouvre l'aperçu calculé des deux couches."));
    box->addWidget(previewButton_);
    densityButton_ = new QPushButton(tr("Voir la carte de densité…"), gradientBox_);
    densityButton_->setObjectName(QStringLiteral("button_gradientDensityMap"));
    densityButton_->setToolTip(tr("Affiche la densité des pénétrations sur le canevas "
                                  "et ses zones de vigilance dans Analyse."));
    box->addWidget(densityButton_);
    connect(densityButton_, &QPushButton::clicked, this,
            &GradientFillPanel::densityRequested);
    connect(previewButton_, &QPushButton::clicked, this,
            &GradientFillPanel::assistantRequested);

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
    densityNote_ = new QLabel;
    densityNote_->setObjectName(QStringLiteral("label_gradientDensityNote"));
    densityNote_->setWordWrap(true);
    box->addWidget(densityNote_);
    connect(startButton_, &QPushButton::clicked, this, [this] { pickColor(true); });
    connect(endButton_, &QPushButton::clicked, this, [this] { pickColor(false); });
    connect(invert_, &QPushButton::clicked, this, [this] {
        std::swap(state_.start, state_.end);
        std::swap(state_.startThread, state_.endThread);
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
        startThreadLabel_->setText(state.startThread.isEmpty() ? tr("Couleur libre (RGB)")
                                                              : state.startThread);
        endThreadLabel_->setText(state.endThread.isEmpty() ? tr("Couleur libre (RGB)")
                                                          : state.endThread);
        profilePreview_->setState(state);
        densityNote_->setText(state.complementary
                                  ? tr("Densités complémentaires : validez la couverture "
                                       "et la tension du tissu sur une broderie test.")
                                  : tr("Fond uniforme et dessus variable : les couches "
                                       "se superposent. Vérifiez la surcharge de points."));
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
