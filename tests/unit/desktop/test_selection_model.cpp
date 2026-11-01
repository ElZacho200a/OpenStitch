// SPDX-License-Identifier: Apache-2.0
// Modèle de sélection pur (HP-OBJ-001) : aucun QApplication requis.
#include <QTest>

#include <algorithm>

#include "selection_model.hpp"

using openstitch::ObjectId;
using openstitch::RegionId;
using openstitch::desktop::normalize_selection;
using openstitch::desktop::Selection;
using openstitch::desktop::selection_state_is_consistent;

namespace {

ObjectId obj(std::uint64_t v) {
    return ObjectId{v};
}
RegionId reg(std::uint64_t v) {
    return RegionId{v};
}

bool knownUpTo5(RegionId r) {
    return r.value >= 1 && r.value <= 5;
}

} // namespace

class SelectionModelTest : public QObject {
    Q_OBJECT
private slots:
    void duplicateObjectsKeepTheLastOccurrence() {
        Selection s;
        s.objects = {obj(1), obj(2), obj(1), obj(3)};
        const Selection n = normalize_selection(s, knownUpTo5);
        const std::vector<ObjectId> expected{obj(2), obj(1), obj(3)};
        QVERIFY(n.objects == expected);
    }

    void regionOrEmbroiderySelectionKeepsOnlyThePrincipalObject() {
        Selection s;
        s.region = reg(1);
        s.objects = {obj(1), obj(2), obj(3)};
        QVERIFY(normalize_selection(s, knownUpTo5).objects == std::vector<ObjectId>{obj(3)});

        Selection e;
        e.embroidery = obj(9);
        e.objects = {obj(4), obj(5)};
        QVERIFY(normalize_selection(e, knownUpTo5).objects == std::vector<ObjectId>{obj(5)});
    }

    void objectsAloneAreKept() {
        Selection s;
        s.objects = {obj(1), obj(2), obj(3)};
        QCOMPARE(normalize_selection(s, knownUpTo5).objects.size(), std::size_t{3});
    }

    void extraRegionsDropDuplicatesActiveAndUnknown() {
        Selection s;
        s.region = reg(1);
        s.extraRegions = {reg(1), reg(2), reg(2), reg(99), reg(3)};
        const Selection n = normalize_selection(s, knownUpTo5);
        QVERIFY(n.extraRegions == (std::vector<RegionId>{reg(2), reg(3)}));
    }

    void extraRegionsAreEmptyWithoutActiveRegion() {
        Selection s;
        s.extraRegions = {reg(2), reg(3)};
        QVERIFY(normalize_selection(s, knownUpTo5).extraRegions.empty());
    }

    void missingPredicateDropsEveryExtraRegion() {
        Selection s;
        s.region = reg(1);
        s.extraRegions = {reg(2)};
        QVERIFY(normalize_selection(s, {}).extraRegions.empty());
    }

    void normalizationIsIdempotent() {
        Selection s;
        s.region = reg(1);
        s.objects = {obj(1), obj(2), obj(1)};
        s.extraRegions = {reg(2), reg(2)};
        const Selection once = normalize_selection(s, knownUpTo5);
        const Selection twice = normalize_selection(once, knownUpTo5);
        QVERIFY(once.region == twice.region);
        QVERIFY(once.objects == twice.objects);
        QVERIFY(once.extraRegions == twice.extraRegions);
    }

    void consistencyRules() {
        const std::optional<ObjectId> three = obj(3);
        // Cas legacy : multi vide, toujours cohérent.
        QVERIFY(selection_state_is_consistent(std::nullopt, {}, false));
        QVERIFY(selection_state_is_consistent(three, {}, true));
        // Multi valide : >= 2, principal = dernier, ni région ni broderie.
        QVERIFY(selection_state_is_consistent(three, {obj(1), obj(3)}, false));
        // Un seul élément en multi : cas legacy exigé.
        QVERIFY(!selection_state_is_consistent(three, {obj(3)}, false));
        // Principal différent du dernier, ou absent.
        QVERIFY(!selection_state_is_consistent(obj(1), {obj(1), obj(3)}, false));
        QVERIFY(!selection_state_is_consistent(std::nullopt, {obj(1), obj(3)}, false));
        // Région ou broderie sélectionnée avec une multi-sélection.
        QVERIFY(!selection_state_is_consistent(three, {obj(1), obj(3)}, true));
        // Doublon.
        QVERIFY(!selection_state_is_consistent(three, {obj(3), obj(1), obj(3)}, false));
    }
};

QTEST_APPLESS_MAIN(SelectionModelTest)
#include "test_selection_model.moc"
