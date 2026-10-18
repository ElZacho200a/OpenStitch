// SPDX-License-Identifier: Apache-2.0
#include "color_blend_dialog.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "app_theme.hpp"
#include "openstitch/stitch_render/segments.hpp"
#include "responsive.hpp"
#include "wheel_guard.hpp"

namespace openstitch::desktop {
namespace {
// Étendue minimale de la zone de transition, en % de l'axe.
constexpr double kMinZonePercent = 5.0;
// Largeur du dialogue (px) à partir de laquelle réglages et aperçu se placent côte à côte.
constexpr int kSideBySideMinWidthPx = 900;

QColor color(ColorBlendDialog::Rgb rgb) {
    return QColor(rgb[0], rgb[1], rgb[2]);
}
ColorBlendDialog::Rgb rgb(const QString& text) {
    const QColor c(text);
    return {static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
            static_cast<std::uint8_t>(c.blue())};
}
} // namespace

// Peinture uniquement : aucune génération de points, ni mutation du document.
class BlendStitchPreview : public QWidget {
public:
    explicit BlendStitchPreview(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("blend_preview"));
        setMinimumSize(460, 260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAccessibleName(tr("Aperçu des points calculés sur la forme"));
        invalidate(tr("L’aperçu sera calculé à l’ouverture."));
        connect(&AppTheme::instance(), &AppTheme::changed, this, [this] { update(); });
    }

    void setSequence(const stitch::StitchSequence& sequence, ObjectId background,
                     ObjectId foreground, ColorBlendDialog::Rgb backgroundRgb,
                     ColorBlendDialog::Rgb foregroundRgb) {
        sequence_ = sequence;
        background_ = background;
        foreground_ = foreground;
        backgroundRgb_ = backgroundRgb;
        foregroundRgb_ = foregroundRgb;
        counts_ = {};
        for (const auto& command : sequence_.commands) {
            if (command.type != stitch::CommandType::Stitch) {
                continue;
            }
            if (command.source == background_) {
                ++counts_[0];
            } else if (command.source == foreground_) {
                ++counts_[1];
            }
        }
        segments_ = buildSegments(0);
        bounds_ = stitch_render::segments_bounds(segments_);
        if (layer_ != 0) {
            segments_ = buildSegments(layer_);
        }
        current_ = true;
        message_.clear();
        updateState();
    }

    void invalidate(const QString& message) {
        // On garde la dernière séquence pour les changements de filtre/thème,
        // mais on ne présente jamais ses anciens points comme un aperçu actuel.
        current_ = false;
        message_ = message;
        updateState();
    }

    void setLayer(int layer) {
        if (layer_ == layer) {
            return;
        }
        layer_ = layer;
        if (current_) {
            segments_ = buildSegments(layer_);
        }
        updateState();
    }

    [[nodiscard]] bool current() const { return current_; }
    [[nodiscard]] std::size_t stitchCount() const {
        if (!current_) {
            return 0;
        }
        return layer_ == 1 ? counts_[0] : layer_ == 2 ? counts_[1] : counts_[0] + counts_[1];
    }
    [[nodiscard]] std::size_t segmentCount() const { return current_ ? segments_.size() : 0; }
    [[nodiscard]] std::size_t backgroundCount() const { return counts_[0]; }
    [[nodiscard]] std::size_t foregroundCount() const { return counts_[1]; }
    [[nodiscard]] ColorBlendDialog::Rgb backgroundRgb() const { return backgroundRgb_; }
    [[nodiscard]] ColorBlendDialog::Rgb foregroundRgb() const { return foregroundRgb_; }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& tokens = AppTheme::instance().tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), tokens.canvasBackground);
        painter.setPen(tokens.border);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
        if (!current_ || segments_.empty()) {
            painter.setPen(tokens.textSecondary);
            painter.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap,
                             current_ ? tr("Aucun segment cousu dans cette couche.") : message_);
            return;
        }
        const QRectF viewport = QRectF(rect()).adjusted(22, 22, -22, -22);
        const double scale = std::min(viewport.width() / std::max(1.0, bounds_.width()),
                                      viewport.height() / std::max(1.0, bounds_.height()));
        painter.translate(viewport.center());
        painter.scale(scale, scale);
        painter.translate(-(bounds_.x0 + bounds_.x1) / 2.0, -(bounds_.y0 + bounds_.y1) / 2.0);
        // build_thread_segments convertit déjà Y haut en Y bas, en mm.
        QPen pen;
        pen.setWidthF(0.25); // diamètre de fil physique approximatif, en mm
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        for (const auto& segment : segments_) {
            pen.setColor(color(segment.rgb));
            painter.setPen(pen);
            painter.drawLine(QPointF(segment.ax, segment.ay), QPointF(segment.bx, segment.by));
        }
    }

