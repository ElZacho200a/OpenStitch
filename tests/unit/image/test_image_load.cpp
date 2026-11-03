// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

#include "openstitch/image/image.hpp"

namespace fs = std::filesystem;
using openstitch::image::load_image;
using openstitch::image::read_image_info;

namespace {

// Écrit un PNG 4x2 uni (bleu pur, en BGR(A) côté OpenCV) dans le dossier temporaire.
fs::path write_test_png(bool with_alpha) {
    const cv::Scalar blue = with_alpha ? cv::Scalar(255, 0, 0, 255) : cv::Scalar(255, 0, 0);
    const cv::Mat m(2, 4, with_alpha ? CV_8UC4 : CV_8UC3, blue);
    const fs::path p = fs::temp_directory_path() /
                       (with_alpha ? "openstitch_test_rgba.png" : "openstitch_test_rgb.png");
    REQUIRE(cv::imwrite(p.string(), m));
    return p;
}

} // namespace

TEST_CASE("metadonnees d'un PNG RGB") {
    const auto p = write_test_png(false);
    const auto info = read_image_info(p);
    REQUIRE(info.has_value());
    CHECK(info->width_px == 4);
    CHECK(info->height_px == 2);
    CHECK(info->channels == 3);
    CHECK_FALSE(info->has_alpha);
    CHECK(info->format == "PNG");
}

TEST_CASE("chargement : normalisation BGR(A) -> RGBA") {
    const auto p = write_test_png(true);
    const auto img = load_image(p);
    REQUIRE(img.has_value());
    CHECK(img->width == 4);
    CHECK(img->height == 2);
    CHECK(img->source_had_alpha);
    REQUIRE(img->rgba.size() == 4u * 2u * 4u);
    // Bleu opaque : R=0, G=0, B=255, A=255
    CHECK(img->rgba[0] == 0);
    CHECK(img->rgba[1] == 0);
    CHECK(img->rgba[2] == 255);
    CHECK(img->rgba[3] == 255);
}

TEST_CASE("fichier inexistant -> erreur utilisateur propre") {
    const auto r = load_image(fs::path("n_existe_pas_openstitch_12345.png"));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().category == openstitch::ErrorCategory::UserInput);
    CHECK_FALSE(r.error().message.empty());
}

TEST_CASE("fichier non-image -> erreur InvalidFile, pas de crash") {
    const fs::path p = fs::temp_directory_path() / "openstitch_test_pas_une_image.png";
    {
        std::ofstream f(p, std::ios::binary);
        f << "ceci n'est pas un PNG";
    }
    const auto r = load_image(p);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().category == openstitch::ErrorCategory::InvalidFile);
}

// HP-AUTO-014 : orientation EXIF 6 (rotation 90 horaire) appliquee au chargement d'un JPEG.
// Les octets EXIF sont forges par le test : aucun binaire dans le depot.
TEST_CASE("JPEG : l'orientation EXIF 6 est appliquee au chargement") {
    cv::Mat m(8, 16, CV_8UC3, cv::Scalar(0, 0, 255));     // rouge (BGR)
    m(cv::Rect(8, 0, 8, 8)).setTo(cv::Scalar(255, 0, 0)); // moitie droite bleue
    std::vector<unsigned char> jpg;
    REQUIRE(cv::imencode(".jpg", m, jpg, {cv::IMWRITE_JPEG_QUALITY, 100}));
    REQUIRE(jpg.size() > 4);

    const std::vector<unsigned char> app1 = {0xFF, 0xE1, 0x00, 0x22, 'E',  'x',  'i',  'f',  0x00,
                                             0x00, 'M',  'M',  0x00, 0x2A, 0x00, 0x00, 0x00, 0x08,
                                             0x00, 0x01, 0x01, 0x12, 0x00, 0x03, 0x00, 0x00, 0x00,
                                             0x01, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::vector<unsigned char> forged(jpg.begin(), jpg.begin() + 2);
    forged.insert(forged.end(), app1.begin(), app1.end());
    forged.insert(forged.end(), jpg.begin() + 2, jpg.end());

    const fs::path p = fs::temp_directory_path() / "openstitch_test_exif6.jpg";
    {
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(forged.data()),
                static_cast<std::streamsize>(forged.size()));
    }
    const auto img = load_image(p);
    REQUIRE(img.has_value());
    CHECK(img->width == 8);
    CHECK(img->height == 16);
    const auto px = [&](int x, int y) { return img->rgba.data() + (y * img->width + x) * 4; };
    CHECK(px(3, 3)[0] > 150); // haut : rouge (ancienne moitie gauche)
    CHECK(px(3, 3)[2] < 100);
    CHECK(px(3, 12)[2] > 150); // bas : bleu
    CHECK(px(3, 12)[0] < 100);

    const auto info = read_image_info(p);
    REQUIRE(info.has_value());
    CHECK(info->width_px == 8);
    CHECK(info->height_px == 16);
    fs::remove(p);
}
