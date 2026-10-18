// SPDX-License-Identifier: Apache-2.0
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

#include "segmentation_dialog.hpp"

using openstitch::desktop::SegmentationDialog;
using openstitch::segmentation::SegmentationOptions;
Q_DECLARE_METATYPE(openstitch::segmentation::SegmentationOptions)

class SegmentationDialogTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qRegisterMetaType<SegmentationOptions>();
        // Stockage isolé : sans nom d'organisation, QSettings ne persiste rien.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchTest"));
        QCoreApplication::setApplicationName(QStringLiteral("segmentation-dialog-test"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }
    void init() { QSettings().clear(); }

    void isNotModalAndShowsTheMinimumSizeInMillimetres() {
        SegmentationDialog dialog(0.25);
        QVERIFY(!dialog.isModal());
        auto* spin = dialog.findChild<QSpinBox*>("segmentMinSizeSpin");
        spin->setValue(16);
        // 16 px à 0,25 mm/px : 16 × 0,0625 = 1,00 mm².
        const QString text = dialog.findChild<QLabel*>("segmentMinSizeMm")->text();
        QVERIFY(text.contains("1,00") || text.contains("1.00"));
        QVERIFY(text.contains("mm²"));
    }

    void rapidChangesProduceOneOptionsSignalWithTheLastValues() {
        SegmentationDialog dialog(0.25);
        QSignalSpy spy(&dialog, &SegmentationDialog::optionsChanged);
        auto* colors = dialog.findChild<QSpinBox*>("segmentColorsSpin");
        colors->setValue(5);
        colors->setValue(4);
        dialog.findChild<QSpinBox*>("segmentSmoothingSpin")->setValue(7);
        QVERIFY(spy.wait(2000));
        QCOMPARE(spy.count(), 1); // un seul recalcul pour la rafale
        const auto options = spy.takeFirst().at(0).value<SegmentationOptions>();
        QCOMPARE(options.max_colors, 4);
        QCOMPARE(options.smoothing_radius_px, 7);
    }

    void applyEmitsTheCurrentOptionsAndPreviewToggleIsForwarded() {
        SegmentationDialog dialog(0.25);
        QSignalSpy applied(&dialog, &SegmentationDialog::applyRequested);
        QSignalSpy toggled(&dialog, &SegmentationDialog::previewToggled);
        dialog.findChild<QSpinBox*>("segmentColorsSpin")->setValue(3);
        dialog.findChild<QPushButton*>("segmentApplyButton")->click();
        QCOMPARE(applied.count(), 1);
        QCOMPARE(applied.takeFirst().at(0).value<SegmentationOptions>().max_colors, 3);

        QVERIFY(dialog.previewEnabled());
        dialog.findChild<QCheckBox*>("segmentPreviewCheck")->setChecked(false);
        QCOMPARE(toggled.count(), 1);
        QVERIFY(!dialog.previewEnabled());
    }

    void remembersItsSettingsOnlyWhenAsked() {
        {
            SegmentationDialog dialog(0.25);
            dialog.findChild<QSpinBox*>("segmentColorsSpin")->setValue(11);
            // Pas de rememberSettings() : fermer sans valider ne mémorise rien.
        }
        {
            SegmentationDialog dialog(0.25);
            QCOMPARE(dialog.options().max_colors, 8); // valeur par défaut
            dialog.findChild<QSpinBox*>("segmentColorsSpin")->setValue(11);
            dialog.rememberSettings();
        }
        SegmentationDialog dialog(0.25);
        QCOMPARE(dialog.options().max_colors, 11);
    }

    void statsAndFailureAreDisplayed() {
        SegmentationDialog dialog(0.25);
        dialog.setStats({.regions = 12, .colors = 5, .smallRegions = 3, .seconds = 0.2});
        const QString stats = dialog.findChild<QLabel*>("segmentStatsLabel")->text();
        QVERIFY(stats.contains("12"));
        QVERIFY(stats.contains("5"));
        QVERIFY(stats.contains("3"));
        dialog.setFailed("Aperçu impossible");
        QVERIFY(dialog.findChild<QLabel*>("segmentStatsLabel")->text().isEmpty());
        QCOMPARE(dialog.findChild<QLabel*>("segmentStateLabel")->text(),
                 QStringLiteral("Aperçu impossible"));
    }

private:
    QTemporaryDir settingsDir_;
};

QTEST_MAIN(SegmentationDialogTest)
#include "test_segmentation_dialog.moc"
