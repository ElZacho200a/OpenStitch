// SPDX-License-Identifier: Apache-2.0
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "placement_template_dialog.hpp"

using namespace openstitch;

namespace {
Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

stitch::StitchSequence bigRectangle() {
    using T = stitch::CommandType;
    stitch::StitchSequence seq;
    seq.commands = {{um(0, 0), T::Stitch, ObjectId{1}},
                    {um(300'000, 0), T::Stitch, ObjectId{1}},
                    {um(300'000, 100'000), T::Stitch, ObjectId{1}},
                    {um(0, 100'000), T::Stitch, ObjectId{1}},
                    {um(0, 100'000), T::End, ObjectId{}}};
    return seq;
}
} // namespace

class TestPlacementTemplateDialog : public QObject {
    Q_OBJECT

private slots:
    void paperAndOrientationChangeThePageGrid() {
        const auto seq = bigRectangle();
        desktop::PlacementTemplateDialog dialog(seq, QStringLiteral("Mon projet"));
        const auto a4Pages = dialog.placementTemplate().pages.size();
        QVERIFY(a4Pages >= 2);
        QVERIFY(dialog.summaryText().contains(QString::number(a4Pages)));

        dialog.landscapeCheck()->setChecked(true);
        QCOMPARE(dialog.placementTemplate().page_width.value, 297'000);
        QVERIFY(dialog.placementTemplate().pages.size() <= a4Pages);

        dialog.paperCombo()->setCurrentIndex(1); // Lettre
        QCOMPARE(dialog.placementTemplate().page_height.value, 215'900);
    }

    void exportsAreValidPdfFiles() {
        const auto seq = bigRectangle();
        desktop::PlacementTemplateDialog dialog(seq, QStringLiteral("Mon projet"));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString corePath = dir.filePath(QStringLiteral("core.pdf"));
        const QString qtPath = dir.filePath(QStringLiteral("qt.pdf"));
        QVERIFY(dialog.exportPdf(corePath));
        QVERIFY(dialog.exportQtPdf(qtPath));
        for (const auto& path : {corePath, qtPath}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.read(4), QByteArray("%PDF"));
        }
        QFile core(corePath);
        QVERIFY(core.open(QIODevice::ReadOnly));
        QVERIFY(core.readAll().contains(
            QByteArray("/Count ") +
            QByteArray::number(static_cast<qulonglong>(dialog.placementTemplate().pages.size()))));
    }
};

QTEST_MAIN(TestPlacementTemplateDialog)
#include "test_placement_template_dialog.moc"
