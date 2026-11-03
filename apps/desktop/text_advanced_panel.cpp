// SPDX-License-Identifier: Apache-2.0
#include "text_advanced_panel.hpp"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "wheel_guard.hpp"

namespace openstitch::desktop {

namespace {

constexpr double kUmPerMm = 1000.0;
constexpr double kMaxCoordMm = 1000.0;

bool parseNumber(QString token, double& out) {
    token.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double v = token.toDouble(&ok);
    if (!ok || !std::isfinite(v)) {
        return false;
    }
    out = v;
    return true;
}

QString trimmedNumber(double v, int decimals = 3) {
    QString s = QString::number(v, 'f', decimals);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) {
            s.chop(1);
        }
        if (s.endsWith(QLatin1Char('.'))) {
            s.chop(1);
        }
    }
    return s;
}

Micrometers fromMm(double mm) {
    return to_micrometers(Millimeters{mm});
}

} // namespace

GlyphEditParse parseGlyphEdits(const QString& text) {
    static const QRegularExpression lineRe(QStringLiteral(R"(^(\d+)\s*:\s*(.*)$)"));
    GlyphEditParse out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const auto match = lineRe.match(line);
        const QString bad = QObject::tr("Ligne %1 ignorée : écrivez « 3: dx dy rotation échelle "
                                        "#RRGGBB » (mm, degrés, multiple).")
                                .arg(i + 1);
        if (!match.hasMatch() || match.captured(1).toInt() < 1) {
            out.errors << bad;
            continue;
        }
        const QStringList tokens = match.captured(2).split(
            QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
        document::GlyphEdit edit;
        edit.index = static_cast<std::uint32_t>(match.captured(1).toInt() - 1);
        double values[4] = {0.0, 0.0, 0.0, 1.0};
        int count = 0;
        bool valid = true;
        for (const QString& token : tokens) {
            if (token.startsWith(QLatin1Char('#'))) {
                const QColor color(token);
                if (!color.isValid()) {
                    valid = false;
                    break;
                }
                edit.rgb = std::array<std::uint8_t, 3>{static_cast<std::uint8_t>(color.red()),
                                                       static_cast<std::uint8_t>(color.green()),
                                                       static_cast<std::uint8_t>(color.blue())};
            } else if (count < 4 && parseNumber(token, values[count])) {
                ++count;
            } else {
                valid = false;
                break;
            }
        }
        if (!valid || std::abs(values[0]) > kMaxCoordMm || std::abs(values[1]) > kMaxCoordMm ||
            std::abs(values[2]) > 360.0 || !(values[3] >= 0.1 && values[3] <= 10.0)) {
            out.errors << bad;
            continue;
        }
        edit.dx = fromMm(values[0]);
        edit.dy = fromMm(values[1]);
        edit.rotation = Angle{values[2] * std::numbers::pi / 180.0};
        edit.scale = values[3];
        const auto same = std::find_if(out.edits.begin(), out.edits.end(),
                                       [&](const auto& e) { return e.index == edit.index; });
        if (same != out.edits.end()) {
            *same = edit; // la dernière ligne gagne
        } else {
            out.edits.push_back(edit);
        }
    }
    std::sort(out.edits.begin(), out.edits.end(),
              [](const auto& a, const auto& b) { return a.index < b.index; });
    return out;
}

QString formatGlyphEdits(const std::vector<document::GlyphEdit>& edits) {
    QStringList lines;
    for (const auto& e : edits) {
        QString line = QStringLiteral("%1: %2 %3 %4 %5")
                           .arg(e.index + 1)
                           .arg(trimmedNumber(static_cast<double>(e.dx.value) / kUmPerMm))
                           .arg(trimmedNumber(static_cast<double>(e.dy.value) / kUmPerMm))
                           .arg(trimmedNumber(e.rotation.radians * 180.0 / std::numbers::pi, 2))
                           .arg(trimmedNumber(e.scale, 3));
        if (e.rgb) {
            line += QLatin1Char(' ') +
                    QColor((*e.rgb)[0], (*e.rgb)[1], (*e.rgb)[2]).name(QColor::HexRgb).toUpper();
        }
        lines << line;
    }
    return lines.join(QLatin1Char('\n'));
}

