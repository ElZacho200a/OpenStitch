// SPDX-License-Identifier: Apache-2.0
#include "compute.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <exception>
#include <new>
#include <utility>

namespace openstitch::compute_experiment {
namespace {

using Clock = std::chrono::steady_clock;

Duration elapsed_since(Clock::time_point start) {
    return std::chrono::duration_cast<Duration>(Clock::now() - start);
}

bool valid_request(const Request& request) {
    const auto& mask = request.mask;
    if (mask.width < 1 || mask.height < 1 || mask.width > kMaxAxis || mask.height > kMaxAxis) {
        return false;
    }
    if (mask.pixels.size() !=
        static_cast<std::size_t>(mask.width) * static_cast<std::size_t>(mask.height)) {
        return false;
    }
    switch (request.method) {
    case Method::OpenEllipse:
        return request.diameter >= 1 && request.diameter <= kMaxDiameter;
    case Method::DistanceL2Mask3:
        return request.diameter == 0;
    }
    return false;
}

std::size_t output_bytes(const Request& request) {
    const std::size_t bytesPerPixel = request.method == Method::OpenEllipse ? 1 : sizeof(float);
    return request.mask.pixels.size() * bytesPerPixel;
}

std::size_t cpu_working_bytes(const Request& request) {
    const auto& mask = request.mask;
    const auto ioBytes = mask.pixels.size() + output_bytes(request);
    if (request.method == Method::OpenEllipse) {
        // Account for one intermediate and the byte kernel.
        const auto diameter = static_cast<std::size_t>(request.diameter);
        return ioBytes + mask.pixels.size() + diameter * diameter;
    }
    // Account for a float scratch image with one pixel of padding on all sides.
    const auto width = static_cast<std::size_t>(mask.width + 2);
    const auto height = static_cast<std::size_t>(mask.height + 2);
    return ioBytes + width * height * sizeof(float);
}

std::expected<BackendOutput, Error> run_cpu(const Request& request, std::size_t budget) {
    const auto workingBytes = cpu_working_bytes(request);
    if (workingBytes > budget) {
        return std::unexpected(Error::BudgetExceeded);
    }
    const auto start = Clock::now();
    try {
        const auto& mask = request.mask;
        // OpenCV takes void* for a borrowed Mat. Both operations treat the source as input.
        const cv::Mat source(mask.height, mask.width, CV_8UC1,
                             const_cast<std::uint8_t*>(mask.pixels.data()));
        BackendOutput output;
        if (request.method == Method::OpenEllipse) {
            auto& values = output.pixels.emplace<std::vector<std::uint8_t>>(mask.pixels.size());
            cv::Mat target(mask.height, mask.width, CV_8UC1, values.data());
            const auto kernel = cv::getStructuringElement(
                cv::MORPH_ELLIPSE, cv::Size(request.diameter, request.diameter));
            cv::morphologyEx(source, target, cv::MORPH_OPEN, kernel);
        } else {
            auto& values = output.pixels.emplace<std::vector<float>>(mask.pixels.size());
            cv::Mat target(mask.height, mask.width, CV_32FC1, values.data());
            cv::distanceTransform(source, target, cv::DIST_L2, cv::DIST_MASK_3);
        }
        output.telemetry.dispatch_elapsed = elapsed_since(start);
        output.telemetry.working_bytes = workingBytes;
        return output;
    } catch (const std::bad_alloc&) {
        return std::unexpected(Error::BudgetExceeded);
    } catch (const cv::Exception& error) {
        return std::unexpected(error.code == cv::Error::StsNoMem ? Error::BudgetExceeded
                                                                 : Error::CpuFailure);
    }
}

bool valid_output(const Request& request, const Pixels& pixels) {
    const auto count = request.mask.pixels.size();
    if (request.method == Method::OpenEllipse) {
        const auto* values = std::get_if<std::vector<std::uint8_t>>(&pixels);
        return values != nullptr && values->size() == count;
    }
    const auto* values = std::get_if<std::vector<float>>(&pixels);
    if (values == nullptr || values->size() != count) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto distance = (*values)[i];
        if (!std::isfinite(distance) || distance < 0.0f ||
            (request.mask.pixels[i] == 0 && distance != 0.0f)) {
            return false;
        }
    }
    return true;
}

std::expected<BackendOutput, Error> run_gpu(const Request& request, std::size_t budget,
                                            const GpuBackend& gpu, std::stop_token stop) {
    if (!gpu) {
        return std::unexpected(Error::Unavailable);
    }
    const auto ioBytes = request.mask.pixels.size() + output_bytes(request);
    if (ioBytes > budget) {
        return std::unexpected(Error::BudgetExceeded);
    }
    try {
        auto output = gpu(request, budget, stop);
        if (stop.stop_requested()) {
            return std::unexpected(Error::Cancelled);
        }
        if (output &&
            (output->telemetry.working_bytes < ioBytes || !valid_output(request, output->pixels))) {
            return std::unexpected(Error::DriverFailure);
        }
        if (output && output->telemetry.working_bytes > budget) {
            return std::unexpected(Error::BudgetExceeded);
        }
        return output;
    } catch (const std::bad_alloc&) {
        return std::unexpected(Error::BudgetExceeded);
    } catch (const std::exception&) {
        return std::unexpected(Error::DriverFailure);
    }
}

bool can_fallback(Error error) {
    return error == Error::Unavailable || error == Error::Unsupported ||
           error == Error::BudgetExceeded || error == Error::DriverFailure;
}

} // namespace

std::expected<Result, Error> execute(const Request& request, const Options& options,
                                     const GpuBackend& gpu, std::stop_token stop) {
    if (stop.stop_requested()) {
        return std::unexpected(Error::Cancelled);
    }
    if (!valid_request(request)) {
        return std::unexpected(Error::InvalidInput);
    }
    const auto start = Clock::now();
    std::optional<Error> fallback;
    if (options.mode != Mode::Cpu) {
        auto output = run_gpu(request, options.gpu_budget_bytes, gpu, stop);
        if (stop.stop_requested()) {
            return std::unexpected(Error::Cancelled);
        }
        if (output) {
            output->telemetry.elapsed = elapsed_since(start);
            return Result{request.mask.revision, Backend::Gpu, std::nullopt, std::move(*output)};
        }
        if (options.mode == Mode::Gpu || !can_fallback(output.error())) {
            return std::unexpected(output.error());
        }
        fallback = output.error();
    }
    if (stop.stop_requested()) {
        return std::unexpected(Error::Cancelled);
    }
    auto output = run_cpu(request, options.cpu_budget_bytes);
    if (stop.stop_requested()) {
        return std::unexpected(Error::Cancelled);
    }
    if (!output) {
        return std::unexpected(output.error());
    }
    output->telemetry.elapsed = elapsed_since(start);
    return Result{request.mask.revision, Backend::Cpu, fallback, std::move(*output)};
}

} // namespace openstitch::compute_experiment
