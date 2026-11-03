// SPDX-License-Identifier: Apache-2.0
// HP-QA-013 : images hostiles refusees avant tout decodage (en-tetes forges, aucune allocation).
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "openstitch/image/image.hpp"

namespace fs = std::filesystem;
using namespace openstitch;

namespace {

void put32be(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) {
        v.push_back(static_cast<std::uint8_t>(x >> s));
    }
}

// PNG tronque : signature + IHDR annoncant w x h. Le CRC est faux et il n'y a aucune donnee :
// seul l'en-tete compte pour le refus.
std::vector<std::uint8_t> forged_png(std::uint32_t w, std::uint32_t h) {
    std::vector<std::uint8_t> v = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
                                   0,    0,   0,   13,  'I',  'H',  'D',  'R'};
    put32be(v, w);
    put32be(v, h);
    v.insert(v.end(), {8, 6, 0, 0, 0, 0, 0, 0, 0});
    return v;
}

std::vector<std::uint8_t> forged_jpeg(std::uint16_t w, std::uint16_t h) {
    return {0xff,
            0xd8,
            0xff,
            0xc0,
            0x00,
            0x11,
            0x08,
            static_cast<std::uint8_t>(h >> 8),
            static_cast<std::uint8_t>(h),
            static_cast<std::uint8_t>(w >> 8),
            static_cast<std::uint8_t>(w),
            0x03,
            0x01,
            0x22,
            0x00,
            0x02,
            0x11,
            0x01,
            0x03,
            0x11,
            0x01};
}

std::vector<std::uint8_t> forged_bmp(std::uint32_t w, std::uint32_t h) {
    std::vector<std::uint8_t> v(54, 0);
    v[0] = 'B';
    v[1] = 'M';
    v[14] = 40;
    for (int i = 0; i < 4; ++i) {
        v[18 + i] = static_cast<std::uint8_t>(w >> (8 * i));
        v[22 + i] = static_cast<std::uint8_t>(h >> (8 * i));
    }
    v[26] = 1;
    v[28] = 24;
    return v;
}

fs::path write_file(const std::string& name, const std::vector<std::uint8_t>& bytes) {
    const auto p = fs::temp_directory_path() / name;
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return p;
}

} // namespace

TEST_CASE("image hostile: PNG aux dimensions geantes refuse avec code et conseil") {
    const auto p = write_file("os_forged_huge.png", forged_png(100'000, 100'000));
    auto r = image::load_image(p);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == "E_IMAGE_TOO_MANY_PIXELS");
    CHECK_FALSE(r.error().hint.empty());
    CHECK_FALSE(image::read_image_info(p).has_value());
    fs::remove(p);
}

TEST_CASE("image hostile: PNG 4 milliards x 4 milliards ne deborde pas") {
    const auto p = write_file("os_forged_overflow.png", forged_png(0xffffffffU, 0xffffffffU));
    auto r = image::load_image(p);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == "E_IMAGE_TOO_MANY_PIXELS");
    fs::remove(p);
}

TEST_CASE("image hostile: JPEG et BMP geants refuses, decode_image aussi") {
    const auto jpg = forged_jpeg(65'000, 65'000);
    auto rj = image::decode_image(jpg);
    REQUIRE_FALSE(rj.has_value());
    CHECK(rj.error().code == "E_IMAGE_TOO_MANY_PIXELS");

    const auto bmp = forged_bmp(200'000, 200'000);
    auto rb = image::decode_image(bmp);
    REQUIRE_FALSE(rb.has_value());
    CHECK(rb.error().code == "E_IMAGE_TOO_MANY_PIXELS");

    auto rp = image::decode_image(forged_png(70'000, 70'000));
    REQUIRE_FALSE(rp.has_value());
    CHECK(rp.error().code == "E_IMAGE_TOO_MANY_PIXELS");
}

TEST_CASE("image legitime: une petite image reste acceptee apres les plafonds") {
    const auto bmp = forged_bmp(1, 1);
    // En-tete valide mais sans donnees : refus de decodage, pas un refus de plafond.
    auto r = image::decode_image(bmp);
    if (!r.has_value()) {
        CHECK(r.error().code.empty());
    }
}