PathParse parseBasePath(const QString& text) {
    PathParse out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QStringList tokens =
            line.split(QRegularExpression(QStringLiteral(R"([\s;]+)")), Qt::SkipEmptyParts);
        double x = 0.0;
        double y = 0.0;
        if (tokens.size() != 2 || !parseNumber(tokens[0], x) || !parseNumber(tokens[1], y) ||
            std::abs(x) > kMaxCoordMm || std::abs(y) > kMaxCoordMm) {
            out.errors << QObject::tr(
                              "Ligne %1 ignorée : écrivez « x y » en mm (|valeur| <= 1000).")
                              .arg(i + 1);
            continue;
        }
        out.points.push_back(Vec2um{fromMm(x), fromMm(y)});
    }
    return out;
}

QString formatBasePath(const std::vector<Vec2um>& points) {
    QStringList lines;
    for (const auto& p : points) {
        lines << trimmedNumber(static_cast<double>(p.x.value) / kUmPerMm) + QLatin1Char(' ') +
                     trimmedNumber(static_cast<double>(p.y.value) / kUmPerMm);
    }
    return lines.join(QLatin1Char('\n'));
}

TextAdvancedPanel::TextAdvancedPanel(const document::TextObject& initial, QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("text_advanced_panel"));
    auto* guard = new WheelGuard(this);

    base_ = new QComboBox(this);
    base_->setObjectName(QStringLiteral("text_base_combo"));
    base_->setAccessibleName(tr("Base du texte"));
    base_->addItem(tr("Ligne droite"), static_cast<int>(document::TextBaseKind::Line));
    base_->addItem(tr("Arc"), static_cast<int>(document::TextBaseKind::Arc));
    base_->addItem(tr("Cercle complet"), static_cast<int>(document::TextBaseKind::Circle));
    base_->addItem(tr("Chemin"), static_cast<int>(document::TextBaseKind::Path));
    base_->setCurrentIndex(std::max(0, base_->findData(static_cast<int>(initial.base.kind))));
    guard->guard(base_);

    radius_ = new QDoubleSpinBox(this);
    radius_->setObjectName(QStringLiteral("text_base_radius_spin"));
    radius_->setAccessibleName(tr("Rayon de la base"));
    radius_->setRange(1.0, 1000.0);
    radius_->setDecimals(1);
    radius_->setSingleStep(1.0);
    radius_->setSuffix(tr(" mm"));
    radius_->setValue(static_cast<double>(initial.base.radius.value) / kUmPerMm);
    guard->guard(radius_);

    inside_ = new QCheckBox(tr("Texte en dessous (sommets vers le centre)"), this);
    inside_->setObjectName(QStringLiteral("text_base_inside_check"));
    inside_->setChecked(initial.base.inside);

    path_ = new QPlainTextEdit(this);
    path_->setObjectName(QStringLiteral("text_base_path_edit"));
    path_->setAccessibleName(tr("Points du chemin"));
    path_->setMaximumHeight(64);
    path_->setPlaceholderText(tr("Un point par ligne : x y en mm, relatifs à l'origine"));
    path_->setPlainText(formatBasePath(initial.base.path));

    const auto connectorCombo = [&](const char* name, document::ConnectorMode mode) {
        auto* c = new QComboBox(this);
        c->setObjectName(QString::fromLatin1(name));
        c->addItem(tr("Règle générale des finitions"),
                   static_cast<int>(document::ConnectorMode::Inherit));
        c->addItem(tr("Toujours couper"), static_cast<int>(document::ConnectorMode::AlwaysTrim));
        c->addItem(tr("Ne jamais couper"), static_cast<int>(document::ConnectorMode::NeverTrim));
        c->setCurrentIndex(std::max(0, c->findData(static_cast<int>(mode))));
        guard->guard(c);
        return c;
    };
    letterConnector_ = connectorCombo("text_letter_connector_combo", initial.letter_connector);
    letterConnector_->setAccessibleName(tr("Connecteur entre lettres"));
    wordConnector_ = connectorCombo("text_word_connector_combo", initial.word_connector);
    wordConnector_->setAccessibleName(tr("Connecteur entre mots"));

    glyphEdits_ = new QPlainTextEdit(this);
    glyphEdits_->setObjectName(QStringLiteral("text_glyph_edits"));
    glyphEdits_->setAccessibleName(tr("Édition des lettres"));
    glyphEdits_->setMaximumHeight(72);
    glyphEdits_->setPlaceholderText(tr("Une lettre par ligne, ex. 3: 0,5 -1 15 1,2 #FF0000"));
    glyphEdits_->setToolTip(tr("Rang de la lettre (espaces exclus) : décalage X Y en mm, "
                               "rotation en degrés, échelle, couleur facultative."));
    glyphEdits_->setPlainText(formatGlyphEdits(initial.glyph_edits));

    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow(tr("Base"), base_);
    form->addRow(tr("Rayon"), radius_);
    form->addRow(QString(), inside_);
    form->addRow(tr("Chemin"), path_);
    form->addRow(tr("Coupe entre lettres"), letterConnector_);
    form->addRow(tr("Coupe entre mots"), wordConnector_);
    form->addRow(tr("Lettres"), glyphEdits_);

    connect(base_, &QComboBox::currentIndexChanged, this, [this] {
        syncEnabled();
        emit changed();
    });
    connect(radius_, &QDoubleSpinBox::valueChanged, this, &TextAdvancedPanel::changed);
    connect(inside_, &QCheckBox::toggled, this, &TextAdvancedPanel::changed);
    connect(path_, &QPlainTextEdit::textChanged, this, &TextAdvancedPanel::changed);
    connect(letterConnector_, &QComboBox::currentIndexChanged, this, &TextAdvancedPanel::changed);
    connect(wordConnector_, &QComboBox::currentIndexChanged, this, &TextAdvancedPanel::changed);
    connect(glyphEdits_, &QPlainTextEdit::textChanged, this, &TextAdvancedPanel::changed);
    syncEnabled();
}

