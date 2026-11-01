// SPDX-License-Identifier: Apache-2.0
// Requêtes de sélection pures (inverser, même type/fil, ordre de couture, chevauchement, lasso).
#include <QTest>

#include "selection_queries.hpp"

using namespace openstitch;
using namespace openstitch::desktop;
using document::EmbroideryObject;
using document::Project;
using document::VectorObject;

namespace {

Micrometers um(int mm) {
    return Micrometers{mm * 1000};
}

// Carré de 10 mm dont le coin bas-gauche est en (x, y) mm.
VectorObject square(std::uint64_t id, int x, int y) {
    VectorObject shape;
    shape.id = ObjectId{id};
    shape.name = "s" + std::to_string(id);
    geometry::PathSet set;
    for (const auto& [dx, dy] : {std::pair{0, 0}, {10, 0}, {10, 10}, {0, 10}}) {
        geometry::PathNode node;
        node.pos = {um(x + dx), um(y + dy)};
        set.outer.nodes.push_back(node);
    }
    shape.paths.push_back(set);
    return shape;
}

EmbroideryObject stitched(std::uint64_t id, std::uint64_t source, document::StitchParams params,
                          std::array<std::uint8_t, 3> rgb) {
    EmbroideryObject emb;
    emb.id = ObjectId{id};
    emb.source_vector = ObjectId{source};
    emb.params = std::move(params);
    emb.rgb = rgb;
    return emb;
}

// 1 tatami rouge, 2 satin rouge, 3 tatami bleu (loin), 4 sans broderie, tous alignés.
Project sample() {
    Project p;
    p.vector_objects = {square(1, 0, 0), square(2, 12, 0), square(3, 100, 0), square(4, 200, 0)};
    p.embroidery_objects = {
        stitched(11, 1, document::TatamiParams{}, {200, 0, 0}),
        stitched(12, 2, document::SatinParams{}, {200, 0, 0}),
        stitched(13, 3, document::TatamiParams{}, {0, 0, 200}),
    };
    return p;
}

const std::vector<ObjectId> kNone;

} // namespace

class SelectionQueriesTest : public QObject {
    Q_OBJECT
private slots:
    void invertKeepsOnlyTheOthersAndHonoursThePredicate() {
        const Project p = sample();
        const auto all = invert_selection(p, {ObjectId{1}}, {});
        QCOMPARE(all.size(), std::size_t{3});
        const auto noFour = invert_selection(p, {ObjectId{1}},
                                             [](const VectorObject& s) { return s.id.value != 4; });
        QCOMPARE(noFour.size(), std::size_t{2});
    }

    void sameStitchTypeKeepsTheActiveLast() {
        const Project p = sample();
        const auto ids = select_same_stitch_type(p, ObjectId{1}, {});
        const std::vector<ObjectId> expected{ObjectId{3}, ObjectId{1}};
        QVERIFY(ids == expected);
        QVERIFY(select_same_stitch_type(p, ObjectId{4}, {}).empty());
    }

    void sameThreadPrefersTheCatalogueReference() {
        Project p = sample();
        QCOMPARE(select_same_thread(p, ObjectId{1}, {}).size(), std::size_t{2});
        p.embroidery_objects[0].thread = thread_palette::ThreadKey{"madeira", "1919"};
        const auto ids = select_same_thread(p, ObjectId{1}, {});
        QCOMPARE(ids.size(), std::size_t{1}); // la forme 2 n'a pas cette référence
        QVERIFY(ids.back() == ObjectId{1});
    }

    void sewingStepWrapsAroundBothWays() {
        const Project p = sample();
        QVERIFY(*step_in_sewing_order(p, std::nullopt, 1, {}) == ObjectId{1});
        QVERIFY(*step_in_sewing_order(p, std::nullopt, -1, {}) == ObjectId{3});
        QVERIFY(*step_in_sewing_order(p, ObjectId{3}, 1, {}) == ObjectId{1});
        QVERIFY(*step_in_sewing_order(p, ObjectId{1}, -1, {}) == ObjectId{3});
        QVERIFY(*step_in_sewing_order(p, ObjectId{1}, 1, {}) == ObjectId{2});
    }

    void sewingRangeIsInclusiveAndOrderFree() {
        const Project p = sample();
        const auto ids = select_sewing_range(p, 3, 2, {});
        const std::vector<ObjectId> expected{ObjectId{2}, ObjectId{3}};
        QVERIFY(ids == expected);
        QCOMPARE(sewing_rank_of_shape(p, ObjectId{3}), std::size_t{3});
        QCOMPARE(sewing_rank_of_shape(p, ObjectId{4}), std::size_t{0});
    }

    void touchingUsesTheBoxPlusMargin() {
        const Project p = sample();
        // 1 finit à x=10 mm, 2 commence à 12 mm : écart de 2 mm.
        QVERIFY(select_touching(p, {ObjectId{1}}, Micrometers{0}, {}).empty());
        const auto near = select_touching(p, {ObjectId{1}}, um(2), {});
        QCOMPARE(near.size(), std::size_t{1});
        QVERIFY(near.front() == ObjectId{2});
    }

    void lassoTakesShapesWhoseCentreIsInside() {
        const Project p = sample();
        const std::vector<Vec2um> poly{
            {um(-5), um(-5)}, {um(30), um(-5)}, {um(30), um(20)}, {um(-5), um(20)}};
        QCOMPARE(select_in_lasso(p, poly, {}).size(), std::size_t{2});
        QVERIFY(select_in_lasso(p, {{um(0), um(0)}, {um(1), um(1)}}, {}).empty());
    }

    void stateQueriesFindHiddenLockedAndUnstitched() {
        Project p = sample();
        p.embroidery_objects[1].visible = false;
        p.embroidery_objects[2].locked = true;
        p.vector_objects[0].locked = true;
        QVERIFY(select_by_state(p, StateKind::HiddenEmbroidery) ==
                std::vector<ObjectId>{ObjectId{2}});
        QVERIFY(select_by_state(p, StateKind::OrderLocked) == std::vector<ObjectId>{ObjectId{3}});
        QVERIFY(select_by_state(p, StateKind::EditLocked) == std::vector<ObjectId>{ObjectId{1}});
        QVERIFY(select_by_state(p, StateKind::NoEmbroidery) == std::vector<ObjectId>{ObjectId{4}});
    }
};

QTEST_APPLESS_MAIN(SelectionQueriesTest)
#include "test_selection_queries.moc"
