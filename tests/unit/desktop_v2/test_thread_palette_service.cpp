// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cctype>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "document_session.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "shape_batch.hpp"
#include "thread_palette_service.hpp"
#include "thread_plan.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
DocumentSession newSession() {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    return s;
}
} // namespace

TEST_CASE("charts are listed and threads are found by name or reference", "[desktop_v2][palette]") {
    const auto charts = chartInfos();
    REQUIRE(charts.size() >= 2);
    for (const auto& c : charts) {
        CHECK_FALSE(c.id.empty());
        CHECK(c.count > 0);
    }
    const auto& first = charts.front();
    const auto all = searchThreads(first.id, "", 5);
    REQUIRE(all.size() == 5);
    const auto byCode = searchThreads(first.id, all[2].code);
    REQUIRE_FALSE(byCode.empty());
    CHECK(std::ranges::any_of(byCode, [&](const auto& t) { return t.code == all[2].code; }));
    // Sans casse.
    std::string upper = all[2].code;
    for (auto& ch : upper) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    CHECK(searchThreads(first.id, upper).size() == byCode.size());
    CHECK(searchThreads("no-such-chart", "").empty());
    CHECK(searchThreads(first.id, "zzzz-no-thread-has-this").empty());
}

TEST_CASE("nearest threads are sorted by distance and an exact colour is distance zero",
          "[desktop_v2][palette]") {
    const auto& chart = chartInfos().front();
    const auto sample = searchThreads(chart.id, "", 1).front();
    const auto near = nearestThreads(sample.rgb, chart.id, 6);
    REQUIRE(near.size() == 6);
    CHECK(near.front().distance == Catch::Approx(0.0).margin(1e-9));
    for (std::size_t i = 1; i < near.size(); ++i) {
        CHECK(near[i].distance >= near[i - 1].distance);
    }
    CHECK(nearestThreads(sample.rgb, "no-such-chart", 6).empty());
    CHECK(nearestThreads(sample.rgb, chart.id, 0).empty());
    CHECK(nearestThreads(sample.rgb, chart.id, 6)[0].code == near[0].code); // déterministe
}

TEST_CASE("assigning a chart thread recolours every object of the thread in one undo step",
          "[desktop_v2][palette]") {
    auto s = newSession();
    REQUIRE(s.applyBatch(batchAddRect({0, 0}, 20, 10), "A").committed);
    REQUIRE(s.applyBatch(batchAddRect({40, 0}, 20, 10), "B").committed);
    const auto entries = threadEntries(s.project());
    REQUIRE_FALSE(entries.empty());
    const auto& chart = chartInfos().front();
    const auto pick = searchThreads(chart.id, "", 3).back();
    const auto depth = s.undoNames().size();
    REQUIRE(assignChartThread(s, 0, chart.id, pick.code).ok);
    CHECK(s.undoNames().size() == depth + 1);
    for (const auto id : entries[0].objects) {
        const auto* obj = s.project().findEmbroidery(ObjectId{id});
        REQUIRE(obj != nullptr);
        REQUIRE(obj->thread.has_value());
        CHECK(obj->thread->code == pick.code);
        CHECK(obj->rgb == pick.rgb);
    }
    CHECK_FALSE(assignChartThread(s, 99, chart.id, pick.code).ok);
    CHECK_FALSE(assignChartThread(s, 0, chart.id, "no-such-code").ok);
    CHECK_FALSE(assignChartThread(s, 0, "no-such-chart", pick.code).ok);
    CHECK(s.undoNames().size() == depth + 1);
    REQUIRE(s.undo());
    const auto* obj = s.project().findEmbroidery(ObjectId{entries[0].objects.front()});
    CHECK(obj->rgb != pick.rgb);
}

TEST_CASE("blending two colours mixes in linear light and clamps the ratio",
          "[desktop_v2][palette]") {
    const Rgb black{0, 0, 0};
    const Rgb white{255, 255, 255};
    CHECK(blendRgb(black, white, 0.0) == black);
    CHECK(blendRgb(black, white, 1.0) == white);
    CHECK(blendRgb(black, white, -3.0) == black);
    CHECK(blendRgb(black, white, 7.0) == white);
    // Moitié noir / moitié blanc en lumière linéaire : plus clair que le gris médian sRGB (128).
    const auto mid = blendRgb(black, white, 0.5);
    CHECK(mid[0] > 128);
    CHECK(mid[0] == mid[1]);
    CHECK(blendRgb({10, 20, 30}, {10, 20, 30}, 0.4) == Rgb{10, 20, 30});
}

TEST_CASE("a blend suggestion is deterministic and never claims to beat a better single thread",
          "[desktop_v2][palette]") {
    const auto& chart = chartInfos().front();
    const Rgb target{123, 77, 201}; // une couleur quelconque
    const auto a = suggestBlend(target, chart.id);
    const auto b = suggestBlend(target, chart.id);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->a.code == b->a.code);
    CHECK(a->b.code == b->b.code);
    CHECK(a->ratioB == b->ratioB);
    CHECK(a->ratioB >= 0.1);
    CHECK(a->ratioB <= 0.9);
    CHECK(a->mixedError >= 0.0);
    CHECK(a->singleError >= 0.0);
    CHECK(a->summary().find("%") != std::string::npos);
    // Une cible qui est exactement un fil : un fil seul est à 0, le mélange ne peut pas faire
    // mieux.
    const auto exact = searchThreads(chart.id, "", 1).front();
    const auto s = suggestBlend(exact.rgb, chart.id);
    REQUIRE(s.has_value());
    CHECK(s->singleError == Catch::Approx(0.0).margin(1e-9));
    CHECK_FALSE(s->betterThanSingle());
    CHECK_FALSE(suggestBlend(target, "no-such-chart").has_value());
}
