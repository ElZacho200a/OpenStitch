// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <filesystem>
#include <fstream>

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

}  // namespace

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
