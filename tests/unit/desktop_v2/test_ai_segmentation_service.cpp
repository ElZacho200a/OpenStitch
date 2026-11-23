// SPDX-License-Identifier: Apache-2.0
// Segmentation IA du Studio : repli explicite sur CIELAB, jamais d'échec silencieux.
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "ai_segmentation_service.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/image/image.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {

document::ImageSource threeBands() {
    document::ImageSource source;
    auto& img = source.original;
    img.width = 60;
    img.height = 40;
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 255);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            auto* p = &img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4];
            const int band = x / 20;
            p[0] = band == 0 ? 220 : 20;
            p[1] = band == 1 ? 200 : 20;
            p[2] = band == 2 ? 220 : 20;
            p[3] = 255;
        }
    }
    return source;
}

bool has(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

std::filesystem::path writeSettings(const char* name, const std::string& json) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << json;
    return path;
}

} // namespace

TEST_CASE("classical engine segments by CIELAB colour without any backend") {
    AiSegmentationRequest request;
    request.engine = kAiEngineClassical;
    request.max_colors = 3;
    const auto out = segmentWithAi(threeBands(), request, TaskContext{});
    REQUIRE(out.has_value());
    CHECK_FALSE(out->used_backend);
    CHECK(out->notice.empty());
    CHECK(out->segmentation.region_count() >= 3);
}

TEST_CASE("an undeclared backend falls back to CIELAB with the reason") {
    AiSegmentationRequest request;
    request.settings_file = std::filesystem::temp_directory_path() / "os_absent_ai_backends.json";
    request.max_colors = 3;
    const auto out = segmentWithAi(threeBands(), request, TaskContext{});
    REQUIRE(out.has_value());
    CHECK_FALSE(out->used_backend);
    CHECK(has(out->notice, "CIELAB"));
    CHECK(out->segmentation.region_count() >= 3);
}

TEST_CASE("a backend that cannot start falls back with its name in the reason") {
    const auto settings = writeSettings(
        "os_ai_service_backends.json",
        R"({"schema":"openstitch.ai.backends/1","preferred":"fake","backends":[{"id":"fake","display_name":"Faux SAM","command":"python","args":[]}]})");
    AiSegmentationRequest request;
    request.settings_file = settings;
    request.max_colors = 3;
    request.job_root = std::filesystem::temp_directory_path() / "os_ai_service_jobs";
    const auto factory = [](const ai_segmentation::BackendConfig&)
        -> ai_segmentation::AiResult<std::unique_ptr<ai_segmentation::ITransport>> {
        return std::unexpected(ai_segmentation::AiError{
            ai_segmentation::AiErrorCode::WorkerStartFailed, "introuvable", "x"});
    };
    const auto out = segmentWithAi(threeBands(), request, TaskContext{}, factory);
    std::filesystem::remove(settings);
    std::filesystem::remove_all(request.job_root);
    REQUIRE(out.has_value());
    CHECK_FALSE(out->used_backend);
    CHECK(has(out->notice, "Faux SAM"));
    CHECK(has(out->notice, "CIELAB"));
    CHECK(out->segmentation.region_count() >= 3);
}

TEST_CASE("a cancelled task stops before any work") {
    std::stop_source source;
    source.request_stop();
    TaskContext task;
    task.stop = source.get_token();
    AiSegmentationRequest request;
    request.engine = kAiEngineClassical;
    const auto out = segmentWithAi(threeBands(), request, task);
    REQUIRE_FALSE(out.has_value());
    CHECK(out.error().category == ErrorCategory::Cancelled);
}

// Recette avec un VRAI backend : activée seulement si OPENSTITCH_AI_TEST_SETTINGS désigne un
// fichier de backends et OPENSTITCH_AI_TEST_ENGINE le backend à lancer (ex. « fake », « sam2 »).
TEST_CASE("real backend end to end (opt-in)", "[.integration]") {
    const char* settings = std::getenv("OPENSTITCH_AI_TEST_SETTINGS");
    const char* engine = std::getenv("OPENSTITCH_AI_TEST_ENGINE");
    if (settings == nullptr || engine == nullptr) {
        SKIP("OPENSTITCH_AI_TEST_SETTINGS / OPENSTITCH_AI_TEST_ENGINE non définis");
    }
    AiSegmentationRequest request;
    request.settings_file = settings;
    request.engine = engine;
    request.max_colors = 4;
    const auto out = segmentWithAi(threeBands(), request, TaskContext{});
    REQUIRE(out.has_value());
    INFO("notice: " << out->notice);
    CHECK(out->used_backend);
    CHECK(out->mask_count > 0);
    CHECK(out->segmentation.region_count() >= 1);
}
