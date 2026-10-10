// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

#include "openstitch/lettering/lettering.hpp"

namespace lettering_test {

using namespace openstitch;

// Police embarquée du dépôt (resources/fonts/). Chargée une fois par exécutable.
inline const lettering::Font& font(const std::string& file) {
    static std::map<std::string, lettering::Font> cache;
    auto it = cache.find(file);
    if (it == cache.end()) {
        auto loaded = lettering::Font::from_file(std::string(OPENSTITCH_FONT_DIR) + "/" + file);
        REQUIRE(loaded.has_value());
        it = cache.emplace(file, std::move(*loaded)).first;
    }
    return it->second;
}

inline const lettering::Font& vera() {
    return font("Vera.ttf");
}
inline const lettering::Font& vera_bold() {
    return font("VeraBd.ttf");
}

inline document::TextObject make_text(const std::string& text, std::int32_t cap_um = 12'000) {
    document::TextObject t;
    t.id = ObjectId{1};
    t.text = text;
    t.font.builtin = "vera-sans";
    t.cap_height = Micrometers{cap_um};
    return t;
}

} // namespace lettering_test
