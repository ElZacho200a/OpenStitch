// SPDX-License-Identifier: Apache-2.0
#include "text_dialog.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "font_catalog.hpp"
#include "openstitch/lettering/lettering.hpp"
#include "wheel_guard.hpp"

namespace openstitch::desktop {

namespace {

constexpr double kUmPerMm = 1000.0;

QDoubleSpinBox* mmSpin(QWidget* parent, const QString& name, double minMm, double maxMm,
                       double stepMm, int decimals = 1) {
    auto* s = new QDoubleSpinBox(parent);
    s->setObjectName(name);
    s->setRange(minMm, maxMm);
    s->setSingleStep(stepMm);
    s->setDecimals(decimals);
    s->setSuffix(QObject::tr(" mm"));
    return s;
}

double toMm(Micrometers um) {
    return static_cast<double>(um.value) / kUmPerMm;
}

Micrometers fromMm(double mm) {
    return to_micrometers(Millimeters{mm});
}

QColor toQColor(const std::array<std::uint8_t, 3>& rgb) {
    return QColor(rgb[0], rgb[1], rgb[2]);
}

} // namespace

TextPreview::TextPreview(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 160);
    setObjectName(QStringLiteral("text_preview"));
    setAccessibleName(tr("Aperçu du texte"));
}

void TextPreview::setShape(const QPainterPath& pathMm, const QColor& color) {
    path_ = pathMm;
    color_ = color;
    update();
}

void TextPreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    // Fond « toile » neutre : l'aperçu montre la forme, pas le thème de l'application.
    p.fillRect(rect(), QColor(250, 250, 248));
    if (path_.isEmpty()) {
        p.setPen(QColor(120, 120, 120));
        p.drawText(rect(), Qt::AlignCenter, tr("Tapez un texte pour voir l'aperçu"));
        return;
    }
    const QRectF box = path_.boundingRect();
    const double margin = 12.0;
    const QRectF avail = QRectF(rect()).adjusted(margin, margin, -margin, -margin);
    const double scale = std::min(avail.width() / std::max(box.width(), 0.1),
                                  avail.height() / std::max(box.height(), 0.1));
    p.translate(avail.center());
    p.scale(scale, scale);
    p.translate(-box.center());
    p.setPen(Qt::NoPen);
    p.setBrush(color_);
    p.drawPath(path_);
}

