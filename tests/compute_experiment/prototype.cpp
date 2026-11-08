// SPDX-License-Identifier: Apache-2.0
#include "d3d11_backend.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

using namespace openstitch::compute_experiment;
using Clock = std::chrono::steady_clock;
using nlohmann::json;
using Mask = std::vector<std::uint8_t>;

struct Scenario {
    const char* name;
    int width;
    int height;
    int count;
    Method method;
    int diameter = 0;
};

constexpr std::array scenarios{
    Scenario{"marine-open", 320, 240, 1, Method::OpenEllipse, 3},
    Scenario{"text-distance", 300, 260, 22, Method::DistanceL2Mask3},
    Scenario{"tentabrode-distance", 320, 241, 106, Method::DistanceL2Mask3},
};

double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

Mask make_mask(int width, int height, int phase) {
    Mask result(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Rounded motifs separated by a fine channel, with an off-centre hole and a thin
            // connector. Some components intersect the frame.
            const int u = (x + phase * 7) % 89 - 43;
            const int v = (y + phase * 11) % 67 - 32;
            const bool outer = 2 * u * u + 3 * v * v < 2'850;
            const bool hole = (u - 6) * (u - 6) + (v + 4) * (v + 4) < 110;
            const bool line = v == u - 9 && u < -12;
            if ((outer && !hole) || line) {
                constexpr std::array<std::uint8_t, 3> shades{255, 17, 1};
                const auto shade = static_cast<std::size_t>((phase + x / 31 + y / 19) % 3);
                result[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                       static_cast<std::size_t>(x)] = shades[shade];
            }
        }
    }
    return result;
}

BackendOutput checked_run(const Request& request, Mode mode, const GpuBackend& gpu) {
    auto result = execute(request, {.mode = mode}, gpu);
    if (!result || result->revision != request.mask.revision ||
        result->backend != (mode == Mode::Gpu ? Backend::Gpu : Backend::Cpu)) {
        const auto reason = result ? "unexpected backend or revision"
                                   : "error " + std::to_string(static_cast<int>(result.error()));
        throw std::runtime_error("compute request failed: " + std::string(reason));
    }
    return std::move(result->output);
}

bool identical(const Pixels& cpu, const Pixels& gpu) {
    if (const auto* mask = std::get_if<Mask>(&cpu)) {
        return *mask == std::get<Mask>(gpu);
    }
    const auto& left = std::get<std::vector<float>>(cpu);
    const auto& right = std::get<std::vector<float>>(gpu);
    return std::memcmp(left.data(), right.data(), left.size() * sizeof(float)) == 0;
}

void verify(const Request& request, const GpuBackend& gpu) {
    const auto cpu = checked_run(request, Mode::Cpu, gpu);
    const auto device = checked_run(request, Mode::Gpu, gpu);
    if (!identical(cpu.pixels, device.pixels)) {
        throw std::runtime_error(
            "CPU/GPU difference at revision " + std::to_string(request.mask.revision) + " for " +
            std::to_string(request.mask.width) + "x" + std::to_string(request.mask.height));
    }
}

int verify_edges(const GpuBackend& gpu) {
    int verified = 0;
    for (std::uint16_t bits = 0; bits < 512; ++bits) {
        std::array<std::uint8_t, 9> pixels{};
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            pixels[i] = ((bits >> i) & 1u) == 0 ? 0 : 255;
        }
        const MaskSnapshot mask{3, 3, bits, pixels};
        verify({mask, Method::OpenEllipse, 2}, gpu);
        verify({mask, Method::OpenEllipse, 3}, gpu);
        verify({mask, Method::DistanceL2Mask3}, gpu);
        verified += 3;
    }
    constexpr std::array sizes{std::pair{1, 2048}, std::pair{2048, 1}, std::pair{7, 8},
                               std::pair{9, 15},   std::pair{16, 17},  std::pair{257, 193},
                               std::pair{512, 511}};
    std::uint64_t revision = 512;
    for (const auto& [width, height] : sizes) {
        auto pixels = make_mask(width, height, 1);
        const MaskSnapshot mask{width, height, revision++, pixels};
        verify({mask, Method::OpenEllipse, 4}, gpu);
        verify({mask, Method::DistanceL2Mask3}, gpu);
        verified += 2;
    }
    for (const auto axis : {1499, 2048}) {
        const auto pixels = make_mask(axis, axis, 1);
        verify({{axis, axis, revision++, pixels}, Method::OpenEllipse, 4}, gpu);
        ++verified;
    }
    Mask solid(33 * 35, 1);
    const MaskSnapshot uniform{33, 35, revision++, solid};
    verify({uniform, Method::DistanceL2Mask3}, gpu);
    solid.front() = 0;
    verify({{33, 35, revision++, solid}, Method::DistanceL2Mask3}, gpu);
    verify({{33, 35, revision++, solid}, Method::OpenEllipse, kMaxDiameter}, gpu);
    return verified + 3;
}

