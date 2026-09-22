// SPDX-License-Identifier: Apache-2.0
// Dialogue « Options de génération » (Lots E/F, audit marine plein cadre) :
// seuil de coupe, type de point d'arrêt, points courts.
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QTest>
#include <QTimer>

#include "generation_options_dialog.hpp"

using openstitch::desktop::editSequenceFinishing;
namespace document = openstitch::document;

class GenerationOptionsTest : public QObject {
    Q_OBJECT

private slots:
    void acceptReturnsEditedValues() {
        const document::SequenceFinishing current;
        QTimer::singleShot(0, [] {
            auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dlg != nullptr);
            auto* trim = dlg->findChild<QDoubleSpinBox*>("trimThreshold");
            auto* lock = dlg->findChild<QComboBox*>("lockType");
            QVERIFY(trim != nullptr && lock != nullptr);
            QCOMPARE(trim->value(), 3.0); // défaut : 3 mm
            trim->setValue(4.5);
            lock->setCurrentIndex(lock->findData(static_cast<int>(document::LockStitch::Triangle)));
            dlg->accept();
        });
        const auto edited = editSequenceFinishing(nullptr, current);
        QVERIFY(edited.has_value());
        QCOMPARE(edited->trim_threshold.value, 4'500);
        QVERIFY(edited->lock_type == document::LockStitch::Triangle);
        QCOMPARE(edited->min_stitch_length.value, current.min_stitch_length.value);
    }

    void cancelReturnsNothing() {
        QTimer::singleShot(0, [] {
            if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                dlg->reject();
            }
        });
        QVERIFY(!editSequenceFinishing(nullptr, document::SequenceFinishing{}).has_value());
    }
};

QTEST_MAIN(GenerationOptionsTest)
#include "test_generation_options.moc"
