// SPDX-License-Identifier: Apache-2.0
// Réglages avancés d'un texte (HP-TXT-005/008/014) : base (ligne, arc, cercle, chemin),
// connecteurs entre lettres / mots et éditions par lettre. Simple saisie : aucune logique
// de lettrage ici, le dialogue de texte relit `applyTo` et la bibliothèque calcule tout.
#pragma once

#include <QStringList>
#include <QWidget>

#include <vector>

#include "openstitch/document/text_object.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPlainTextEdit;

namespace openstitch::desktop {

// Éditions par lettre, une par ligne : « 3: dx dy rotation echelle #RRGGBB » (dx, dy en mm,
// rotation en degrés, échelle en multiple ; seuls les champs de tête sont obligatoires,
// virgule décimale acceptée). Le rang est celui de la lettre dans le texte (espaces exclus),
// compté à partir de 1 dans la saisie. Une ligne invalide est ignorée et signalée.
struct GlyphEditParse {
    std::vector<document::GlyphEdit> edits;
    QStringList errors;
};
[[nodiscard]] GlyphEditParse parseGlyphEdits(const QString& text);
[[nodiscard]] QString formatGlyphEdits(const std::vector<document::GlyphEdit>& edits);

// Points du chemin, un par ligne « x y » en mm relatifs à l'origine du texte.
struct PathParse {
    std::vector<Vec2um> points;
    QStringList errors;
};
[[nodiscard]] PathParse parseBasePath(const QString& text);
[[nodiscard]] QString formatBasePath(const std::vector<Vec2um>& points);

class TextAdvancedPanel : public QWidget {
    Q_OBJECT

public:
    explicit TextAdvancedPanel(const document::TextObject& initial, QWidget* parent = nullptr);

    // Reporte base, connecteurs et éditions de lettres dans `text` (autres champs intacts).
    void applyTo(document::TextObject& text) const;
    // Lignes rejetées à la dernière saisie (une phrase par ligne).
    [[nodiscard]] QStringList errors() const;

signals:
    void changed();

private:
    void syncEnabled();

    QComboBox* base_{nullptr};
    QDoubleSpinBox* radius_{nullptr};
    QCheckBox* inside_{nullptr};
    QPlainTextEdit* path_{nullptr};
    QComboBox* letterConnector_{nullptr};
    QComboBox* wordConnector_{nullptr};
    QPlainTextEdit* glyphEdits_{nullptr};
};

} // namespace openstitch::desktop