private:
    [[nodiscard]] std::vector<stitch_render::ThreadSegment> buildSegments(int layer) const {
        return stitch_render::build_thread_segments(
            sequence_, sequence_.commands.size(),
            [this, layer](ObjectId source) -> std::optional<ColorBlendDialog::Rgb> {
                if (source == background_ && layer != 2) {
                    return backgroundRgb_;
                }
                if (source == foreground_ && layer != 1) {
                    return foregroundRgb_;
                }
                return std::nullopt;
            });
    }

    void updateState() {
        // État d'affichage inspectable, également décrit pour l'accessibilité.
        setProperty("hasStitchPreview", current_ && !segments_.empty());
        setProperty("stitchCount", static_cast<qulonglong>(stitchCount()));
        setProperty("segmentCount", static_cast<qulonglong>(segmentCount()));
        setAccessibleDescription(current_ ? tr("%1 points cousus · %2 segments de fil visibles.")
                                                .arg(static_cast<qulonglong>(stitchCount()))
                                                .arg(static_cast<qulonglong>(segmentCount()))
                                          : message_);
        update();
    }

    stitch::StitchSequence sequence_;
    ObjectId background_{};
    ObjectId foreground_{};
    ColorBlendDialog::Rgb backgroundRgb_{};
    ColorBlendDialog::Rgb foregroundRgb_{};
    std::array<std::size_t, 2> counts_{};
    std::vector<stitch_render::ThreadSegment> segments_;
    stitch_render::RectMm bounds_{};
    QString message_;
    int layer_{0};
    bool current_{false};
};

