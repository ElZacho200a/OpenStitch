// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
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

TEST_CASE("a brush stroke moves a boundary in one undo step and undoes exactly",
          "[desktop_v2][regions][brush]") {
    Digitized d;
    const auto regions = listRegions(d.session.project());
    REQUIRE(regions.size() >= 3);
    const auto painted = regions.front().id; // la plus grande : elle gagne
    // Une région voisine et l'un de ses pixels.
    const auto neighbours = neighbourRegions(d.session.project(), painted);
    REQUIRE_FALSE(neighbours.empty());
    const auto other = neighbours.front();
    const auto& seg = *d.session.project().segmentation;
    std::size_t at = 0;
    for (std::size_t i = 0; i < seg.labels.size(); ++i) {
        if (seg.labels[i] == other) {
            at = i;
            break;
        }
    }
    const double cx = static_cast<double>(at % static_cast<std::size_t>(seg.width)) + 0.5;
    const double cy = static_cast<double>(at / static_cast<std::size_t>(seg.width)) + 0.5;
    const auto pixelsOf = [&](std::uint64_t id) {
        for (const auto& r : listRegions(d.session.project())) {
            if (r.id == id) {
                return r.pixels;
            }
        }
        return std::size_t{0};
    };
    const auto labelsBefore = seg.labels;
    const auto paintedBefore = pixelsOf(painted);
    const auto otherBefore = pixelsOf(other);
    const auto depth = d.session.undoNames().size();

    REQUIRE(paintRegionStroke(d.session, painted, {{cx, cy}}, 2.0, false).ok);
    CHECK(d.session.undoNames().size() == depth + 1);
    CHECK(regionAtPixel(d.session.project(), static_cast<int>(cx), static_cast<int>(cy)) ==
          painted);
    const auto gained = pixelsOf(painted) - paintedBefore;
    CHECK(gained > 0);
    // Les comptes restent cohérents avec la carte d'étiquettes.
    std::size_t counted = 0;
    for (const auto label : d.session.project().segmentation->labels) {
        counted += label == painted ? 1 : 0;
    }
    CHECK(counted == pixelsOf(painted));
    CHECK(pixelsOf(other) <= otherBefore);

    REQUIRE(d.session.undo());
    CHECK(d.session.project().segmentation->labels == labelsBefore);
    CHECK(pixelsOf(painted) == paintedBefore);
    CHECK(pixelsOf(other) == otherBefore);
    REQUIRE(d.session.redo());
    CHECK(pixelsOf(painted) == paintedBefore + gained);
}

TEST_CASE("the brush eraser only clears the chosen region and refuses useless strokes",
          "[desktop_v2][regions][brush]") {
    Digitized d;
    const auto regions = listRegions(d.session.project());
    REQUIRE(regions.size() >= 3);
    const auto chosen = regions.front().id;
    const auto& seg = *d.session.project().segmentation;
    std::size_t inside = 0;
    for (std::size_t i = 0; i < seg.labels.size(); ++i) {
        if (seg.labels[i] == chosen) {
            inside = i;
            break;
        }
    }
    const double cx = static_cast<double>(inside % static_cast<std::size_t>(seg.width)) + 0.5;
    const double cy = static_cast<double>(inside / static_cast<std::size_t>(seg.width)) + 0.5;
    const auto depth = d.session.undoNames().size();

    // Peindre là où la région est déjà : rien à faire, aucune trace.
    CHECK_FALSE(paintRegionStroke(d.session, chosen, {{cx, cy}}, 0.5, false).ok);
    CHECK_FALSE(paintRegionStroke(d.session, 999999, {{cx, cy}}, 3.0, false).ok); // région inconnue
    CHECK_FALSE(
        paintRegionStroke(d.session, chosen, {{cx, cy}}, 0.1, false).ok); // rayon trop petit
    CHECK_FALSE(paintRegionStroke(d.session, chosen, {{cx, cy}}, 500.0, false).ok); // trop grand
    CHECK_FALSE(paintRegionStroke(d.session, chosen, {}, 3.0, false).ok);           // trait vide
    CHECK_FALSE(paintRegionStroke(d.session, chosen, {{std::nan(""), 0.0}}, 3.0, false).ok);
    CHECK(d.session.undoNames().size() == depth);

    const auto before = seg.labels;
    REQUIRE(paintRegionStroke(d.session, chosen, {{cx, cy}}, 2.0, true).ok); // gomme
    const auto& after = d.session.project().segmentation->labels;
    CHECK(after[inside] == 0u);
    std::size_t changed = 0;
    for (std::size_t i = 0; i < after.size(); ++i) {
        if (after[i] != before[i]) {
            ++changed;
            CHECK(before[i] == chosen); // seuls les pixels de la région choisie ont bougé
        }
    }
    CHECK(changed > 0);
    REQUIRE(d.session.undo());
    CHECK(d.session.project().segmentation->labels == before);
}