TextDialog::TextDialog(const document::TextObject& initial, bool isNew, QWidget* parent)
    : QDialog(parent), initial_(initial), color_(toQColor(initial.rgb)) {
    setObjectName(QStringLiteral("text_dialog"));
    setWindowTitle(isNew ? tr("Nouveau texte") : tr("Modifier le texte"));
    setModal(true);
    resize(760, 520);

    auto* guard = new WheelGuard(this);

    text_ = new QPlainTextEdit(this);
    text_->setObjectName(QStringLiteral("text_edit"));
    text_->setPlainText(QString::fromStdString(initial.text));
    text_->setTabChangesFocus(true);
    text_->setMaximumHeight(84);
    text_->setPlaceholderText(tr("Votre texte (Entrée : nouvelle ligne)"));
    text_->setAccessibleName(tr("Texte"));

    fontCombo_ = new QComboBox(this);
    fontCombo_->setObjectName(QStringLiteral("font_combo"));
    fontCombo_->setAccessibleName(tr("Police"));
    fontCombo_->setMaxVisibleItems(18);
    guard->guard(fontCombo_);
    const auto& entries = FontCatalog::instance().entries();
    const int selectedFont = FontCatalog::instance().indexOf(initial.font);
    if (selectedFont < 0) {
        // Police du document introuvable : entrée explicite, jamais un remplacement muet.
        const QString name = QString::fromStdString(
            initial.font.family.empty() ? initial.font.file : initial.font.family);
        fontCombo_->addItem(tr("%1 (introuvable)").arg(name), -1);
    }
    for (int i = 0; i < entries.size(); ++i) {
        fontCombo_->addItem(entries[i].display, i);
    }
    fontCombo_->setCurrentIndex(selectedFont < 0 ? 0 : fontCombo_->findData(selectedFont));

    height_ = mmSpin(this, QStringLiteral("height_spin"), 1.0, 300.0, 0.5);
    height_->setValue(toMm(initial.cap_height));
    height_->setToolTip(tr("Hauteur d'une capitale (la lettre « H »). Sous 5 mm le satin devient "
                           "fragile."));
    letterSpacing_ = mmSpin(this, QStringLiteral("letter_spacing_spin"), -10.0, 50.0, 0.1);
    letterSpacing_->setValue(toMm(initial.letter_spacing));
    wordSpacing_ = mmSpin(this, QStringLiteral("word_spacing_spin"), -10.0, 100.0, 0.5);
    wordSpacing_->setValue(toMm(initial.word_spacing));
    lineSpacing_ = new QDoubleSpinBox(this);
    lineSpacing_->setObjectName(QStringLiteral("line_spacing_spin"));
    lineSpacing_->setRange(0.5, 5.0);
    lineSpacing_->setSingleStep(0.1);
    lineSpacing_->setDecimals(2);
    lineSpacing_->setPrefix(QStringLiteral("× "));
    lineSpacing_->setValue(initial.line_spacing);
    lineSpacing_->setToolTip(
        tr("Distance entre deux lignes, en multiple de la hauteur de capitale."));

    align_ = new QComboBox(this);
    align_->setObjectName(QStringLiteral("align_combo"));
    align_->addItem(tr("Gauche"), static_cast<int>(document::TextAlign::Left));
    align_->addItem(tr("Centré"), static_cast<int>(document::TextAlign::Center));
    align_->addItem(tr("Droite"), static_cast<int>(document::TextAlign::Right));
    align_->addItem(tr("Justifié"), static_cast<int>(document::TextAlign::Justify));
    align_->setCurrentIndex(std::max(0, align_->findData(static_cast<int>(initial.align))));
    guard->guard(align_);
    justifyWidth_ = mmSpin(this, QStringLiteral("justify_width_spin"), 1.0, 1000.0, 1.0);
    justifyWidth_->setValue(initial.justify_width.value > 0 ? toMm(initial.justify_width) : 80.0);
    justifyWidth_->setToolTip(tr("Largeur à remplir en mode justifié (la dernière ligne reste "
                                 "alignée à gauche)."));

    fill_ = new QComboBox(this);
    fill_->setObjectName(QStringLiteral("fill_combo"));
    fill_->addItem(tr("Automatique (satin ou tatami selon le trait)"),
                   static_cast<int>(document::TextFill::Auto));
    fill_->addItem(tr("Satin (traits fins)"), static_cast<int>(document::TextFill::Satin));
    fill_->addItem(tr("Tatami (grosses lettres)"), static_cast<int>(document::TextFill::Tatami));
    fill_->addItem(tr("Contour"), static_cast<int>(document::TextFill::Contour));
    fill_->setCurrentIndex(std::max(0, fill_->findData(static_cast<int>(initial.fill))));
    guard->guard(fill_);
    maxSatin_ = mmSpin(this, QStringLiteral("max_satin_spin"), 1.0, 30.0, 0.5);
    maxSatin_->setValue(toMm(initial.max_satin_width));
    maxSatin_->setToolTip(
        tr("Largeur de trait au-delà de laquelle une lettre est cousue en tatami."));
    density_ = mmSpin(this, QStringLiteral("density_spin"), 0.2, 2.0, 0.05, 2);
    density_->setValue(toMm(initial.density));
    density_->setToolTip(tr("Écart entre les points de satin / les rangées de tatami."));
    kerning_ = new QCheckBox(tr("Crénage de la police"), this);
    kerning_->setObjectName(QStringLiteral("kerning_check"));
    kerning_->setChecked(initial.kerning);

    posX_ = mmSpin(this, QStringLiteral("pos_x_spin"), -2000.0, 2000.0, 1.0);
    posX_->setValue(toMm(initial.origin.x));
    posY_ = mmSpin(this, QStringLiteral("pos_y_spin"), -2000.0, 2000.0, 1.0);
    posY_->setValue(toMm(initial.origin.y));
    posX_->setToolTip(tr("Abscisse de la ligne de base (bord gauche, centre ou bord droit selon "
                         "l'alignement)."));
    rotation_ = new QDoubleSpinBox(this);
    rotation_->setObjectName(QStringLiteral("rotation_spin"));
    rotation_->setRange(-360.0, 360.0);
    rotation_->setDecimals(1);
    rotation_->setSuffix(QStringLiteral(" °"));
    rotation_->setValue(initial.rotation.radians * 180.0 / std::numbers::pi);

    colorButton_ = new QPushButton(this);
    colorButton_->setObjectName(QStringLiteral("text_color_button"));
    colorButton_->setAccessibleName(tr("Couleur du fil"));
    connect(colorButton_, &QPushButton::clicked, this, &TextDialog::pickColor);
    setColor(color_);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("text_summary"));
    summary_->setWordWrap(true);
    warningLabel_ = new QLabel(this);
    warningLabel_->setObjectName(QStringLiteral("text_warnings"));
    warningLabel_->setWordWrap(true);
    warningLabel_->setTextFormat(Qt::PlainText);

    preview_ = new TextPreview(this);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow(tr("Police"), fontCombo_);
    form->addRow(tr("Hauteur"), height_);
    form->addRow(tr("Couleur"), colorButton_);
    form->addRow(tr("Type de point"), fill_);
    form->addRow(tr("Alignement"), align_);
    form->addRow(tr("Largeur justifiée"), justifyWidth_);
    form->addRow(tr("Espace entre lettres"), letterSpacing_);
    form->addRow(tr("Espace entre mots"), wordSpacing_);
    form->addRow(tr("Interligne"), lineSpacing_);
    form->addRow(QString(), kerning_);
    form->addRow(tr("Satin jusqu'à"), maxSatin_);
    form->addRow(tr("Densité"), density_);
    form->addRow(tr("Position X"), posX_);
    form->addRow(tr("Position Y"), posY_);
    form->addRow(tr("Rotation"), rotation_);

    auto* left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("Texte :"), this));
    left->addWidget(text_);
    left->addLayout(form);

    auto* right = new QVBoxLayout;
    right->addWidget(preview_, 1);
    right->addWidget(summary_);
    right->addWidget(warningLabel_);

    auto* columns = new QHBoxLayout;
    columns->addLayout(left, 1);
    columns->addLayout(right, 1);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(isNew ? tr("Créer le texte") : tr("Appliquer"));
    buttons_->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(columns, 1);
    root->addWidget(buttons_);

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(80);
    connect(refreshTimer_, &QTimer::timeout, this, &TextDialog::refresh);

    connect(text_, &QPlainTextEdit::textChanged, this, &TextDialog::scheduleRefresh);
    connect(fontCombo_, &QComboBox::currentIndexChanged, this, &TextDialog::scheduleRefresh);
    connect(align_, &QComboBox::currentIndexChanged, this, &TextDialog::scheduleRefresh);
    connect(fill_, &QComboBox::currentIndexChanged, this, &TextDialog::scheduleRefresh);
    connect(kerning_, &QCheckBox::toggled, this, &TextDialog::scheduleRefresh);
    for (QDoubleSpinBox* s : {height_, letterSpacing_, wordSpacing_, lineSpacing_, justifyWidth_,
                              maxSatin_, density_, posX_, posY_, rotation_}) {
        connect(s, &QDoubleSpinBox::valueChanged, this, &TextDialog::scheduleRefresh);
        guard->guard(s);
    }

    refresh();
    text_->setFocus();
    text_->selectAll();
}