ColorBlendDialog::ColorBlendDialog(Rgb background, Rgb foreground, bool canAnalyze,
                                   bool mergesZones, QWidget* parent)
    : QDialog(parent), mergesZones_(mergesZones) {
    setObjectName(QStringLiteral("colorBlendDialog"));
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    explanation_ = new QLabel(this);
    explanation_->setObjectName(QStringLiteral("blend_explanation"));
    explanation_->setWordWrap(true);
    layout->addWidget(explanation_);
    auto* content = new QHBoxLayout();
    contentLayout_ = content;
    auto* settings = new QVBoxLayout();
    auto* layers = new QGroupBox(tr("1 · Choisir les deux couches"), this);
    auto* layersLayout = new QVBoxLayout(layers);
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
    profile_ = new QComboBox(this);
    profile_->setObjectName(QStringLiteral("combo_blendProfile"));
    profile_->addItems({tr("Densités complémentaires"), tr("Fond uniforme")});
    form->addRow(tr("Profil du fondu :"), profile_);
    layersLayout->addLayout(form);
    profileNote_ = new QLabel(this);
    profileNote_->setObjectName(QStringLiteral("blend_profileNote"));
    profileNote_->setWordWrap(true);
    markSecondaryText(profileNote_);
    layersLayout->addWidget(profileNote_);
    settings->addWidget(layers);

    auto* ramp = new QGroupBox(tr("2 · Régler la transition"), this);
    auto* rampLayout = new QVBoxLayout(ramp);
    auto* rampForm = new QFormLayout();
    const auto spacing = [this, rampForm](const QString& title, const char* name, double value) {
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
        rampForm->addRow(title, spin);
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
    rampForm->addRow(tr("Angle du fondu :"), angle_);
    const auto zone = [this, rampForm](const QString& title, const char* name, double value,
                                       const QString& tip) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setObjectName(QString::fromLatin1(name));
        spin->setRange(0.0, 100.0);
        spin->setDecimals(0);
        spin->setSingleStep(5.0);
        spin->setSuffix(tr(" %"));
        spin->setKeyboardTracking(false);
        spin->setValue(value);
        spin->setToolTip(tip);
        rampForm->addRow(title, spin);
        return spin;
    };
    zoneStart_ = zone(tr("Début de la transition :"), "spin_blendZoneStart", 0.0,
                      tr("Position sur l'axe où l'écart commence à varier (0 % = bord de départ). "
                         "Avant ce point, l'écart du début reste constant : aplat net."));
    zoneEnd_ = zone(tr("Fin de la transition :"), "spin_blendZoneEnd", 100.0,
                    tr("Position sur l'axe où l'écart atteint sa valeur de fin (100 % = bord "
                       "d'arrivée). Après ce point, l'écart de fin reste constant : aplat net."));
    curve_ = new QComboBox(this);
    curve_->setObjectName(QStringLiteral("combo_blendCurve"));
    curve_->addItem(tr("Linéaire"), static_cast<int>(document::DensityCurve::Linear));
    curve_->addItem(tr("Douce"), static_cast<int>(document::DensityCurve::Smooth));
    curve_->addItem(tr("Marquée"), static_cast<int>(document::DensityCurve::Steep));
    curve_->setToolTip(tr("Forme de la transition entre les deux écarts. Linéaire : régulière. "
                          "Douce : départ et arrivée adoucis. Marquée : aplats plus longs, "
                          "changement plus rapide au milieu."));
    rampForm->addRow(tr("Courbe de transition :"), curve_);
    rampLayout->addLayout(rampForm);

    auto* reverse = new QPushButton(tr("Inverser le sens du fondu"), this);
    reverse->setObjectName(QStringLiteral("button_blendReverse"));
    connect(reverse, &QPushButton::clicked, this, [this] {
        const double start = start_->value();
        const QSignalBlocker b1(start_), b2(end_);
        start_->setValue(end_->value());
        end_->setValue(start);
        settingsChanged();
    });
    rampLayout->addWidget(reverse);
    settings->addWidget(ramp);
    auto* analyze = new QPushButton(tr("Proposer les couleurs depuis l'image"), this);
    analyze->setObjectName(QStringLiteral("button_blendAnalyze"));
    analyze->setEnabled(canAnalyze);
    analyze->setToolTip(canAnalyze ? tr("Analyse la forme et utilise le nuancier actif du "
                                        "panneau Fils. La proposition reste modifiable.")
                                   : tr("Une image raster sous une forme unique est nécessaire. "
                                        "Le fondu manuel reste disponible."));
    connect(analyze, &QPushButton::clicked, this, &ColorBlendDialog::analysisRequested);
    settings->addWidget(analyze);
    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("blend_message"));
    message_->setWordWrap(true);
    settings->addWidget(message_);
    settings->addStretch();
    content->addLayout(settings);

    auto* previewGroup = new QGroupBox(tr("3 · Vérifier les points sur la forme"), this);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    auto* previewControls = new QHBoxLayout();
    previewLayer_ = new QComboBox(this);
    previewLayer_->setObjectName(QStringLiteral("combo_blendPreviewLayer"));
    previewLayer_->setAccessibleName(tr("Couches affichées dans l’aperçu"));
    previewLayer_->addItems({tr("Deux couches"), tr("Fond seul"), tr("Dessus seul")});
    previewControls->addWidget(previewLayer_, 1);
    refresh_ = new QPushButton(tr("Actualiser l’aperçu"), this);
    refresh_->setObjectName(QStringLiteral("button_blendPreview"));
    previewControls->addWidget(refresh_);
    previewLayout->addLayout(previewControls);
    preview_ = new BlendStitchPreview(this);
    previewLayout->addWidget(preview_, 1);
    previewStats_ = new QLabel(this);
    previewStats_->setObjectName(QStringLiteral("blend_previewStats"));
    previewLayout->addWidget(previewStats_);
    previewLegend_ = new QLabel(this);
    previewLegend_->setObjectName(QStringLiteral("blend_previewLegend"));
    previewLegend_->setTextFormat(Qt::RichText);
    previewLegend_->setWordWrap(true);
    previewLayout->addWidget(previewLegend_);
    auto* note = new QLabel(tr("Aperçu des points calculés, sans simulation du tissu. "
                               "Les sauts ne sont pas tracés. Vue ajustée à la forme."),
                            this);
    note->setWordWrap(true);
    markSecondaryText(note);
    previewLayout->addWidget(note);
    content->addWidget(previewGroup, 1);
    layout->addLayout(content, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    accept_ = buttons->button(QDialogButtonBox::Ok);
    accept_->setObjectName(QStringLiteral("button_blendAccept"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    // Le contenu défile sur un petit écran, OK / Annuler restent visibles (HP-UX-025).
    scrollDialogBody(this, buttons);
    resize(kSideBySideMinWidthPx + 40, 600);
    fitDialogToScreen(this);
    previewTimer_ = new QTimer(this);
    previewTimer_->setObjectName(QStringLiteral("timer_blendPreview"));
    previewTimer_->setSingleShot(true);
    previewTimer_->setInterval(180);
    connect(previewTimer_, &QTimer::timeout, this, &ColorBlendDialog::requestPreview);
    connect(refresh_, &QPushButton::clicked, this, &ColorBlendDialog::settingsChanged);
    connect(previewLayer_, &QComboBox::currentIndexChanged, this, [this](int layer) {
        preview_->setLayer(layer);
        updatePreviewDetails();
    });
    connect(curve_, &QComboBox::currentIndexChanged, this, &ColorBlendDialog::settingsChanged);
    connect(profile_, &QComboBox::currentIndexChanged, this, [this] {
        updateExplanation();
        settingsChanged();
    });
    for (auto* edit : {background_, foreground_}) {
        connect(edit, &QLineEdit::textChanged, this, &ColorBlendDialog::settingsChanged);
    }
    for (auto* spin : {start_, end_, angle_, zoneStart_, zoneEnd_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, &ColorBlendDialog::settingsChanged);
    }
    // La zone garde toujours au moins kMinZonePercent d'étendue : les bornes
    // se resserrent mutuellement plutôt que d'accepter une zone vide.
    connect(zoneStart_, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { zoneEnd_->setMinimum(value + kMinZonePercent); });
    connect(zoneEnd_, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { zoneStart_->setMaximum(value - kMinZonePercent); });
    zoneEnd_->setMinimum(kMinZonePercent);
    zoneStart_->setMaximum(100.0 - kMinZonePercent);
    auto* guard = new WheelGuard(this);
    guard->guardAll(this);
    setEditing(false);
    settingsChanged();
}

bool ColorBlendDialog::validSettings() const {
    return background_->hasAcceptableInput() && foreground_->hasAcceptableInput() &&
           QColor(background_->text()) != QColor(foreground_->text());
}

void ColorBlendDialog::settingsChanged() {
    ++settingsRevision_;
    const bool valid = background_->hasAcceptableInput() && foreground_->hasAcceptableInput();
    const bool distinct = validSettings();
    accept_->setEnabled(distinct);
    refresh_->setEnabled(distinct);
    previewTimer_->stop();
    if (!distinct) {
        previewPending_ = false;
        message_->setText(valid ? tr("Choisissez deux couleurs différentes.")
                                : tr("Saisissez deux couleurs valides au format #RRGGBB."));
        preview_->invalidate(tr("Aperçu indisponible : corrigez les couleurs des deux fils."));
        updatePreviewDetails();
        return;
    }
    message_->clear();
    preview_->invalidate(tr("Aperçu à actualiser · les réglages ont changé."));
    updatePreviewDetails();
    if (requestingPreview_) {
        previewPending_ = true;
    } else if (shown_ && isVisible()) {
        previewTimer_->start();
    }
}

void ColorBlendDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (contentLayout_ != nullptr) {
        contentLayout_->setDirection(width() >= kSideBySideMinWidthPx ? QBoxLayout::LeftToRight
                                                                      : QBoxLayout::TopToBottom);
    }
}

void ColorBlendDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    const bool needsPreview = !shown_ || !preview_->current();
    shown_ = true;
    if (needsPreview && !requestingPreview_ && validSettings()) {
        previewTimer_->start();
    }
}

