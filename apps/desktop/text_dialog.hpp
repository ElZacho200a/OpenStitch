// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QDialog>
#include <QPainterPath>
#include <QStringList>

#include <memory>

#include "openstitch/document/text_object.hpp"
#include "openstitch/lettering/font.hpp"

class QComboBox;
class QCheckBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;

namespace openstitch::desktop {

// Aperçu des lettres (contours remplis) : ne fait que peindre un QPainterPath calculé
// par la bibliothèque de lettrage.
class TextPreview : public QWidget {
    Q_OBJECT

public:
    explicit TextPreview(QWidget* parent = nullptr);
    void setShape(const QPainterPath& pathMm, const QColor& color);
    [[nodiscard]] bool hasShape() const { return !path_.isEmpty(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPainterPath path_;
    QColor color_{Qt::black};
};

// Dialogue de texte (outil Texte, double-clic sur une lettre, inspecteur). Ne modifie
// pas le document : renvoie un `TextObject` (intention) que MainWindow transforme en
// lettres par `lettering::build_text_objects` puis applique par `SetTextObjectCommand`.
// L'aperçu et les avertissements viennent de la bibliothèque de lettrage.
class TextDialog : public QDialog {
    Q_OBJECT

public:
    TextDialog(const document::TextObject& initial, bool isNew, QWidget* parent = nullptr);

    // Réglages courants du formulaire (id, origine et rotation d'`initial` conservés
    // quand le formulaire ne les expose pas).
    [[nodiscard]] document::TextObject textObject() const;
    // Avertissements de lettrage actuellement affichés (pour les tests).
    [[nodiscard]] QStringList warnings() const { return warnings_; }

private slots:
    void scheduleRefresh();
    void refresh();
    void pickColor();

private:
    void setColor(const QColor& color);
    void loadSelectedFont();

    document::TextObject initial_;
    QPlainTextEdit* text_{nullptr};
    QComboBox* fontCombo_{nullptr};
    QDoubleSpinBox* height_{nullptr};
    QDoubleSpinBox* letterSpacing_{nullptr};
    QDoubleSpinBox* wordSpacing_{nullptr};
    QDoubleSpinBox* lineSpacing_{nullptr};
    QComboBox* align_{nullptr};
    QDoubleSpinBox* justifyWidth_{nullptr};
    QComboBox* fill_{nullptr};
    QDoubleSpinBox* maxSatin_{nullptr};
    QDoubleSpinBox* density_{nullptr};
    QCheckBox* kerning_{nullptr};
    QDoubleSpinBox* posX_{nullptr};
    QDoubleSpinBox* posY_{nullptr};
    QDoubleSpinBox* rotation_{nullptr};
    QPushButton* colorButton_{nullptr};
    QLabel* summary_{nullptr};
    QLabel* warningLabel_{nullptr};
    TextPreview* preview_{nullptr};
    QDialogButtonBox* buttons_{nullptr};
    QTimer* refreshTimer_{nullptr};

    QColor color_;
    std::shared_ptr<lettering::Font> font_;
    QString fontError_;
    QStringList warnings_;
    int lastFontIndex_{-2};
};

} // namespace openstitch::desktop