void TextDialog::setColor(const QColor& color) {
    color_ = color;
    QPixmap swatch(40, 16);
    swatch.fill(color);
    colorButton_->setIcon(QIcon(swatch));
    colorButton_->setText(color.name().toUpper());
}

void TextDialog::pickColor() {
    const QColor chosen = QColorDialog::getColor(color_, this, tr("Couleur du fil"));
    if (chosen.isValid()) {
        setColor(chosen);
        scheduleRefresh();
    }
}

void TextDialog::scheduleRefresh() {
    refreshTimer_->start();
}

void TextDialog::loadSelectedFont() {
    const int index = fontCombo_->currentData().toInt();
    if (index == lastFontIndex_) {
        return;
    }
    lastFontIndex_ = index;
    fontError_.clear();
    font_.reset();
    if (index < 0) {
        fontError_ = tr("Police introuvable : choisissez-en une autre pour modifier ce texte. Les "
                        "lettres déjà générées restent cousues.");
        return;
    }
    const FontEntry& entry = FontCatalog::instance().entries().at(index);
    font_ = FontCatalog::instance().load(FontCatalog::refOf(entry), &fontError_);
}

document::TextObject TextDialog::textObject() const {
    document::TextObject t = initial_;
    t.text = text_->toPlainText().toStdString();
    const int index = fontCombo_->currentData().toInt();
    if (index >= 0) {
        t.font = FontCatalog::refOf(FontCatalog::instance().entries().at(index));
    }
    t.cap_height = fromMm(height_->value());
    t.letter_spacing = fromMm(letterSpacing_->value());
    t.word_spacing = fromMm(wordSpacing_->value());
    t.line_spacing = lineSpacing_->value();
    t.kerning = kerning_->isChecked();
    t.align = static_cast<document::TextAlign>(align_->currentData().toInt());
    t.justify_width = fromMm(justifyWidth_->value());
    t.origin = Vec2um{fromMm(posX_->value()), fromMm(posY_->value())};
    t.rotation = Angle{rotation_->value() * std::numbers::pi / 180.0};
    t.rgb = {static_cast<std::uint8_t>(color_.red()), static_cast<std::uint8_t>(color_.green()),
             static_cast<std::uint8_t>(color_.blue())};
    t.fill = static_cast<document::TextFill>(fill_->currentData().toInt());
    t.max_satin_width = fromMm(maxSatin_->value());
    t.density = fromMm(density_->value());
    return t;
}

