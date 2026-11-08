// SPDX-License-Identifier: Apache-2.0
#include "d3d11_backend.hpp"

#include "d3d11_kernels.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <mutex>
#include <new>
#include <string_view>
#include <utility>

namespace openstitch::compute_experiment {
namespace {

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

constexpr UINT kRead = D3D11_BIND_SHADER_RESOURCE;
constexpr UINT kWrite = D3D11_BIND_UNORDERED_ACCESS;
constexpr UINT kTile = 16;
constexpr UINT kRowsPerGroup = 64;
using Offset = std::array<std::int32_t, 2>;
static_assert(sizeof(Offset) == 8);

struct Parameters {
    UINT width;
    UINT height;
    UINT offset_count;
    UINT has_zero;
};
static_assert(sizeof(Parameters) == 16);

enum class Kernel : std::size_t { Erode, Dilate, NearestInRow, NearestAcrossRows, Count };

struct AdapterState {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    std::array<ComPtr<ID3D11ComputeShader>, static_cast<std::size_t>(Kernel::Count)> shaders;
    std::mutex mutex;

    ID3D11ComputeShader* shader(Kernel kernel) const {
        return shaders[static_cast<std::size_t>(kernel)].Get();
    }
};

struct Candidate {
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 description;
};

Error failure(HRESULT hr) {
    if (hr == E_OUTOFMEMORY) {
        return Error::BudgetExceeded;
    }
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        return Error::Unsupported;
    }
    return Error::DriverFailure;
}

std::expected<std::vector<Candidate>, Error> enumerate(IDXGIFactory1& factory,
                                                       IDXGIFactory6* preferred) {
    std::vector<Candidate> candidates;
    for (UINT index = 0;; ++index) {
        Candidate candidate{};
        const HRESULT hr = preferred != nullptr
                               ? preferred->EnumAdapterByGpuPreference(
                                     index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                     IID_PPV_ARGS(candidate.adapter.GetAddressOf()))
                               : factory.EnumAdapters1(index, candidate.adapter.GetAddressOf());
        if (hr == DXGI_ERROR_NOT_FOUND) {
            return candidates;
        }
        if (FAILED(hr)) {
            return std::unexpected(failure(hr));
        }
        if (SUCCEEDED(candidate.adapter->GetDesc1(&candidate.description)) &&
            (candidate.description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
            candidates.push_back(std::move(candidate));
        }
    }
}

std::expected<std::vector<Candidate>, Error> select_candidates() {
    ComPtr<IDXGIFactory1> factory;
    const HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        return std::unexpected(failure(hr));
    }
    ComPtr<IDXGIFactory6> preferred;
    if (SUCCEEDED(factory.As(&preferred))) {
        auto candidates = enumerate(*factory.Get(), preferred.Get());
        if (candidates && !candidates->empty()) {
            return candidates;
        }
    }
    auto candidates = enumerate(*factory.Get(), nullptr);
    if (candidates) {
        std::stable_sort(
            candidates->begin(), candidates->end(), [](const Candidate& a, const Candidate& b) {
                return a.description.DedicatedVideoMemory > b.description.DedicatedVideoMemory;
            });
    }
    return candidates;
}

std::string adapter_name(const wchar_t* name) {
    const int count = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 1) {
        return "D3D11 hardware adapter";
    }
    std::string utf8(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8.data(), count, nullptr, nullptr) != count) {
        return "D3D11 hardware adapter";
    }
    utf8.pop_back();
    return utf8;
}

std::expected<void, Error> compile_shaders(AdapterState& state) {
    constexpr std::array<std::pair<std::string_view, const char*>, 4> sources{{
        {d3d11_kernels::morphology, "erode"},
        {d3d11_kernels::morphology, "dilate"},
        {d3d11_kernels::distance, "nearestInRow"},
        {d3d11_kernels::distance, "nearestAcrossRows"},
    }};
    static_assert(sources.size() == static_cast<std::size_t>(Kernel::Count));
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto& [source, entry] = sources[i];
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        HRESULT hr =
            D3DCompile(source.data(), source.size(), "OpenStitch R01-03", nullptr, nullptr, entry,
                       "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                       bytecode.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(hr)) {
            if (errors) {
                (void)std::fwrite(errors->GetBufferPointer(), 1, errors->GetBufferSize(), stderr);
            }
            return std::unexpected(failure(hr));
        }
        hr = state.device->CreateComputeShader(bytecode->GetBufferPointer(),
                                               bytecode->GetBufferSize(), nullptr,
                                               state.shaders[i].GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(failure(hr));
        }
    }
    return {};
}

