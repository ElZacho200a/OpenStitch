// SPDX-License-Identifier: Apache-2.0
// AI-02 : registre de formats -- une seule liste, consultable par id ou par
// extension. S2a n'inscrit que "dst" ; PES/JEF/EXP (S2b/S2c) ajoutent leur
// propre ligne plus tard sans toucher à ce test.
#include <catch2/catch_test_macros.hpp>

#include "openstitch/formats/format_registry.hpp"

using namespace openstitch;
using namespace openstitch::formats;

TEST_CASE("registered_formats : contient dst, lecture et ecriture") {
    const auto formats = registered_formats();
    const auto* dst = find_format("dst");
    REQUIRE(dst != nullptr);
    CHECK(dst->can_read);
    CHECK(dst->can_write);
    CHECK(dst->encode != nullptr);
    CHECK(dst->decode != nullptr);
    CHECK(dst->extensions == std::vector<std::string>{"dst"});

    bool found = false;
    for (const auto& f : formats) {
        found |= (f.id == "dst");
    }
    CHECK(found);
}

TEST_CASE("find_format : identifiant inconnu -> nullptr") {
    CHECK(find_format("xyz") == nullptr);
}

TEST_CASE("find_format_for_extension : insensible a la casse, sans le point") {
    const auto* byLower = find_format_for_extension("dst");
    const auto* byUpper = find_format_for_extension("DST");
    REQUIRE(byLower != nullptr);
    REQUIRE(byUpper != nullptr);
    CHECK(byLower->id == "dst");
    CHECK(byUpper->id == "dst");
    CHECK(find_format_for_extension("xyz") == nullptr);
}

TEST_CASE("registre : encode(dst)/decode(dst) fonctionnent via les pointeurs du registre") {
    const auto* dst = find_format("dst");
    REQUIRE(dst != nullptr);

    stitch::StitchSequence seq;
    seq.commands = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, stitch::CommandType::Stitch, ObjectId{}},
        {Vec2um{Micrometers{1'000}, Micrometers{0}}, stitch::CommandType::Stitch, ObjectId{}},
        {Vec2um{Micrometers{1'000}, Micrometers{0}}, stitch::CommandType::End, ObjectId{}},
    };
    const auto bytes = dst->encode(seq);
    REQUIRE(bytes.has_value());
    const auto decoded = dst->decode(*bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->commands.size() == seq.commands.size());
    for (std::size_t i = 0; i < seq.commands.size(); ++i) {
        CHECK(decoded->commands[i].type == seq.commands[i].type);
        CHECK(decoded->commands[i].pos == seq.commands[i].pos);
    }
}