void TextDialog::refresh() {
    refreshTimer_->stop();
    loadSelectedFont();
    justifyWidth_->setEnabled(align_->currentData().toInt() ==
                              static_cast<int>(document::TextAlign::Justify));
    const bool needsSatin =
        fill_->currentData().toInt() != static_cast<int>(document::TextFill::Tatami) &&
        fill_->currentData().toInt() != static_cast<int>(document::TextFill::Contour);
    maxSatin_->setEnabled(needsSatin);

    warnings_.clear();
    QPainterPath shape;
    QString summary;
    bool ok = false;
    const document::TextObject t = textObject();
    if (!font_) {
        warnings_ << fontError_;
        for (const auto& w : lettering::check_text_size(t)) {
            warnings_ << QString::fromStdString(w.message);
        }
    } else {
        // Aperçu rapide : la vérification du satin par squelette est faite à la création.
        IdGenerator<ObjectId> ids;
        lettering::BuildOptions options;
        options.verify_satin = false;
        const auto built = lettering::build_text_objects(*font_, t, ids, options);
        if (!built) {
            warnings_ << QString::fromStdString(built.error().message);
        } else {
            for (const auto& w : built->warnings) {
                warnings_ << QString::fromStdString(w.message);
            }
            int satin = 0;
            int tatami = 0;
            int contour = 0;
            for (const auto& e : built->embroideries) {
                if (e.is_auto_satin()) {
                    ++satin;
                } else if (e.is_tatami()) {
                    ++tatami;
                } else {
                    ++contour;
                }
            }
            for (const auto& v : built->vectors) {
                for (const auto& set : v.paths) {
                    const auto addPath = [&shape](const geometry::Path& p) {
                        QPolygonF poly;
                        for (const auto& n : p.nodes) {
                            poly << QPointF(n.pos.x.value / kUmPerMm, -n.pos.y.value / kUmPerMm);
                        }
                        shape.addPolygon(poly);
                        shape.closeSubpath();
                    };
                    addPath(set.outer);
                    for (const auto& hole : set.holes) {
                        addPath(hole);
                    }
                }
            }
            shape.setFillRule(Qt::OddEvenFill);
            ok = !built->vectors.empty();
            if (ok) {
                summary = tr("%n lettre(s) : %1 satin, %2 tatami, %3 contour", nullptr,
                             static_cast<int>(built->layout.glyphs.size()))
                              .arg(satin)
                              .arg(tatami)
                              .arg(contour);
                // Largeur réelle du texte, utile pour le placer dans le cadre.
                summary += tr(" — %1 × %2 mm")
                               .arg(toMm(built->layout.width), 0, 'f', 1)
                               .arg(toMm(built->layout.height), 0, 'f', 1);
            }
        }
    }
    warnings_.removeAll(QString());
    preview_->setShape(shape, color_);
    summary_->setText(summary);
    warningLabel_->setText(warnings_.join(QLatin1Char('\n')));
    warningLabel_->setVisible(!warnings_.isEmpty());
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(ok);
}

} // namespace openstitch::desktop
