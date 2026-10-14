// SPDX-License-Identifier: Apache-2.0
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <algorithm>

#include "app_theme.hpp"
#include "color_blend_dialog.hpp"

using namespace openstitch;
using openstitch::desktop::ColorBlendDialog;

namespace {
constexpr ColorBlendDialog::Rgb backgroundRgb{200, 30, 30};
constexpr ColorBlendDialog::Rgb foregroundRgb{30, 30, 200};
constexpr ObjectId backgroundId{10};
constexpr ObjectId foregroundId{20};

stitch::StitchSequence previewSequence() {
    using enum stitch::CommandType;
    return {.commands = {
                {{Micrometers{0}, Micrometers{0}}, Stitch, backgroundId},
                {{Micrometers{10'000}, Micrometers{0}}, Stitch, backgroundId},
                {{Micrometers{10'000}, Micrometers{10'000}}, Jump, backgroundId},
                {{Micrometers{0}, Micrometers{10'000}}, Stitch, backgroundId},
                // Le changement d'objet coupe la continuité même sans ColorChange.
                {{Micrometers{0}, Micrometers{0}}, Stitch, foregroundId},
                {{Micrometers{10'000}, Micrometers{10'000}}, Stitch, foregroundId},
                {{}, Trim, foregroundId},
                {{Micrometers{10'000}, Micrometers{0}}, Stitch, foregroundId},
                {{Micrometers{0}, Micrometers{0}}, Stitch, foregroundId},
                {{}, ColorChange, foregroundId},
                // Une autre source du snapshot n'appartient pas au fondu.
                {{Micrometers{100'000}, Micrometers{0}}, Stitch, ObjectId{30}},
                {{Micrometers{110'000}, Micrometers{0}}, Stitch, ObjectId{30}},
                {{}, End, {}},
            }};
}

void supplyPreview(ColorBlendDialog& dialog) {
    dialog.setStitchPreview(previewSequence(), backgroundId, foregroundId, backgroundRgb,
                            foregroundRgb);
}

class PaintCounter : public QObject {
public:
    int paints{0};

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Paint) {
            ++paints;
        }
        return QObject::eventFilter(watched, event);
    }
};
} // namespace

class ColorBlendDialogTest : public QObject {
    Q_OBJECT

private slots:
    void validationAndInversion();
    void editingAndProfile();
    void initialPreviewIsDeferredAndSettingsAreDebounced();
    void proposalIsGroupedAndKeepsAnalyzedExtent();
    void stitchPreviewAndLayerFilter();
    void changesInvalidatePreviewAndInvalidColorsStopRequests();
    void previewMessageClearsCurrentDisplay();
    void synchronousPreviewDoesNotRecurse();
    void staleResultDuringNestedEventsIsRejected();
};

void ColorBlendDialogTest::validationAndInversion() {
    ColorBlendDialog dialog(backgroundRgb, foregroundRgb, false, false);
    auto* background = dialog.findChild<QLineEdit*>("edit_blendBackground");
    auto* foreground = dialog.findChild<QLineEdit*>("edit_blendForeground");
    auto* accept = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
    auto* refresh = dialog.findChild<QPushButton*>("button_blendPreview");
    QVERIFY(background && foreground && accept && refresh);
    QVERIFY(accept->isEnabled());
    foreground->setText(background->text());
    QVERIFY(!accept->isEnabled());
    QVERIFY(!refresh->isEnabled());
    QVERIFY(!dialog.findChild<QLabel*>("blend_message")->text().isEmpty());
    foreground->setText(QStringLiteral("#123"));
    QVERIFY(!accept->isEnabled());
    foreground->setText(QStringLiteral("#3355ee"));
    QVERIFY(accept->isEnabled());
    QVERIFY(refresh->isEnabled());
    QCOMPARE(dialog.foregroundColor(), (ColorBlendDialog::Rgb{0x33, 0x55, 0xee}));
    QCOMPARE(dialog.startSpacing(), Micrometers{4'000});
    QCOMPARE(dialog.endSpacing(), Micrometers{400});
    dialog.findChild<QPushButton*>("button_blendReverse")->click();
    QCOMPARE(dialog.startSpacing(), Micrometers{400});
    QCOMPARE(dialog.endSpacing(), Micrometers{4'000});
    dialog.findChild<QPushButton*>("button_blendReverse")->click();
    QCOMPARE(dialog.startSpacing(), Micrometers{4'000});
    QCOMPARE(dialog.endSpacing(), Micrometers{400});
    QVERIFY(!dialog.findChild<QPushButton*>("button_blendAnalyze")->isEnabled());

    ColorBlendDialog analyzable(backgroundRgb, foregroundRgb, true, false);
    QSignalSpy analysis(&analyzable, &ColorBlendDialog::analysisRequested);
    analyzable.findChild<QPushButton*>("button_blendAnalyze")->click();
    QCOMPARE(analysis.count(), 1);
    analyzable.setAnalysisMessage(QStringLiteral("Proposition analysée"));
    QCOMPARE(analyzable.findChild<QLabel*>("blend_message")->text(),
             QStringLiteral("Proposition analysée"));
}

void ColorBlendDialogTest::editingAndProfile() {
    ColorBlendDialog dialog(backgroundRgb, foregroundRgb, false, true);
    auto* profile = dialog.findChild<QComboBox*>("combo_blendProfile");
    auto* explanation = dialog.findChild<QLabel*>("blend_explanation");
    auto* note = dialog.findChild<QLabel*>("blend_profileNote");
    auto* accept = dialog.findChild<QPushButton*>("button_blendAccept");
    QVERIFY(profile && explanation && note && accept);
    QCOMPARE(profile->count(), 2);
    QCOMPARE(profile->itemText(0), QStringLiteral("Densités complémentaires"));
    QCOMPARE(profile->itemText(1), QStringLiteral("Fond uniforme"));
    QVERIFY(dialog.complementary());
    QVERIFY(note->text().contains(QStringLiteral("sous-couche est désactivée")));
    QVERIFY(explanation->text().contains(QStringLiteral("Les deux zones seront unies")));
    const QString creationTitle = dialog.windowTitle();
    dialog.setEditing(true);
    QVERIFY(dialog.windowTitle() != creationTitle);
    QCOMPARE(accept->text(), QStringLiteral("Appliquer les modifications"));
    QVERIFY(explanation->text().contains(QStringLiteral("identifiants (IDs)")));
    QVERIFY(!explanation->text().contains(QStringLiteral("Les deux zones seront unies")));
    dialog.setComplementary(false);
    QVERIFY(!dialog.complementary());
    QCOMPARE(profile->currentIndex(), 1);
    QVERIFY(note->text().contains(QStringLiteral("Le fond reste uniforme")));
    dialog.setEditing(false);
    QCOMPARE(dialog.windowTitle(), creationTitle);
    QCOMPARE(accept->text(), QStringLiteral("Créer le fondu"));
    QVERIFY(explanation->text().contains(QStringLiteral("Les deux zones seront unies")));
    profile->setCurrentIndex(0);
    QVERIFY(dialog.complementary());
}

void ColorBlendDialogTest::initialPreviewIsDeferredAndSettingsAreDebounced() {
    ColorBlendDialog dialog(backgroundRgb, foregroundRgb, false, false);
    QSignalSpy requests(&dialog, &ColorBlendDialog::previewRequested);
    connect(&dialog, &ColorBlendDialog::previewRequested, &dialog, [&] { supplyPreview(dialog); });
    QCOMPARE(requests.count(), 0);
    auto* timer = dialog.findChild<QTimer*>("timer_blendPreview");
    QVERIFY(timer && timer->isSingleShot());
    QVERIFY(timer->interval() >= 150 && timer->interval() <= 250);
    QVERIFY(!timer->isActive());
    dialog.show();
    QCOMPARE(requests.count(), 0);
    QTRY_COMPARE(requests.count(), 1);
    QVERIFY(!timer->isActive());
    auto* angle = dialog.findChild<QDoubleSpinBox*>("spin_blendAngle");
    angle->setValue(20.0);
    angle->setValue(50.0);
    dialog.findChild<QDoubleSpinBox*>("spin_blendStart")->setValue(3.2);
    dialog.findChild<QDoubleSpinBox*>("spin_blendEnd")->setValue(0.6);
    dialog.findChild<QLineEdit*>("edit_blendForeground")->setText(QStringLiteral("#1122ee"));
    dialog.setComplementary(false);
    QCOMPARE(requests.count(), 1);
    QTRY_COMPARE(requests.count(), 2);
    QVERIFY(!timer->isActive());
    dialog.findChild<QPushButton*>("button_blendReverse")->click();
    QCOMPARE(requests.count(), 2);
    QTRY_COMPARE(requests.count(), 3);
    dialog.findChild<QPushButton*>("button_blendPreview")->click();
    QCOMPARE(requests.count(), 3);
    QTRY_COMPARE(requests.count(), 4);
    QVERIFY(!timer->isActive());
}

void ColorBlendDialogTest::proposalIsGroupedAndKeepsAnalyzedExtent() {
    ColorBlendDialog dialog(backgroundRgb, foregroundRgb, false, false);
    QSignalSpy requests(&dialog, &ColorBlendDialog::previewRequested);
    connect(&dialog, &ColorBlendDialog::previewRequested, &dialog, [&] { supplyPreview(dialog); });
    dialog.show();
    QTRY_COMPARE(requests.count(), 1);
    const document::DensityGradient gradient{
        .from = {Micrometers{-4'000}, Micrometers{-2'000}},
        .to = {Micrometers{-4'000}, Micrometers{12'000}},
        .spacing_from = Micrometers{3'000},
        .spacing_to = Micrometers{500},
    };
    dialog.setProposal({11, 22, 33}, {100, 110, 120}, gradient);
    QCOMPARE(requests.count(), 1);
    QCOMPARE(dialog.angleDegrees(), 90.0);
    QCOMPARE(dialog.startSpacing(), Micrometers{3'000});
    QCOMPARE(dialog.endSpacing(), Micrometers{500});
    QVERIFY(dialog.proposedGradient().has_value());
    QCOMPARE(dialog.proposedGradient()->from, gradient.from);
    QCOMPARE(dialog.proposedGradient()->to, gradient.to);
    QTRY_COMPARE(requests.count(), 2);
    QVERIFY(!dialog.findChild<QTimer*>("timer_blendPreview")->isActive());
    dialog.findChild<QPushButton*>("button_blendReverse")->click();
    QVERIFY(dialog.proposedGradient().has_value());
    QCOMPARE(dialog.proposedGradient()->spacing_from, Micrometers{500});
    QCOMPARE(dialog.proposedGradient()->spacing_to, Micrometers{3'000});
    dialog.findChild<QDoubleSpinBox*>("spin_blendAngle")->setValue(91.0);
    QVERIFY(!dialog.proposedGradient().has_value());
}

void ColorBlendDialogTest::stitchPreviewAndLayerFilter() {
    ColorBlendDialog dialog(backgroundRgb, foregroundRgb, false, false);
    auto* preview = dialog.findChild<QWidget*>("blend_preview");
    auto* layers = dialog.findChild<QComboBox*>("combo_blendPreviewLayer");
    QVERIFY(preview && layers);
    QVERIFY(preview->minimumWidth() >= 460 && preview->minimumHeight() >= 260);
    QCOMPARE(layers->count(), 3);
    PaintCounter paints;
    preview->installEventFilter(&paints);
    QSignalSpy requests(&dialog, &ColorBlendDialog::previewRequested);
    connect(&dialog, &ColorBlendDialog::previewRequested, &dialog, [&] { supplyPreview(dialog); });
    dialog.show();
    QTRY_COMPARE(requests.count(), 1);
    QTRY_VERIFY(paints.paints > 0);
    QVERIFY(preview->property("hasStitchPreview").toBool());
    QCOMPARE(preview->property("stitchCount").toULongLong(), qulonglong{7});
    // Quatre brins : pas de saut, de pont entre objets, ni de pont après coupe.
    QCOMPARE(preview->property("segmentCount").toULongLong(), qulonglong{4});
    auto* stats = dialog.findChild<QLabel*>("blend_previewStats");
    QVERIFY(stats->text().contains(QStringLiteral("7 points cousus")));
    auto* legend = dialog.findChild<QLabel*>("blend_previewLegend");
    QVERIFY(legend->text().contains(QStringLiteral("#c81e1e")));
    QVERIFY(legend->text().contains(QStringLiteral("#1e1ec8")));
    const int firstPaints = paints.paints;
    layers->setCurrentIndex(1);
    QCOMPARE(preview->property("stitchCount").toULongLong(), qulonglong{3});
    QCOMPARE(preview->property("segmentCount").toULongLong(), qulonglong{2});
    QTRY_VERIFY(paints.paints > firstPaints);
    const int backgroundPaints = paints.paints;
    layers->setCurrentIndex(2);
    QCOMPARE(preview->property("stitchCount").toULongLong(), qulonglong{4});
    QCOMPARE(preview->property("segmentCount").toULongLong(), qulonglong{2});
    QTRY_VERIFY(paints.paints > backgroundPaints);
    const int foregroundPaints = paints.paints;
    layers->setCurrentIndex(0);
    QCOMPARE(preview->property("stitchCount").toULongLong(), qulonglong{7});
    QTRY_VERIFY(paints.paints > foregroundPaints);
    const int beforeTheme = paints.paints;
    QVERIFY(QMetaObject::invokeMethod(&desktop::AppTheme::instance(), "changed"));
    QTRY_VERIFY(paints.paints > beforeTheme);
    QVERIFY(preview->property("hasStitchPreview").toBool());
    QVERIFY(!dialog.findChild<QTimer*>("timer_blendPreview")->isActive());
    QCOMPARE(requests.count(), 1);

    // Les couleurs affichées sont celles de la séquence fournie par le contrôleur.
    dialog.setStitchPreview(previewSequence(), backgroundId, foregroundId, {1, 2, 3}, {4, 5, 6});
    QVERIFY(legend->text().contains(QStringLiteral("#010203")));
    QVERIFY(legend->text().contains(QStringLiteral("#040506")));
    layers->setCurrentIndex(1);
    QCOMPARE(preview->property("segmentCount").toULongLong(), qulonglong{2});
    QCOMPARE(requests.count(), 1);
}

void ColorBlendDialogTest::changesInvalidatePreviewAndInvalidColorsStopRequests() {
    ColorBlendDialog dialog(backgroundRgb, foregroundRgb, false, false);
    QSignalSpy requests(&dialog, &ColorBlendDialog::previewRequested);
    connect(&dialog, &ColorBlendDialog::previewRequested, &dialog, [&] { supplyPreview(dialog); });
    dialog.show();
    QTRY_COMPARE(requests.count(), 1);
    auto* preview = dialog.findChild<QWidget*>("blend_preview");
    auto* timer = dialog.findChild<QTimer*>("timer_blendPreview");
    QVERIFY(preview->property("hasStitchPreview").toBool());
    dialog.findChild<QDoubleSpinBox*>("spin_blendAngle")->setValue(45.0);
    QVERIFY(!preview->property("hasStitchPreview").toBool());
    QCOMPARE(preview->property("stitchCount").toULongLong(), qulonglong{0});
    QVERIFY(dialog.findChild<QLabel*>("blend_previewLegend")->text().isEmpty());
    QVERIFY(timer->isActive());
    dialog.findChild<QLineEdit*>("edit_blendForeground")->setText(QStringLiteral("#1"));
    QVERIFY(!timer->isActive());
    supplyPreview(dialog); // Un ancien résultat ne rétablit pas un aperçu invalide.
    QVERIFY(!preview->property("hasStitchPreview").toBool());
    QTimer barrier;
    barrier.setSingleShot(true);
    QSignalSpy elapsed(&barrier, &QTimer::timeout);
    barrier.start(timer->interval() + 50);
    QTRY_COMPARE(elapsed.count(), 1);
    QCOMPARE(requests.count(), 1);
    dialog.findChild<QLineEdit*>("edit_blendForeground")->setText(QStringLiteral("#0033ff"));
    QTRY_COMPARE(requests.count(), 2);
    QVERIFY(preview->property("hasStitchPreview").toBool());
    dialog.setComplementary(false);
    QVERIFY(!preview->property("hasStitchPreview").toBool());
    QTRY_COMPARE(requests.count(), 3);
    QVERIFY(preview->property("hasStitchPreview").toBool());
}

void ColorBlendDialogTest::previewMessageClearsCurrentDisplay() {
    ColorBlendDialog dialog(backgroundRgb, foregroundRgb, false, false);
    supplyPreview(dialog);
    auto* preview = dialog.findChild<QWidget*>("blend_preview");
    QVERIFY(preview->property("hasStitchPreview").toBool());
    dialog.setPreviewMessage(QStringLiteral("Impossible de calculer les points"));
    QVERIFY(!preview->property("hasStitchPreview").toBool());
    QCOMPARE(preview->accessibleDescription(), QStringLiteral("Impossible de calculer les points"));
    QVERIFY(dialog.findChild<QLabel*>("blend_previewLegend")->text().isEmpty());
    dialog.findChild<QComboBox*>("combo_blendPreviewLayer")->setCurrentIndex(1);
    QVERIFY(!preview->property("hasStitchPreview").toBool());
    supplyPreview(dialog);
    QVERIFY(preview->property("hasStitchPreview").toBool());
    QCOMPARE(preview->property("stitchCount").toULongLong(), qulonglong{3});
    dialog.setStitchPreview({}, backgroundId, foregroundId, backgroundRgb, foregroundRgb);
    QVERIFY(!preview->property("hasStitchPreview").toBool());
    QCOMPARE(preview->property("segmentCount").toULongLong(), qulonglong{0});
}

void ColorBlendDialogTest::synchronousPreviewDoesNotRecurse() {
    ColorBlendDialog dialog(backgroundRgb, foregroundRgb, false, false);
    QSignalSpy requests(&dialog, &ColorBlendDialog::previewRequested);
    int depth = 0;
    int maxDepth = 0;
    connect(&dialog, &ColorBlendDialog::previewRequested, &dialog, [&] {
        ++depth;
        maxDepth = std::max(depth, maxDepth);
        dialog.setPreviewMessage(QStringLiteral("Calcul en cours"));
        supplyPreview(dialog);
        QApplication::processEvents();
        --depth;
    });
    dialog.show();
    QTRY_COMPARE(requests.count(), 1);
    QCOMPARE(maxDepth, 1);
    QVERIFY(!dialog.findChild<QTimer*>("timer_blendPreview")->isActive());
    QTimer barrier;
    barrier.setSingleShot(true);
    QSignalSpy elapsed(&barrier, &QTimer::timeout);
    barrier.start(260);
    QTRY_COMPARE(elapsed.count(), 1);
    QCOMPARE(requests.count(), 1);
    QCOMPARE(depth, 0);
}

void ColorBlendDialogTest::staleResultDuringNestedEventsIsRejected() {
    ColorBlendDialog dialog(backgroundRgb, foregroundRgb, false, false);
    QSignalSpy requests(&dialog, &ColorBlendDialog::previewRequested);
    auto* preview = dialog.findChild<QWidget*>("blend_preview");
    int calls = 0;
    int depth = 0;
    int maxDepth = 0;
    connect(&dialog, &ColorBlendDialog::previewRequested, &dialog, [&] {
        ++depth;
        maxDepth = std::max(maxDepth, depth);
        ++calls;
        if (calls == 1) {
            // Simule les événements traités pendant un BusyIndicator synchrone.
            QTimer::singleShot(0, &dialog, [&] {
                dialog.findChild<QDoubleSpinBox*>("spin_blendAngle")->setValue(90.0);
                dialog.findChild<QPushButton*>("button_blendPreview")->click();
            });
            QTRY_COMPARE(dialog.angleDegrees(), 90.0);
            QVERIFY(!dialog.findChild<QTimer*>("timer_blendPreview")->isActive());
            supplyPreview(dialog);
            QVERIFY(!preview->property("hasStitchPreview").toBool());
        } else {
            supplyPreview(dialog);
        }
        --depth;
    });
    dialog.show();
    QTRY_COMPARE(requests.count(), 2);
    QCOMPARE(maxDepth, 1);
    QCOMPARE(depth, 0);
    QVERIFY(preview->property("hasStitchPreview").toBool());
    QVERIFY(!dialog.findChild<QTimer*>("timer_blendPreview")->isActive());
}

QTEST_MAIN(ColorBlendDialogTest)
#include "test_color_blend_dialog.moc"