std::expected<ComPtr<ID3D11Buffer>, Error> make_buffer(ID3D11Device& device,
                                                       const D3D11_BUFFER_DESC& description,
                                                       const void* data = nullptr) {
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = data;
    ComPtr<ID3D11Buffer> buffer;
    const HRESULT hr = device.CreateBuffer(&description, data != nullptr ? &initial : nullptr,
                                           buffer.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(failure(hr));
    }
    return buffer;
}

struct Resource {
    ComPtr<ID3D11Buffer> buffer;
    ComPtr<ID3D11ShaderResourceView> read;
    ComPtr<ID3D11UnorderedAccessView> write;
};

std::expected<Resource, Error> make_structured(ID3D11Device& device, UINT count, UINT stride,
                                               UINT bind, const void* data = nullptr) {
    auto buffer =
        make_buffer(device,
                    {.ByteWidth = count * stride,
                     .Usage = data != nullptr ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT,
                     .BindFlags = bind,
                     .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                     .StructureByteStride = stride},
                    data);
    if (!buffer) {
        return std::unexpected(buffer.error());
    }
    Resource resource{};
    resource.buffer = std::move(*buffer);
    if ((bind & kRead) != 0) {
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_UNKNOWN;
        view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        view.Buffer.NumElements = count;
        const HRESULT hr = device.CreateShaderResourceView(resource.buffer.Get(), &view,
                                                           resource.read.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(failure(hr));
        }
    }
    if ((bind & kWrite) != 0) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_UNKNOWN;
        view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        view.Buffer.NumElements = count;
        const HRESULT hr = device.CreateUnorderedAccessView(resource.buffer.Get(), &view,
                                                            resource.write.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(failure(hr));
        }
    }
    return resource;
}

struct GpuTimer {
    ComPtr<ID3D11Query> disjoint;
    ComPtr<ID3D11Query> start;
    ComPtr<ID3D11Query> finish;

    explicit GpuTimer(ID3D11Device& device) {
        const D3D11_QUERY_DESC interval{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        const D3D11_QUERY_DESC point{D3D11_QUERY_TIMESTAMP, 0};
        if (FAILED(device.CreateQuery(&interval, disjoint.GetAddressOf())) ||
            FAILED(device.CreateQuery(&point, start.GetAddressOf())) ||
            FAILED(device.CreateQuery(&point, finish.GetAddressOf()))) {
            disjoint.Reset();
        }
    }

    void begin(ID3D11DeviceContext& context) const {
        if (disjoint) {
            context.Begin(disjoint.Get());
            context.End(start.Get());
        }
    }

    void end(ID3D11DeviceContext& context) const {
        if (disjoint) {
            context.End(finish.Get());
            context.End(disjoint.Get());
        }
    }

    std::optional<Duration> elapsed(ID3D11DeviceContext& context) const {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timebase{};
        UINT64 first = 0;
        UINT64 last = 0;
        constexpr UINT flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;
        if (!disjoint ||
            context.GetData(disjoint.Get(), &timebase, sizeof(timebase), flags) != S_OK ||
            timebase.Disjoint || timebase.Frequency == 0 ||
            context.GetData(start.Get(), &first, sizeof(first), flags) != S_OK ||
            context.GetData(finish.Get(), &last, sizeof(last), flags) != S_OK || last < first) {
            return std::nullopt;
        }
        const auto seconds = std::chrono::duration<double>(static_cast<double>(last - first) /
                                                           static_cast<double>(timebase.Frequency));
        return std::chrono::duration_cast<Duration>(seconds);
    }
};

void dispatch(ID3D11DeviceContext& context, ID3D11ComputeShader* shader, ID3D11Buffer* parameters,
              ID3D11ShaderResourceView* source, ID3D11ShaderResourceView* offsets,
              ID3D11UnorderedAccessView* target, UINT x, UINT y) {
    const std::array<ID3D11ShaderResourceView*, 2> input{source, offsets};
    context.CSSetShader(shader, nullptr, 0);
    context.CSSetConstantBuffers(0, 1, &parameters);
    context.CSSetShaderResources(0, 2, input.data());
    context.CSSetUnorderedAccessViews(0, 1, &target, nullptr);
    context.Dispatch(x, y, 1);
    // The intermediate becomes a shader resource in the next pass.
    const std::array<ID3D11ShaderResourceView*, 2> none{};
    ID3D11UnorderedAccessView* noTarget = nullptr;
    ID3D11Buffer* noParameters = nullptr;
    context.CSSetUnorderedAccessViews(0, 1, &noTarget, nullptr);
    context.CSSetShaderResources(0, 2, none.data());
    context.CSSetConstantBuffers(0, 1, &noParameters);
    context.CSSetShader(nullptr, nullptr, 0);
}

std::vector<Offset> ellipse(int diameter) {
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, {diameter, diameter});
    const int anchor = diameter / 2;
    std::vector<Offset> offsets;
    offsets.reserve(static_cast<std::size_t>(diameter * diameter));
    for (int y = 0; y < diameter; ++y) {
        const auto* row = kernel.ptr<std::uint8_t>(y);
        for (int x = 0; x < diameter; ++x) {
            if (row[x] != 0) {
                offsets.push_back({x - anchor, y - anchor});
            }
        }
    }
    return offsets;
}

