// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>
#include <QImage>
#include <QString>
#include <memory>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_analysis/production_sheet.hpp"
#include "openstitch/stitch_render/params.hpp"

class QDateEdit;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QPrinter;
class QTextBrowser;
class QTextDocument;
class QTimer;

namespace openstitch::desktop {

// Dialogue « Fiche de production » (Fichier > Fiche de production…) : saisie du nom, de la
// date, de la vitesse supposée et des notes ; la fiche elle-même (chiffres, blocs,
// avertissements) est calculée par `stitch_analysis::make_production_sheet` puis mise en
// page en HTML par la même bibliothèque -- ce dialogue ne calcule rien, il affiche ce
// document, l'imprime (QPrinter) ou l'exporte en PDF A4 portrait (QPdfWriter).
//
// `project` et `sequence` (séquence EFFECTIVE) doivent survivre au dialogue (modal).
class ProductionDialog : public QDialog {
    Q_OBJECT

public:
    ProductionDialog(const document::Project& project, const stitch::StitchSequence& sequence,
                     const QString& suggestedName, const stitch_render::RenderParams& renderParams,
                     QWidget* parent = nullptr);
    ~ProductionDialog() override;

    // Fiche courante (reflète les champs saisis après `refreshNow()`).
    [[nodiscard]] const stitch_analysis::ProductionSheet& sheet() const { return sheet_; }
    // Recalcule tout de suite (sinon différé de quelques ms après une saisie).
    void refreshNow();
    // Écrit la fiche en PDF A4 portrait. Faux si le fichier n'a pas pu être écrit.
    bool exportPdf(const QString& path);

    // Accès pour les tests.
    [[nodiscard]] QPlainTextEdit* notesEdit() const { return notes_; }
    [[nodiscard]] QDoubleSpinBox* speedSpin() const { return speed_; }
    [[nodiscard]] QLineEdit* nameEdit() const { return name_; }

private:
    void buildDocument(QTextDocument& doc) const;
    void configurePrinter(QPrinter& printer) const;
    void printDocument(QPrinter* printer) const;
    void print();
    void printPreview();
    void exportPdfInteractive();
    [[nodiscard]] QImage renderPreviewImage() const;

    const document::Project& project_;
    const stitch::StitchSequence& sequence_;
    stitch_render::RenderParams renderParams_;
    stitch_analysis::ProductionSheet sheet_;
    QImage previewImage_;

    QLineEdit* name_{nullptr};
    QDateEdit* date_{nullptr};
    QDoubleSpinBox* speed_{nullptr};
    QPlainTextEdit* notes_{nullptr};
    QTextBrowser* view_{nullptr};
    QTimer* timer_{nullptr};
};

} // namespace openstitch::desktop
