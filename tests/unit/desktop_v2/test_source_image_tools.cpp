// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "digitize_pipeline.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "source_image_tools.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

namespace {
struct Doc {
    DocumentSession session;
    image::Image processed;
};
// Tentabrode numérisé, puis vidé de ses objets : il reste l'image source, prête à être cliquée.
Doc imageOnlyDoc() {
    DigitizeRequest request;
    request.image = fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = 80.0;
    request.settings.max_colors = 4;
    auto digitized = run_digitize(request, TaskContext{});
    REQUIRE(digitized.has_value());
    document::Project project = std::move(digitized->project);
    project.embroidery_objects.clear();
    project.vector_objects.clear();
    Doc d;
    d.session.replaceProject(std::move(project), true);
    auto processed = processedImage(d.session.project());
    REQUIRE(processed.has_value());
    d.processed = std::move(*processed);
    return d;
}
// Un pixel sombre bien à l'intérieur du motif (le corps du tentacule), en mm « écran ».
std::pair<double, double> darkPointMm(const Doc& d) {
    const double mmpp = d.session.project().mm_per_px.value;
    std::optional<std::pair<double, double>> found;
    for (int y = d.processed.height / 2; y < d.processed.height && !found; ++y) {
        for (int x = d.processed.width / 3; x < 2 * d.processed.width / 3 && !found; ++x) {
            const auto* p =
                &d.processed.rgba[(static_cast<std::size_t>(y) * d.processed.width + x) * 4];
            if (p[3] > 250 && p[0] < 60 && p[1] < 80 && p[2] < 100) { // opaque : fond transparent
                found = pixelCentreMm(d.processed, mmpp, {x, y});
            }
        }
    }
    REQUIRE(found.has_value());
    return *found;
}
} // namespace

TEST_CASE("pixel <-> mm conversion round-trips and rejects points outside the image",
          "[desktop_v2][source]") {
    auto d = imageOnlyDoc();
    const double mmpp = d.session.project().mm_per_px.value;
    const PixelPoint p{10, 20};
    const auto [x, y] = pixelCentreMm(d.processed, mmpp, p);
    const auto back = pixelUnder(d.processed, mmpp, x, y);
    REQUIRE(back.has_value());
    CHECK(back->x == 10);
    CHECK(back->y == 20);
    CHECK_FALSE(pixelUnder(d.processed, mmpp, 1e6, 0).has_value());
    CHECK_FALSE(pixelUnder(d.processed, 0.0, 0, 0).has_value());
    CHECK_FALSE(pixelUnder(image::Image{}, mmpp, 0, 0).has_value());
}

TEST_CASE("a project without a source image refuses the image tools with a hint",
          "[desktop_v2][source]") {
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    const auto r = processedImage(*created);
    REQUIRE_FALSE(r.has_value());
    CHECK_FALSE(r.error().detail.empty());
}

TEST_CASE("click-to-stitch turns a colour area into one undoable stitched object",
          "[desktop_v2][source]") {
    auto d = imageOnlyDoc();
    const auto [x, y] = darkPointMm(d);
    const auto depth = d.session.undoNames().size();
    const auto r = clickToStitch(d.session, d.processed, x, y, 20, ClickKind::Fill);
    INFO(r.message);
    REQUIRE(r.ok);
    CHECK(r.objectId != 0);
    CHECK(d.session.project().embroidery_objects.size() == 1);
    CHECK(d.session.undoNames().size() == depth + 1);
    REQUIRE(d.session.undo());
    CHECK(d.session.project().embroidery_objects.empty());
    CHECK(d.session.project().vector_objects.empty()); // annuler retire AUSSI la forme vectorielle
}

TEST_CASE("click-to-stitch outside the image or with a hopeless area is refused cleanly",
          "[desktop_v2][source]") {
    auto d = imageOnlyDoc();
    const auto revision = d.session.revision();
    const auto outside = clickToStitch(d.session, d.processed, 1e5, 1e5, 20, ClickKind::Auto);
    CHECK_FALSE(outside.ok);
    CHECK_FALSE(outside.message.empty());
    CHECK(d.session.revision() == revision);
}

TEST_CASE("detail zones are painted, erased, cleared and undone as single strokes",
          "[desktop_v2][source]") {
    auto d = imageOnlyDoc();
    const auto [x, y] = darkPointMm(d);
    CHECK(d.session.project().detail_mask.empty());
    CHECK(detailWeightAt(d.session.project(), d.processed, x, y) == 0);

    REQUIRE(
        paintDetail(d.session, d.processed, {{x, y}, {x + 0.5, y}, {x + 1.0, y}}, 2.0, false).ok);
    CHECK(detailWeightAt(d.session.project(), d.processed, x, y) > 200);
    CHECK(d.session.undoNames().size() == 1); // un trait = une étape

    REQUIRE(paintDetail(d.session, d.processed, {{x, y}}, 3.0, true).ok);
    CHECK(detailWeightAt(d.session.project(), d.processed, x, y) == 0);
    REQUIRE(d.session.undo());
    CHECK(detailWeightAt(d.session.project(), d.processed, x, y) > 200);

    REQUIRE(clearDetail(d.session).ok);
    CHECK(d.session.project().detail_mask.empty());
    CHECK_FALSE(clearDetail(d.session).ok); // plus rien à effacer
    REQUIRE(d.session.undo());
    CHECK_FALSE(d.session.project().detail_mask.empty());
}

TEST_CASE("detail strokes refuse bad radii and strokes outside the image", "[desktop_v2][source]") {
    auto d = imageOnlyDoc();
    const auto [x, y] = darkPointMm(d);
    CHECK_FALSE(paintDetail(d.session, d.processed, {{x, y}}, 0.05, false).ok);
    CHECK_FALSE(paintDetail(d.session, d.processed, {{x, y}}, 99.0, false).ok);
    CHECK_FALSE(paintDetail(d.session, d.processed, {{1e6, 1e6}}, 2.0, false).ok);
    CHECK_FALSE(paintDetail(d.session, image::Image{}, {{x, y}}, 2.0, false).ok);
    CHECK_FALSE(d.session.canUndo());
}
