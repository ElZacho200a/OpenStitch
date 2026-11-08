// SPDX-License-Identifier: Apache-2.0
#include "compute.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

using namespace openstitch::compute_experiment;

namespace {

constexpr int kWidth = 7;
constexpr int kHeight = 6;
constexpr std::uint64_t kRevision = 42;
constexpr std::array<std::uint8_t, kWidth * kHeight> kMask{
    255, 255, 255, 0, 0,   0, 255, 255, 0, 255, 0, 0,   0,   0, 255, 255, 255, 0, 0, 0, 0,
    0,   0,   0,   0, 255, 0, 0,   0,   0, 0,   0, 255, 255, 0, 0,   0,   0,   0, 0, 0, 0,
};

Request request(Method method, int diameter = 0) {
    return {{kWidth, kHeight, kRevision, kMask}, method, diameter};
}

BackendOutput echo_output() {
    BackendOutput output{std::vector<std::uint8_t>(kMask.begin(), kMask.end()), {}};
    output.telemetry.working_bytes = kMask.size() * 2;
    return output;
}

void require_error(const std::expected<Result, Error>& result, Error error) {
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == error);
}

} // namespace

TEST_CASE("CPU compute matches elliptical OpenCV morphology", "[compute_experiment]") {
    const auto diameter = GENERATE(1, 2, 3, kMaxDiameter);
    auto sourceBytes = kMask;
    sourceBytes[0] = 180;
    const auto original = sourceBytes;
    auto input = request(Method::OpenEllipse, diameter);
    input.mask.pixels = sourceBytes;
    const auto result = execute(input, {.mode = Mode::Cpu});
    REQUIRE(result.has_value());
    CHECK(result->backend == Backend::Cpu);
    CHECK(result->revision == kRevision);
    CHECK_FALSE(result->fallback_reason);
    CHECK(result->output.telemetry.dispatch_elapsed.has_value());
    CHECK_FALSE(result->output.telemetry.upload_elapsed);
    CHECK_FALSE(result->output.telemetry.readback_elapsed);
    CHECK(result->output.telemetry.working_bytes >= kMask.size() * 2);
    CHECK(sourceBytes == original);

    const cv::Mat source(kHeight, kWidth, CV_8UC1, sourceBytes.data());
    cv::Mat reference;
    const auto kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, {diameter, diameter});
    cv::morphologyEx(source, reference, cv::MORPH_OPEN, kernel);
    const auto& values = std::get<std::vector<std::uint8_t>>(result->output.pixels);
    REQUIRE(values.size() == kMask.size());
    CHECK(std::equal(values.begin(), values.end(), reference.ptr<std::uint8_t>()));
}

TEST_CASE("CPU compute matches distance L2 mask 3 in pixels", "[compute_experiment]") {
    const auto foreground = GENERATE(false, true);
    auto sourceBytes = kMask;
    if (foreground) {
        sourceBytes.fill(73);
    }
    auto input = request(Method::DistanceL2Mask3);
    input.mask.pixels = sourceBytes;
    const auto result = execute(input, {.mode = Mode::Cpu});
    REQUIRE(result.has_value());
    const cv::Mat source(kHeight, kWidth, CV_8UC1, sourceBytes.data());
    cv::Mat reference;
    cv::distanceTransform(source, reference, cv::DIST_L2, cv::DIST_MASK_3);
    const auto& values = std::get<std::vector<float>>(result->output.pixels);
    REQUIRE(values.size() == kMask.size());
    CHECK(std::equal(values.begin(), values.end(), reference.ptr<float>()));
}

TEST_CASE("Compute works with no GPU adapter", "[compute_experiment]") {
    const auto input = request(Method::OpenEllipse, 1);
    const auto result = execute(input);
    REQUIRE(result.has_value());
    CHECK(result->backend == Backend::Cpu);
    CHECK(result->fallback_reason == Error::Unavailable);
    CHECK(result->output.pixels == echo_output().pixels);
    require_error(execute(input, {.mode = Mode::Gpu}), Error::Unavailable);
}

