// SPDX-License-Identifier: Apache-2.0
// Dialogue d'export machine : option « Réduire les coupes » (HP-SEQ-006), demandée pour un export
// seulement. Le dialogue ne calcule rien : il transmet les réglages au résumé et à l'appelant.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "machine_export_dialog.hpp"

using openstitch::desktop::MachineExportDialog;
namespace project_io = openstitch::project_io;

class MachineExportDialogTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("MachineExportDialog"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        QSettings().clear();
    }

    void startPointDefaultsToFirstPointAndReachesTheCaller() {
        using openstitch::formats::StartMode;
        MachineExportDialog dialog(summarizeNothing(), QStringLiteral("dst"), nullptr);
        auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("combo_exportStartMode"));
        auto* x = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("spin_exportStartX"));
        auto* y = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("spin_exportStartY"));
        QVERIFY(mode != nullptr && x != nullptr && y != nullptr);
        QCOMPARE(dialog.options().start.mode, StartMode::FirstPoint);
        QVERIFY(!x->isEnabled() && !y->isEnabled());

        mode->setCurrentIndex(mode->findData(static_cast<int>(StartMode::Center)));
        QCOMPARE(dialog.options().start.mode, StartMode::Center);
        QVERIFY(!x->isEnabled());

        mode->setCurrentIndex(mode->findData(static_cast<int>(StartMode::Custom)));
        QVERIFY(x->isEnabled() && y->isEnabled());
        x->setValue(-12.5);
        y->setValue(30.0);
        const auto options = dialog.options();
        QCOMPARE(options.start.mode, StartMode::Custom);
        QCOMPARE(options.start.point.x.value, -12'500);
        QCOMPARE(options.start.point.y.value, 30'000);
    }

    // Régression : scrollDialogBody déplaçait les items sans reparenter les widgets ; le formulaire
    // restait enfant du dialogue, recouvert par le viewport de la zone défilante (dialogue vide).
    void formWidgetsLiveInsideTheScrollBody() {
        MachineExportDialog dialog(summarizeNothing(), QStringLiteral("dst"), nullptr);
        auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("dialogScrollBody"));
        QVERIFY(scroll != nullptr);
        for (const char* name : {"combo_exportFormat", "check_exportTrims", "combo_exportStops",
                                 "label_exportSummary"}) {
            auto* w = dialog.findChild<QWidget*>(QString::fromLatin1(name));
            QVERIFY2(w != nullptr, name);
            QVERIFY2(scroll->widget()->isAncestorOf(w), name);
        }
    }

    void reductionIsOffByDefault() {
        MachineExportDialog dialog(summarizeNothing(), QStringLiteral("dst"), nullptr);
        auto* reduce = dialog.findChild<QCheckBox*>(QStringLiteral("check_exportReduceTrims"));
        auto* same =
            dialog.findChild<QCheckBox*>(QStringLiteral("check_exportReduceTrimsSameColour"));
        auto* maxBridge =
            dialog.findChild<QDoubleSpinBox*>(QStringLiteral("spin_exportReduceTrimsMax"));
        QVERIFY(reduce != nullptr && same != nullptr && maxBridge != nullptr);
        QVERIFY(!reduce->isChecked());
        QVERIFY(!dialog.optimizations().reduce_trims);
        // Les réglages n'ont de sens que si l'option est cochée.
        QVERIFY(!same->isEnabled());
        QVERIFY(!maxBridge->isEnabled());
        QCOMPARE(maxBridge->value(), 15.0);
    }

    void optionReachesTheSummaryAndTheCaller() {
        project_io::ExportOptimizations seen;
        int calls = 0;
        MachineExportDialog dialog(
            [&](const openstitch::formats::FormatInfo&,
                const openstitch::formats::MachineExportOptions&,
                const project_io::ExportOptimizations& opt) {
                seen = opt;
                ++calls;
                return MachineExportDialog::Summary{};
            },
            QStringLiteral("dst"), nullptr);
        const int before = calls;
        auto* reduce = dialog.findChild<QCheckBox*>(QStringLiteral("check_exportReduceTrims"));
        auto* same =
            dialog.findChild<QCheckBox*>(QStringLiteral("check_exportReduceTrimsSameColour"));
        auto* maxBridge =
            dialog.findChild<QDoubleSpinBox*>(QStringLiteral("spin_exportReduceTrimsMax"));
        QVERIFY(reduce != nullptr && same != nullptr && maxBridge != nullptr);

        reduce->setChecked(true);
        // HP-UX-032 : le résumé (et l'analyse) est recalculé après 300 ms d'inactivité ; l'état
        // des cases, lui, suit tout de suite.
        QVERIFY(same->isEnabled());
        dialog.flushPendingRefresh();
        QVERIFY(calls > before); // résumé recalculé
        QVERIFY(seen.reduce_trims);
        same->setChecked(true);
        maxBridge->setValue(22.5);
        maxBridge->editingFinished();
        dialog.flushPendingRefresh();

        const auto opt = dialog.optimizations();
        QVERIFY(opt.reduce_trims);
        QVERIFY(opt.hidden_travel.allow_same_color_surface);
        QCOMPARE(opt.hidden_travel.max_bridge_length.value, 22'500);
        QVERIFY(seen == opt);

        // Décocher l'option annule aussi « même fil » côté appelant.
        reduce->setChecked(false);
        QVERIFY(!dialog.optimizations().reduce_trims);
        QVERIFY(!dialog.optimizations().hidden_travel.allow_same_color_surface);
    }

    void threeLevelsAreSelectableAndReachTheCaller() {
        MachineExportDialog dialog(summarizeNothing(), QStringLiteral("dst"), nullptr);
        auto* reduce = dialog.findChild<QCheckBox*>(QStringLiteral("check_exportReduceTrims"));
        auto* between = dialog.findChild<QCheckBox*>(QStringLiteral("check_exportRoutesBetween"));
        auto* within = dialog.findChild<QCheckBox*>(QStringLiteral("check_exportRoutesWithin"));
        auto* underlay = dialog.findChild<QCheckBox*>(QStringLiteral("check_exportRoutesUnderlay"));
        QVERIFY(reduce != nullptr && between != nullptr && within != nullptr &&
                underlay != nullptr);
        // Par défaut les trois niveaux sont demandés, mais grisés tant que l'option est décochée.
        QVERIFY(between->isChecked() && within->isChecked() && underlay->isChecked());
        QVERIFY(!between->isEnabled() && !within->isEnabled() && !underlay->isEnabled());
        reduce->setChecked(true);
        QVERIFY(between->isEnabled() && within->isEnabled() && underlay->isEnabled());
        within->setChecked(false);
        underlay->setChecked(false);
        const auto opt = dialog.optimizations();
        QVERIFY(opt.hidden_travel.between_objects);
        QVERIFY(!opt.hidden_travel.within_object);
        QVERIFY(!opt.hidden_travel.reorder_underlay);
    }

    void advancedRegionModeIsOffByDefaultAndExplicit() {
        MachineExportDialog dialog(summarizeNothing(), QStringLiteral("dst"), nullptr);
        auto* reduce = dialog.findChild<QCheckBox*>(QStringLiteral("check_exportReduceTrims"));
        auto* regions = dialog.findChild<QCheckBox*>(QStringLiteral("check_exportRoutesRegions"));
        QVERIFY(reduce != nullptr && regions != nullptr);
        QVERIFY(!regions->isChecked()); // plus invasif : jamais coché sans demande
        reduce->setChecked(true);
        QVERIFY(!dialog.optimizations().hidden_travel.reorder_regions);
        regions->setChecked(true);
        QVERIFY(dialog.optimizations().hidden_travel.reorder_regions);
    }

    void optionIsAvailableForDstToo() {
        // Le DST garde son codage des coupes (cases grisées) mais profite de la réduction.
        MachineExportDialog dialog(summarizeNothing(), QStringLiteral("dst"), nullptr);
        auto* trims = dialog.findChild<QCheckBox*>(QStringLiteral("check_exportTrims"));
        auto* reduce = dialog.findChild<QCheckBox*>(QStringLiteral("check_exportReduceTrims"));
        QVERIFY(trims != nullptr && reduce != nullptr);
        QVERIFY(!trims->isEnabled());
        QVERIFY(reduce->isEnabled());
    }

    void colorCompanionIsOfferedForDstAndExpOnly() {
        // HP-FMT-009 / HP-FMT-005 : le fichier de couleurs n'a de sens que pour les formats sans
        // vraies couleurs ; la demande n'atteint l'appelant que dans ce cas.
        MachineExportDialog dialog(summarizeNothing(), QStringLiteral("dst"), nullptr);
        auto* companion =
            dialog.findChild<QCheckBox*>(QStringLiteral("check_exportColorCompanion"));
        auto* kind = dialog.findChild<QComboBox*>(QStringLiteral("combo_exportColorCompanionKind"));
        auto* format = dialog.findChild<QComboBox*>(QStringLiteral("combo_exportFormat"));
        QVERIFY(companion != nullptr && kind != nullptr && format != nullptr);
        QVERIFY(!companion->isChecked()); // jamais sans demande
        QVERIFY(!dialog.companion().write);
        QVERIFY(companion->isEnabled());

        companion->setChecked(true);
        QVERIFY(dialog.companion().write);
        QCOMPARE(dialog.companion().kind, openstitch::formats::CompanionKind::Csv);
        kind->setCurrentIndex(
            kind->findData(static_cast<int>(openstitch::formats::CompanionKind::Inf)));
        QCOMPARE(dialog.companion().kind, openstitch::formats::CompanionKind::Inf);

        format->setCurrentIndex(format->findData(QStringLiteral("exp")));
        QVERIFY(companion->isEnabled());
        QVERIFY(dialog.companion().write);

        format->setCurrentIndex(format->findData(QStringLiteral("pes")));
        QVERIFY(!companion->isEnabled());
        QVERIFY(!dialog.companion().write); // PES emporte ses couleurs
    }

private:
    static MachineExportDialog::SummaryFn summarizeNothing() {
        return
            [](const openstitch::formats::FormatInfo&,
               const openstitch::formats::MachineExportOptions&,
               const project_io::ExportOptimizations&) { return MachineExportDialog::Summary{}; };
    }

    QTemporaryDir settingsDir_;
};

QTEST_MAIN(MachineExportDialogTest)
#include "test_machine_export_dialog.moc"
