// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "document_session.hpp"
#include "image_prep.hpp"
#include "openstitch/project_ops/factory.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {

DocumentSession sessionWithImage(bool withImage = true) {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    if (withImage) {
        auto& img = created->original;
        img.width = 40;
        img.height = 30;
        img.rgba.resize(40u * 30u * 4u);
        for (int y = 0; y < 30; ++y) {
            for (int x = 0; x < 40; ++x) {
                auto* p = &img.rgba[(static_cast<std::size_t>(y) * 40 + x) * 4];
                p[0] = static_cast<std::uint8_t>(x * 6);
                p[1] = static_cast<std::uint8_t>(y * 8);
                p[2] = 90;
                p[3] = 255;
            }
        }
    }
    s.replaceProject(std::move(*created), true);
    return s;
}

PrepRequest of(PrepKind kind, double a = 0, double b = 0) {
    PrepRequest r;
    r.kind = kind;
    r.a = a;
    r.b = b;
    return r;
}

} // namespace

TEST_CASE("image preparation steps stack one undo step each", "[desktop_v2][imageprep]") {
    auto s = sessionWithImage();
    CHECK(workingImageSize(s.project()).width == 40);
    const auto depth = s.undoNames().size();
    REQUIRE(applyImagePrep(s, of(PrepKind::BrightnessContrast, 20, -10)).ok);
    REQUIRE(applyImagePrep(s, of(PrepKind::FlipHorizontal)).ok);
    REQUIRE(applyImagePrep(s, of(PrepKind::Grayscale)).ok);
    REQUIRE(applyImagePrep(s, of(PrepKind::Sharpen, 1.0)).ok);
    REQUIRE(applyImagePrep(s, of(PrepKind::Clahe, 2.0)).ok);
    CHECK(s.undoNames().size() == depth + 5);
    CHECK(imagePrepNames(s.project()).size() == 5);
    REQUIRE(s.undo());
    CHECK(imagePrepNames(s.project()).size() == 4);
}

TEST_CASE("rotations swap the working size and crops use the current size",
          "[desktop_v2][imageprep]") {
    auto s = sessionWithImage();
    REQUIRE(applyImagePrep(s, of(PrepKind::RotateRight)).ok);
    const auto size = workingImageSize(s.project());
    CHECK(size.width == 30);
    CHECK(size.height == 40);
    PrepRequest crop = of(PrepKind::Crop);
    crop.x = 2;
    crop.y = 3;
    crop.w = 20;
    crop.h = 30;
    REQUIRE(applyImagePrep(s, crop).ok);
    CHECK(workingImageSize(s.project()).width == 20);
    CHECK(workingImageSize(s.project()).height == 30);
    REQUIRE(applyImagePrep(s, of(PrepKind::RotateLeft)).ok);
    REQUIRE(applyImagePrep(s, of(PrepKind::Rotate180)).ok);
    CHECK(workingImageSize(s.project()).width == 30);
}

TEST_CASE("bad preparation requests are refused without stacking", "[desktop_v2][imageprep]") {
    auto s = sessionWithImage();
    const auto depth = s.undoNames().size();
    CHECK_FALSE(applyImagePrep(s, of(PrepKind::BrightnessContrast, 0, 0)).ok);
    CHECK_FALSE(applyImagePrep(s, of(PrepKind::BrightnessContrast, 500, 0)).ok);
    CHECK_FALSE(applyImagePrep(s, of(PrepKind::Sharpen, 9)).ok);
    CHECK_FALSE(applyImagePrep(s, of(PrepKind::Clahe, 0.2)).ok);
    PrepRequest crop = of(PrepKind::Crop);
    crop.w = 100;
    crop.h = 100;
    CHECK_FALSE(applyImagePrep(s, crop).ok);
    crop.x = 0;
    crop.y = 0;
    crop.w = 40;
    crop.h = 30;
    CHECK_FALSE(applyImagePrep(s, crop).ok); // toute l'image
    crop.w = 4;
    CHECK_FALSE(applyImagePrep(s, crop).ok); // trop petit
    CHECK(s.undoNames().size() == depth);
    auto empty = sessionWithImage(false);
    CHECK_FALSE(applyImagePrep(empty, of(PrepKind::Grayscale)).ok);
    CHECK(workingImageSize(empty.project()).width == 0);
}
