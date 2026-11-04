// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "selection_model.hpp"

using namespace openstitch::desktop_v2;

namespace {
Target obj(std::uint64_t id) {
    return {TargetKind::EmbroideryObject, id};
}
} // namespace

TEST_CASE("selection replace dedupes keeping the last occurrence", "[desktop_v2][selection]") {
    SelectionModel s;
    s.replace({obj(1), obj(2), obj(1), obj(3)});
    REQUIRE(s.targets() == std::vector<Target>{obj(2), obj(1), obj(3)});
    CHECK(s.primary() == obj(3));
}

TEST_CASE("selection ignores invalid ids and no-op changes", "[desktop_v2][selection]") {
    SelectionModel s;
    int notified = 0;
    (void)s.subscribe([&](const SelectionModel&) { ++notified; });
    s.replace({obj(0)});
    CHECK(s.empty());
    CHECK(notified == 0);
    s.replace({obj(5)});
    CHECK(notified == 1);
    const auto rev = s.revision();
    s.replace({obj(5)}); // identique : ni révision ni notification
    CHECK(s.revision() == rev);
    CHECK(notified == 1);
}

TEST_CASE("selection add makes an existing target primary", "[desktop_v2][selection]") {
    SelectionModel s;
    s.replace({obj(1), obj(2)});
    s.add(obj(1));
    CHECK(s.primary() == obj(1));
    CHECK(s.targets().size() == 2);
}

TEST_CASE("selection toggle adds then removes", "[desktop_v2][selection]") {
    SelectionModel s;
    s.toggle(obj(4));
    CHECK(s.contains(obj(4)));
    s.toggle(obj(4));
    CHECK(s.empty());
    CHECK_FALSE(s.primary().has_value());
}

TEST_CASE("selection distinguishes kinds sharing an id", "[desktop_v2][selection]") {
    SelectionModel s;
    s.replace({{TargetKind::Region, 7}, {TargetKind::EmbroideryObject, 7}});
    CHECK(s.targets().size() == 2);
}

TEST_CASE("selection prune drops vanished targets and notifies once", "[desktop_v2][selection]") {
    SelectionModel s;
    s.replace({obj(1), obj(2), obj(3)});
    int notified = 0;
    (void)s.subscribe([&](const SelectionModel&) { ++notified; });
    s.prune([](const Target& t) { return t.id != 2; });
    CHECK(s.targets() == std::vector<Target>{obj(1), obj(3)});
    CHECK(notified == 1);
    s.prune([](const Target&) { return true; });
    CHECK(notified == 1);
}

TEST_CASE("selection reentrant change leaves every listener on the final state",
          "[desktop_v2][selection]") {
    SelectionModel s;
    std::vector<std::size_t> seenByA;
    std::vector<std::size_t> seenByB;
    (void)s.subscribe([&](const SelectionModel& m) { seenByA.push_back(m.targets().size()); });
    (void)s.subscribe([&](const SelectionModel& m) {
        seenByB.push_back(m.targets().size());
        if (m.targets().size() == 1) {
            const_cast<SelectionModel&>(m).add(obj(9)); // un écouteur réagit en modifiant
        }
    });
    s.replace({obj(1)});
    REQUIRE_FALSE(seenByA.empty());
    CHECK(seenByA.back() == 2);
    CHECK(seenByB.back() == 2);
    CHECK(s.targets().size() == 2);
}

TEST_CASE("selection unsubscribe stops notifications", "[desktop_v2][selection]") {
    SelectionModel s;
    int notified = 0;
    const auto token = s.subscribe([&](const SelectionModel&) { ++notified; });
    s.add(obj(1));
    s.unsubscribe(token);
    s.add(obj(2));
    CHECK(notified == 1);
}