TEST_CASE("Compute preserves revision and telemetry from an injected GPU", "[compute_experiment]") {
    const auto input = request(Method::OpenEllipse, 1);
    const auto mode = GENERATE(Mode::Auto, Mode::Cpu, Mode::Gpu);
    const std::stop_source stop;
    int calls = 0;
    const GpuBackend gpu = [&](const Request& received, std::size_t budget, std::stop_token token) {
        ++calls;
        CHECK(received.mask.pixels.data() == input.mask.pixels.data());
        CHECK(received.mask.revision == kRevision);
        CHECK(received.method == Method::OpenEllipse);
        CHECK(budget == 1024);
        CHECK(token == stop.get_token());
        auto output = echo_output();
        output.telemetry.upload_elapsed = Duration{20};
        output.telemetry.dispatch_elapsed = Duration{30};
        output.telemetry.readback_elapsed = Duration{40};
        output.telemetry.upload_bytes = kMask.size();
        output.telemetry.download_bytes = kMask.size();
        output.telemetry.working_bytes = 128;
        return output;
    };
    const auto result =
        execute(input, {.mode = mode, .gpu_budget_bytes = 1024}, gpu, stop.get_token());
    REQUIRE(result.has_value());
    CHECK(result->revision == kRevision);
    CHECK_FALSE(result->fallback_reason);
    CHECK(result->output.pixels == echo_output().pixels);
    if (mode == Mode::Cpu) {
        CHECK(calls == 0);
        CHECK(result->backend == Backend::Cpu);
    } else {
        CHECK(calls == 1);
        CHECK(result->backend == Backend::Gpu);
        CHECK(result->output.telemetry.upload_elapsed == Duration{20});
        CHECK(result->output.telemetry.dispatch_elapsed == Duration{30});
        CHECK(result->output.telemetry.readback_elapsed == Duration{40});
        CHECK(result->output.telemetry.upload_bytes == kMask.size());
        CHECK(result->output.telemetry.download_bytes == kMask.size());
        CHECK(result->output.telemetry.working_bytes == 128);
    }
}

TEST_CASE("Auto compute falls back only on recoverable errors", "[compute_experiment]") {
    const auto input = request(Method::OpenEllipse, 1);
    const auto error = GENERATE(Error::Unsupported, Error::BudgetExceeded, Error::DriverFailure,
                                Error::Unavailable);
    const GpuBackend failing = [error](const Request&, std::size_t, std::stop_token) {
        return std::unexpected(error);
    };
    const auto result = execute(input, {}, failing);
    REQUIRE(result.has_value());
    CHECK(result->backend == Backend::Cpu);
    CHECK(result->fallback_reason == error);
    CHECK(result->output.pixels == echo_output().pixels);
    require_error(execute(input, {.mode = Mode::Gpu}, failing), error);
}

TEST_CASE("GPU input errors and cancellation are terminal", "[compute_experiment]") {
    const auto error = GENERATE(Error::InvalidInput, Error::Cancelled);
    const GpuBackend failing = [error](const Request&, std::size_t, std::stop_token) {
        return std::unexpected(error);
    };
    require_error(execute(request(Method::OpenEllipse, 1), {}, failing), error);
}

TEST_CASE("Compute rejects malformed GPU output", "[compute_experiment]") {
    const auto input = request(Method::OpenEllipse, 1);
    const auto wrongType = GENERATE(false, true);
    const GpuBackend malformed = [wrongType](const Request&, std::size_t, std::stop_token) {
        auto output = echo_output();
        if (wrongType) {
            output.pixels = std::vector<float>(kMask.size());
        } else {
            std::get<std::vector<std::uint8_t>>(output.pixels).pop_back();
        }
        return output;
    };
    const auto result = execute(input, {}, malformed);
    REQUIRE(result.has_value());
    CHECK(result->backend == Backend::Cpu);
    CHECK(result->fallback_reason == Error::DriverFailure);
    CHECK(result->output.pixels == echo_output().pixels);
    require_error(execute(input, {.mode = Mode::Gpu}, malformed), Error::DriverFailure);
}

