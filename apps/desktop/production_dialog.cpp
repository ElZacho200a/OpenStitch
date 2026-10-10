// SPDX-License-Identifier: Apache-2.0
#include "production_dialog.hpp"

#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMarginsF>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <optional>
#include <unordered_map>

#include "openstitch/stitch_render/raster.hpp"
#include "openstitch/stitch_render/segments.hpp"

namespace openstitch::desktop {

namespace {

constexpr int kRefreshDelayMs = 250;
constexpr int kPreviewMaxPx = 1000;
constexpr double kPageMarginMm = 15.0;
const QString kPreviewResource = QStringLiteral("apercu-motif.png");

} // namespace

ProductionDialog::ProductionDialog(const document::Project& project,
                                   const stitch::StitchSequence& sequence,
                                   const QString& suggestedName,
                                   const stitch_render::RenderParams& renderParams, QWidget* parent)
    : QDialog(parent), project_(project), sequence_(sequence), renderParams_(renderParams) {
    setObjectName(QStringLiteral("productionDialog"));
    setWindowTitle(tr("Fiche de production"));
    resize(1000, 720);

    name_ = new QLineEdit(suggestedName, this);
    name_->setObjectName(QStringLiteral("production_name"));
    name_->setAccessibleName(tr("Nom du projet"));
    date_ = new QDateEdit(QDate::currentDate(), this);
    date_->setObjectName(QStringLiteral("production_date"));
    date_->setCalendarPopup(true);
    date_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    date_->setAccessibleName(tr("Date de la fiche"));
    speed_ = new QDoubleSpinBox(this);
    speed_->setObjectName(QStringLiteral("production_speed"));
    speed_->setRange(100.0, 2000.0);
    speed_->setDecimals(0);
    speed_->setSingleStep(50.0);
    speed_->setValue(700.0);
    speed_->setSuffix(tr(" points/min"));
    speed_->setAccessibleName(tr("Vitesse de broderie supposée, en points par minute"));
    speed_->setToolTip(tr("Hypothèse pour le temps estimé : 600 à 800 points/min pour une machine "
                          "courante. La vitesse réelle dépend de la machine et du motif."));
    notes_ = new QPlainTextEdit(this);
    notes_->setObjectName(QStringLiteral("production_notes"));
    notes_->setAccessibleName(tr("Notes libres de la fiche"));
    notes_->setPlaceholderText(tr("Client, tissu, entoilage, consignes de broderie…"));
    notes_->setTabChangesFocus(true);

    auto* form = new QFormLayout();
    form->addRow(tr("&Nom du projet"), name_);
    form->addRow(tr("&Date"), date_);
    form->addRow(tr("&Vitesse supposée"), speed_);
    auto* notesLabel = new QLabel(tr("N&otes"), this);
    notesLabel->setBuddy(notes_);

    auto* left = new QVBoxLayout();
    left->addLayout(form);
    left->addWidget(notesLabel);
    left->addWidget(notes_, 1);

    view_ = new QTextBrowser(this);
    view_->setObjectName(QStringLiteral("production_view"));
    view_->setAccessibleName(tr("Aperçu de la fiche de production"));
    view_->setOpenLinks(false);

    auto* body = new QHBoxLayout();
    auto* leftBox = new QWidget(this);
    leftBox->setLayout(left);
    leftBox->setMaximumWidth(320);
    body->addWidget(leftBox);
    body->addWidget(view_, 1);

    auto* buttons = new QDialogButtonBox(this);
    auto* previewBtn =
        buttons->addButton(tr("&Aperçu avant impression…"), QDialogButtonBox::ActionRole);
    auto* printBtn = buttons->addButton(tr("&Imprimer…"), QDialogButtonBox::ActionRole);
    auto* pdfBtn = buttons->addButton(tr("&Exporter en PDF…"), QDialogButtonBox::ActionRole);
    auto* closeBtn = buttons->addButton(tr("&Fermer"), QDialogButtonBox::RejectRole);
    previewBtn->setObjectName(QStringLiteral("production_preview"));
    printBtn->setObjectName(QStringLiteral("production_print"));
    pdfBtn->setObjectName(QStringLiteral("production_pdf"));
    connect(previewBtn, &QPushButton::clicked, this, &ProductionDialog::printPreview);
    connect(printBtn, &QPushButton::clicked, this, &ProductionDialog::print);
    connect(pdfBtn, &QPushButton::clicked, this, &ProductionDialog::exportPdfInteractive);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(body, 1);
    root->addWidget(buttons);

    // Ordre de tabulation logique : champs, notes, aperçu, boutons.
    setTabOrder(name_, date_);
    setTabOrder(date_, speed_);
    setTabOrder(speed_, notes_);
    setTabOrder(notes_, view_);
    setTabOrder(view_, previewBtn);
    setTabOrder(previewBtn, printBtn);
    setTabOrder(printBtn, pdfBtn);
    setTabOrder(pdfBtn, closeBtn);

    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(kRefreshDelayMs);
    connect(timer_, &QTimer::timeout, this, &ProductionDialog::refreshNow);
    const auto schedule = [this] { timer_->start(); };
    connect(name_, &QLineEdit::textChanged, this, schedule);
    connect(date_, &QDateEdit::dateChanged, this, schedule);
    connect(speed_, &QDoubleSpinBox::valueChanged, this, schedule);
    connect(notes_, &QPlainTextEdit::textChanged, this, schedule);

    // Premier calcul : sert aussi à fournir les traits pour l'aperçu en lignes.
    stitch_analysis::ProductionOptions opts;
    opts.project_name = name_->text().toStdString();
    opts.date = date_->date().toString(Qt::ISODate).toStdString();
    sheet_ = stitch_analysis::make_production_sheet(project_, sequence_, opts);
    previewImage_ = renderPreviewImage();
    refreshNow();
}

ProductionDialog::~ProductionDialog() = default;

QImage ProductionDialog::renderPreviewImage() const {
    std::unordered_map<std::uint64_t, std::array<std::uint8_t, 3>> colorOf;
    for (const auto& emb : project_.embroidery_objects) {
        colorOf[emb.id.value] = emb.rgb;
    }
    const std::array<std::uint8_t, 3> fallback =
        sheet_.blocks.empty() ? std::array<std::uint8_t, 3>{0, 0, 0} : sheet_.blocks.front().rgb;
    const auto segments = stitch_render::build_thread_segments(
        sequence_, sequence_.commands.size(),
        [&](ObjectId id) -> std::optional<std::array<std::uint8_t, 3>> {
            const auto it = colorOf.find(id.value);
            return it != colorOf.end() ? it->second : fallback;
        });
    if (segments.empty()) {
        return {};
    }
    const stitch_render::RenderParams params = stitch_render::sanitized(renderParams_);
    stitch_render::RectMm rect = stitch_render::segments_bounds(segments);
    const double pad = std::max(2.0, params.thread_width_mm);
    rect = {rect.x0 - pad, rect.y0 - pad, rect.x1 + pad, rect.y1 + pad};
    const double ppm =
        std::min(12.0, static_cast<double>(kPreviewMaxPx) / std::max(rect.width(), rect.height()));
    const stitch_render::RasterView view = stitch_render::plan_view(rect, ppm, 4'000'000);
    if (view.width <= 0 || view.height <= 0) {
        return {};
    }
    if (stitch_render::choose_detail(view.px_per_mm, params) == stitch_render::Detail::Realistic) {
        const stitch_render::RasterImage image =
            stitch_render::render_threads(segments, params, view);
        const QImage qimage(image.rgba.data(), image.width, image.height, image.width * 4,
                            QImage::Format_RGBA8888_Premultiplied);
        return qimage.convertToFormat(QImage::Format_ARGB32); // copie détachée du tampon local
    }
    // Grand motif : le fil texturé serait illisible, retombe sur des lignes colorées.
    QImage img(view.width, view.height, QImage::Format_ARGB32);
    img.fill(QColor(params.fabric_rgb[0], params.fabric_rgb[1], params.fabric_rgb[2]));
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const auto& st : sheet_.preview) {
        const auto& rgb = sheet_.blocks[st.block].rgb;
        QPen pen(QColor(rgb[0], rgb[1], rgb[2]));
        pen.setWidthF(std::max(1.0, 0.3 * view.px_per_mm));
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        QPolygonF poly;
        poly.reserve(static_cast<qsizetype>(st.points.size()));
        for (const auto& p : st.points) {
            poly << QPointF((p.first - view.rect.x0) * view.px_per_mm,
                            (p.second - view.rect.y0) * view.px_per_mm);
        }
        painter.drawPolyline(poly);
    }
    return img;
}

