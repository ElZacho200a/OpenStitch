// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "openstitch/core/pipeline_profile.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_generation/pull_map.hpp"
#include "openstitch/stitch_render/presentation.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
// clang-format off
#include <psapi.h>
// clang-format on
#elif defined(__linux__)
#include <sys/resource.h>
#endif

namespace {

using namespace openstitch;
using Clock = std::chrono::steady_clock;
using nlohmann::json;

struct Config {
    std::filesystem::path image;
    int reps{};
    project_ops::DigitizeSettings digitize;
    bool linen{false};
    bool instrument{true};
};

Config parse(int argc, char** argv) {
    if (argc < 4) {
        throw std::invalid_argument("usage: openstitch-profile-engine IMAGE DPI REPS "
                                    "[--satin-bands] [--dark-lines] [--linen] [--no-instrument]");
    }
    Config config;
    config.image = argv[1];
    std::size_t end{};
    config.digitize.dpi = std::stod(argv[2], &end);
    if (end != std::string_view(argv[2]).size() || !std::isfinite(config.digitize.dpi) ||
        config.digitize.dpi <= 0.0) {
        throw std::invalid_argument("DPI must be finite and positive");
    }
    config.reps = std::stoi(argv[3], &end);
    if (end != std::string_view(argv[3]).size() || config.reps <= 0) {
        throw std::invalid_argument("REPS must be a positive integer");
    }
    config.digitize.pre = "median:1";
    config.digitize.hidden_travel = true;
    for (int i = 4; i < argc; ++i) {
        const std::string_view flag(argv[i]);
        if (flag == "--satin-bands") {
            config.digitize.satin_bands = true;
        } else if (flag == "--dark-lines") {
            config.digitize.dark_lines = true;
        } else if (flag == "--linen") {
            config.linen = true;
        } else if (flag == "--no-instrument") {
            config.instrument = false;
        } else {
            throw std::invalid_argument("unknown option: " + std::string(flag));
        }
    }
    return config;
}

double ms(Clock::duration elapsed) {
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

std::optional<double> process_cpu_ms() {
#ifdef _WIN32
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) == 0) {
        return std::nullopt;
    }
    const auto ticks = [](const FILETIME& time) {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10'000.0;
#else
    const auto ticks = std::clock();
    if (ticks == static_cast<std::clock_t>(-1)) {
        return std::nullopt;
    }
    return static_cast<double>(ticks) * 1000.0 / CLOCKS_PER_SEC;
#endif
}

struct Memory {
    json rss = nullptr;
    json peak = nullptr;
};

Memory process_memory() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
        return {counters.WorkingSetSize, counters.PeakWorkingSetSize};
    }
#elif defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return {nullptr, static_cast<std::uint64_t>(usage.ru_maxrss) * 1024};
    }
#endif
    return {};
}

template <typename Range> std::string hash(const Range& values) {
    std::uint64_t value = 14695981039346656037ull;
    for (const auto element : values) {
        value = (value ^ element) * 1099511628211ull;
    }
    return std::to_string(value);
}

json stage_data(const pipeline_profile::Samples& samples) {
    using pipeline_profile::Stage;
    for (const auto stage :
         {Stage::Decode, Stage::Preprocess, Stage::Segment, Stage::Digitize, Stage::Finish}) {
        const auto index = static_cast<std::size_t>(stage);
        if (samples[index].calls.load() == 0) {
            throw std::runtime_error(std::string("missing timing for ") +
                                     pipeline_profile::stage_names[index]);
        }
    }
    json result = json::object();
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& sample = samples[i];
        result[pipeline_profile::stage_names[i]] = {
            {"task_ms", static_cast<double>(sample.elapsed_ns.load()) / 1'000'000.0},
            {"calls", sample.calls.load()},
            {"upload_bytes", sample.upload_bytes.load()},
            {"download_bytes", sample.download_bytes.load()},
            {"buffers", sample.buffers.load()},
        };
    }
    return result;
}

json run(const Config& config, int index) {
    pipeline_profile::Samples samples;
    std::optional<pipeline_profile::Recording> recording;
    if (config.instrument) {
        recording.emplace(samples);
    }

    document::Project project;
    const auto cpuStart = process_cpu_ms();
    const auto started = Clock::now();
    const auto report = project_ops::digitize_image_into(project, config.image, config.digitize);
    if (!report) {
        throw std::runtime_error(report.error().message);
    }
    const auto digitized = Clock::now();
    const auto generated = stitch_generation::effective_sequence_report(project);
    if (!generated) {
        throw std::runtime_error(generated.error().message);
    }
    const auto sequenced = Clock::now();
    const auto& sequence = generated->sequence;
    stitch::StitchSequence simulated;
    if (config.linen) {
        simulated = stitch_generation::simulate_fabric_pull(
            sequence, stitch_generation::fabric_preset("linen"));
    }
    const auto simulatedAt = Clock::now();
    using Color = std::array<std::uint8_t, 3>;
    std::map<ObjectId, Color> colors;
    for (const auto& object : project.embroidery_objects) {
        if (object.visible) {
            colors.emplace(object.id, object.rgb);
        }
    }
    const auto preview = stitch_render::render_sequence(
        config.linen ? simulated : sequence,
        [&colors](ObjectId id) -> std::optional<Color> {
            const auto color = colors.find(id);
            return color == colors.end() ? std::nullopt : std::optional{color->second};
        },
        {.px_per_mm = 3.0, .max_pixels = 2'000'000});
    const auto done = Clock::now();
    const auto cpuEnd = process_cpu_ms();
    const auto memory = process_memory();
    recording.reset();

    const auto stats = stitch::compute_stats(sequence);
    if (stats.stitches == 0 || preview.rgba.empty()) {
        throw std::runtime_error("empty stitch sequence or preview");
    }
    return {
        {"run", index},
        {"wall_ms", ms(done - started)},
        {"cpu_ms", cpuStart && cpuEnd ? json(*cpuEnd - *cpuStart) : json(nullptr)},
        {"rss_bytes", memory.rss},
        {"peak_rss_bytes", memory.peak},
        {"digitize_ms", ms(digitized - started)},
        {"sequence_ms", ms(sequenced - digitized)},
        {"preview_ms", ms(done - simulatedAt)},
        {"fabric_ms", config.linen ? ms(simulatedAt - sequenced) : 0.0},
        {"image_width", report->image_width_px},
        {"image_height", report->image_height_px},
        {"regions", report->regions},
        {"objects", report->embroidery_objects},
        {"stitches", stats.stitches},
        {"jumps", stats.jumps},
        {"trims", stats.trims},
        {"trims_removed", generated->report.trims_removed},
        {"sequence_hash", std::to_string(stitch_generation::fingerprint(sequence.commands))},
        {"label_hash", hash(project.segmentation->labels)},
        {"preview_hash", hash(preview.rgba)},
        {"image_bytes", project.original.rgba.size()},
        {"preview_bytes", preview.rgba.size()},
        {"stages", config.instrument ? stage_data(samples) : json(nullptr)},
    };
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto config = parse(argc, argv);
        // Profile a full stitch rebuild on every run. Other caches stay warm after run 0.
        openstitch::stitch_generation::set_chunk_cache_enabled(false);
        for (int index = 0; index < config.reps; ++index) {
            std::cout << run(config, index).dump() << std::endl;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
