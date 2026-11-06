// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "digitize_pipeline.hpp"
#include "display_filters.hpp"
#include "document_session.hpp"
#include "object_labels.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

namespace {
DocumentSession digitized() {
    DigitizeRequest request;
    request.image = fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = 50.0;
    request.settings.max_colors = 4;
    auto out = run_digitize(request, TaskContext{});
    REQUIRE(out.has_value());
    DocumentSession s;
    s.replaceProject(std::move(out->project), true);
    return s;
}

std::size_t visibleCount(const document::Project& p) {
    std::size_t n = 0;
    for (const auto& o : p.embroidery_objects) {
        n += o.visible ? 1 : 0;
    }
    return n;
}
} // namespace

TEST_CASE("show only one colour hides the rest in one undo step and show all restores",
          "[desktop_v2][filters]") {
    auto s = digitized();
    const auto total = s.project().embroidery_objects.size();
    REQUIRE(total > 5);
    const auto rgb = s.project().embroidery_objects.front().rgb;
    const auto depth = s.undoNames().size();
    REQUIRE(showOnlyColour(s, rgb).ok);
    CHECK(s.undoNames().size() == depth + 1);
    const auto shown = visibleCount(s.project());
    CHECK(shown < total);
    CHECK(shown > 0);
    for (const auto& o : s.project().embroidery_objects) {
        CHECK(o.visible == (o.rgb == rgb));
    }
    CHECK(hiddenObjectCount(s.project()) == total - shown);
    CHECK_FALSE(showOnlyColour(s, rgb).ok); // déjà ainsi : refus clair, pas d'étape inutile
    CHECK(s.undoNames().size() == depth + 1);
    REQUIRE(showAllObjects(s).ok);
    CHECK(visibleCount(s.project()) == total);
    CHECK_FALSE(showAllObjects(s).ok); // rien à afficher de plus
    REQUIRE(s.undo());                 // « tout afficher »
    REQUIRE(s.undo());                 // le filtre
    CHECK(visibleCount(s.project()) == total);
}

TEST_CASE("show only a stitch type, with refusals for unknown colours and types",
          "[desktop_v2][filters]") {
    auto s = digitized();
    const std::string type(stitchTypeId(s.project().embroidery_objects.front()));
    REQUIRE(showOnlyType(s, type).ok);
    for (const auto& o : s.project().embroidery_objects) {
        CHECK(o.visible == (stitchTypeId(o) == type));
    }
    const auto before = s.undoNames().size();
    CHECK_FALSE(showOnlyType(s, "type_qui_n_existe_pas").ok);
    CHECK_FALSE(showOnlyColour(s, {1, 2, 3}).ok); // couleur absente du motif
    CHECK(s.undoNames().size() == before);        // un refus ne touche jamais au document
}
