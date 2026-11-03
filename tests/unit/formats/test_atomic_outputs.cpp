// SPDX-License-Identifier: Apache-2.0
// HP-QA-021 : les sorties de libs/formats passent par la primitive d'ecriture atomique.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "openstitch/formats/dst.hpp"
#include "openstitch/formats/dxf.hpp"
#include "openstitch/formats/svg.hpp"

using namespace openstitch;
using namespace openstitch::formats;
using stitch::CommandType;
namespace fs = std::filesystem;

namespace {

stitch::StitchSequence sample_sequence() {
    stitch::StitchSequence seq;
    seq.commands = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, CommandType::Stitch, ObjectId{}},
        {Vec2um{Micrometers{5'000}, Micrometers{0}}, CommandType::Stitch, ObjectId{}},
        {Vec2um{Micrometers{5'000}, Micrometers{5'000}}, CommandType::Stitch, ObjectId{}},
        {Vec2um{Micrometers{5'000}, Micrometers{5'000}}, CommandType::End, ObjectId{}},
    };
    return seq;
}

fs::path fresh_dir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("os_fmt_atomic_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int count_tmp(const fs::path& dir) {
    int n = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        n += e.path().extension() == ".tmp" ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("sorties formats: dst et svg remplacent la cible sans temporaire residuel") {
    const auto dir = fresh_dir("ok");
    const auto seq = sample_sequence();
    const auto dst = dir / "a.dst";
    const auto svg = dir / "a.svg";
    { std::ofstream(dst, std::ios::binary) << "ancien"; }
    REQUIRE(write_dst_file(dst, seq, {}).has_value());
    REQUIRE(write_svg_file(svg, seq).has_value());
    CHECK(slurp(dst).size() >= 512);
    CHECK(slurp(svg).find("<svg") == 0);
    CHECK(count_tmp(dir) == 0);
}

TEST_CASE("sorties formats: echec force laisse l'ancien fichier intact") {
    const auto dir = fresh_dir("fail");
    const auto seq = sample_sequence();
    const auto dst = dir / "keep.dst";
    REQUIRE(write_dst_file(dst, seq, {}).has_value());
    const auto before = slurp(dst);

    // Dossier de destination absent : l'ecriture echoue avec un code et un conseil.
    auto r = write_dst_file(dir / "absent" / "x.dst", seq, {});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == "E_WRITE_FAILED");

    // Cible occupee par un dossier : echec, rien n'est casse.
    fs::create_directories(dir / "dossier.svg");
    CHECK_FALSE(write_svg_file(dir / "dossier.svg", seq).has_value());
    CHECK(slurp(dst) == before);
    CHECK(count_tmp(dir) == 0);
}

TEST_CASE("sorties formats: deux exports simultanes vers la meme cible ne se corrompent pas") {
    const auto dir = fresh_dir("concurrent");
    const auto target = dir / "same.dst";
    auto seqA = sample_sequence();
    auto seqB = sample_sequence();
    seqB.commands[1].pos = Vec2um{Micrometers{9'000}, Micrometers{1'000}};
    auto bytesA = encode_dst(seqA, {});
    auto bytesB = encode_dst(seqB, {});
    REQUIRE(bytesA.has_value());
    REQUIRE(bytesB.has_value());
    REQUIRE(*bytesA != *bytesB);

    std::vector<std::thread> threads;
    for (const auto* seq : {&seqA, &seqB}) {
        threads.emplace_back([&, seq] {
            for (int i = 0; i < 20; ++i) {
                (void)write_dst_file(target, *seq, {});
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    const auto final = slurp(target);
    const std::string a(bytesA->begin(), bytesA->end());
    const std::string b(bytesB->begin(), bytesB->end());
    CHECK((final == a || final == b));
    CHECK(count_tmp(dir) == 0);
}
