// SPDX-License-Identifier: Apache-2.0
// HP-QA-013 : archives .osp hostiles (bombe de decompression, doublons, trop d'entrees) et
// HP-QA-021 : ecriture atomique du .osp.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "archive.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;
using project_io::detail::Blob;

namespace {

std::vector<std::uint8_t> slurp_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
}

void spit_bytes(const fs::path& p, const std::vector<std::uint8_t>& v) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size()));
}

void wr32(std::vector<std::uint8_t>& v, std::size_t i, std::uint32_t x) {
    for (int k = 0; k < 4; ++k) {
        v[i + k] = static_cast<std::uint8_t>(x >> (8 * k));
    }
}

// Applique `fn(offset)` a chaque en-tete de repertoire central (signature PK\1\2) du ZIP.
template <typename Fn> void for_each_central_header(const std::vector<std::uint8_t>& v, Fn fn) {
    for (std::size_t i = 0; i + 46 <= v.size(); ++i) {
        if (v[i] == 'P' && v[i + 1] == 'K' && v[i + 2] == 1 && v[i + 3] == 2) {
            fn(i);
        }
    }
}

fs::path make_zip(const std::string& name, const std::map<std::string, Blob>& entries) {
    const auto path = fs::temp_directory_path() / name;
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());
    return path;
}

Blob text_blob(const std::string& s) {
    return Blob(s.begin(), s.end());
}

int count_tmp(const fs::path& dir) {
    int n = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        n += e.path().extension() == ".tmp" ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("osp hostile: entree annoncant 3 Gio refusee sans allocation") {
    const auto path = make_zip("os_bomb_entry.osp", {{"project.json", text_blob("{}")}});
    auto bytes = slurp_bytes(path);
    // Taille decompressee annoncee (repertoire central, +24) : 3 Gio pour un fichier de ~150
    // octets.
    for_each_central_header(bytes, [&](std::size_t i) { wr32(bytes, i + 24, 0xC0000000U); });
    spit_bytes(path, bytes);

    auto r = project_io::load_project(path);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == "E_ZIP_ENTRY_TOO_LARGE");
    CHECK_FALSE(r.error().hint.empty());
    fs::remove(path);
}

TEST_CASE("osp hostile: total decompresse superieur au plafond refuse") {
    const auto path = make_zip("os_bomb_total.osp", {{"a", text_blob("x")},
                                                     {"b", text_blob("x")},
                                                     {"c", text_blob("x")},
                                                     {"project.json", text_blob("{}")}});
    auto bytes = slurp_bytes(path);
    // 4 entrees de 700 Mio annoncees : chacune sous le plafond d'entree, 2,8 Gio au total.
    for_each_central_header(bytes,
                            [&](std::size_t i) { wr32(bytes, i + 24, 700U * 1024U * 1024U); });
    spit_bytes(path, bytes);

    auto r = project_io::detail::read_zip(path);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == "E_ZIP_TOTAL_TOO_LARGE");
    fs::remove(path);
}

TEST_CASE("osp hostile: trop d'entrees refuse") {
    std::map<std::string, Blob> entries;
    for (int i = 0; i < 1025; ++i) {
        entries.emplace("e" + std::to_string(i), Blob{1});
    }
    const auto path = make_zip("os_many_entries.osp", entries);
    auto r = project_io::load_project(path);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == "E_ZIP_TOO_MANY_ENTRIES");
    fs::remove(path);
}

TEST_CASE("osp hostile: deux project.json refuses avec un message clair") {
    // Deux entrees de noms differents de meme longueur, puis le second nom est reecrit.
    const auto path =
        make_zip("os_dup_entry.osp",
                 {{"project.json", text_blob(R"({"schemaVersion":5,"document":{}})")},
                  {"project.jsoX", text_blob(R"({"schemaVersion":5,"document":{"x":1}})")}});
    auto bytes = slurp_bytes(path);
    const std::string from = "project.jsoX";
    const std::string to = "project.json";
    for (auto it = bytes.begin();
         (it = std::search(it, bytes.end(), from.begin(), from.end())) != bytes.end();) {
        std::copy(to.begin(), to.end(), it);
    }
    spit_bytes(path, bytes);

    auto r = project_io::load_project(path);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == "E_ZIP_DUPLICATE_ENTRY");
    CHECK(r.error().message.find("project.json") != std::string::npos);
    fs::remove(path);
}

TEST_CASE("save_project: echec force laisse l'ancien projet intact, sans temporaire") {
    const auto dir = fs::temp_directory_path() / "os_save_atomic";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto path = dir / "p.osp";

    document::Project project;
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto before = slurp_bytes(path);

    // Cible occupee par un dossier.
    fs::create_directories(dir / "dossier.osp");
    CHECK_FALSE(project_io::save_project(dir / "dossier.osp", project).has_value());

    CHECK(slurp_bytes(path) == before);
    CHECK(count_tmp(dir) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("save_project: deux enregistrements simultanes vers la meme cible restent lisibles") {
    const auto dir = fs::temp_directory_path() / "os_save_concurrent";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto path = dir / "p.osp";

    document::Project project;
    std::vector<std::thread> threads;
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 10; ++i) {
                (void)project_io::save_project(path, project);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    CHECK(project_io::load_project(path).has_value());
    CHECK(count_tmp(dir) == 0);
    fs::remove_all(dir, ec);
}