void verify_work_guard(const GpuBackend& gpu) {
    Mask pixels(static_cast<std::size_t>(kMaxAxis) * kMaxAxis, 255);
    pixels.front() = 0;
    const MaskSnapshot mask{kMaxAxis, kMaxAxis, 44, pixels};
    const Request distance{mask, Method::DistanceL2Mask3};
    const Request opening{mask, Method::OpenEllipse, kMaxDiameter};
    for (const auto& request : {distance, opening}) {
        const auto forced = execute(request, {.mode = Mode::Gpu}, gpu);
        if (forced || forced.error() != Error::Unsupported) {
            throw std::runtime_error("oversized GPU dispatch was not refused");
        }
    }
    const auto automatic = execute(distance, {}, gpu);
    const auto cpu = checked_run(distance, Mode::Cpu, gpu);
    if (!automatic || automatic->backend != Backend::Cpu ||
        automatic->fallback_reason != Error::Unsupported ||
        !identical(automatic->output.pixels, cpu.pixels)) {
        throw std::runtime_error("oversized GPU dispatch did not fall back to the CPU");
    }
}

void add_timing(std::optional<Duration>& total, std::optional<Duration> sample) {
    if (total && sample) {
        *total += *sample;
    } else {
        total.reset();
    }
}

json timing_value(std::optional<Duration> duration) {
    return duration ? json(milliseconds(*duration)) : json(nullptr);
}

void measure(const Scenario& scenario, const std::vector<Mask>& masks,
             const std::vector<Pixels>& reference, Mode mode, const GpuBackend& gpu, int rep) {
    std::optional<Duration> upload{Duration{}};
    std::optional<Duration> dispatch{Duration{}};
    std::optional<Duration> readback{Duration{}};
    std::size_t uploadBytes = 0;
    std::size_t downloadBytes = 0;
    std::size_t peakWorkingBytes = 0;
    std::vector<Pixels> results;
    results.reserve(masks.size());
    const auto start = Clock::now();
    for (std::size_t i = 0; i < masks.size(); ++i) {
        const Request request{
            {scenario.width, scenario.height, i, masks[i]}, scenario.method, scenario.diameter};
        auto result = checked_run(request, mode, gpu);
        const auto& telemetry = result.telemetry;
        add_timing(upload, telemetry.upload_elapsed);
        add_timing(dispatch, telemetry.dispatch_elapsed);
        add_timing(readback, telemetry.readback_elapsed);
        uploadBytes += telemetry.upload_bytes;
        downloadBytes += telemetry.download_bytes;
        peakWorkingBytes = std::max(peakWorkingBytes, telemetry.working_bytes);
        results.push_back(std::move(result.pixels));
    }
    const auto wall = milliseconds(Clock::now() - start);
    const char* backend = mode == Mode::Cpu ? "cpu" : "gpu";
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (!identical(reference[i], results[i])) {
            throw std::runtime_error(std::string("unstable result for ") + scenario.name + " " +
                                     backend + " run " + std::to_string(rep) + " mask " +
                                     std::to_string(i));
        }
    }
    std::cout << json{{"kind", "sample"},
                      {"scenario", scenario.name},
                      {"mode", backend},
                      {"run", rep},
                      {"calls", masks.size()},
                      {"bit_exact", true},
                      {"width", scenario.width},
                      {"height", scenario.height},
                      {"wall_ms", wall},
                      {"upload_ms", timing_value(upload)},
                      {"dispatch_ms", timing_value(dispatch)},
                      {"readback_ms", timing_value(readback)},
                      {"upload_bytes", uploadBytes},
                      {"download_bytes", downloadBytes},
                      {"peak_working_bytes", peakWorkingBytes}}
                     .dump()
              << '\n';
}

