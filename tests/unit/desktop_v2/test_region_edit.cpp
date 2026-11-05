// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>

#include "digitize_pipeline.hpp"
#include "document_session.hpp"
#include "openstitch/image/image.hpp"
#include "region_edit.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

namespace {

struct Digitized {
    DocumentSession session;
    image::Image working;
    project_ops::DigitizeSettings settings;
    Digitized() {
        DigitizeRequest request;
        request.image = fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
        request.target_height_mm = 60.0;
        request.settings.max_colors = 5;
        settings = request.settings;
        auto out = run_digitize(request, TaskContext{});
        REQUIRE(out.has_value());
        session.replaceProject(std::move(out->project), true);
        auto img = image::load_image(request.image);
        REQUIRE(img.has_value());
        working = std::move(*img);
    }
};

} // namespace

TEST_CASE("a digitized project exposes its regions, neighbours and a region map",
          "[desktop_v2][regions]") {
    Digitized d;
    const auto regions = listRegions(d.session.project());
    REQUIRE(regions.size() >= 2);
    for (std::size_t i = 1; i < regions.size(); ++i) {
        CHECK(regions[i - 1].pixels >= regions[i].pixels); // les plus grandes d'abord
    }
    // Une région a au moins une voisine ; la carte a la taille de l'image segmentée.
    bool anyNeighbour = false;
    for (const auto& r : regions) {
        anyNeighbour = anyNeighbour || !neighbourRegions(d.session.project(), r.id).empty();
    }
    CHECK(anyNeighbour);
    const auto map = renderRegionMap(d.session.project(), regions.front().id);
    CHECK(map.width > 0);
    CHECK(map.rgba.size() == static_cast<std::size_t>(map.width) * map.height * 4);
    // Un pixel de la plus grande région la retrouve ; un pixel hors image ne retrouve rien.
    bool found = false;
    for (int y = 0; y < map.height && !found; y += 7) {
        for (int x = 0; x < map.width && !found; x += 7) {
            if (regionAtPixel(d.session.project(), x, y) == regions.front().id) {
                found = true;
            }
        }
    }
    CHECK(found);
    CHECK_FALSE(regionAtPixel(d.session.project(), -5, -5).has_value());
}

TEST_CASE("deleting or merging a region is one undo step and restores exactly",
          "[desktop_v2][regions]") {
    Digitized d;
    const auto before = listRegions(d.session.project());
    REQUIRE(before.size() >= 3);
    const auto depth = d.session.undoNames().size();

    // Supprimer la plus petite région : elle est absorbée par une voisine.
    REQUIRE(deleteRegion(d.session, before.back().id).ok);
    CHECK(d.session.undoNames().size() == depth + 1);
    CHECK(listRegions(d.session.project()).size() == before.size() - 1);
    REQUIRE(d.session.undo());
    const auto restored = listRegions(d.session.project());
    REQUIRE(restored.size() == before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        CHECK(restored[i].id == before[i].id);
        CHECK(restored[i].pixels == before[i].pixels);
    }

    // Fusionner deux voisines : la première garde sa couleur.
    std::uint64_t keep = 0;
    std::uint64_t absorb = 0;
    for (const auto& r : before) {
        const auto n = neighbourRegions(d.session.project(), r.id);
        if (!n.empty()) {
            keep = r.id;
            absorb = n.front();
            break;
        }
    }
    REQUIRE(keep != 0);
    REQUIRE(mergeRegionInto(d.session, keep, absorb).ok);
    CHECK(listRegions(d.session.project()).size() == before.size() - 1);
    REQUIRE(d.session.undo());
    CHECK(listRegions(d.session.project()).size() == before.size());
}

TEST_CASE("region corrections refuse nonsense with a clear message", "[desktop_v2][regions]") {
    Digitized d;
    const auto regions = listRegions(d.session.project());
    const auto depth = d.session.undoNames().size();
    CHECK_FALSE(mergeRegionInto(d.session, regions.front().id, regions.front().id).ok);
    CHECK_FALSE(mergeRegionInto(d.session, regions.front().id, 999999).ok);
    CHECK_FALSE(deleteRegion(d.session, 999999).ok);
    CHECK_FALSE(cleanSmallIslands(d.session, 0).ok);
    CHECK_FALSE(cleanSmallIslands(d.session, 1).ok); // aucune région n'a moins d'un pixel
    CHECK(d.session.undoNames().size() == depth);    // aucun refus ne laisse de trace

    DocumentSession empty; // projet sans segmentation
    CHECK_FALSE(deleteRegion(empty, 1).ok);
    CHECK_FALSE(mergeRegionInto(empty, 1, 2).ok);
    CHECK(listRegions(empty.project()).empty());
}

TEST_CASE("regenerating objects follows the corrected regions and is one undo step",
          "[desktop_v2][regions]") {
    Digitized d;
    const auto objectsBefore = d.session.project().embroidery_objects.size();
    REQUIRE(objectsBefore >= 2);
    const auto regions = listRegions(d.session.project());
    REQUIRE(deleteRegion(d.session, regions.back().id).ok);

    // Les objets ne suivent pas encore la correction : seule la régénération les met à jour.
    CHECK(d.session.project().embroidery_objects.size() == objectsBefore);
    TaskContext task;
    const auto revision = d.session.revision();
    const auto regen =
        regenerateObjects(d.session.snapshot(), revision, d.working, d.settings, task);
    REQUIRE(regen.has_value());
    CHECK(regen->objectsBefore == objectsBefore);
    CHECK(d.session.revision() == revision); // le calcul ne modifie pas le document

    const auto depth = d.session.undoNames().size();
    REQUIRE(applyRegeneration(d.session, *regen).ok);
    CHECK(d.session.undoNames().size() == depth + 1);
    CHECK(d.session.project().embroidery_objects.size() == regen->objectsAfter);
    CHECK(d.session.project().segmentation.has_value()); // la segmentation corrigée est gardée

    // Annuler rend exactement les objets d'avant la régénération.
    REQUIRE(d.session.undo());
    CHECK(d.session.project().embroidery_objects.size() == objectsBefore);
}

