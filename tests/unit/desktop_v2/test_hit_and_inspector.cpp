// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "digitize_pipeline.hpp"
#include "document_session.hpp"
#include "hit_index.hpp"
#include "inspector_model.hpp"
#include "preview_service.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {

stitch_render::ThreadSegment seg(float ax, float ay, float bx, float by, std::uint64_t id) {
    stitch_render::ThreadSegment s;
    s.ax = ax;
    s.ay = ay;
    s.bx = bx;
    s.by = by;
    s.rgb = encode_id(id);
    return s;
}

const document::Project& tentabrode80() {
    static const document::Project project = [] {
        DigitizeRequest request;
        request.image = std::filesystem::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
        request.target_height_mm = 80.0;
        request.settings.max_colors = 4;
        auto r = run_digitize(request, TaskContext{});
        REQUIRE(r.has_value());
        return std::move(r->project);
    }();
    return project;
}

} // namespace

TEST_CASE("object ids round-trip through the colour encoding", "[desktop_v2][hit]") {
    for (const std::uint64_t id : {1ULL, 255ULL, 256ULL, 65535ULL, 65536ULL, kMaxEncodableId}) {
        CHECK(decode_id(encode_id(id)) == id);
    }
}

TEST_CASE("pick returns the topmost object within tolerance", "[desktop_v2][hit]") {
    // Objet 1 puis objet 2 se croisent en (5, 5) ; 2 est cousu après : il est dessus.
    const auto index = HitIndex::build({seg(0, 5, 10, 5, 1), seg(5, 0, 5, 10, 2)});
    CHECK(index.pick(5.0, 5.0, 0.5) == 2);
    CHECK(index.pick(1.0, 5.0, 0.5) == 1);
    CHECK(index.pick(5.0, 9.0, 0.5) == 2);
    CHECK_FALSE(index.pick(8.0, 8.0, 0.5).has_value());
    CHECK(index.pick(8.0, 5.4, 0.5) == 1); // dans la tolérance
    CHECK_FALSE(index.pick(8.0, 5.6, 0.5).has_value());
}

TEST_CASE("pick works across grid cell borders and for degenerate segments", "[desktop_v2][hit]") {
    const auto index = HitIndex::build({seg(3.9F, 3.9F, 4.1F, 4.1F, 7), seg(20, 20, 20, 20, 9)});
    CHECK(index.pick(4.0, 4.0, 0.3) == 7);
    CHECK(index.pick(20.2, 20.0, 0.3) == 9);
}

TEST_CASE("inRect lists intersecting objects by increasing id, bounds are exact",
          "[desktop_v2][hit]") {
    const auto index =
        HitIndex::build({seg(0, 0, 2, 2, 5), seg(10, 10, 12, 12, 3), seg(1, 9, 3, 11, 4)});
    CHECK(index.objectCount() == 3);
    CHECK(index.inRect({0, 0, 11, 11}) == std::vector<std::uint64_t>{3, 4, 5});
    CHECK(index.inRect({9, 9, 13, 13}) == std::vector<std::uint64_t>{3});
    CHECK(index.inRect({0, 8, 4, 12}) == std::vector<std::uint64_t>{4});
    CHECK(index.inRect({50, 50, 60, 60}).empty());
    const auto b = index.boundsOf(3);
    REQUIRE(b.has_value());
    CHECK(b->x0 == 10.0);
    CHECK(b->y1 == 12.0);
    CHECK_FALSE(index.boundsOf(99).has_value());
}

TEST_CASE("preview carries a coherent mm/pixel frame and a hit index",
          "[desktop_v2][hit][preview]") {
    auto snapshot = std::make_shared<const document::Project>(tentabrode80());
    const auto preview = render_preview(snapshot, 1, 3.0, TaskContext{});
    REQUIRE(preview.has_value());
    REQUIRE(preview->hits != nullptr);
    CHECK(preview->hits->objectCount() > 1);
    // Toute emprise d'objet tombe DANS l'image : le repère mm -> pixels est cohérent.
    for (const auto& o : snapshot->embroidery_objects) {
        const auto b = preview->hits->boundsOf(o.id.value);
        if (!b) {
            continue;
        }
        CHECK(preview->mmToPxX(b->x0) >= -1.0);
        CHECK(preview->mmToPxY(b->y0) >= -1.0);
        CHECK(preview->mmToPxX(b->x1) <= preview->width + 1.0);
        CHECK(preview->mmToPxY(b->y1) <= preview->height + 1.0);
    }
    CHECK(preview->pxToMmX(preview->mmToPxX(12.5)) == Catch::Approx(12.5).margin(1e-9));
}

