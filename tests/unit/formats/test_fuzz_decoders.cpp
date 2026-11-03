// SPDX-License-Identifier: Apache-2.0
// HP-QA-003 : fuzzing déterministe des décodeurs (DST, PES, JEF, EXP, SVG, DXF, compagnon de
// couleurs). Graine fixe : prouve qu'une entrée tronquée, altérée ou aléatoire ne provoque ni
// exception, ni crash, ni boucle infinie (le test se termine) ; pas de dépendance à libFuzzer.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "machine_test_helpers.hpp"
#include "openstitch/formats/color_companion.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/formats/dxf.hpp"
#include "openstitch/formats/exp.hpp"
#include "openstitch/formats/jef.hpp"
#include "openstitch/formats/pes.hpp"
#include "openstitch/formats/svg_import.hpp"
#include "openstitch/formats/vp3.hpp"

using namespace openstitch;
using namespace openstitch::formats;
using namespace machine_test;

namespace {

using Bytes = std::vector<std::uint8_t>;
using Decoder = std::function<void(const Bytes&)>;

// Générateur borné par seed : les sorties brutes de mt19937 sont portables (contrairement aux
// distributions de <random>).
struct Rng {
    std::mt19937 gen;
    explicit Rng(std::uint32_t seed) : gen(seed) {}
    std::uint32_t next(std::uint32_t bound) { return bound == 0 ? 0 : gen() % bound; }
};

Bytes bytes_of(const std::string& s) {
    return {s.begin(), s.end()};
}

// Corpus minimal : un fichier valide par format, produit par nos encodeurs.
Bytes valid_dst() {
    return *encode_dst(rich_long_jump());
}
Bytes valid_pes() {
    return *encode_pes(rich_long_jump());
}
Bytes valid_jef() {
    return *encode_jef(rich_long_jump());
}
Bytes valid_vp3() {
    return *encode_vp3(rich_long_jump());
}
Bytes valid_exp() {
    return *encode_exp(rich_long_jump());
}
Bytes valid_svg() {
    return bytes_of(
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="40mm" height="30mm" viewBox="0 0 40 30">)"
        R"(<g><path d="M2 2 L30 2 L30 20 Q15 28 2 20 Z" fill="#f00"/>)"
        R"(<rect x="5" y="5" width="10" height="8" stroke="#000" fill="none"/>)"
        R"(<circle cx="20" cy="15" r="4"/></g></svg>)");
}
Bytes valid_dxf() {
    return bytes_of("0\nSECTION\n2\nENTITIES\n0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n11\n10.0\n21\n0.0\n"
                    "0\nLWPOLYLINE\n90\n3\n70\n1\n10\n0\n20\n0\n10\n10\n20\n0\n10\n5\n20\n5\n"
                    "0\nENDSEC\n0\nEOF\n");
}
Bytes valid_inf() {
    const std::vector<ThreadColor> c{{1, 2, 3}, {4, 5, 6}};
    return *encode_color_companion(c, CompanionKind::Inf);
}
Bytes valid_csv() {
    const std::vector<ThreadColor> c{{1, 2, 3}, {4, 5, 6}};
    return *encode_color_companion(c, CompanionKind::Csv);
}

void run_dst(const Bytes& b) {
    (void)decode_dst(b);
}
void run_pes(const Bytes& b) {
    (void)decode_pes(b);
}
void run_jef(const Bytes& b) {
    (void)decode_jef(b);
}
void run_vp3(const Bytes& b) {
    (void)decode_vp3(b);
}
void run_exp(const Bytes& b) {
    (void)decode_exp(b);
}
void run_svg(const Bytes& b) {
    (void)decode_svg(b);
}
void run_dxf(const Bytes& b) {
    (void)decode_dxf(b);
}
void run_companion(const Bytes& b) {
    (void)decode_color_companion(b);
}

struct Target {
    const char* name;
    Bytes (*seed)();
    Decoder run;
};

std::vector<Target> targets() {
    return {{"dst", valid_dst, run_dst},      {"pes", valid_pes, run_pes},
            {"jef", valid_jef, run_jef},      {"vp3", valid_vp3, run_vp3},
            {"exp", valid_exp, run_exp},      {"svg", valid_svg, run_svg},
            {"dxf", valid_dxf, run_dxf},      {"inf", valid_inf, run_companion},
            {"csv", valid_csv, run_companion}};
}

} // namespace

