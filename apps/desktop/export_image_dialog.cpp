// SPDX-License-Identifier: Apache-2.0
#include "export_image_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "openstitch/stitch_render/raster.hpp"

namespace openstitch::desktop {

namespace {

constexpr const char* kSettingsResolution = "exportImage/pxPerMm";
constexpr const char* kSettingsMargin = "exportImage/marginMm";
constexpr const char* kSettingsOpaque = "exportImage/opaque";
constexpr const char* kSettingsQuality = "exportImage/quality";
constexpr const char* kSettingsFormat = "exportImage/format";
constexpr const char* kSettingsJpegQuality = "exportImage/jpegQuality";

} // namespace

ExportImageDialog::ExportImageDialog(double widthMm, double heightMm,
                                     const stitch_render::RenderParams& baseParams, QWidget* parent)
    : QDialog(parent), widthMm_(widthMm), heightMm_(heightMm), base_(baseParams) {
    setWindowTitle(tr("Exporter l'image"));
    setObjectName(QStringLiteral("dialog_exportImage"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    resolution_ = new QDoubleSpinBox(this);
    resolution_->setObjectName(QStringLiteral("spin_exportImageResolution"));
    resolution_->setRange(2.0, 60.0);
    resolution_->setDecimals(1);
    resolution_->setSingleStep(1.0);
    resolution_->setSuffix(tr(" px/mm"));
    resolution_->setToolTip(tr("Résolution de l'image : 10 px/mm donne un fil bien lisible, "
                               "25 px/mm convient à un tirage. Abaissée automatiquement si "
                               "l'image dépasserait la mémoire disponible."));
    form->addRow(tr("Résolution :"), resolution_);

    margin_ = new QDoubleSpinBox(this);
    margin_->setObjectName(QStringLiteral("spin_exportImageMargin"));
    margin_->setRange(0.0, 50.0);
    margin_->setDecimals(1);
    margin_->setSingleStep(1.0);
    margin_->setSuffix(tr(" mm"));
    form->addRow(tr("Marge autour du motif :"), margin_);

    opaque_ = new QCheckBox(tr("Fond de tissu opaque"), this);
    opaque_->setObjectName(QStringLiteral("check_exportImageOpaque"));
    opaque_->setToolTip(tr("Décoché : le tissu est transparent (PNG seulement), pratique pour "
                           "poser le motif sur un autre fond."));
    form->addRow(QString(), opaque_);

    quality_ = new QComboBox(this);
    quality_->setObjectName(QStringLiteral("combo_exportImageQuality"));
    quality_->addItem(tr("Rapide"), static_cast<int>(stitch_render::Quality::Fast));
    quality_->addItem(tr("Haute"), static_cast<int>(stitch_render::Quality::High));
    form->addRow(tr("Qualité du rendu :"), quality_);

    format_ = new QComboBox(this);
    format_->setObjectName(QStringLiteral("combo_exportImageFormat"));
    format_->addItem(tr("PNG (sans perte, transparence possible)"),
                     static_cast<int>(ImageFileFormat::Png));
    format_->addItem(tr("JPEG (plus léger, fond opaque)"), static_cast<int>(ImageFileFormat::Jpeg));
    form->addRow(tr("Format de fichier :"), format_);

    jpegQuality_ = new QSpinBox(this);
    jpegQuality_->setObjectName(QStringLiteral("spin_exportImageJpegQuality"));
    jpegQuality_->setRange(1, 100);
    jpegQuality_->setSuffix(tr(" %"));
    form->addRow(tr("Qualité JPEG :"), jpegQuality_);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("label_exportImageSummary"));
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    auto* buttons = new QDialogButtonBox(this);
    auto* choose = buttons->addButton(tr("Choisir le fichier…"), QDialogButtonBox::AcceptRole);
    choose->setDefault(true);
    buttons->addButton(tr("Annuler"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Réglages mémorisés (par poste) ; le tissu vient du rendu réaliste.
    QSettings settings;
    resolution_->setValue(settings.value(kSettingsResolution, 10.0).toDouble());
    margin_->setValue(settings.value(kSettingsMargin, 3.0).toDouble());
    opaque_->setChecked(settings.value(kSettingsOpaque, base_.fabric_opaque).toBool());
    if (const int idx = quality_->findData(
            settings.value(kSettingsQuality, static_cast<int>(base_.quality)).toInt());
        idx >= 0) {
        quality_->setCurrentIndex(idx);
    }
    if (const int idx = format_->findData(settings.value(kSettingsFormat, 0).toInt()); idx >= 0) {
        format_->setCurrentIndex(idx);
    }
    jpegQuality_->setValue(settings.value(kSettingsJpegQuality, 92).toInt());

    const auto onChange = [this] { refresh(); };
    connect(resolution_, &QDoubleSpinBox::valueChanged, this, onChange);
    connect(margin_, &QDoubleSpinBox::valueChanged, this, onChange);
    connect(opaque_, &QCheckBox::toggled, this, onChange);
    connect(format_, &QComboBox::currentIndexChanged, this, onChange);
    connect(this, &QDialog::accepted, this, [this] {
        QSettings s;
        s.setValue(kSettingsResolution, resolution_->value());
        s.setValue(kSettingsMargin, margin_->value());
        s.setValue(kSettingsOpaque, opaque_->isChecked());
        s.setValue(kSettingsQuality, quality_->currentData().toInt());
        s.setValue(kSettingsFormat, format_->currentData().toInt());
        s.setValue(kSettingsJpegQuality, jpegQuality_->value());
    });
    refresh();
}

ExportImageSettings ExportImageDialog::settings() const {
    ExportImageSettings out;
    out.format = static_cast<ImageFileFormat>(format_->currentData().toInt());
    out.jpegQuality = jpegQuality_->value();
    out.presentation.px_per_mm = resolution_->value();
    out.presentation.margin_mm = margin_->value();
    out.presentation.params = base_;
    out.presentation.params.fabric_opaque =
        out.format == ImageFileFormat::Jpeg ? true : opaque_->isChecked();
    out.presentation.params.quality =
        static_cast<stitch_render::Quality>(quality_->currentData().toInt());
    return out;
}

void ExportImageDialog::refresh() {
    const bool jpeg =
        static_cast<ImageFileFormat>(format_->currentData().toInt()) == ImageFileFormat::Jpeg;
    // Un JPEG n'a pas de transparence : le tissu y est toujours opaque.
    opaque_->setEnabled(!jpeg);
    jpegQuality_->setEnabled(jpeg);
    const double margin = margin_->value();
    const stitch_render::RectMm region{0.0, 0.0, widthMm_ + 2 * margin, heightMm_ + 2 * margin};
    const auto settings = this->settings();
    const auto view = stitch_render::plan_view(region, settings.presentation.px_per_mm,
                                               settings.presentation.max_pixels);
    QString text = tr("Motif : %1 × %2 mm. Image : %3 × %4 px.")
                       .arg(widthMm_, 0, 'f', 1)
                       .arg(heightMm_, 0, 'f', 1)
                       .arg(view.width)
                       .arg(view.height);
    if (view.px_per_mm + 1e-9 < settings.presentation.px_per_mm) {
        text += tr(" Résolution abaissée à %1 px/mm (image trop grande).")
                    .arg(view.px_per_mm, 0, 'f', 1);
    }
    summary_->setText(text);
}

} // namespace openstitch::desktop
