// SPDX-License-Identifier: Apache-2.0
// Logique pure de la table des gestes (lot L5, §5.1) : QtCore/Gui seulement,
// volontairement SANS QApplication.

#include <QPainterPath>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QtTest>

#include <optional>

#include "interaction_map.hpp"

using namespace openstitch::desktop;

namespace {

QString rowId(const Row& r) {
    return QString::fromLatin1(r.id);
}

bool listedIn(const QList<const Row*>& rows, const char* id) {
    for (const Row* r : rows) {
        if (QLatin1String(r->id) == QLatin1String(id)) {
            return true;
        }
    }
    return false;
}

// QSettings « natif » redirigé vers un dossier temporaire, quelle que soit la plate-forme.
struct SettingsSandbox {
    QTemporaryDir dir;
    QSettings::Format savedFormat = QSettings::defaultFormat();
    SettingsSandbox() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, dir.path());
    }
    ~SettingsSandbox() { QSettings::setDefaultFormat(savedFormat); }
};

QPainterPath rectShape(const QRectF& r) {
    QPainterPath p;
    p.addRect(r);
    return p;
}

} // namespace

class TestInteractionMap : public QObject {
    Q_OBJECT
private slots:
    void init() {
        InteractionMap::setPreset(Preset::OpenStitch);
        InteractionMap::setLongPressMsForTesting(-1);
    }