TEST_CASE("recoloring a region is undoable and rejects unchanged or missing colors",
          "[desktop_v2][regions]") {
    Digitized d;
    const auto original = listRegions(d.session.project()).front();
    std::array<std::uint8_t, 3> changed{23, 67, 119};
    if (changed == original.rgb) {
        changed = {119, 67, 23};
    }
    const auto depth = d.session.undoNames().size();
    REQUIRE(recolorRegion(d.session, original.id, changed).ok);
    CHECK(d.session.undoNames().size() == depth + 1);
    REQUIRE(d.session.project().segmentation.has_value());
    REQUIRE(d.session.project().segmentation->find(RegionId{original.id}) != nullptr);
    CHECK(d.session.project().segmentation->find(RegionId{original.id})->rgb == changed);
    CHECK_FALSE(recolorRegion(d.session, original.id, changed).ok);
    CHECK_FALSE(recolorRegion(d.session, 999999, changed).ok);
    CHECK(d.session.undoNames().size() == depth + 1);
    REQUIRE(d.session.undo());
    CHECK(d.session.project().segmentation->find(RegionId{original.id})->rgb == original.rgb);
    REQUIRE(d.session.redo());
    CHECK(d.session.project().segmentation->find(RegionId{original.id})->rgb == changed);
    DocumentSession empty;
    CHECK_FALSE(recolorRegion(empty, original.id, changed).ok);
}

TEST_CASE("merging two whole color classes is one undo step with exact restoration",
          "[desktop_v2][regions]") {
    Digitized d;
    const auto before = listRegions(d.session.project());
    REQUIRE(before.size() >= 2);
    const auto source = before.front();
    const auto target = std::find_if(before.begin(), before.end(),
                                     [&](const RegionInfo& r) { return r.rgb != source.rgb; });
    REQUIRE(target != before.end());

    const auto depth = d.session.undoNames().size();
    REQUIRE(mergeRegionColors(d.session, source.id, target->id).ok);
    CHECK(d.session.undoNames().size() == depth + 1);
    const auto after = listRegions(d.session.project());
    CHECK(std::none_of(after.begin(), after.end(),
                       [&](const RegionInfo& r) { return r.rgb == source.rgb; }));
    CHECK(std::any_of(after.begin(), after.end(),
                      [&](const RegionInfo& r) { return r.rgb == target->rgb; }));
    CHECK_FALSE(mergeRegionColors(d.session, target->id, target->id).ok);
    CHECK_FALSE(mergeRegionColors(d.session, 999999, target->id).ok);
    CHECK(d.session.undoNames().size() == depth + 1);

    REQUIRE(d.session.undo());
    const auto restored = listRegions(d.session.project());
    REQUIRE(restored.size() == before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        CHECK(restored[i].id == before[i].id);
        CHECK(restored[i].rgb == before[i].rgb);
        CHECK(restored[i].pixels == before[i].pixels);
    }
    DocumentSession empty;
    CHECK_FALSE(mergeRegionColors(empty, source.id, target->id).ok);
}

TEST_CASE("small enclosed transparent holes can be repaired and undone", "[desktop_v2][regions]") {
    document::Project project;
    segmentation::Segmentation seg;
    seg.width = 5;
    seg.height = 5;
    seg.labels.assign(25, 1);
    seg.labels[12] = 0; // trou intérieur
    seg.labels[0] = 0;  // fond extérieur : ne doit jamais être rempli
    seg.region_slots.emplace_back(segmentation::Region{RegionId{1}, {130, 40, 75}, 23});
    project.segmentation = std::move(seg);
    DocumentSession session;
    session.replaceProject(std::move(project), true);

    CHECK_FALSE(fillSmallHoles(session, 0).ok);
    const auto depth = session.undoNames().size();
    REQUIRE(fillSmallHoles(session, 1).ok);
    CHECK(session.undoNames().size() == depth + 1);
    CHECK(session.project().segmentation->labels[12] == 1);
    CHECK(session.project().segmentation->labels[0] == 0);
    CHECK(session.project().segmentation->find(RegionId{1})->pixel_count == 24);
    CHECK_FALSE(fillSmallHoles(session, 1).ok);
    REQUIRE(session.undo());
    CHECK(session.project().segmentation->labels[12] == 0);
    CHECK(session.project().segmentation->find(RegionId{1})->pixel_count == 23);
    REQUIRE(session.redo());
    CHECK(session.project().segmentation->labels[12] == 1);

    DocumentSession empty;
    CHECK_FALSE(fillSmallHoles(empty, 1).ok);
}

TEST_CASE("a regeneration computed for an older document is refused", "[desktop_v2][regions]") {
    Digitized d;
    TaskContext task;
    const auto regen =
        regenerateObjects(d.session.snapshot(), d.session.revision(), d.working, d.settings, task);
    REQUIRE(regen.has_value());
    const auto regions = listRegions(d.session.project());
    REQUIRE(deleteRegion(d.session, regions.back().id).ok); // le document change ensuite
    const auto depth = d.session.undoNames().size();
    const auto r = applyRegeneration(d.session, *regen);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.message.empty());
    CHECK(d.session.undoNames().size() == depth);
}
