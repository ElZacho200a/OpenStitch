// SPDX-License-Identifier: Apache-2.0
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>

#include <array>
#include <vector>

#include "segmentation_colors_panel.hpp"

using openstitch::desktop::SegmentationColorsPanel;
using openstitch::segmentation::ColorClass;
using Rgb = SegmentationColorsPanel::Rgb;
Q_DECLARE_METATYPE(std::vector<Rgb>)
Q_DECLARE_METATYPE(Rgb)

namespace {

constexpr Rgb kRed{220, 30, 30};
constexpr Rgb kBlue{30, 30, 220};
constexpr Rgb kGreen{30, 200, 30};

std::vector<ColorClass> threeClasses() {
    std::vector<ColorClass> classes(3);
    classes[0].rgb = kRed;
    classes[0].pixel_count = 600;
    classes[0].regions = {openstitch::RegionId{1}, openstitch::RegionId{4}};
    classes[1].rgb = kBlue;
    classes[1].pixel_count = 300;
    classes[1].regions = {openstitch::RegionId{2}};
    classes[2].rgb = kGreen;
    classes[2].pixel_count = 100;
    classes[2].regions = {openstitch::RegionId{3}};
    return classes;
}

} // namespace

class SegmentationColorsPanelTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qRegisterMetaType<std::vector<Rgb>>();
        qRegisterMetaType<Rgb>();
    }

    void listsDistinctColorsWithShareAndRegionCount() {
        SegmentationColorsPanel panel;
        panel.setClasses(threeClasses(), 1000);
        auto* list = panel.findChild<QListWidget*>("segColorsList");
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 3);
        QVERIFY(list->item(0)->text().contains("60,0") || list->item(0)->text().contains("60.0"));
        QVERIFY(list->item(0)->text().contains("2"));
        // Aucune réduction active au départ : appliquer et annuler sont grisés.
        QVERIFY(!panel.findChild<QPushButton*>("segColorsApplyButton")->isEnabled());
        QVERIFY(!panel.findChild<QPushButton*>("segColorsMergeButton")->isEnabled());
    }

    void selectingRowsEmitsColorsButRefreshDoesNot() {
        SegmentationColorsPanel panel;
        panel.setClasses(threeClasses(), 1000);
        auto* list = panel.findChild<QListWidget*>("segColorsList");
        QSignalSpy spy(&panel, &SegmentationColorsPanel::colorsSelected);
        list->item(1)->setSelected(true);
        QCOMPARE(spy.count(), 1);
        const auto colors = spy.takeFirst().at(0).value<std::vector<Rgb>>();
        QCOMPARE(colors.size(), std::size_t{1});
        QCOMPARE(colors.front(), kBlue);

        // Une mise à jour par le document conserve la sélection par couleur, sans signal.
        panel.setClasses(threeClasses(), 1000);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(panel.selectedColors().size(), std::size_t{1});
        QCOMPARE(panel.selectedColors().front(), kBlue);
    }

    void manualMergeNeedsTwoColorsAndTargetsTheActiveOne() {
        SegmentationColorsPanel panel;
        panel.setClasses(threeClasses(), 1000);
        auto* list = panel.findChild<QListWidget*>("segColorsList");
        auto* merge = panel.findChild<QPushButton*>("segColorsMergeButton");
        QSignalSpy spy(&panel, &SegmentationColorsPanel::mergeRequested);

        list->item(0)->setSelected(true);
        QVERIFY(!merge->isEnabled()); // une seule teinte : rien à fusionner
        list->item(2)->setSelected(true);
        list->setCurrentItem(list->item(0), QItemSelectionModel::NoUpdate); // teinte active = rouge
        QVERIFY(merge->isEnabled());
        merge->click();
        QCOMPARE(spy.count(), 1);
        const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).value<std::vector<Rgb>>().size(), std::size_t{2});
        QCOMPARE(args.at(1).value<Rgb>(), kRed);
        QCOMPARE(args.at(2).toBool(), true); // « fusionner les voisines » coché par défaut
    }

    void reductionPreviewIsDebouncedAndClearsWhenNeutral() {
        SegmentationColorsPanel panel;
        panel.setClasses(threeClasses(), 1000);
        auto* count = panel.findChild<QSpinBox*>("segColorsCountSpin");
        QCOMPARE(count->maximum(), 3);
        QCOMPARE(count->value(), 3);
        QSignalSpy preview(&panel, &SegmentationColorsPanel::reductionPreviewRequested);
        QSignalSpy cleared(&panel, &SegmentationColorsPanel::reductionPreviewCleared);

        count->setValue(2);
        count->setValue(1); // deux changements rapprochés : un seul aperçu, avec la dernière valeur
        QVERIFY(preview.wait(2000));
        QCOMPARE(preview.count(), 1);
        QCOMPARE(preview.at(0).at(0).toInt(), 1);
        QCOMPARE(preview.at(0).at(1).toDouble(), 0.0);
        QVERIFY(panel.findChild<QPushButton*>("segColorsApplyButton")->isEnabled());

        count->setValue(3); // retour à la valeur neutre : l'aperçu est retiré
        QVERIFY(cleared.wait(2000));
        QVERIFY(!panel.findChild<QPushButton*>("segColorsApplyButton")->isEnabled());
    }

    void distanceThresholdAlonePreviewsWithNoColorLimit() {
        SegmentationColorsPanel panel;
        panel.setClasses(threeClasses(), 1000);
        QSignalSpy preview(&panel, &SegmentationColorsPanel::reductionPreviewRequested);
        panel.findChild<QDoubleSpinBox*>("segColorsDistanceSpin")->setValue(5.0);
        QVERIFY(preview.wait(2000));
        QCOMPARE(preview.at(0).at(0).toInt(), 0); // 0 = pas de limite de nombre
        QCOMPARE(preview.at(0).at(1).toDouble(), 5.0);
    }

    void applyAndCancelButtons() {
        SegmentationColorsPanel panel;
        panel.setClasses(threeClasses(), 1000);
        auto* count = panel.findChild<QSpinBox*>("segColorsCountSpin");
        auto* apply = panel.findChild<QPushButton*>("segColorsApplyButton");
        auto* cancel = panel.findChild<QPushButton*>("segColorsCancelButton");
        panel.findChild<QCheckBox*>("segColorsAdjacentCheck")->setChecked(false);
        count->setValue(2);

        QSignalSpy applied(&panel, &SegmentationColorsPanel::reductionApplyRequested);
        apply->click();
        QCOMPARE(applied.count(), 1);
        QCOMPARE(applied.at(0).at(0).toInt(), 2);
        QCOMPARE(applied.at(0).at(2).toBool(), false);

        QSignalSpy cleared(&panel, &SegmentationColorsPanel::reductionPreviewCleared);
        cancel->click();
        QCOMPARE(cleared.count(), 1);
        QCOMPARE(count->value(), 3);
        QVERIFY(!apply->isEnabled());
    }
};

QTEST_MAIN(SegmentationColorsPanelTest)
#include "test_segmentation_colors_panel.moc"