void verify_fallback() {
    const std::array<std::uint8_t, 4> input{0, 255, 255, 0};
    const Request request{{2, 2, 42, input}, Method::DistanceL2Mask3};
    const auto result = execute(request);
    if (!result || result->backend != Backend::Cpu ||
        result->fallback_reason != Error::Unavailable ||
        result->revision != request.mask.revision) {
        throw std::runtime_error("missing CPU fallback when the GPU adapter is unavailable");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        constexpr int repetitions = 21;
        const bool cpuOnly = argc == 2 && std::string_view(argv[1]) == "--cpu-only";
        if (argc != 1 && !cpuOnly) {
            throw std::invalid_argument("usage: openstitch-compute-prototype [--cpu-only]");
        }
        verify_fallback();
        if (cpuOnly) {
            std::cout << "{\"kind\":\"parity\",\"cpu_fallback\":true}\n";
            return 0;
        }
        const auto started = Clock::now();
        const auto adapter = make_d3d11_adapter();
        if (!adapter) {
            throw std::runtime_error("D3D11 adapter unavailable: " +
                                     std::to_string(static_cast<int>(adapter.error())));
        }
        const auto init = milliseconds(Clock::now() - started);
        const auto first = make_mask(320, 240, 0);
        const Request firstRequest{{320, 240, 0, first}, Method::OpenEllipse, 3};
        const auto firstCall = Clock::now();
        const auto firstGpu = checked_run(firstRequest, Mode::Gpu, adapter->run);
        const auto firstLatency = milliseconds(Clock::now() - firstCall);
        const auto firstCpu = checked_run(firstRequest, Mode::Cpu, adapter->run);
        if (!identical(firstCpu.pixels, firstGpu.pixels)) {
            throw std::runtime_error("CPU/GPU difference for first morphological opening");
        }
        std::cout << json{{"kind", "device"},
                          {"adapter", adapter->name},
                          {"init_ms", init},
                          {"first_gpu_wall_ms", firstLatency}}
                         .dump()
                  << '\n';
        const int edgeCases = verify_edges(adapter->run);
        verify_work_guard(adapter->run);
        int batches = 0;
        for (const auto& scenario : scenarios) {
            std::vector<Mask> masks;
            std::vector<Pixels> reference;
            masks.reserve(static_cast<std::size_t>(scenario.count));
            reference.reserve(static_cast<std::size_t>(scenario.count));
            for (int i = 0; i < scenario.count; ++i) {
                masks.push_back(make_mask(scenario.width, scenario.height, i));
                const Request request{
                    {scenario.width, scenario.height, static_cast<std::uint64_t>(i), masks.back()},
                    scenario.method,
                    scenario.diameter};
                reference.push_back(checked_run(request, Mode::Cpu, adapter->run).pixels);
                ++batches;
            }
            for (int rep = 0; rep < repetitions; ++rep) {
                // Alternate order to avoid giving one backend the same position in every pair.
                const auto firstMode = rep % 2 == 0 ? Mode::Cpu : Mode::Gpu;
                const auto secondMode = firstMode == Mode::Cpu ? Mode::Gpu : Mode::Cpu;
                measure(scenario, masks, reference, firstMode, adapter->run, rep);
                measure(scenario, masks, reference, secondMode, adapter->run, rep);
            }
        }
        std::cout << json{{"kind", "parity"},
                          {"edge_cases", edgeCases},
                          {"batch_masks", batches},
                          {"cpu_fallback", true},
                          {"work_guard", true}}
                         .dump()
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