TEST_CASE("Compute rejects inconsistent GPU distances", "[compute_experiment]") {
    const auto input = request(Method::DistanceL2Mask3);
    const auto badDistance = GENERATE(-1.0f, std::numeric_limits<float>::infinity(),
                                      std::numeric_limits<float>::quiet_NaN(), 1.0f);
    const GpuBackend malformed = [badDistance](const Request&, std::size_t, std::stop_token) {
        std::vector<float> distances(kMask.size());
        distances[3] = badDistance; // Source byte is zero.
        BackendOutput output{std::move(distances), {}};
        output.telemetry.working_bytes = kMask.size() * (1 + sizeof(float));
        return output;
    };
    const auto result = execute(input, {}, malformed);
    REQUIRE(result.has_value());
    CHECK(result->backend == Backend::Cpu);
    CHECK(result->fallback_reason == Error::DriverFailure);
    require_error(execute(input, {.mode = Mode::Gpu}, malformed), Error::DriverFailure);
}

TEST_CASE("Compute validates mask dimensions size and method parameters", "[compute_experiment]") {
    const auto good = request(Method::OpenEllipse, 1);
    const std::array invalid{
        Request{{0, kHeight, kRevision, kMask}, Method::OpenEllipse, 1},
        Request{{kWidth, -1, kRevision, kMask}, Method::OpenEllipse, 1},
        Request{{kMaxAxis + 1, 1, kRevision, kMask}, Method::OpenEllipse, 1},
        Request{{kWidth, kHeight, kRevision, std::span{kMask}.first(1)}, Method::OpenEllipse, 1},
        request(Method::OpenEllipse, 0),
        request(Method::OpenEllipse, kMaxDiameter + 1),
        request(Method::DistanceL2Mask3, 1),
    };
    int calls = 0;
    const GpuBackend gpu = [&](const Request&, std::size_t, std::stop_token) {
        ++calls;
        return echo_output();
    };
    for (const auto& input : invalid) {
        require_error(execute(input, {}, gpu), Error::InvalidInput);
    }
    CHECK(calls == 0);
    REQUIRE(execute(good, {.mode = Mode::Cpu}));
    const std::vector<std::uint8_t> pixels(kMaxAxis, 255);
    const Request edge{{kMaxAxis, 1, kRevision, pixels}, Method::OpenEllipse, 1};
    REQUIRE(execute(edge, {.mode = Mode::Cpu}));
}

TEST_CASE("Compute enforces distinct CPU and GPU byte budgets", "[compute_experiment]") {
    const auto input = request(Method::OpenEllipse, 1);
    require_error(execute(input, {.mode = Mode::Cpu, .cpu_budget_bytes = 0}),
                  Error::BudgetExceeded);
    int calls = 0;
    const GpuBackend gpu = [&](const Request&, std::size_t, std::stop_token) {
        ++calls;
        auto output = echo_output();
        output.telemetry.working_bytes = 1025;
        return output;
    };
    const auto fallback = execute(input, {.gpu_budget_bytes = 0}, gpu);
    REQUIRE(fallback.has_value());
    CHECK(fallback->backend == Backend::Cpu);
    CHECK(fallback->fallback_reason == Error::BudgetExceeded);
    require_error(execute(input, {.mode = Mode::Gpu, .gpu_budget_bytes = 0}, gpu),
                  Error::BudgetExceeded);
    CHECK(calls == 0);
    require_error(execute(input, {.mode = Mode::Gpu, .gpu_budget_bytes = 1024}, gpu),
                  Error::BudgetExceeded);
    CHECK(calls == 1);
    const GpuBackend underreported = [](const Request&, std::size_t, std::stop_token) {
        auto output = echo_output();
        --output.telemetry.working_bytes;
        return output;
    };
    require_error(execute(input, {.mode = Mode::Gpu}, underreported), Error::DriverFailure);
}

TEST_CASE("Compute observes stop before and after an adapter call", "[compute_experiment]") {
    const auto input = request(Method::OpenEllipse, 1);
    const auto fail = GENERATE(false, true);
    std::stop_source stop;
    int calls = 0;
    const GpuBackend gpu = [&](const Request&, std::size_t,
                               std::stop_token) -> std::expected<BackendOutput, Error> {
        ++calls;
        stop.request_stop();
        if (fail) {
            return std::unexpected(Error::DriverFailure);
        }
        return echo_output();
    };
    require_error(execute(input, {}, gpu, stop.get_token()), Error::Cancelled);
    CHECK(calls == 1);
    require_error(execute(input, {}, gpu, stop.get_token()), Error::Cancelled);
    CHECK(calls == 1);
}
