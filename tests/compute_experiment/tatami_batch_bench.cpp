// SPDX-License-Identifier: Apache-2.0
#include "tatami_d3d11.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <new>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "openstitch/formats/dst.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/experimental_tatami_scan.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace {

using namespace openstitch;
using namespace openstitch::stitch_generation::experimental;
using openstitch::compute_experiment::make_tatami_d3d11_adapter;
using openstitch::compute_experiment::TatamiBatchJob;
using openstitch::compute_experiment::TatamiD3d11Adapter;
using Clock = std::chrono::steady_clock;
using nlohmann::json;

enum class Mode { Scan, Indexed, Gpu };

const char* mode_name(Mode mode) {
    switch (mode) {
    case Mode::Scan:
        return "cpu_scan";
    case Mode::Indexed:
        return "cpu_indexed";
    case Mode::Gpu:
        return "gpu";
    }
    throw std::logic_error("unknown scanline mode");
}

struct Work {
    std::vector<TatamiBatchJob> jobs;
    std::uint32_t count{0};
};

std::optional<Work> prepare(std::span<const double> rows, std::span<const TatamiEdge> edges) {
    constexpr std::uint32_t maxCrossings = 4 * 1024 * 1024;
    Work work;
    work.jobs.reserve(edges.size());
    for (const auto& edge : edges) {
        const auto [low, high] = std::minmax(edge.y0, edge.y1);
        if (low == high) {
            continue;
        }
        const auto first = std::lower_bound(rows.begin(), rows.end(), low);
        const auto end = std::lower_bound(first, rows.end(), high);
        const auto count = static_cast<std::uint32_t>(end - first);
        if (count == 0) {
            continue;
        }
        if (count > maxCrossings - work.count) {
            return std::nullopt;
        }
        work.jobs.push_back({edge.x0, edge.y0, edge.x1 - edge.x0, edge.y1 - edge.y0,
                             static_cast<std::uint32_t>(first - rows.begin()), count, work.count});
        work.count += count;
    }
    return work;
}

std::vector<double> indexed_cpu(std::span<const double> rows, const Work& work) {
    std::vector<double> result(work.count);
    for (const auto& job : work.jobs) {
        for (std::uint32_t i = 0; i < job.row_count; ++i) {
            const double t = (rows[job.first_row + i] - job.y) / job.dy;
            result[job.output_offset + i] = job.x + t * job.dx;
        }
    }
    return result;
}

struct Stats {
    std::atomic<std::uint64_t> batches{0};
    std::atomic<std::uint64_t> rows{0};
    std::atomic<std::uint64_t> edges{0};
    std::atomic<std::uint64_t> crossings{0};
    std::atomic<std::uint64_t> gpu_calls{0};
    std::atomic<std::uint64_t> gpu_ns{0};
    std::atomic<std::uint64_t> fallback{0};
    std::atomic<std::uint64_t> verified_crossings{0};
    std::atomic<std::uint64_t> abscissa_mismatches{0};
    std::atomic<std::uint64_t> max_error_bits{0};
};

void compare_abscissas(std::span<const double> cpu, std::span<const double> gpu, Stats& stats) {
    std::uint64_t mismatches = 0;
    double error = 0.0;
    for (std::size_t i = 0; i < cpu.size(); ++i) {
        mismatches += std::bit_cast<std::uint64_t>(cpu[i]) != std::bit_cast<std::uint64_t>(gpu[i]);
        error = std::max(error, std::abs(cpu[i] - gpu[i]));
    }
    stats.verified_crossings += cpu.size();
    stats.abscissa_mismatches += mismatches;
    // Bit order agrees with numeric order for these nonnegative finite doubles.
    const auto bits = std::bit_cast<std::uint64_t>(error);
    auto old = stats.max_error_bits.load(std::memory_order_relaxed);
    while (old < bits &&
           !stats.max_error_bits.compare_exchange_weak(old, bits, std::memory_order_relaxed)) {
    }
}

std::optional<TatamiRows> compute_batch(std::span<const double> rows,
                                        std::span<const TatamiEdge> edges, Mode mode,
                                        const TatamiD3d11Adapter* adapter, Stats& stats,
                                        bool verify) {
    ++stats.batches;
    stats.rows += rows.size();
    stats.edges += edges.size();
    auto work = prepare(rows, edges);
    if (!work) {
        return std::nullopt;
    }
    stats.crossings += work->count;
    std::vector<double> packed;
    if (mode == Mode::Gpu && work->count != 0) {
        ++stats.gpu_calls;
        const auto started = Clock::now();
        auto gpu = adapter->run(rows, work->jobs, work->count);
        stats.gpu_ns += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
        if (!gpu || gpu->size() != work->count ||
            !std::ranges::all_of(*gpu, [](double x) { return std::isfinite(x); })) {
            return std::nullopt;
        }
        packed = std::move(*gpu);
        if (verify) {
            const auto cpu = indexed_cpu(rows, *work);
            compare_abscissas(cpu, packed, stats);
        }
    } else {
        packed = indexed_cpu(rows, *work);
    }

    TatamiRows result(rows.size());
    for (const auto& job : work->jobs) {
        for (std::uint32_t i = 0; i < job.row_count; ++i) {
            result[job.first_row + i].push_back(packed[job.output_offset + i]);
        }
    }
    return result;
}

