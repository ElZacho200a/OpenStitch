// SPDX-License-Identifier: Apache-2.0
#include <QtTest/QtTest>

#include "autodigitize_presets.hpp"

using namespace openstitch::desktop_v2::studio;

class StudioAutodigitizePresetsTest : public QObject {
    Q_OBJECT

private slots:
    void builtinsAreDistinctAndImmutable() {
        AutodigitizePresetStore store;
        QCOMPARE(store.all().size(), std::size_t{3});
        const auto standard = store.apply("Standard");
        const auto details = store.apply("Détails fins");
        const auto simple = store.apply("Aplats simples");
        QVERIFY(standard.has_value());
        QVERIFY(details.has_value());
        QVERIFY(simple.has_value());
        QCOMPARE(standard->max_colors, 8);
        QCOMPARE(details->max_colors, 10);
        QCOMPARE(simple->max_colors, 5);
        QVERIFY(!store.remove("Standard"));
        QVERIFY(!store.save("Standard", *details));
        QCOMPARE(store.all().size(), std::size_t{3});
    }

    void updatingUserPresetDoesNotMutateBuiltinsOrOtherProfiles() {
        AutodigitizePresetStore store;
        auto original = store.apply("Standard");
        QVERIFY(original.has_value());
        auto customized = *original;
        customized.max_colors = 3;
        customized.fill_overlap_mm = 0.8;
        QVERIFY(!store.update("Standard", customized));
        QVERIFY(!store.update("Inconnu", customized));
        QVERIFY(store.save("Premier", *original));
        QVERIFY(store.save("Second", *original));
        QVERIFY(store.update("Premier", customized));
        const auto changed = store.apply("Premier");
        const auto untouched = store.apply("Second");
        const auto builtin = store.apply("Standard");
        QVERIFY(changed.has_value());
        QVERIFY(untouched.has_value());
        QVERIFY(builtin.has_value());
        QCOMPARE(changed->max_colors, 3);
        QCOMPARE(changed->fill_overlap_mm, 0.8);
        QCOMPARE(untouched->max_colors, original->max_colors);
        QCOMPARE(builtin->max_colors, original->max_colors);
        QCOMPARE(store.all().size(), std::size_t{5});
        QVERIFY(store.remove("Premier"));
        QVERIFY(!store.update("Premier", customized));
    }

    void userPresetRoundTripPreservesAdvancedSettings() {
        AutodigitizePresetStore store;
        openstitch::project_ops::DigitizeSettings custom;
        custom.max_colors = 7;
        custom.skip_background = 2;
        custom.mode = "contours";
        custom.detail = 0.75;
        custom.single_path = true;
        custom.max_threads = 4;
        custom.fill_overlap_mm = 0.65;
        custom.dark_lines = true;
        custom.dark_lines_width_mm = 1.2;
        custom.pre = "bilateral:2+median:1";
        QVERIFY(store.save("Mon profil", custom));
        QVERIFY(!store.save("Mon profil", custom));
        QVERIFY(!store.save("", custom));
        QVERIFY(!store.save("   ", custom));
        const auto loaded = store.apply("Mon profil");
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->max_colors, custom.max_colors);
        QCOMPARE(loaded->skip_background, custom.skip_background);
        QCOMPARE(loaded->mode, custom.mode);
        QCOMPARE(loaded->detail, custom.detail);
        QCOMPARE(loaded->single_path, custom.single_path);
        QCOMPARE(loaded->max_threads, custom.max_threads);
        QCOMPARE(loaded->fill_overlap_mm, custom.fill_overlap_mm);
        QCOMPARE(loaded->dark_lines, custom.dark_lines);
        QCOMPARE(loaded->dark_lines_width_mm, custom.dark_lines_width_mm);
        QCOMPARE(loaded->pre, custom.pre);
        QVERIFY(store.remove("Mon profil"));
        QVERIFY(!store.apply("Mon profil").has_value());
        QVERIFY(!store.remove("Mon profil"));
        QCOMPARE(store.all().size(), std::size_t{3});
    }
};

QTEST_GUILESS_MAIN(StudioAutodigitizePresetsTest)
#include "test_studio_autodigitize_presets.moc"