TEST_CASE("a brush stroke spares the small regions it would cover entirely",
          "[desktop_v2][regions][brush]") {
    Digitized d;
    const auto before = listRegions(d.session.project());
    REQUIRE(before.size() >= 3);
    const auto chosen = before.front().id;
    const auto& seg = *d.session.project().segmentation;
    std::size_t inside = 0;
    for (std::size_t i = 0; i < seg.labels.size(); ++i) {
        if (seg.labels[i] == chosen) {
            inside = i;
            break;
        }
    }
    const double cx = static_cast<double>(inside % static_cast<std::size_t>(seg.width)) + 0.5;
    const double cy = static_cast<double>(inside / static_cast<std::size_t>(seg.width)) + 0.5;
    const auto labelsBefore = seg.labels;
    const auto depth = d.session.undoNames().size();

    // Un très gros pinceau couvre entièrement les petites régions voisines : elles sont épargnées.
    const auto r = paintRegionStroke(d.session, chosen, {{cx, cy}}, 200.0, false);
    if (!r.ok) { // tout est déjà dans la région choisie ou protégé : refus sans trace
        CHECK(d.session.undoNames().size() == depth);
        return;
    }
    CHECK(d.session.undoNames().size() == depth + 1);
    const auto after = listRegions(d.session.project());
    CHECK(after.size() == before.size()); // aucune région n'a disparu
    for (const auto& region : after) {
        CHECK(region.pixels > 0);
    }
    std::size_t counted = 0;
    for (const auto label : d.session.project().segmentation->labels) {
        counted += label == chosen ? 1 : 0;
    }
    const auto it = std::find_if(after.begin(), after.end(),
                                 [&](const RegionInfo& x) { return x.id == chosen; });
    REQUIRE(it != after.end());
    CHECK(it->pixels == counted); // comptes cohérents avec la carte d'étiquettes
    REQUIRE(d.session.undo());
    CHECK(d.session.project().segmentation->labels == labelsBefore);
}

TEST_CASE("region sets: same colour, neighbours, invert and whole-set edits are one undo step",
          "[desktop_v2][regions][selection]") {
    Digitized d;
    const auto& project = d.session.project();
    const auto all = allRegionIds(project);
    REQUIRE(all.size() >= 3);

    // Inverser : le complémentaire exact, sans doublon.
    const std::vector<std::uint64_t> one{all.front()};
    const auto inverse = invertRegionSet(project, one);
    CHECK(inverse.size() == all.size() - 1);
    CHECK(std::find(inverse.begin(), inverse.end(), all.front()) == inverse.end());
    CHECK(invertRegionSet(project, all).empty());

    // Même couleur : contient la région elle-même, et que des régions de cette couleur.
    const auto same = sameColorRegions(project, all.front());
    REQUIRE_FALSE(same.empty());
    CHECK(std::find(same.begin(), same.end(), all.front()) != same.end());
    const auto regions = listRegions(project);
    for (const auto id : same) {
        const auto it = std::find_if(regions.begin(), regions.end(),
                                     [id](const RegionInfo& r) { return r.id == id; });
        REQUIRE(it != regions.end());
        CHECK(it->rgb == regions.front().rgb);
    }
    CHECK(sameColorRegions(project, 999999).empty());

    // Voisines d'un ensemble : jamais l'ensemble lui-même.
    const auto around = neighboursOfSet(project, one);
    for (const auto id : around) {
        CHECK(id != all.front());
    }

    // Fusion d'un ensemble : UNE étape d'annulation, la plus grande région survit.
    const auto depth = d.session.undoNames().size();
    const std::vector<std::uint64_t> trio{all[0], all[1], all[2]};
    REQUIRE(mergeRegionSet(d.session, trio).ok);
    CHECK(d.session.undoNames().size() == depth + 1);
    const auto afterMerge = allRegionIds(d.session.project());
    CHECK(afterMerge.size() == all.size() - 2);
    CHECK(std::find(afterMerge.begin(), afterMerge.end(), all[0]) != afterMerge.end());
    REQUIRE(d.session.undo());
    CHECK(allRegionIds(d.session.project()) == all);

    // Recolorer l'ensemble : une étape, annulable ; couleur déjà identique refusée.
    const std::array<std::uint8_t, 3> blue{12, 34, 200};
    REQUIRE(recolorRegionSet(d.session, trio, blue).ok);
    CHECK(d.session.undoNames().size() == depth + 1);
    for (const auto id : trio) {
        CHECK(d.session.project().segmentation->find(RegionId{id})->rgb == blue);
    }
    CHECK_FALSE(recolorRegionSet(d.session, trio, blue).ok);
    REQUIRE(d.session.undo());

    // Supprimer l'ensemble : refusé s'il ne resterait rien.
    CHECK_FALSE(deleteRegionSet(d.session, all).ok);
    CHECK_FALSE(mergeRegionSet(d.session, one).ok); // une seule région : rien à fusionner
    const std::vector<std::uint64_t> pair{all[1], all[2]};
    REQUIRE(deleteRegionSet(d.session, pair).ok);
    CHECK(d.session.undoNames().size() == depth + 1);
    REQUIRE(d.session.undo());
}