void TextAdvancedPanel::syncEnabled() {
    const auto kind = static_cast<document::TextBaseKind>(base_->currentData().toInt());
    const bool curved =
        kind == document::TextBaseKind::Arc || kind == document::TextBaseKind::Circle;
    radius_->setEnabled(curved);
    inside_->setEnabled(curved);
    path_->setEnabled(kind == document::TextBaseKind::Path);
}

void TextAdvancedPanel::applyTo(document::TextObject& text) const {
    text.base.kind = static_cast<document::TextBaseKind>(base_->currentData().toInt());
    text.base.radius = fromMm(radius_->value());
    text.base.inside = inside_->isChecked();
    text.base.path = parseBasePath(path_->toPlainText()).points;
    text.letter_connector =
        static_cast<document::ConnectorMode>(letterConnector_->currentData().toInt());
    text.word_connector =
        static_cast<document::ConnectorMode>(wordConnector_->currentData().toInt());
    text.glyph_edits = parseGlyphEdits(glyphEdits_->toPlainText()).edits;
}

QStringList TextAdvancedPanel::errors() const {
    QStringList out;
    if (path_->isEnabled()) {
        out << parseBasePath(path_->toPlainText()).errors;
        if (parseBasePath(path_->toPlainText()).points.size() < 2) {
            out << tr("Base « chemin » : au moins deux points sont nécessaires (ligne droite "
                      "utilisée en attendant).");
        }
    }
    out << parseGlyphEdits(glyphEdits_->toPlainText()).errors;
    return out;
}

} // namespace openstitch::desktop
