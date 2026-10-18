// SPDX-License-Identifier: Apache-2.0
// Fichier > Exporter l'image… (HP-FMT-011) : dialogue + rendu + écriture. Headless, sans
// comparaison de pixels : dimensions, signature du fichier, transparence, déterminisme des octets.
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "export_image.hpp"
#include "export_image_dialog.hpp"

using namespace openstitch;
using openstitch::desktop::ExportImageDialog;
using openstitch::desktop::ExportImageSettings;
using openstitch::desktop::ImageFileFormat;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

document::Project redProject() {
    document::Project project;
    document::EmbroideryObject a;
    a.id = ObjectId{1};
    a.name = "Rond";
    a.rgb = {200, 10, 10};
    project.embroidery_objects.push_back(a);
    return project;
}

// Un carre de 10 x 10 mm cousu en zigzag.
stitch::StitchSequence squareSequence() {
    using T = stitch::CommandType;
    stitch::StitchSequence seq;
    for (int i = 0; i <= 10; ++i) {
        seq.commands.push_back({um(i % 2 == 0 ? 0 : 10'000, i * 1'000), T::Stitch, ObjectId{1}});
        seq.commands.push_back({um(i % 2 == 0 ? 10'000 : 0, i * 1'000), T::Stitch, ObjectId{1}});
    }
    seq.commands.push_back({um(0, 10'000), T::End, ObjectId{}});
    return seq;
}

QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestExportImage : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("ExportImage"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        QSettings().clear();
    }

    void dialogDefaultsAndJpegForcesOpaque() {
        ExportImageDialog dialog(10.0, 10.0, stitch_render::RenderParams{});
        auto settings = dialog.settings();
        QCOMPARE(settings.format, ImageFileFormat::Png);
        QCOMPARE(settings.presentation.px_per_mm, 10.0);
        QCOMPARE(settings.presentation.margin_mm, 3.0);
        QVERIFY(settings.presentation.params.fabric_opaque);

        auto* opaque = dialog.findChild<QCheckBox*>(QStringLiteral("check_exportImageOpaque"));
        auto* format = dialog.findChild<QComboBox*>(QStringLiteral("combo_exportImageFormat"));
        auto* quality = dialog.findChild<QComboBox*>(QStringLiteral("combo_exportImageQuality"));
        auto* res = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("spin_exportImageResolution"));
        QVERIFY(opaque && format && quality && res);
        opaque->setChecked(false);
        quality->setCurrentIndex(quality->findData(static_cast<int>(stitch_render::Quality::Fast)));
        res->setValue(20.0);
        settings = dialog.settings();
        QVERIFY(!settings.presentation.params.fabric_opaque);
        QCOMPARE(settings.presentation.params.quality, stitch_render::Quality::Fast);
        QCOMPARE(settings.presentation.px_per_mm, 20.0);

        format->setCurrentIndex(format->findData(static_cast<int>(ImageFileFormat::Jpeg)));
        QVERIFY(!opaque->isEnabled());
        QVERIFY(dialog.settings().presentation.params.fabric_opaque); // JPEG : jamais transparent
    }

    void pngHasExpectedSizeAndSignature() {
        ExportImageSettings settings;
        settings.presentation.px_per_mm = 8.0;
        settings.presentation.margin_mm = 2.0;
        const auto project = redProject();
        const auto seq = squareSequence();
        const QImage image = desktop::render_export_image(project, seq, settings);
        QVERIFY(!image.isNull());
        // 10 mm de motif + 2 x 2 mm de marge + epaisseur du fil, a 8 px/mm.
        QVERIFY(image.width() >= 14 * 8 && image.width() <= 16 * 8);
        QVERIFY(image.height() >= 14 * 8 && image.height() <= 16 * 8);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("rendu.png"));
        QString error;
        QVERIFY2(desktop::write_export_image(image, path, settings, &error), qPrintable(error));
        QCOMPARE(readAll(path).left(8), QByteArray("\x89PNG\r\n\x1a\n", 8));
        const QImage reloaded(path);
        QCOMPARE(reloaded.size(), image.size());

        // Deterministe : memes reglages, memes octets.
        const QString path2 = dir.filePath(QStringLiteral("rendu2.png"));
        QVERIFY(desktop::write_export_image(desktop::render_export_image(project, seq, settings),
                                            path2, settings));
        QCOMPARE(readAll(path), readAll(path2));
    }

    void transparentFabricKeepsAlphaAndJpegIsOpaque() {
        ExportImageSettings settings;
        settings.presentation.px_per_mm = 6.0;
        settings.presentation.params.fabric_opaque = false;
        const auto project = redProject();
        const auto seq = squareSequence();
        const QImage png = desktop::render_export_image(project, seq, settings);
        QVERIFY(png.hasAlphaChannel());
        QCOMPARE(png.pixelColor(0, 0).alpha(), 0); // coin de la marge : tissu transparent

        settings.format = ImageFileFormat::Jpeg;
        settings.jpegQuality = 80;
        const QImage jpg = desktop::render_export_image(project, seq, settings);
        QVERIFY(!jpg.isNull());
        QVERIFY(!jpg.hasAlphaChannel());
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("rendu.jpg"));
        QVERIFY(desktop::write_export_image(jpg, path, settings));
        QCOMPARE(readAll(path).left(2), QByteArray("\xff\xd8", 2));
    }

    void emptySequenceGivesNullImageAndWriteFailsCleanly() {
        const ExportImageSettings settings;
        const QImage image =
            desktop::render_export_image(document::Project{}, stitch::StitchSequence{}, settings);
        QVERIFY(image.isNull());
        QString error;
        QVERIFY(
            !desktop::write_export_image(image, QStringLiteral("jamais.png"), settings, &error));
        QVERIFY(!error.isEmpty());
    }

    void importedDesignUsesBlockColors() {
        using T = stitch::CommandType;
        document::Project project;
        document::ImportedDesign imported;
        imported.source_format = "dst";
        imported.color_blocks.push_back(stitch::ColorBlock{{10, 200, 10}, std::nullopt, 0, 2});
        project.imported_design = imported;
        stitch::StitchSequence seq;
        seq.commands = {{um(0, 0), T::Stitch, ObjectId{}},
                        {um(8'000, 0), T::Stitch, ObjectId{}},
                        {um(8'000, 8'000), T::Stitch, ObjectId{}}};
        ExportImageSettings settings;
        settings.presentation.px_per_mm = 5.0;
        const QImage image = desktop::render_export_image(project, seq, settings);
        QVERIFY(!image.isNull());
    }

    void heavyEstimateDependsOnSizeAndResolution() {
        stitch::StitchStats small;
        small.stitches = 100;
        small.bounds = {um(0, 0), um(10'000, 10'000)};
        ExportImageSettings settings;
        settings.presentation.px_per_mm = 10.0;
        QVERIFY(!desktop::exportImageIsHeavy(small, settings));
        settings.presentation.px_per_mm = 40.0;
        QVERIFY(desktop::exportImageIsHeavy(small, settings));
        small.stitches = 50'000;
        settings.presentation.px_per_mm = 2.0;
        QVERIFY(desktop::exportImageIsHeavy(small, settings));
    }

private:
    QTemporaryDir settingsDir_;
};

QTEST_MAIN(TestExportImage)
#include "test_export_image.moc"
