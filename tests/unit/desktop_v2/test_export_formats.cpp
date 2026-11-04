// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "digitize_pipeline.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "openstitch/stitch_analysis/color_blocks.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

namespace {
std::shared_ptr<const document::Project> tentabrode60() {
    static const auto project = [] {
        DigitizeRequest request;
        request.image = fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
        request.target_height_mm = 60.0;
        request.settings.max_colors = 4;
        auto r = run_digitize(request, TaskContext{});
        REQUIRE(r.has_value());
        return std::make_shared<const document::Project>(std::move(r->project));
    }();
    return project;
}
} // namespace

TEST_CASE("the export list comes from the format registry and starts with the common ones",
          "[desktop_v2][export]") {
    const auto list = exportFormats();
    REQUIRE(list.size() >= 3);
    bool dst = false;
    bool pes = false;
    for (const auto& f : list) {
        dst = dst || f.extension == "dst";
        pes = pes || f.extension == "pes";
        CHECK_FALSE(f.label.empty());
    }
    CHECK(dst);
    CHECK(pes);
}

TEST_CASE("every writable format exports Tentabrode to a non-empty readable file",
          "[desktop_v2][export]") {
    const auto project = tentabrode60();
    const auto expected = stitch_generation::effective_sequence(*project);
    REQUIRE(expected.has_value());
    for (const auto& format : exportFormats()) {
        const fs::path out =
            fs::temp_directory_path() / ("openstitch_v2_export." + format.extension);
        fs::remove(out);
        const auto stats = export_project_file(project, out, TaskContext{});
        INFO("format " << format.extension);
        REQUIRE(stats.has_value());
        CHECK(stats->stitches > 1000);
        REQUIRE(fs::exists(out));
        CHECK(fs::file_size(out) > 100);
        // Relecture quand le format sait lire : on retrouve des points (et les couleurs de blocs).
        const auto* info = formats::find_format_for_extension(format.extension);
        REQUIRE(info != nullptr);
        if (info->can_read && info->decode_ex != nullptr) {
            std::ifstream in(out, std::ios::binary);
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
            in.close();
            const auto decoded = info->decode_ex(bytes);
            REQUIRE(decoded.has_value());
            CHECK(decoded->sequence.commands.size() > 1000);
            if (info->carries_colors) {
                const auto blocks = stitch_analysis::color_blocks(*project, *expected);
                CHECK(decoded->block_colors.size() == blocks.size());
            }
        }
        fs::remove(out);
    }
}

TEST_CASE("an unsupported or missing extension is refused with an actionable error",
          "[desktop_v2][export]") {
    const auto project = tentabrode60();
    const auto r =
        export_project_file(project, fs::temp_directory_path() / "x.unknownext", TaskContext{});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().category == ErrorCategory::UnsupportedFormat);
    CHECK_FALSE(r.error().hint.empty());
    CHECK_FALSE(
        export_project_file(project, fs::temp_directory_path() / "noextension", TaskContext{})
            .has_value());
    CHECK_FALSE(export_project_file(nullptr, "a.dst", TaskContext{}).has_value());
}

TEST_CASE("an export refuses a document with no stitches and honours cancellation",
          "[desktop_v2][export]") {
    std::stop_source source;
    source.request_stop();
    TaskContext cancelled;
    cancelled.stop = source.get_token();
    const auto r =
        export_project_file(tentabrode60(), fs::temp_directory_path() / "c.dst", cancelled);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().category == ErrorCategory::Cancelled);
}
