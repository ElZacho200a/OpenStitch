// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "openstitch/core/ids.hpp"

using namespace openstitch;

static_assert(!std::is_same_v<ObjectId, RegionId>, "les identifiants doivent être des types distincts");

TEST_CASE("id par défaut invalide, générateur monotone") {
    CHECK_FALSE(ObjectId{}.valid());

    IdGenerator<ObjectId> gen;
    const auto a = gen.next();
    const auto b = gen.next();
    CHECK(a.valid());
    CHECK(a != b);
    CHECK(a.value < b.value);
}

TEST_CASE("reprise après désérialisation") {
    IdGenerator<ObjectId> gen;
    gen.reset(41);
    CHECK(gen.next().value == 42);
}