struct ScanScope {
    explicit ScanScope(const TatamiScan* scan) : previous(set_tatami_scan(scan)) {}
    ~ScanScope() { set_tatami_scan(previous); }
    ScanScope(const ScanScope&) = delete;
    ScanScope& operator=(const ScanScope&) = delete;
    const TatamiScan* previous;
};

stitch::StitchSequence generate(const document::Project& project, Mode mode,
                                const TatamiD3d11Adapter* adapter, Stats& stats, double& wallMs,
                                bool verify = false) {
    const TatamiScan scan{
        [&](auto rows, auto edges) {
            return compute_batch(rows, edges, mode, adapter, stats, verify);
        },
        [&] { ++stats.fallback; },
    };
    const ScanScope scope(mode == Mode::Scan ? nullptr : &scan);
    const auto start = Clock::now();
    auto result = stitch_generation::effective_sequence(project);
    wallMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    if (!result) {
        throw std::runtime_error(result.error().message);
    }
    if (mode != Mode::Scan && (stats.batches == 0 || stats.fallback != 0 ||
                               (mode == Mode::Gpu && stats.gpu_calls == 0))) {
        throw std::runtime_error(std::string(mode_name(mode)) + " did not execute every batch");
    }
    return std::move(*result);
}

geometry::Path contour(Vec2um center, Micrometers radius, int nodes, double relief) {
    geometry::Path path;
    path.nodes.reserve(static_cast<std::size_t>(nodes));
    for (int i = 0; i < nodes; ++i) {
        const double angle = 2 * std::numbers::pi * i / nodes;
        const double r = radius.value * (1 + relief * std::sin(27 * angle));
        path.nodes.push_back(
            {.pos =
                 center +
                 Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(r * std::cos(angle)))},
                        Micrometers{static_cast<std::int32_t>(std::lround(r * std::sin(angle)))}}});
    }
    return path;
}

