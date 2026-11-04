// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "command_catalog.hpp"
#include "hit_index.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
std::vector<CommandEntry> sample() {
    return {
        {"file.open", "Ouvrir une image ou un projet…", "Fichier", "Ctrl+O", true},
        {"tool.draw.ellipse", "Ellipse", "Outil", "E", false},
        {"tool.select", "Sélection", "Outil", "V", true},
        {"edit.undo", "Annuler", "Édition", "Ctrl+Z", true},
        {"workspace.digitize", "Espace : Numérisation", "Espace", "Ctrl+2", true},
    };
}
std::vector<std::string> ids(const std::vector<const CommandEntry*>& v) {
    std::vector<std::string> out;
    for (const auto* e : v) {
        out.push_back(e->id);
    }
    return out;
}
} // namespace

TEST_CASE("search normalisation drops case and French accents", "[desktop_v2][palette]") {
    CHECK(normalizeForSearch("Éléphant Ça Où") == "elephant ca ou");
    CHECK(normalizeForSearch("NUMÉRISATION") == "numerisation");
    CHECK(normalizeForSearch("plain ascii 123") == "plain ascii 123");
}

TEST_CASE("palette filter matches accent-insensitively on label group and id",
          "[desktop_v2][palette]") {
    const auto entries = sample();
    CHECK(ids(filterCommands(entries, "numerisation")) ==
          std::vector<std::string>{"workspace.digitize"});
    CHECK(ids(filterCommands(entries, "SELECTION")) == std::vector<std::string>{"tool.select"});
    CHECK(ids(filterCommands(entries, "fichier")) ==
          std::vector<std::string>{"file.open"}); // groupe
    CHECK(ids(filterCommands(entries, "draw.ell")) ==
          std::vector<std::string>{"tool.draw.ellipse"}); // identifiant
    CHECK(filterCommands(entries, "zzz").empty());
}

TEST_CASE("palette filter requires every word and ranks enabled label-prefix matches first",
          "[desktop_v2][palette]") {
    const auto entries = sample();
    CHECK(ids(filterCommands(entries, "ouvrir image")) == std::vector<std::string>{"file.open"});
    CHECK(filterCommands(entries, "ouvrir ellipse").empty());
    // « e » : activées avant désactivées ; l'ellipse (désactivée) passe après les autres.
    const auto r = ids(filterCommands(entries, "e"));
    REQUIRE(r.size() >= 2);
    CHECK(r.back() == "tool.draw.ellipse");
}

TEST_CASE("empty query lists everything, enabled first, in stable order", "[desktop_v2][palette]") {
    const auto entries = sample();
    const auto r = ids(filterCommands(entries, ""));
    REQUIRE(r.size() == 5);
    CHECK(r.front() == "file.open");
    CHECK(r.back() == "tool.draw.ellipse");
    CHECK(ids(filterCommands(entries, "")) == r); // déterministe
}

TEST_CASE("inside selects only objects fully within the rectangle", "[desktop_v2][hit]") {
    stitch_render::ThreadSegment a;
    a.ax = 1;
    a.ay = 1;
    a.bx = 3;
    a.by = 3;
    a.rgb = encode_id(1);
    stitch_render::ThreadSegment b;
    b.ax = 8;
    b.ay = 8;
    b.bx = 14;
    b.by = 14;
    b.rgb = encode_id(2);
    const auto index = HitIndex::build({a, b});
    CHECK(index.inside({0, 0, 10, 10}) == std::vector<std::uint64_t>{1}); // 2 dépasse : exclu
    CHECK(index.inside({0, 0, 20, 20}) == std::vector<std::uint64_t>{1, 2});
    CHECK(index.inside({5, 5, 6, 6}).empty());
}