TEST_CASE("fuzz : le corpus valide est bien decode (garde-fou du corpus)") {
    CHECK(decode_dst(valid_dst()).has_value());
    CHECK(decode_pes(valid_pes()).has_value());
    CHECK(decode_jef(valid_jef()).has_value());
    CHECK(decode_vp3(valid_vp3()).has_value());
    CHECK(decode_exp(valid_exp()).has_value());
    CHECK(decode_svg(valid_svg()).has_value());
    CHECK(decode_dxf(valid_dxf()).has_value());
    CHECK(decode_color_companion(valid_inf()).has_value());
    CHECK(decode_color_companion(valid_csv()).has_value());
}

TEST_CASE("fuzz : decodeurs sur toutes les troncatures du corpus") {
    for (const auto& t : targets()) {
        INFO(t.name);
        const Bytes full = t.seed();
        for (std::size_t len = 0; len < full.size(); ++len) {
            const Bytes cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(len));
            REQUIRE_NOTHROW(t.run(cut));
        }
    }
}

TEST_CASE("fuzz : decodeurs sur octets alteres (graine fixe)") {
    Rng rng(0xC0FFEEu);
    for (const auto& t : targets()) {
        INFO(t.name);
        const Bytes full = t.seed();
        for (int iter = 0; iter < 400; ++iter) {
            Bytes mutated = full;
            const std::uint32_t edits = 1 + rng.next(8);
            for (std::uint32_t e = 0; e < edits; ++e) {
                const std::size_t at = rng.next(static_cast<std::uint32_t>(mutated.size()));
                switch (rng.next(4)) {
                case 0:
                    mutated[at] ^= static_cast<std::uint8_t>(1u << rng.next(8));
                    break;
                case 1:
                    mutated[at] = static_cast<std::uint8_t>(rng.next(256));
                    break;
                case 2:
                    mutated[at] = (rng.next(2) != 0) ? 0xFF : 0x00;
                    break;
                default:
                    mutated.insert(mutated.begin() + static_cast<std::ptrdiff_t>(at),
                                   static_cast<std::uint8_t>(rng.next(256)));
                    break;
                }
            }
            REQUIRE_NOTHROW(t.run(mutated));
        }
    }
}

TEST_CASE("fuzz : decodeurs sur octets purement aleatoires (graine fixe)") {
    Rng rng(0xBADF00Du);
    for (const auto& t : targets()) {
        INFO(t.name);
        for (int iter = 0; iter < 300; ++iter) {
            Bytes noise(rng.next(2048));
            for (auto& b : noise) {
                b = static_cast<std::uint8_t>(rng.next(256));
            }
            REQUIRE_NOTHROW(t.run(noise));
        }
    }
}

TEST_CASE(
    "fuzz : en-tetes extremes (champs de taille a 0xFF) ne provoquent pas d'allocation folle") {
    for (const auto& t : targets()) {
        INFO(t.name);
        Bytes full = t.seed();
        const std::size_t n = std::min<std::size_t>(full.size(), 600);
        for (std::size_t at = 0; at + 4 <= n; at += 2) {
            Bytes mutated = full;
            for (std::size_t k = 0; k < 4; ++k) {
                mutated[at + k] = 0xFF;
            }
            REQUIRE_NOTHROW(t.run(mutated));
        }
    }
}

TEST_CASE("fuzz : la reussite d'un decodage donne un resultat borne et deterministe") {
    Rng rng(42u);
    const Bytes full = valid_dst();
    for (int iter = 0; iter < 200; ++iter) {
        Bytes mutated = full;
        mutated[rng.next(static_cast<std::uint32_t>(mutated.size()))] ^= 0x5A;
        const auto a = decode_dst(mutated);
        const auto b = decode_dst(mutated);
        REQUIRE(a.has_value() == b.has_value());
        if (a) {
            CHECK(a->commands.size() == b->commands.size());
            CHECK(a->commands.size() <= mutated.size() * 2 + 16);
        }
    }
}
TEST_CASE("fuzz : regression svg, caractere parasite apres Z ne boucle plus") {
    // Trouve par le fuzzing : "Z;" n'avancait jamais dans parse_path_data (boucle infinie).
    for (const char* tail : {";", "1", "Z;", "z 5 5", "Z,"}) {
        const std::string svg =
            std::string(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10">)") +
            R"(<path d="M1 1 L9 1 L9 9 Z)" + tail + R"("/></svg>)";
        const auto r = decode_svg(bytes_of(svg));
        REQUIRE(r.has_value());
        CHECK(r->objects.size() == 1);
    }
}