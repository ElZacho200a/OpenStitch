// SPDX-License-Identifier: Apache-2.0
#include "import_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace openstitch::desktop_v2 {

namespace {
constexpr int kThumb = 300;

QLabel* thumbnail(QWidget* parent, const QString& name) {
    auto* label = new QLabel(parent);
    label->setObjectName(name);
    label->setMinimumSize(kThumb, kThumb);
    label->setAlignment(Qt::AlignCenter);
    label->setFrameShape(QFrame::StyledPanel);
    return label;
}
} // namespace

ImportDialog::ImportDialog(const QString& imagePath, double defaultHeightMm, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("importDialog"));
    setWindowTitle(tr("Importer une image"));
    auto* column = new QVBoxLayout(this);

    auto* pair = new QHBoxLayout;
    auto* left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("<b>Image d'origine</b>"), this));
    source_ = thumbnail(this, QStringLiteral("importSource"));
    left->addWidget(source_);
    auto* right = new QVBoxLayout;
    right->addWidget(new QLabel(tr("<b>Segmentation obtenue</b>"), this));
    segmented_ = thumbnail(this, QStringLiteral("importSegmented"));
    segmented_->setText(tr("Calcul en cours…"));
    right->addWidget(segmented_);
    pair->addLayout(left);
    pair->addLayout(right);
    column->addLayout(pair);

    QImage original(imagePath);
    if (!original.isNull()) {
        source_->setPixmap(QPixmap::fromImage(original).scaled(kThumb, kThumb, Qt::KeepAspectRatio,
                                                               Qt::SmoothTransformation));
        sourceShown_ = true;
    } else {
        source_->setText(tr("Aperçu indisponible"));
    }

    auto* form = new QFormLayout;
    form_ = form;
    height_ = new QDoubleSpinBox(this);
    height_->setObjectName(QStringLiteral("importHeight"));
    height_->setRange(0.0, 2000.0);
    height_->setSuffix(tr(" mm"));
    height_->setSpecialValueText(tr("Taille d'origine"));
    height_->setValue(defaultHeightMm);
    form->addRow(tr("Hauteur du motif :"), height_);

    colors_ = new QSpinBox(this);
    colors_->setObjectName(QStringLiteral("importColors"));
    colors_->setRange(2, 64);
    colors_->setValue(8);
    form->addRow(tr("Nombre de couleurs :"), colors_);

    minRegion_ = new QSpinBox(this);
    minRegion_->setObjectName(QStringLiteral("importMinRegion"));
    minRegion_->setRange(1, 5000);
    minRegion_->setValue(16);
    minRegion_->setSuffix(tr(" px"));
    minRegion_->setToolTip(tr("Les régions plus petites sont absorbées par leur voisine."));
    form->addRow(tr("Plus petite région :"), minRegion_);

    smoothing_ = new QSpinBox(this);
    smoothing_->setObjectName(QStringLiteral("importSmoothing"));
    smoothing_->setRange(0, 20);
    smoothing_->setValue(3);
    smoothing_->setSuffix(tr(" px"));
    smoothing_->setToolTip(tr("Lisse les frontières ; un rayon trop grand efface les détails."));
    form->addRow(tr("Lissage des frontières :"), smoothing_);

    background_ = new QComboBox(this);
    background_->setObjectName(QStringLiteral("importBackground"));
    background_->addItem(tr("Automatique"), -1);
    background_->addItem(tr("Tout coudre"), 0);
    background_->addItem(tr("Ne pas coudre la couleur du fond"), 1);
    background_->addItem(tr("Fond extérieur seulement"), 2);
    form->addRow(tr("Fond :"), background_);

    mode_ = new QComboBox(this);
    mode_->setObjectName(QStringLiteral("importMode"));
    mode_->addItem(tr("Formes remplies"), QStringLiteral("shapes"));
    mode_->addItem(tr("Contours"), QStringLiteral("contours"));
    mode_->addItem(tr("Pointillé"), QStringLiteral("stipple"));
    form->addRow(tr("Style :"), mode_);
    column->addLayout(form);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("importSummary"));
    summary_->setWordWrap(true);
    column->addWidget(summary_);
    palette_ = new QWidget(this);
    palette_->setObjectName(QStringLiteral("importPalette"));
    new QHBoxLayout(palette_);
    column->addWidget(palette_);

    auto* buttons = new QDialogButtonBox(this);
    auto* ok = buttons->addButton(tr("Numériser"), QDialogButtonBox::AcceptRole);
    ok->setObjectName(QStringLiteral("importAccept"));
    accept_ = ok;
    ok->setDefault(true);
    buttons->addButton(tr("Annuler"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);

    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(300);
    connect(debounce_, &QTimer::timeout, this,
            [this] { emit previewRequested(segmentationOptions()); });
    for (auto* spin : {colors_, minRegion_, smoothing_}) {
        connect(spin, &QSpinBox::valueChanged, this, [this](int) { scheduleRequest(); });
    }
    scheduleRequest();
}

