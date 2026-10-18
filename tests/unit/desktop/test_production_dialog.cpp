// SPDX-License-Identifier: Apache-2.0
#include <QDoubleSpinBox>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTest>

#include "production_dialog.hpp"

using namespace openstitch;

namespace {
Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}
} // namespace

class TestProductionDialog : public QObject {
    Q_OBJECT

private slots:
    void fieldsFeedTheSheetAndPdfIsWritten() {
        using T = stitch::CommandType;
        document::Project project;
        document::EmbroideryObject a;
        a.id = ObjectId{1};
        a.name = "Rond";
        a.rgb = {200, 10, 10};
        project.embroidery_objects.push_back(a);
        stitch::StitchSequence seq;
        seq.commands = {{um(0, 0), T::Stitch, ObjectId{1}},
                        {um(10'000, 0), T::Stitch, ObjectId{1}},
                        {um(10'000, 10'000), T::Stitch, ObjectId{1}},
                        {um(10'000, 10'000), T::End, ObjectId{}}};

        desktop::ProductionDialog dialog(project, seq, QStringLiteral("Mon projet"),
                                         stitch_render::RenderParams{});
        QCOMPARE(dialog.sheet().project_name, std::string("Mon projet"));
        QCOMPARE(dialog.sheet().stitches, std::size_t{3});

        dialog.notesEdit()->setPlainText(QStringLiteral("Client : Dupont"));
        dialog.speedSpin()->setValue(600.0);
        dialog.refreshNow();
        QCOMPARE(dialog.sheet().notes, std::string("Client : Dupont"));
        QCOMPARE(dialog.sheet().stitches_per_minute, 600.0);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("fiche.pdf"));
        QVERIFY(dialog.exportPdf(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.read(4), QByteArray("%PDF"));
    }

    void threadConsumptionAndBobbinsAreShown() {
        // HP-PROD-005 : lignes de consommation par couleur et nombre de canettes.
        using T = stitch::CommandType;
        document::Project project;
        document::EmbroideryObject a;
        a.id = ObjectId{1};
        a.name = "Rond";
        a.rgb = {200, 10, 10};
        project.embroidery_objects.push_back(a);
        stitch::StitchSequence seq;
        seq.commands = {{um(0, 0), T::Stitch, ObjectId{1}},
                        {um(100'000, 0), T::Stitch, ObjectId{1}},
                        {um(100'000, 100'000), T::Stitch, ObjectId{1}},
                        {um(100'000, 100'000), T::End, ObjectId{}}};

        desktop::ProductionDialog dialog(project, seq, QStringLiteral("Conso"),
                                         stitch_render::RenderParams{});
        QVERIFY(!dialog.sheet().consumption.empty());
        QVERIFY(dialog.sheet().total_top_m > 0.2); // 0,2 m cousus, facteur > 1
        QCOMPARE(dialog.sheet().bobbins_needed, std::size_t{1});
        QVERIFY(dialog.viewPlainText().contains(QStringLiteral("Consommation de fil")));
        QVERIFY(dialog.viewPlainText().contains(QStringLiteral("Canettes nécessaires")));

        // La fiche suit le champ « capacité d'une canette ».
        dialog.bobbinSpin()->setValue(40.0);
        dialog.refreshNow();
        QCOMPARE(dialog.sheet().bobbin_capacity_m, 40.0);
        QVERIFY(dialog.viewPlainText().contains(QStringLiteral("40 m")));
    }
};

QTEST_MAIN(TestProductionDialog)
#include "test_production_dialog.moc"