void ColorBlendDialog::requestPreview() {
    if (!validSettings() || !isVisible()) {
        return;
    }
    if (requestingPreview_) {
        previewPending_ = true;
        return;
    }
    requestingPreview_ = true;
    previewPending_ = false;
    requestedRevision_ = settingsRevision_;
    preview_->invalidate(tr("Calcul de l’aperçu des points…"));
    updatePreviewDetails();
    emit previewRequested();
    requestingPreview_ = false;
    // BusyIndicator peut ouvrir une boucle d'événements pendant le calcul.
    // Un résultat ne déclenche jamais à lui seul une seconde génération.
    if (previewPending_ && validSettings() && isVisible()) {
        previewTimer_->start();
    }
}

void ColorBlendDialog::setStitchPreview(const stitch::StitchSequence& sequence, ObjectId background,
                                        ObjectId foreground, Rgb backgroundRgb, Rgb foregroundRgb) {
    if (!validSettings() || (requestingPreview_ && requestedRevision_ != settingsRevision_)) {
        return;
    }
    previewTimer_->stop();
    previewPending_ = false;
    preview_->setSequence(sequence, background, foreground, backgroundRgb, foregroundRgb);
    updatePreviewDetails();
}

void ColorBlendDialog::setPreviewMessage(const QString& message) {
    if (requestingPreview_ && requestedRevision_ != settingsRevision_) {
        return;
    }
    previewTimer_->stop();
    previewPending_ = false;
    preview_->invalidate(message);
    updatePreviewDetails();
}

