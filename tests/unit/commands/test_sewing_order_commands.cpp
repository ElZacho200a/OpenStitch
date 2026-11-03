// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "openstitch/commands/calque_commands.hpp"
#include "openstitch/commands/sewing_order_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"

using namespace openstitch;

namespace {

struct Fixture {
    document::Project project;
    ObjectId a, b, c;
    ObjectId calqueTop;

    Fixture() {
        const auto add = [&](ObjectId& out) {
            document::VectorObject v;
            v.id = project.object_ids.next();
            project.vector_objects.push_back(v);
            document::EmbroideryObject e;
            e.id = project.object_ids.next();
            e.source_vector = v.id;
            project.embroidery_objects.push_back(e);
            out = e.id;
        };
        add(a);
        add(b);
        add(c);
    }

    [[nodiscard]] std::vector<ObjectId> sewn() const {
        std::vector<ObjectId> out;
        for (const std::size_t i : project.sewingOrder()) {
            out.push_back(project.embroidery_objects[i].id);
        }
        return out;
    }
};

} // namespace

TEST_CASE("ordre de couture : deplacement par glisser-deposer, un pas d'annulation") {
    Fixture f;
    commands::UndoStack stack;
    stack.execute(std::make_unique<commands::MoveInSewingOrderCommand>(f.c, 0), f.project);
    CHECK(f.sewn() == std::vector<ObjectId>{f.c, f.a, f.b});
    stack.execute(std::make_unique<commands::MoveInSewingOrderCommand>(f.c, 1), f.project);
    CHECK(f.sewn() == std::vector<ObjectId>{f.a, f.c, f.b});

    stack.undo(f.project);
    CHECK(f.sewn() == std::vector<ObjectId>{f.c, f.a, f.b});
    stack.undo(f.project);
    CHECK(f.sewn() == std::vector<ObjectId>{f.a, f.b, f.c});
    stack.redo(f.project);
    CHECK(f.sewn() == std::vector<ObjectId>{f.c, f.a, f.b});
}

TEST_CASE("ordre de couture : positions invalides sans effet") {
    Fixture f;
    CHECK_FALSE(commands::moved_sewing_order(f.project, f.a, 0).has_value()); // meme place
    CHECK_FALSE(commands::moved_sewing_order(f.project, f.a, 9).has_value()); // hors bornes
    CHECK_FALSE(commands::moved_sewing_order(f.project, ObjectId{999}, 0).has_value());
    CHECK(commands::moved_sewing_order(f.project, f.a, 2).has_value());
}

TEST_CASE("ordre de couture : ne franchit pas un calque") {
    Fixture f;
    commands::UndoStack stack;
    const ObjectId c1 = f.project.object_ids.next();
    const ObjectId c2 = f.project.object_ids.next();
    stack.execute(std::make_unique<commands::AddCalqueCommand>(c2, "Haut", c1), f.project);
    stack.execute(std::make_unique<commands::SetShapesCalqueCommand>(
                      std::vector<ObjectId>{f.project.embroidery_objects[2].source_vector}, c2),
                  f.project);
    REQUIRE(f.sewn() == std::vector<ObjectId>{f.a, f.b, f.c});
    // c est dans le calque du dessus : il ne peut pas passer avant a ou b.
    CHECK_FALSE(commands::moved_sewing_order(f.project, f.c, 0).has_value());
    // a et b s'echangent librement dans leur calque.
    CHECK(commands::moved_sewing_order(f.project, f.a, 1).has_value());
    // a ne peut pas passer apres c.
    CHECK_FALSE(commands::moved_sewing_order(f.project, f.a, 2).has_value());
}