void ProductionDialog::buildDocument(QTextDocument& doc) const {
    stitch_analysis::ProductionHtmlOptions htmlOpts;
    htmlOpts.embed_svg_fallback = false; // QTextDocument ne rend pas le SVG
    if (!previewImage_.isNull()) {
        htmlOpts.preview_src = kPreviewResource.toStdString();
        const double aspect =
            static_cast<double>(previewImage_.width()) / std::max(1, previewImage_.height());
        htmlOpts.preview_width_px = std::clamp(static_cast<int>(340.0 * aspect), 160, 560);
        doc.addResource(QTextDocument::ImageResource, QUrl(kPreviewResource),
                        QVariant::fromValue(previewImage_));
    }
    // Aucun point : la section d'aperçu reste vide (le tableau le dit).
    doc.setHtml(QString::fromStdString(stitch_analysis::production_to_html(sheet_, htmlOpts)));
}

void ProductionDialog::refreshNow() {
    timer_->stop();
    stitch_analysis::ProductionOptions opts;
    opts.project_name = name_->text().toStdString();
    opts.date = date_->date().toString(Qt::ISODate).toStdString();
    opts.notes = notes_->toPlainText().toStdString();
    opts.stitches_per_minute = speed_->value();
    sheet_ = stitch_analysis::make_production_sheet(project_, sequence_, opts);
    buildDocument(*view_->document());
}

