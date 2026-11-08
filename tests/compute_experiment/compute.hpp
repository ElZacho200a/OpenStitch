// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <variant>
#include <vector>

namespace openstitch::compute_experiment {

inline constexpr int kMaxAxis = 2048;
inline constexpr int kMaxDiameter = 63;
inline constexpr std::size_t kDefaultBudgetBytes = 128 * 1024 * 1024;

enum class Method { OpenEllipse, DistanceL2Mask3 };
enum class Mode { Auto, Cpu, Gpu };
enum class Backend { Cpu, Gpu };
enum class Error {
    InvalidInput,
    Cancelled,
    Unavailable,
    Unsupported,
    BudgetExceeded,
    DriverFailure,
    CpuFailure,
};

// The caller keeps the tightly packed, row-major pixels alive and immutable for the call.
// All 256 byte values are valid. Opening works in grayscale; distance treats all nonzero
// values as foreground.
struct MaskSnapshot {
    int width;
    int height;
    std::uint64_t revision;
    std::span<const std::uint8_t> pixels;
};

struct Request {
    MaskSnapshot mask;
    Method method;
    // An OpenCV ellipse centered at diameter / 2 (including for even diameters).
    // The distance method requires zero here.
    int diameter{0};
};

struct Options {
    Mode mode{Mode::Auto};
    std::size_t cpu_budget_bytes{kDefaultBudgetBytes};
    std::size_t gpu_budget_bytes{kDefaultBudgetBytes};
};

using Duration = std::chrono::nanoseconds;

struct Telemetry {
    // Total host time, including any failed GPU attempt and subsequent CPU reprocessing.
    Duration elapsed{};
    std::optional<Duration> upload_elapsed;
    std::optional<Duration> dispatch_elapsed;
    std::optional<Duration> readback_elapsed;
    std::size_t upload_bytes{0};
    std::size_t download_bytes{0};
    // Accounted live buffers for the effective backend, including input and output.
    // CPU scratch is an estimate.
    std::size_t working_bytes{0};
};

// OpenEllipse returns bytes with OpenCV's default morphology border behavior.
// DistanceL2Mask3 returns distances in pixels to the nearest zero-valued sample.
using Pixels = std::variant<std::vector<std::uint8_t>, std::vector<float>>;

struct BackendOutput {
    Pixels pixels;
    Telemetry telemetry;
};

struct Result {
    std::uint64_t revision;
    Backend backend;
    std::optional<Error> fallback_reason;
    BackendOutput output;
};

// The synchronous adapter owns its resources. It reports unsupported capabilities or
// budget overruns before allocation and driver failures as errors.
using GpuBackend = std::function<std::expected<BackendOutput, Error>(
    const Request&, std::size_t budget_bytes, std::stop_token)>;

// CPU runs without an adapter. Auto tries the adapter, then CPU on recoverable backend errors.
// Cancellation is checked before and after each backend call. An active OpenCV call runs to
// completion, and its result is discarded if cancellation was requested.
[[nodiscard]] std::expected<Result, Error> execute(const Request& request,
                                                   const Options& options = {},
                                                   const GpuBackend& gpu = {},
                                                   std::stop_token stop = {});

} // namespace openstitch::compute_experiment