TEST_CASE("hidden objects are neither rendered nor selectable on the canvas",
          "[desktop_v2][hit][preview]") {
    document::Project project = tentabrode80();
    const auto hiddenId = project.embroidery_objects.front().id.value;
    project.embroidery_objects.front().visible = false;
    const auto preview =
        render_preview(std::make_shared<const document::Project>(project), 1, 2.0, TaskContext{});
    REQUIRE(preview.has_value());
    REQUIRE(preview->hits != nullptr);
    CHECK_FALSE(preview->hits->boundsOf(hiddenId).has_value());
}

TEST_CASE("inspector reports common and mixed values", "[desktop_v2][inspector]") {
    const auto& project = tentabrode80();
    REQUIRE(project.embroidery_objects.size() >= 2);
    // Deux objets de couleurs différentes.
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    for (const auto& o : project.embroidery_objects) {
        if (a == 0) {
            a = o.id.value;
        } else if (o.rgb != project.embroidery_objects.front().rgb) {
            b = o.id.value;
            break;
        }
    }
    REQUIRE(b != 0);
    const auto one = inspect(project, {{TargetKind::EmbroideryObject, a}});
    CHECK(one.count == 1);
    CHECK_FALSE(one.rgb.mixed);
    CHECK_FALSE(one.name.mixed);
    const auto two =
        inspect(project, {{TargetKind::EmbroideryObject, a}, {TargetKind::EmbroideryObject, b}});
    CHECK(two.count == 2);
    CHECK(two.rgb.mixed);
    CHECK(two.name.mixed);
    // Régions et cibles disparues sont ignorées.
    const auto none =
        inspect(project, {{TargetKind::Region, 1}, {TargetKind::EmbroideryObject, 999999}});
    CHECK(none.count == 0);
}

TEST_CASE("inspector edits are one undo step each and use the shared batch path",
          "[desktop_v2][inspector]") {
    DocumentSession session;
    session.replaceProject(document::Project(tentabrode80()), true);
    const auto& objects = session.project().embroidery_objects;
    const std::vector<std::uint64_t> ids{objects[0].id.value, objects[1].id.value};

    const auto colour = session.applyBatch(batchSetColor(ids, {10, 200, 30}), "Couleur");
    REQUIRE(colour.committed);
    CHECK(session.project().embroidery_objects[0].rgb == std::array<std::uint8_t, 3>{10, 200, 30});
    CHECK(session.project().embroidery_objects[1].rgb == std::array<std::uint8_t, 3>{10, 200, 30});
    CHECK(session.undoNames().size() == 1); // deux objets, UNE étape

    const auto type = session.applyBatch(batchSetStitchType(ids, "running"), "Type de points");
    REQUIRE(type.committed);
    CHECK(inspect(session.project(), {{TargetKind::EmbroideryObject, ids[0]}}).typeId.value ==
          "running");

    const auto hidden = session.applyBatch(batchSetVisible(ids, false), "Masquer");
    REQUIRE(hidden.committed);
    CHECK_FALSE(session.project().embroidery_objects[0].visible);
    const auto locked = session.applyBatch(batchSetLocked(ids, true), "Verrouiller");
    REQUIRE(locked.committed);
    CHECK(session.project().embroidery_objects[1].locked);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(session.undo());
    }
    CHECK_FALSE(session.canUndo());
    CHECK(session.project().embroidery_objects[0].rgb == tentabrode80().embroidery_objects[0].rgb);
}

TEST_CASE("inspector batches refuse invalid input instead of guessing", "[desktop_v2][inspector]") {
    CHECK(batchSetStitchType({1}, "satin").empty()); // le satin passe par auto_satin
    CHECK(batchSetStitchType({1}, "nope\"}").empty());
    CHECK(batchSetColor({}, {1, 2, 3}).empty());
    CHECK(batchSetVisible({}, true).empty());
    const auto json = batchSetStitchType({7, 8}, "tatami");
    CHECK(json.find("\"object\":7") != std::string::npos);
    CHECK(json.find("\"object\":8") != std::string::npos);
    CHECK(json.find('#') == std::string::npos);
}