TEST_CASE("regions and objects are linked in both directions", "[desktop_v2][regions][selection]") {
    Digitized d;
    const auto& project = d.session.project();
    const auto regions = allRegionIds(project);
    // Toutes les régions ensemble donnent tous les objets qui ont une région d'origine.
    const auto objects = objectsOfRegions(project, regions);
    CHECK_FALSE(objects.empty());
    // Aller-retour : les régions des objets sont des régions vivantes.
    const auto back = regionsOfObjects(project, objects);
    REQUIRE_FALSE(back.empty());
    for (const auto id : back) {
        CHECK(std::find(regions.begin(), regions.end(), id) != regions.end());
    }
    // Les objets d'une seule région ne contiennent que des objets qui reviennent à cette région.
    const auto mine = objectsOfRegions(project, {back.front()});
    REQUIRE_FALSE(mine.empty());
    for (const auto objectId : mine) {
        CHECK(regionsOfObjects(project, {objectId}) == std::vector<std::uint64_t>{back.front()});
    }
    CHECK(objectsOfRegions(project, {}).empty());
    CHECK(regionsOfObjects(project, {123456789}).empty());
}

TEST_CASE("palette reduction previews its result and is one undo step",
          "[desktop_v2][regions][palette]") {
    Digitized d;
    const auto before = previewColorReduction(d.session.project(), 0, 0.0, true);
    CHECK(before.colorsAfter == before.colorsBefore); // aucune limite : rien à réduire
    const auto preview = previewColorReduction(d.session.project(), 2, 0.0, true);
    REQUIRE(preview.colorsBefore >= 3);
    CHECK(preview.colorsAfter <= 2);
    CHECK(preview.colorsAfter < preview.colorsBefore);

    const auto depth = d.session.undoNames().size();
    const auto regionsBefore = allRegionIds(d.session.project()).size();
    REQUIRE(reduceColors(d.session, 2, 0.0, true).ok);
    CHECK(d.session.undoNames().size() == depth + 1);
    CHECK(allRegionIds(d.session.project()).size() <= regionsBefore);
    std::set<std::array<std::uint8_t, 3>> colours;
    for (const auto& r : listRegions(d.session.project())) {
        colours.insert(r.rgb);
    }
    CHECK(colours.size() <= 2);
    CHECK_FALSE(reduceColors(d.session, 2, 0.0, true).ok); // déjà réduite : refus clair
    REQUIRE(d.session.undo());
    CHECK(allRegionIds(d.session.project()).size() == regionsBefore);
}

TEST_CASE("re-segmenting with other settings replaces the objects in one undo step",
          "[desktop_v2][regions][resegment]") {
    Digitized d;
    const auto regionsBefore = allRegionIds(d.session.project()).size();
    auto fewer = d.settings;
    fewer.max_colors = 2;
    const auto result = resegmentObjects(d.session.snapshot(), d.session.revision(), d.working,
                                         fewer, TaskContext{});
    REQUIRE(result.has_value());
    REQUIRE(result->after != nullptr);
    CHECK(result->after->segmentation->region_count() < regionsBefore);
    const auto depth = d.session.undoNames().size();
    REQUIRE(applyRegeneration(d.session, *result).ok);
    CHECK(d.session.undoNames().size() == depth + 1);
    CHECK(allRegionIds(d.session.project()).size() < regionsBefore);
    REQUIRE(d.session.undo());
    CHECK(allRegionIds(d.session.project()).size() == regionsBefore);

    // Une image vide ou un document modifié entre-temps : refus, jamais d'écrasement.
    CHECK_FALSE(resegmentObjects(d.session.snapshot(), d.session.revision(), image::Image{}, fewer,
                                 TaskContext{})
                    .has_value());
    // Document modifié depuis le calcul : l'ancien résultat est refusé.
    const auto stale = *result;
    REQUIRE(recolorRegion(d.session, allRegionIds(d.session.project()).front(), {1, 2, 3}).ok);
    CHECK_FALSE(applyRegeneration(d.session, stale).ok);
}