document::Project stress_project() {
    document::Project project;
    geometry::PathSet shape{.outer = contour({}, Micrometers{145'000}, 2048, 0.025)};
    for (const int x : {-60'000, 0, 60'000}) {
        for (const int y : {-60'000, 0, 60'000}) {
            shape.holes.push_back(
                contour({Micrometers{x}, Micrometers{y}}, Micrometers{11'000}, 128, 0.01));
        }
    }
    document::VectorObject vector;
    vector.id = project.object_ids.next();
    vector.paths.push_back(std::move(shape));
    document::EmbroideryObject fill;
    fill.id = project.object_ids.next();
    fill.source_vector = vector.id;
    document::TatamiParams params;
    params.angle = Angle{0.31};
    fill.params = params;
    project.vector_objects.push_back(std::move(vector));
    project.embroidery_objects.push_back(std::move(fill));
    return project;
}

document::Project image_project(const char* path, double dpi) {
    document::Project project;
    project_ops::DigitizeSettings settings;
    settings.dpi = dpi;
    settings.pre = "median:1";
    const auto digitized = project_ops::digitize_image_into(project, path, settings);
    if (!digitized) {
        throw std::runtime_error(digitized.error().message);
    }
    return project;
}

void verify_sequence(const stitch::StitchSequence& reference, const stitch::StitchSequence& seq,
                     Mode mode) {
    if (seq.commands != reference.commands) {
        throw std::runtime_error(std::string("effective commands differ in ") + mode_name(mode));
    }
}

std::uint64_t verify_fallback(const document::Project& project,
                              const stitch::StitchSequence& reference) {
    std::atomic<std::uint64_t> fallbacks{0};
    const TatamiScan reject{
        [](auto, auto) -> std::optional<TatamiRows> { throw std::bad_alloc{}; },
        [&] { ++fallbacks; },
    };
    const ScanScope scope(&reject);
    const auto result = stitch_generation::effective_sequence(project);
    if (!result || result->commands != reference.commands || fallbacks == 0) {
        throw std::runtime_error("experimental tatami fallback differs from CPU scan");
    }
    return fallbacks.load();
}

} // namespace

int main(int argc, char** argv) {
    try {
        const bool stress = argc == 3 && std::string_view(argv[1]) == "--stress";
        if (!stress && argc != 4) {
            throw std::invalid_argument(
                "usage: openstitch-tatami-batch IMAGE DPI REPS | --stress REPS");
        }
        const int repetitions = std::stoi(argv[stress ? 2 : 3]);
        const double dpi = stress ? 96.0 : std::stod(argv[2]);
        if (repetitions < 2 || repetitions > 200 || !std::isfinite(dpi) || dpi <= 0.0) {
            throw std::invalid_argument("invalid repetitions or DPI");
        }
        const auto project = stress ? stress_project() : image_project(argv[1], dpi);
        stitch_generation::set_chunk_cache_enabled(false);
        Stats baselineStats;
        double coldCpu = 0.0;
        const auto reference = generate(project, Mode::Scan, nullptr, baselineStats, coldCpu);
        const auto stitchStats = stitch::compute_stats(reference);
        if (stitchStats.stitches == 0) {
            throw std::runtime_error("empty stitch sequence");
        }
        const auto expectedDst = formats::encode_dst(reference);
        if (!expectedDst) {
            throw std::runtime_error(expectedDst.error().message);
        }
        const auto init = Clock::now();
        const auto gpu = make_tatami_d3d11_adapter();
        const auto initMs = std::chrono::duration<double, std::milli>(Clock::now() - init).count();
        const std::array modes{Mode::Scan, Mode::Indexed, Mode::Gpu};
        auto count = gpu ? modes.size() : modes.size() - 1;
        std::cout << json{{"kind", "scenario"},
                          {"adapter", gpu ? json(gpu->name) : json(nullptr)},
                          {"adapter_error",
                           gpu ? json(nullptr) : json(static_cast<int>(gpu.error()))},
                          {"init_ms", initMs},
                          {"cold_cpu_ms", coldCpu},
                          {"objects", project.embroidery_objects.size()},
                          {"stitches", stitchStats.stitches},
                          {"jumps", stitchStats.jumps},
                          {"trims", stitchStats.trims},
                          {"commands", reference.commands.size()},
                          {"verified_fallbacks", verify_fallback(project, reference)},
                          {"fingerprint",
                           std::to_string(stitch_generation::fingerprint(reference.commands))}}
                         .dump()
                  << '\n';

        for (std::size_t i = 0; i < count; ++i) {
            Stats verification;
            double wall = 0.0;
            try {
                const auto seq =
                    generate(project, modes[i], gpu ? &*gpu : nullptr, verification, wall, true);
                verify_sequence(reference, seq, modes[i]);
                const auto dst = formats::encode_dst(seq);
                if (!dst || *dst != *expectedDst) {
                    throw std::runtime_error(std::string("DST differs in ") + mode_name(modes[i]));
                }
            } catch (const std::runtime_error& error) {
                if (modes[i] != Mode::Gpu) {
                    throw;
                }
                std::cout << json{{"kind", "gpu_rejected"},
                                  {"reason", error.what()},
                                  {"abscissas_compared", verification.verified_crossings.load()},
                                  {"abscissa_bit_mismatches",
                                   verification.abscissa_mismatches.load()},
                                  {"max_abscissa_error_um",
                                   std::bit_cast<double>(verification.max_error_bits.load())}}
                                 .dump()
                          << '\n';
                --count;
                break;
            }
            std::cout << json{{"kind", "verification"},
                              {"mode", mode_name(modes[i])},
                              {"dst_identical", true},
                              {"abscissas_compared", verification.verified_crossings.load()},
                              {"abscissa_bit_mismatches", verification.abscissa_mismatches.load()},
                              {"max_abscissa_error_um",
                               std::bit_cast<double>(verification.max_error_bits.load())}}
                             .dump()
                      << '\n';
        }
        for (int rep = 0; rep < repetitions; ++rep) {
            for (std::size_t i = 0; i < count; ++i) {
                const auto mode = modes[(static_cast<std::size_t>(rep) + i) % count];
                Stats stats;
                double wall = 0.0;
                const auto seq = generate(project, mode, gpu ? &*gpu : nullptr, stats, wall);
                verify_sequence(reference, seq, mode);
                std::cout << json{{"kind", "sample"},
                                  {"mode", mode_name(mode)},
                                  {"run", rep},
                                  {"wall_ms", wall},
                                  {"batches", stats.batches.load()},
                                  {"rows", stats.rows.load()},
                                  {"edges", stats.edges.load()},
                                  {"crossings", stats.crossings.load()},
                                  {"gpu_calls", stats.gpu_calls.load()},
                                  {"sum_adapter_ms",
                                   static_cast<double>(stats.gpu_ns.load()) / 1e6},
                                  {"identical_commands", true}}
                                 .dump()
                          << '\n';
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
