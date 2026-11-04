// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "document_session.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "satin_column.hpp"
#include "workspace_state.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
std::filesystem::path freshDir(const char* name) {
    auto dir = std::filesystem::temp_directory_path() / "openstitch_v2_tests" / name;
    std::filesystem::remove_all(dir);
    return dir;
}
} // namespace

TEST_CASE("recent files are deduplicated, ordered and capped", "[desktop_v2][workspace]") {
    std::vector<std::string> list;
    list = pushRecent(list, "a.osp");
    list = pushRecent(list, "b.osp");
    list = pushRecent(list, "a.osp");
    REQUIRE(list == std::vector<std::string>{"a.osp", "b.osp"});
    CHECK(pushRecent(list, "").size() == 2);
    for (int i = 0; i < 20; ++i) {
        list = pushRecent(list, "f" + std::to_string(i) + ".osp");
    }
    CHECK(list.size() == kMaxRecentFiles);
    CHECK(list.front() == "f19.osp");
}

TEST_CASE("missing recent files are pruned", "[desktop_v2][workspace]") {
    const auto dir = freshDir("recent");
    std::filesystem::create_directories(dir);
    const auto real = dir / "real.osp";
    std::ofstream(real) << "x";
    const auto u8 = real.u8string();
    const std::string realText(reinterpret_cast<const char*>(u8.data()), u8.size());
    const auto kept = pruneMissingRecent({realText, "does-not-exist.osp"});
    CHECK(kept.size() == 1);
}

TEST_CASE("autosave writes, reloads and clears a project", "[desktop_v2][workspace]") {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    REQUIRE(createSatinColumn(s, {{{-10, 0}, {-10, 4}}, {{10, 0}, {10, 4}}}).ok);

    AutosaveStore store(freshDir("autosave"));
    CHECK_FALSE(store.exists());
    REQUIRE(store.write(s.project()).has_value());
    CHECK(store.exists());
    CHECK_FALSE(std::filesystem::exists(std::filesystem::path(store.file()) += ".tmp"));
    const auto loaded = store.load();
    REQUIRE(loaded.has_value());
    CHECK(loaded->embroidery_objects.size() == 1);
    // une seconde écriture remplace la première
    REQUIRE(store.write(s.project()).has_value());
    store.clear();
    CHECK_FALSE(store.exists());
    store.clear(); // sans effet
    CHECK_FALSE(store.load().has_value());
}

TEST_CASE("the recent list survives a save and a load", "[desktop_v2][workspace]") {
    const auto file = freshDir("recent_list") / "recent.txt";
    CHECK(loadRecentList(file).empty());
    const std::vector<std::string> list{"c.osp", "b.osp", "a.osp"};
    REQUIRE(saveRecentList(file, list));
    CHECK(loadRecentList(file) == list);
}
