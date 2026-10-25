// SPDX-License-Identifier: Apache-2.0
#include "placement_template_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QMarginsF>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPen>
#include <QPrinter>
#include <QPushButton>
#include <QVBoxLayout>

#include <string>
#include <vector>

#include "print_preview.hpp"

namespace openstitch::desktop {

namespace {

void drawSegments(QPainter& p, const std::vector<stitch_analysis::TemplateSegment>& segs,
                  const QPen& pen) {
    if (segs.empty()) {
        return;
    }
    p.setPen(pen);
    for (const auto& s : segs) {
        p.drawLine(QPointF(s.a.x.value, s.a.y.value), QPointF(s.b.x.value, s.b.y.value));
    }
}

QPen penMm(const QColor& color, double widthMm, Qt::PenStyle style = Qt::SolidLine) {
    QPen pen(color);
    pen.setWidthF(widthMm * 1000.0); // le peintre travaille en µm
    pen.setStyle(style);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

} // namespace

void paintPlacementTemplate(QPagedPaintDevice* device,
                            const stitch_analysis::PlacementTemplate& tpl) {
    device->setPageSize(
        QPageSize(QSizeF(tpl.page_width.value / 1000.0, tpl.page_height.value / 1000.0),
                  QPageSize::Millimeter));
    device->setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    QPainter painter(device);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int dpi = device->logicalDpiX();
    // Origine = coin du PAPIER (une imprimante réelle décale la zone imprimable).
    const QPageLayout layout = device->pageLayout();
    const QRect full = layout.fullRectPixels(dpi);
    const QRect paint = layout.paintRectPixels(dpi);
    painter.translate(full.topLeft() - paint.topLeft());
    painter.scale(dpi / 25400.0, dpi / 25400.0); // 1 unité = 1 µm
    bool first = true;
    for (const auto& page : tpl.pages) {
        if (!first) {
            device->newPage();
        }
        first = false;
        drawSegments(painter, page.stitches, penMm(QColor(30, 30, 30), 0.15));
        drawSegments(painter, page.axes, penMm(QColor(120, 120, 120), 0.12, Qt::DashLine));
        drawSegments(painter, page.crosshair, penMm(QColor(200, 30, 30), 0.3));
        drawSegments(painter, page.marks, penMm(Qt::black, 0.2));
        drawSegments(painter, page.ruler, penMm(Qt::black, 0.3));
        painter.setPen(QColor(50, 50, 50));
        for (const auto& t : page.texts) {
            QFont font(QStringLiteral("Helvetica"));
            font.setPixelSize(static_cast<int>(t.size_pt * 25400.0 / 72.0));
            painter.setFont(font);
            painter.drawText(
                QRectF(t.pos.x.value, t.pos.y.value, 190'000, t.size_pt * 25400.0 / 72.0 * 1.4),
                Qt::AlignLeft | Qt::AlignTop, QString::fromStdString(t.text));
        }
    }
}

PlacementTemplateDialog::PlacementTemplateDialog(const stitch::StitchSequence& sequence,
                                                 const QString& projectName, QWidget* parent)
    : QDialog(parent), sequence_(sequence), name_(projectName) {
    setObjectName(QStringLiteral("placementTemplateDialog"));
    setWindowTitle(tr("Gabarit de placement 1:1"));

    paper_ = new QComboBox(this);
    paper_->setObjectName(QStringLiteral("placement_paper"));
    paper_->setAccessibleName(tr("Format du papier"));
    paper_->addItem(tr("A4 (210 × 297 mm)"), QStringLiteral("a4"));
    paper_->addItem(tr("Lettre (216 × 279 mm)"), QStringLiteral("letter"));
    landscape_ = new QCheckBox(tr("Orientation paysage"), this);
    landscape_->setObjectName(QStringLiteral("placement_landscape"));
    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("placement_summary"));
    summary_->setWordWrap(true);
    auto* hint = new QLabel(
        tr("Imprimez à 100 % (sans « ajuster à la page »), puis vérifiez que la règle de la "
           "première page mesure exactement 100 mm. Assemblez les pages sur les équerres, "
           "centrez la croix rouge sur le point de départ du cadre."),
        this);
    hint->setWordWrap(true);

    auto* form = new QFormLayout();
    form->addRow(tr("&Papier"), paper_);
    form->addRow(QString(), landscape_);

    auto* buttons = new QDialogButtonBox(this);
    auto* previewBtn =
        buttons->addButton(tr("Aperçu avant impression…"), QDialogButtonBox::ActionRole);
    auto* printBtn = buttons->addButton(tr("Imprimer…"), QDialogButtonBox::ActionRole);
    auto* pdfBtn = buttons->addButton(tr("Exporter en PDF…"), QDialogButtonBox::ActionRole);
    auto* closeBtn = buttons->addButton(tr("Fermer"), QDialogButtonBox::RejectRole);
    previewBtn->setObjectName(QStringLiteral("placement_preview"));
    printBtn->setObjectName(QStringLiteral("placement_print"));
    pdfBtn->setObjectName(QStringLiteral("placement_pdf"));
    connect(previewBtn, &QPushButton::clicked, this, &PlacementTemplateDialog::preview);
    connect(printBtn, &QPushButton::clicked, this, &PlacementTemplateDialog::print);
    connect(pdfBtn, &QPushButton::clicked, this, &PlacementTemplateDialog::exportPdfInteractive);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(paper_, &QComboBox::currentIndexChanged, this, [this] { refreshNow(); });
    connect(landscape_, &QCheckBox::toggled, this, [this] { refreshNow(); });

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(summary_);
    root->addWidget(hint);
    root->addWidget(buttons);
    refreshNow();
}

void PlacementTemplateDialog::refreshNow() {
    stitch_analysis::PlacementTemplateOptions opts;
    if (paper_->currentData().toString() == QLatin1String("letter")) {
        opts.paper_width = Micrometers{215'900};
        opts.paper_height = Micrometers{279'400};
    }
    opts.landscape = landscape_->isChecked();
    opts.title = name_.toStdString();
    tpl_ = stitch_analysis::make_placement_template(sequence_, opts);
    summary_->setText(summaryText());
}

QString PlacementTemplateDialog::summaryText() const {
    return tr("Motif : %1 × %2 mm — %3 page(s) (%4 colonne(s) × %5 ligne(s)).")
        .arg(tpl_.design_width.value / 1000.0, 0, 'f', 1)
        .arg(tpl_.design_height.value / 1000.0, 0, 'f', 1)
        .arg(tpl_.pages.size())
        .arg(tpl_.columns)
        .arg(tpl_.rows);
}

bool PlacementTemplateDialog::exportPdf(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const std::string pdf = stitch_analysis::placement_template_to_pdf(tpl_);
    return file.write(pdf.data(), static_cast<qint64>(pdf.size())) ==
           static_cast<qint64>(pdf.size());
}

bool PlacementTemplateDialog::exportQtPdf(const QString& path) const {
    {
        QPdfWriter writer(path);
        writer.setCreator(QStringLiteral("OpenStitch Studio"));
        writer.setTitle(tr("Gabarit de placement - %1").arg(name_));
        paintPlacementTemplate(&writer, tpl_);
    }
    QFile file(path);
    return file.exists() && file.size() > 0;
}

void PlacementTemplateDialog::configurePrinter(QPrinter& printer) const {
    printer.setDocName(tr("Gabarit de placement - %1").arg(name_));
}

void PlacementTemplateDialog::preview() {
    refreshNow();
    QPrinter printer(QPrinter::HighResolution);
    configurePrinter(printer);
    showPrintPreview(this, tr("Aperçu avant impression - gabarit de placement"), printer,
                     [this](QPrinter* p) { paintPlacementTemplate(p, tpl_); });
}

void PlacementTemplateDialog::print() {
    refreshNow();
    QPrinter printer(QPrinter::HighResolution);
    configurePrinter(printer);
    printWithDialog(this, tr("Imprimer le gabarit de placement"), printer,
                    [this](QPrinter* p) { paintPlacementTemplate(p, tpl_); });
}

void PlacementTemplateDialog::exportPdfInteractive() {
    refreshNow();
    QString suggested = name_.trimmed().isEmpty() ? tr("gabarit") : name_.trimmed();
    suggested += QStringLiteral("-gabarit-1-1.pdf");
    const QString path = QFileDialog::getSaveFileName(this, tr("Exporter le gabarit en PDF"),
                                                      suggested, tr("Document PDF (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    if (!exportPdf(path)) {
        QMessageBox::warning(this, tr("Gabarit de placement"),
                             tr("Impossible d'écrire le fichier : %1").arg(path));
    }
}

} // namespace openstitch::desktop