void ImportDialog::setResegmentMode(const QImage& source) {
    resegment_ = true;
    setWindowTitle(tr("Re-segmenter l'image"));
    form_->setRowVisible(height_, false); // la taille du motif ne change pas
    accept_->setText(tr("Appliquer"));
    accept_->setToolTip(
        tr("Remplace la segmentation et régénère les objets (annulable en une "
           "étape). Vos corrections de régions et retouches d'objets sont perdues."));
    if (!source.isNull()) {
        source_->setPixmap(QPixmap::fromImage(source).scaled(kThumb, kThumb, Qt::KeepAspectRatio,
                                                             Qt::SmoothTransformation));
        sourceShown_ = true;
    }
}

void ImportDialog::setSettings(const project_ops::DigitizeSettings& s) {
    colors_->setValue(s.max_colors);
    minRegion_->setValue(s.min_region_px);
    smoothing_->setValue(s.smoothing_px);
    if (const int i = background_->findData(s.skip_background); i >= 0) {
        background_->setCurrentIndex(i);
    }
    if (const int i = mode_->findData(QString::fromStdString(s.mode)); i >= 0) {
        mode_->setCurrentIndex(i);
    }
}

void ImportDialog::scheduleRequest() {
    previewShown_ = false;
    segmented_->setText(tr("Calcul en cours…"));
    segmented_->setPixmap(QPixmap());
    debounce_->start();
}

double ImportDialog::heightMm() const {
    return height_->value();
}

segmentation::SegmentationOptions ImportDialog::segmentationOptions() const {
    segmentation::SegmentationOptions options;
    options.max_colors = colors_->value();
    options.min_region_px = minRegion_->value();
    options.smoothing_radius_px = smoothing_->value();
    return options;
}

project_ops::DigitizeSettings ImportDialog::settings() const {
    project_ops::DigitizeSettings s;
    s.max_colors = colors_->value();
    s.min_region_px = minRegion_->value();
    s.smoothing_px = smoothing_->value();
    s.skip_background = background_->currentData().toInt();
    s.mode = mode_->currentData().toString().toStdString();
    return s;
}

void ImportDialog::setColors(int colors) {
    colors_->setValue(colors);
}
void ImportDialog::setMinRegionPx(int px) {
    minRegion_->setValue(px);
}
void ImportDialog::setSmoothing(int px) {
    smoothing_->setValue(px);
}

void ImportDialog::setPreview(const SegmentationPreview& preview) {
    // Un résultat calculé pour d'autres réglages (l'utilisateur a continué à tourner les boutons)
    // n'est jamais affiché : il dirait autre chose que ce qui est réglé.
    const auto now = segmentationOptions();
    if (preview.options.max_colors != now.max_colors ||
        preview.options.min_region_px != now.min_region_px ||
        preview.options.smoothing_radius_px != now.smoothing_radius_px) {
        return;
    }
    QImage map(preview.rgba.data(), preview.width, preview.height, preview.width * 4,
               QImage::Format_RGBA8888);
    // Fond blanc : les pixels transparents (fond détecté) ne sont pas cousus.
    QImage flat(preview.width, preview.height, QImage::Format_RGB32);
    flat.fill(Qt::white);
    {
        QPainter painter(&flat);
        painter.drawImage(0, 0, map);
    }
    segmented_->setPixmap(QPixmap::fromImage(flat).scaled(kThumb, kThumb, Qt::KeepAspectRatio,
                                                          Qt::FastTransformation));
    previewShown_ = true;
    summary_->setText(tr("%n région(s) pour %1 couleur(s) maximum, la plus petite fait %2 px "
                         "(calculé en %3 s).",
                         "", static_cast<int>(preview.regions))
                          .arg(preview.options.max_colors)
                          .arg(preview.smallestRegionPx)
                          .arg(preview.seconds, 0, 'f', 2));
    auto* layout = palette_->layout();
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    paletteCount_ = 0;
    for (const auto& rgb : preview.palette) {
        if (paletteCount_ >= 16) {
            break; // au-delà, la barre n'est plus lisible
        }
        auto* swatch = new QLabel(palette_);
        swatch->setFixedSize(18, 18);
        swatch->setStyleSheet(QStringLiteral("background:#%1%2%3;border:1px solid #555;")
                                  .arg(rgb[0], 2, 16, QLatin1Char('0'))
                                  .arg(rgb[1], 2, 16, QLatin1Char('0'))
                                  .arg(rgb[2], 2, 16, QLatin1Char('0')));
        layout->addWidget(swatch);
        ++paletteCount_;
    }
}

void ImportDialog::setPreviewError(const QString& message) {
    segmented_->setText(tr("Aperçu impossible : %1").arg(message));
    summary_->setText(QString());
}

QString ImportDialog::summary() const {
    return summary_->text();
}

} // namespace openstitch::desktop_v2