bool valid_request(const Request& request) {
    const auto& mask = request.mask;
    if (mask.width < 1 || mask.height < 1 || mask.width > kMaxAxis || mask.height > kMaxAxis ||
        mask.pixels.size() !=
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

Duration elapsed_since(Clock::time_point start) {
    return std::chrono::duration_cast<Duration>(Clock::now() - start);
}

std::expected<BackendOutput, Error> run(AdapterState& state, const Request& request,
                                        std::size_t budget, std::stop_token stop) {
    if (stop.stop_requested()) {
        return std::unexpected(Error::Cancelled);
    }
    if (!valid_request(request)) {
        return std::unexpected(Error::InvalidInput);
    }
    const auto& mask = request.mask;
    const auto count = static_cast<UINT>(mask.pixels.size());
    constexpr auto word = static_cast<UINT>(sizeof(std::uint32_t));
    constexpr auto offsetStride = static_cast<UINT>(sizeof(Offset));
    const UINT packedBytes = count * word;
    const bool morph = request.method == Method::OpenEllipse;
    const auto kernelArea = static_cast<std::size_t>(request.diameter * request.diameter);
    // Bound the work submitted before the blocking Map. A sparse mask can force the distance
    // shader to visit every row; a wide ellipse visits its neighbourhood in both passes.
    constexpr std::uint64_t maxVisits = 128ull * 1024 * 1024;
    const std::uint64_t visitsPerPixel =
        morph ? 2 * kernelArea : static_cast<std::uint64_t>(mask.height + 2);
    if (static_cast<std::uint64_t>(count) * visitsPerPixel > maxVisits) {
        return std::unexpected(Error::Unsupported);
    }
    // Count the borrowed mask, result, host pack, four D3D buffers, ellipse storage and constants.
    const std::size_t workingBytes = mask.pixels.size() * (morph ? 2u : 1u + word) +
                                     static_cast<std::size_t>(packedBytes) * 5 +
                                     kernelArea * (1 + 2 * offsetStride) + sizeof(Parameters);
    if (workingBytes > budget) {
        return std::unexpected(Error::BudgetExceeded);
    }
    std::scoped_lock lock(state.mutex);
    if (stop.stop_requested()) {
        return std::unexpected(Error::Cancelled);
    }
    auto& device = *state.device.Get();
    auto& context = *state.context.Get();

    const auto uploadStart = Clock::now();
    std::vector<std::uint32_t> packed;
    packed.reserve(count);
    bool hasZero = false;
    for (auto value : mask.pixels) {
        packed.push_back(value);
        hasZero = hasZero || value == 0;
    }
    const auto offsets = morph ? ellipse(request.diameter) : std::vector<Offset>{};
    const Parameters parameters{static_cast<UINT>(mask.width), static_cast<UINT>(mask.height),
                                static_cast<UINT>(offsets.size()), hasZero ? 1u : 0u};
    const auto constants = make_buffer(device,
                                       {.ByteWidth = sizeof(Parameters),
                                        .Usage = D3D11_USAGE_IMMUTABLE,
                                        .BindFlags = D3D11_BIND_CONSTANT_BUFFER},
                                       &parameters);
    if (!constants) {
        return std::unexpected(constants.error());
    }
    const auto source = make_structured(device, count, word, kRead, packed.data());
    if (!source) {
        return std::unexpected(source.error());
    }
    const auto intermediate = make_structured(device, count, word, kRead | kWrite);
    if (!intermediate) {
        return std::unexpected(intermediate.error());
    }
    const auto target = make_structured(device, count, word, kWrite);
    if (!target) {
        return std::unexpected(target.error());
    }
    const auto staging = make_buffer(device, {.ByteWidth = packedBytes,
                                              .Usage = D3D11_USAGE_STAGING,
                                              .CPUAccessFlags = D3D11_CPU_ACCESS_READ});
    if (!staging) {
        return std::unexpected(staging.error());
    }
    ComPtr<ID3D11ShaderResourceView> gpuOffsets;
    if (morph) {
        const auto resource =
            make_structured(device, parameters.offset_count, offsetStride, kRead, offsets.data());
        if (!resource) {
            return std::unexpected(resource.error());
        }
        gpuOffsets = resource->read;
    }
    GpuTimer timer(device);
    BackendOutput output;
    if (morph) {
        output.pixels.emplace<std::vector<std::uint8_t>>(count);
    } else {
        output.pixels.emplace<std::vector<float>>(count);
    }
    output.telemetry.upload_elapsed = elapsed_since(uploadStart);
    output.telemetry.upload_bytes =
        packedBytes + offsets.size() * offsetStride + sizeof(Parameters);
    output.telemetry.download_bytes = packedBytes;
    output.telemetry.working_bytes = workingBytes;
    if (stop.stop_requested()) {
        return std::unexpected(Error::Cancelled);
    }

    const auto groupsX = (parameters.width + kTile - 1) / kTile;
    const auto groupsY = (parameters.height + kTile - 1) / kTile;
    timer.begin(context);
    if (morph) {
        dispatch(context, state.shader(Kernel::Erode), constants->Get(), source->read.Get(),
                 gpuOffsets.Get(), intermediate->write.Get(), groupsX, groupsY);
        dispatch(context, state.shader(Kernel::Dilate), constants->Get(), intermediate->read.Get(),
                 gpuOffsets.Get(), target->write.Get(), groupsX, groupsY);
    } else {
        if (hasZero) {
            const auto groups = (parameters.height + kRowsPerGroup - 1) / kRowsPerGroup;
            dispatch(context, state.shader(Kernel::NearestInRow), constants->Get(),
                     source->read.Get(), nullptr, intermediate->write.Get(), groups, 1);
        }
        dispatch(context, state.shader(Kernel::NearestAcrossRows), constants->Get(),
                 intermediate->read.Get(), nullptr, target->write.Get(), groupsX, groupsY);
    }
    timer.end(context);

    const auto readbackStart = Clock::now();
    context.CopyResource(staging->Get(), target->buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT hr = context.Map(staging->Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        return std::unexpected(failure(hr));
    }
    const auto* words = static_cast<const std::uint32_t*>(mapped.pData);
    if (morph) {
        auto& pixels = std::get<std::vector<std::uint8_t>>(output.pixels);
        std::transform(words, words + count, pixels.begin(),
                       [](std::uint32_t value) { return static_cast<std::uint8_t>(value); });
    } else {
        auto& pixels = std::get<std::vector<float>>(output.pixels);
        std::transform(words, words + count, pixels.begin(),
                       [](std::uint32_t value) { return static_cast<float>(value) / 65536.0f; });
    }
    context.Unmap(staging->Get(), 0);
    output.telemetry.readback_elapsed = elapsed_since(readbackStart);
    output.telemetry.dispatch_elapsed = timer.elapsed(context);
    if (stop.stop_requested()) {
        return std::unexpected(Error::Cancelled);
    }
    return output;
}

} // namespace

std::expected<D3d11Adapter, Error> make_d3d11_adapter() {
    try {
        auto candidates = select_candidates();
        if (!candidates) {
            return std::unexpected(candidates.error());
        }
        if (candidates->empty()) {
            return std::unexpected(Error::Unavailable);
        }

        const D3D_FEATURE_LEVEL required = D3D_FEATURE_LEVEL_11_0;
        Error last = Error::Unsupported;
        for (const auto& candidate : *candidates) {
            auto state = std::make_shared<AdapterState>();
            const HRESULT hr = D3D11CreateDevice(candidate.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN,
                                                 nullptr, 0, &required, 1, D3D11_SDK_VERSION,
                                                 state->device.ReleaseAndGetAddressOf(), nullptr,
                                                 state->context.ReleaseAndGetAddressOf());
            if (FAILED(hr)) {
                last = hr == E_INVALIDARG ? Error::Unsupported : failure(hr);
                continue;
            }
            if (auto compiled = compile_shaders(*state); !compiled) {
                last = compiled.error();
                continue;
            }
            return D3d11Adapter{
                adapter_name(candidate.description.Description),
                [state](const Request& request, std::size_t budget,
                        std::stop_token stop) -> std::expected<BackendOutput, Error> {
                    try {
                        return run(*state, request, budget, stop);
                    } catch (const std::bad_alloc&) {
                        return std::unexpected(Error::BudgetExceeded);
                    } catch (const cv::Exception& error) {
                        return std::unexpected(error.code == cv::Error::StsNoMem
                                                   ? Error::BudgetExceeded
                                                   : Error::DriverFailure);
                    }
                }};
        }
        return std::unexpected(last);
    } catch (const std::bad_alloc&) {
        return std::unexpected(Error::BudgetExceeded);
    }
}

} // namespace openstitch::compute_experiment