void ColorBlendDialog::updatePreviewDetails() {
    if (!preview_->current()) {
        previewStats_->setText(tr("Aucun aperçu actuel des points."));
        previewLegend_->clear();
        return;
    }
    previewStats_->setText(tr("%1 points cousus · %2 segments affichés")
                               .arg(static_cast<qulonglong>(preview_->stitchCount()))
                               .arg(static_cast<qulonglong>(preview_->segmentCount())));
    const auto legend = [this](const QString& name, Rgb value, std::size_t count) {
        const QString hex = color(value).name();
        return tr("<span style=\"color:%1\">●</span> %2 · %1 · %3 points")
            .arg(hex, name.toHtmlEscaped())
            .arg(static_cast<qulonglong>(count));
    };
    previewLegend_->setText(
        legend(tr("Fond, premier fil"), preview_->backgroundRgb(), preview_->backgroundCount()) +
        QStringLiteral("<br>") +
        legend(tr("Dessus, second fil"), preview_->foregroundRgb(), preview_->foregroundCount()));
}

bool ColorBlendDialog::complementary() const {
    return profile_->currentIndex() == 0;
}

void ColorBlendDialog::setComplementary(bool complementary) {
    profile_->setCurrentIndex(complementary ? 0 : 1);
}

void ColorBlendDialog::setEditing(bool editing) {
    editing_ = editing;
    setWindowTitle(editing ? tr("Modifier le fondu de couleurs · deux fils")
                           : tr("Fondu de couleurs · deux fils"));
    accept_->setText(editing ? tr("Appliquer les modifications") : tr("Créer le fondu"));
    updateExplanation();
}

void ColorBlendDialog::updateExplanation() {
    explanation_->setText(
        editing_       ? tr("Modifiez les deux couches du fondu existant. Le fond et le dessus "
                                  "conservent leurs identifiants (IDs) et leur ordre de couture. "
                                  "Aucune couche supplémentaire n’est créée. Annulable en une fois.")
        : mergesZones_ ? tr("Les deux zones seront unies en une forme avec deux couches. "
                            "Le fond sera cousu en premier, puis le dessus juste après. "
                            "Annulable en une fois.")
                       : tr("Deux couches sur la même forme : le fond est cousu en premier, "
                            "puis le dessus juste après. Choisissez deux fils et le profil "
                            "du fondu, avec ou sans image. Annulable en une fois."));
    profileNote_->setText(
        complementary()
            ? tr("Les densités du fond et du dessus évoluent en sens opposés : une couche "
                 "prend le relais de l’autre. La sous-couche est désactivée sur les deux "
                 "couches pour ne pas surcharger le fondu.")
            : tr("Le fond reste uniforme ; seul l’écart entre les lignes du dessus varie. "
                 "Les grands écarts laissent voir le premier fil. La sous-couche du dessus "
                 "reste désactivée."));
}

void ColorBlendDialog::setProposal(Rgb background, Rgb foreground,
                                   const document::DensityGradient& gradient) {
    const QSignalBlocker b1(background_), b2(foreground_), b3(start_), b4(end_), b5(angle_),
        b6(curve_);
    curve_->setCurrentIndex(std::max(0, curve_->findData(static_cast<int>(gradient.curve))));
    background_->setText(color(background).name());
    foreground_->setText(color(foreground).name());
    start_->setValue(to_millimeters(gradient.spacing_from).value);
    end_->setValue(to_millimeters(gradient.spacing_to).value);
    const double angle =
        std::atan2(static_cast<double>(gradient.to.y.value - gradient.from.y.value),
                   static_cast<double>(gradient.to.x.value - gradient.from.x.value));
    angle_->setValue(std::fmod(angle * 180.0 / std::numbers::pi + 360.0, 360.0));
    proposal_ = gradient;
    settingsChanged();
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
document::DensityCurve ColorBlendDialog::curve() const {
    return static_cast<document::DensityCurve>(curve_->currentData().toInt());
}
double ColorBlendDialog::transitionStart() const {
    return zoneStart_->value() / 100.0;
}
double ColorBlendDialog::transitionEnd() const {
    return zoneEnd_->value() / 100.0;
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