    void tableHasNoDuplicateContextGesture() {
        const auto rows = InteractionMap::rawRows();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            for (std::size_t j = i + 1; j < rows.size(); ++j) {
                const bool same =
                    rows[i].context == rows[j].context && rows[i].gesture == rows[j].gesture;
                QVERIFY2(!same, qPrintable(rowId(rows[i]) + " et " + rowId(rows[j]) +
                                           " ont le meme (contexte, geste)"));
            }
        }
    }

    void tableIdsAreUnique() {
        QSet<QString> ids;
        for (const Row& r : InteractionMap::rawRows()) {
            QVERIFY2(!ids.contains(rowId(r)), qPrintable("id duplique " + rowId(r)));
            ids.insert(rowId(r));
        }
        QVERIFY(ids.size() > 30);
    }

    void specificRowsMayOnlyShadowGlobalWhenOverrideFlagged() {
        const auto rows = InteractionMap::rawRows();
        for (const Row& r : rows) {
            bool shadows = false;
            for (const Row& g : rows) {
                if (g.context == Context::Global && g.gesture == r.gesture && !g.planned) {
                    shadows = true;
                }
            }
            if (r.context == Context::Global || r.planned) {
                QVERIFY2(!r.overridesGlobal,
                         qPrintable(rowId(r) + " : overridesGlobal sans objet"));
                continue;
            }
            QCOMPARE_EQ(shadows, r.overridesGlobal);
        }
    }

    void plannedRowsAreNeverResolvedNorListed() {
        int planned = 0;
        for (const Row& r : InteractionMap::rawRows()) {
            if (!r.planned) {
                continue;
            }
            ++planned;
            QVERIFY2(!InteractionMap::resolve(r.context, r.gesture).has_value(), r.id);
            QVERIFY2(!listedIn(InteractionMap::allRows(false), r.id), r.id);
            QVERIFY2(listedIn(InteractionMap::allRows(true), r.id), r.id);
            QVERIFY2(!r.routed && !r.hint, r.id);
            for (const Hint& h : InteractionMap::hintsFor(r.context, {})) {
                QVERIFY(h.gesture != InteractionMap::describe(r.gesture) ||
                        h.label != InteractionMap::label(r));
            }
        }
        QVERIFY(planned >= 3); // S9, S9b, N3
    }

    void everyRowResolvesToItself_data() {
        QTest::addColumn<int>("index");
        const auto rows = InteractionMap::rawRows();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            QTest::newRow(rows[i].id) << static_cast<int>(i);
        }
    }
    void everyRowResolvesToItself() {
        QFETCH(int, index);
        const Row& r = InteractionMap::rawRows()[static_cast<std::size_t>(index)];
        const auto got = InteractionMap::resolve(r.context, r.gesture);
        if (r.planned) {
            QVERIFY(!got.has_value());
        } else {
            QVERIFY(got.has_value());
            QCOMPARE(*got, r.intent);
        }
    }

    void globalGesturesResolveInEveryContext() {
        const Gesture middleDrag{GestureKind::Drag, Qt::MiddleButton, {}, Qt::Key(0)};
        for (const Context c : {Context::Select, Context::DrawFreeform, Context::DrawClicks,
                                Context::NodeEdit, Context::DrawBezier, Context::Crop}) {
            QCOMPARE(InteractionMap::resolve(c, middleDrag),
                     std::optional<Intent>(Intent::PanView));
        }
        // Escape : Global partout, mais la ligne spécifique prime en DrawClicks.
        const Gesture esc{GestureKind::Key, Qt::NoButton, {}, Qt::Key_Escape};
        QCOMPARE(InteractionMap::resolve(Context::Select, esc),
                 std::optional<Intent>(Intent::CancelTool));
    }

    void specificContextWinsOverGlobal() {
        const Gesture del{GestureKind::Key, Qt::NoButton, {}, Qt::Key_Delete};
        QCOMPARE(InteractionMap::resolve(Context::Select, del),
                 std::optional<Intent>(Intent::DeleteSelection));
        QCOMPARE(InteractionMap::resolve(Context::NodeEdit, del),
                 std::optional<Intent>(Intent::DeleteNodes));
    }

    void modifiersAreMatchedExactly() {
        const Gesture plain{GestureKind::Click, Qt::LeftButton, {}, Qt::Key(0)};
        const Gesture shift{GestureKind::Click, Qt::LeftButton, Qt::ShiftModifier, Qt::Key(0)};
        const Gesture both{GestureKind::Click, Qt::LeftButton,
                           Qt::ShiftModifier | Qt::ControlModifier, Qt::Key(0)};
        QCOMPARE(InteractionMap::resolve(Context::Select, plain),
                 std::optional<Intent>(Intent::SelectReplace));
        QCOMPARE(InteractionMap::resolve(Context::Select, shift),
                 std::optional<Intent>(Intent::SelectAdd));
        QVERIFY(!InteractionMap::resolve(Context::Select, both).has_value());
        // Maj + clic n'est pas un clic en contexte de dessin.
        QVERIFY(!InteractionMap::resolve(Context::DrawClicks, shift).has_value());
    }

    void ctrlWinsOverShiftForSelectMode() {
        QCOMPARE(InteractionMap::selectModeFor({}), SelectMode::Replace);
        QCOMPARE(InteractionMap::selectModeFor(Qt::ShiftModifier), SelectMode::Add);
        QCOMPARE(InteractionMap::selectModeFor(Qt::ControlModifier), SelectMode::Toggle);
        QCOMPARE(InteractionMap::selectModeFor(Qt::ShiftModifier | Qt::ControlModifier),
                 SelectMode::Toggle);
    }

    void contextForMapsToolsAndEditModes() {
        QCOMPARE(InteractionMap::contextFor(Tool::Select, false, false), Context::Select);
        QCOMPARE(InteractionMap::contextFor(Tool::Pan, false, false), Context::Pan);
        QCOMPARE(InteractionMap::contextFor(Tool::Rect, false, false), Context::Crop);
        QCOMPARE(InteractionMap::contextFor(Tool::DrawEllipse, false, false), Context::DrawBox);
        QCOMPARE(InteractionMap::contextFor(Tool::DrawSatinColumn, false, false),
                 Context::DrawClicks);
        QCOMPARE(InteractionMap::contextFor(Tool::DrawSatinCutLine, false, false),
                 Context::DrawBezier);
        QCOMPARE(InteractionMap::contextFor(Tool::DrawFreeform, false, false),
                 Context::DrawFreeform);
        QCOMPARE(InteractionMap::contextFor(Tool::Select, true, false), Context::NodeEdit);
        QCOMPARE(InteractionMap::contextFor(Tool::Select, true, true), Context::StitchEdit);
    }

    void hintsWithoutModifierAreFlaggedRowsOnly() {
        for (const Context c : {Context::Select, Context::DrawClicks, Context::NodeEdit,
                                Context::Pan, Context::Global}) {
            const QList<Hint> hints = InteractionMap::hintsFor(c, {});
            QVERIFY2(!hints.isEmpty(), qPrintable(InteractionMap::contextName(c)));
            QVERIFY(hints.size() <= 6);
            for (const Hint& h : hints) {
                bool flagged = false;
                for (const Row& r : InteractionMap::rawRows()) {
                    if (r.hint && !r.planned && InteractionMap::describe(r.gesture) == h.gesture) {
                        flagged = true;
                    }
                }
                QVERIFY2(flagged, qPrintable(h.gesture));
            }
        }
        const QList<Hint> select = InteractionMap::hintsFor(Context::Select, {});
        QCOMPARE(select.at(0).gesture, QStringLiteral("Clic"));
        QCOMPARE(select.at(0).label, QStringLiteral("sélectionner (le vide désélectionne)"));
    }

    void hintsWithShiftListShiftRows() {
        const QList<Hint> hints = InteractionMap::hintsFor(Context::Select, Qt::ShiftModifier);
        QVERIFY(!hints.isEmpty());
        bool addClick = false;
        for (const Hint& h : hints) {
            QVERIFY2(h.gesture.contains(QStringLiteral("Maj")), qPrintable(h.gesture));
            if (h.gesture == QStringLiteral("Maj + clic")) {
                addClick = true;
                QCOMPARE(h.label, QStringLiteral("ajouter à la sélection"));
            }
        }
        QVERIFY(addClick);
    }

    void hintsDifferBetweenSelectAndDrawContexts() {
        const QList<Hint> select = InteractionMap::hintsFor(Context::Select, Qt::ShiftModifier);
        const QList<Hint> box = InteractionMap::hintsFor(Context::DrawBox, Qt::ShiftModifier);
        const QList<Hint> move = InteractionMap::hintsFor(Context::Move, Qt::ShiftModifier);
        QVERIFY(select.size() >= 1 && box.size() >= 1 && move.size() >= 1);
        QVERIFY(select != box);
        QVERIFY(select != move);
        QCOMPARE(box.first().label, QStringLiteral("contraindre la forme (carré, cercle)"));
        QCOMPARE(move.first().label, QStringLiteral("verrouiller l'axe"));
    }

    void shadowedGlobalRowsAreNotListedTwice() {
        const QList<Hint> hints = InteractionMap::hintsFor(Context::NodeEdit, {});
        int suppr = 0;
        for (const Hint& h : hints) {
            suppr += h.gesture == QStringLiteral("Suppr") ? 1 : 0;
        }
        QCOMPARE(suppr, 1);
    }

    void touchpadPresetHidesMiddleButtonRowsAndKeepsSpacePan() {
        QVERIFY(listedIn(InteractionMap::allRows(), "G2"));
        QVERIFY(listedIn(InteractionMap::allRows(), "G3"));
        QVERIFY(listedIn(InteractionMap::allRows(), "G4"));

        InteractionMap::setPreset(Preset::Touchpad);
        const auto rows = InteractionMap::allRows();
        QVERIFY(!listedIn(rows, "G2"));
        QVERIFY(!listedIn(rows, "G3"));
        QVERIFY(!listedIn(rows, "G4"));
        QVERIFY(listedIn(rows, "G5"));  // Espace + glisser
        QVERIFY(listedIn(rows, "G13")); // Ctrl + molette dans les deux préréglages
        QVERIFY(listedIn(rows, "G1"));

        const QList<Hint> hints = InteractionMap::hintsFor(Context::Select, {});
        bool space = false;
        for (const Hint& h : hints) {
            QVERIFY(!h.gesture.contains(QStringLiteral("molette + glisser")));
            space = space || h.gesture == QStringLiteral("Espace + glisser");
        }
        QVERIFY(space);

        // Le préréglage ne change que l'affichage : la résolution reste intacte.
        const Gesture middleDrag{GestureKind::Drag, Qt::MiddleButton, {}, Qt::Key(0)};
        QVERIFY(InteractionMap::resolve(Context::Select, middleDrag).has_value());
        const Gesture spaceDrag{GestureKind::Drag, Qt::LeftButton, {}, Qt::Key_Space};
        QCOMPARE(InteractionMap::resolve(Context::DrawFreeform, spaceDrag),
                 std::optional<Intent>(Intent::PanView));
    }

    void noDuplicateContextGestureWithinEachPreset() {
        for (const Preset p : {Preset::OpenStitch, Preset::Touchpad}) {
            InteractionMap::setPreset(p);
            const auto rows = InteractionMap::allRows(true);
            for (qsizetype i = 0; i < rows.size(); ++i) {
                for (qsizetype j = i + 1; j < rows.size(); ++j) {
                    QVERIFY(!(rows[i]->context == rows[j]->context &&
                              rows[i]->gesture == rows[j]->gesture));
                }
            }
        }
    }

    void presetRoundTripsThroughQSettings() {
        SettingsSandbox sandbox;
        QVERIFY(sandbox.dir.isValid());
        InteractionMap::setPreset(Preset::Touchpad);
        // setPreset() n'écrit pas.
        QCOMPARE(InteractionMap::loadPreset(), Preset::OpenStitch);
        InteractionMap::setPreset(Preset::Touchpad);
        InteractionMap::savePreset();
        InteractionMap::setPreset(Preset::OpenStitch);
        QCOMPARE(InteractionMap::loadPreset(), Preset::Touchpad);
        QCOMPARE(InteractionMap::preset(), Preset::Touchpad);
        InteractionMap::setPreset(Preset::OpenStitch);
        InteractionMap::savePreset();
        InteractionMap::setPreset(Preset::Touchpad);
        QCOMPARE(InteractionMap::loadPreset(), Preset::OpenStitch);
    }

    void longPressDelayIsClampedAndConfigurable() {
        SettingsSandbox sandbox;
        QCOMPARE(InteractionMap::longPressMs(), InteractionMap::kLongPressMs);
        {
            QSettings s(QSettings::defaultFormat(), QSettings::UserScope,
                        QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"));
            s.setValue(QStringLiteral("navigation/longPressMs"), 700);
        }
        QCOMPARE(InteractionMap::longPressMs(), 700);
        {
            QSettings s(QSettings::defaultFormat(), QSettings::UserScope,
                        QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"));
            s.setValue(QStringLiteral("navigation/longPressMs"), 5);
        }
        QCOMPARE(InteractionMap::longPressMs(), 300);
        {
            QSettings s(QSettings::defaultFormat(), QSettings::UserScope,
                        QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"));
            s.setValue(QStringLiteral("navigation/longPressMs"), 99999);
        }
        QCOMPARE(InteractionMap::longPressMs(), 1000);
        // Le setter de test prime sur tout, -1 revient à la valeur normale.
        InteractionMap::setLongPressMsForTesting(1);
        QCOMPARE(InteractionMap::longPressMs(), 1);
        InteractionMap::setLongPressMsForTesting(-1);
        QCOMPARE(InteractionMap::longPressMs(), 1000);
    }

    void rectSelectsWindowRequiresFullContainment() {
        const QRectF rect(0, 0, 10, 10);
        QVERIFY(InteractionMap::rectSelects(rect, rectShape({2, 2, 4, 4}), false));
        QVERIFY(!InteractionMap::rectSelects(rect, rectShape({8, 8, 4, 4}), false));
        QVERIFY(!InteractionMap::rectSelects(rect, rectShape({20, 20, 4, 4}), false));
        QVERIFY(!InteractionMap::rectSelects(rect, QPainterPath{}, false));
    }

    void rectSelectsCrossingAcceptsPartialOverlap() {
        const QRectF rect(0, 0, 10, 10);
        QVERIFY(InteractionMap::rectSelects(rect, rectShape({8, 8, 4, 4}), true));
        QVERIFY(InteractionMap::rectSelects(rect, rectShape({2, 2, 4, 4}), true));
        QVERIFY(!InteractionMap::rectSelects(rect, rectShape({20, 20, 4, 4}), true));
        QVERIFY(!InteractionMap::rectSelects(rect, QPainterPath{}, true));
    }

    void describeFormatsModifiersInFrench() {
        QCOMPARE(InteractionMap::describe({GestureKind::Drag, Qt::MiddleButton,
                                           Qt::ControlModifier | Qt::ShiftModifier, Qt::Key(0)}),
                 QStringLiteral("Ctrl + Maj + clic molette + glisser"));
        QCOMPARE(InteractionMap::describe(
                     {GestureKind::Click, Qt::LeftButton, Qt::ShiftModifier, Qt::Key(0)}),
                 QStringLiteral("Maj + clic"));
        QCOMPARE(InteractionMap::describe({GestureKind::Click, Qt::LeftButton, {}, Qt::Key(0)}),
                 QStringLiteral("Clic"));
        QCOMPARE(InteractionMap::describe({GestureKind::Drag, Qt::LeftButton, {}, Qt::Key_Space}),
                 QStringLiteral("Espace + glisser"));
        QCOMPARE(InteractionMap::describe(
                     {GestureKind::Wheel, Qt::NoButton, Qt::AltModifier, Qt::Key(0)}),
                 QStringLiteral("Alt + molette"));
        QCOMPARE(InteractionMap::describe({GestureKind::Key, Qt::NoButton, {}, Qt::Key_Escape}),
                 QStringLiteral("Échap"));
        QCOMPARE(
            InteractionMap::describe({GestureKind::DoubleClick, Qt::MiddleButton, {}, Qt::Key(0)}),
            QStringLiteral("Double-clic molette"));
        QCOMPARE(InteractionMap::describe({GestureKind::LongPress, Qt::LeftButton, {}, Qt::Key(0)}),
                 QStringLiteral("Appui long"));
    }

    void generatedRowsHaveLabelsAndContextNames() {
        for (const Row* r : InteractionMap::allRows()) {
            QVERIFY2(!InteractionMap::label(*r).isEmpty(), r->id);
            QVERIFY2(!InteractionMap::describe(r->gesture).isEmpty(), r->id);
            QVERIFY2(!InteractionMap::contextName(r->context).isEmpty(), r->id);
        }
    }
};

QTEST_APPLESS_MAIN(TestInteractionMap)
#include "test_interaction_map.moc"