void ProductionDialog::configurePrinter(QPrinter& printer) const {
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Portrait);
    printer.setPageMargins(QMarginsF(kPageMarginMm, kPageMarginMm, kPageMarginMm, kPageMarginMm),
                           QPageLayout::Millimeter);
    printer.setDocName(tr("Fiche de production - %1").arg(name_->text()));
}

void ProductionDialog::printDocument(QPrinter* printer) const {
    QTextDocument doc;
    buildDocument(doc);
    doc.print(printer);
}

void ProductionDialog::print() {
    refreshNow();
    QPrinter printer(QPrinter::HighResolution);
    configurePrinter(printer);
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Imprimer la fiche de production"));
    if (dialog.exec() == QDialog::Accepted) {
        printDocument(&printer);
    }
}

void ProductionDialog::printPreview() {
    refreshNow();
    QPrinter printer(QPrinter::HighResolution);
    configurePrinter(printer);
    QPrintPreviewDialog preview(&printer, this);
    preview.setWindowTitle(tr("Aperçu avant impression - fiche de production"));
    connect(&preview, &QPrintPreviewDialog::paintRequested, this,
            [this](QPrinter* p) { printDocument(p); });
    preview.exec();
}

bool ProductionDialog::exportPdf(const QString& path) {
    refreshNow();
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageOrientation(QPageLayout::Portrait);
        writer.setPageMargins(QMarginsF(kPageMarginMm, kPageMarginMm, kPageMarginMm, kPageMarginMm),
                              QPageLayout::Millimeter);
        writer.setTitle(tr("Fiche de production - %1").arg(name_->text()));
        writer.setCreator(QStringLiteral("OpenStitch Studio"));
        QTextDocument doc;
        buildDocument(doc);
        doc.print(&writer);
    } // le fichier est finalisé à la destruction du writer
    QFile file(path);
    return file.exists() && file.size() > 0;
}

void ProductionDialog::exportPdfInteractive() {
    QString suggested =
        name_->text().trimmed().isEmpty() ? tr("fiche-de-production") : name_->text().trimmed();
    suggested += QStringLiteral(".pdf");
    const QString path = QFileDialog::getSaveFileName(this, tr("Exporter la fiche en PDF"),
                                                      suggested, tr("Document PDF (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    if (!exportPdf(path)) {
        QMessageBox::warning(this, tr("Fiche de production"),
                             tr("Le fichier « %1 » n'a pas pu être écrit.").arg(path));
    }
}

} // namespace openstitch::desktop
