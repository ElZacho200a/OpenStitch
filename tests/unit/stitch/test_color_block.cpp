// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch/color_block.hpp"

using namespace openstitch;
using namespace openstitch::stitch;

TEST_CASE("ColorBlock : egalite et bornes") {
    ColorBlock a;
    a.rgb = {10, 20, 30};
    a.start = 0;
    a.end = 5;

    ColorBlock b = a;
    CHECK(a == b);

    b.end = 6;
    CHECK_FALSE(a == b);
}

TEST_CASE("ColorBlock : thread_key facultatif, vide par defaut") {
    ColorBlock block;
    CHECK_FALSE(block.thread_key.has_value());

    block.thread_key = thread_palette::ThreadKey{"madeira_polyneon", "1919"};
    REQUIRE(block.thread_key.has_value());
    CHECK(block.thread_key->chart_id == "madeira_polyneon");
    CHECK(block.thread_key->code == "1919");
}
