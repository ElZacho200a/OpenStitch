// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace openstitch::pipeline_profile {

enum class Stage {
    Decode,
    Preprocess,
    Segment,
    MaskSmoothing,
    Morphology,
    Vectorize,
    Digitize,
    Distance,
    Skeleton,
    Tatami,
    Directional,
    Radial,
    Finish,
    HiddenTravel,
    Count,
};

inline constexpr std::array<const char*, static_cast<std::size_t>(Stage::Count)> stage_names{
    "decode",      "preprocess", "segment",  "mask_smoothing", "morphology",
    "vectorize",   "digitize",   "distance", "skeleton",       "tatami",
    "directional", "radial",     "finish",   "hidden_travel",
};

struct Sample {
    std::atomic<std::uint64_t> elapsed_ns{0};
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> upload_bytes{0};
    std::atomic<std::uint64_t> download_bytes{0};
    std::atomic<std::uint64_t> buffers{0};
};

// Only the standalone benchmark enables recording, around one synchronous pipeline run.
// Worker tasks update the same sample and must have joined before the recording ends.
using Samples = std::array<Sample, stage_names.size()>;
inline std::atomic<Samples*> active_samples{nullptr};

class Recording {
public:
    explicit Recording(Samples& samples) {
        active_samples.store(&samples, std::memory_order_release);
    }
    ~Recording() { active_samples.store(nullptr, std::memory_order_release); }
    Recording(const Recording&) = delete;
    Recording& operator=(const Recording&) = delete;
};

class Scope {
public:
    explicit Scope(Stage stage, std::size_t upload = 0, std::size_t download = 0,
                   std::size_t buffers = 0) {
        if (auto* samples = active_samples.load(std::memory_order_acquire)) {
            sample_ = &(*samples)[static_cast<std::size_t>(stage)];
            sample_->upload_bytes.fetch_add(upload, std::memory_order_relaxed);
            sample_->download_bytes.fetch_add(download, std::memory_order_relaxed);
            sample_->buffers.fetch_add(buffers, std::memory_order_relaxed);
            started_ = Clock::now();
        }
    }

    ~Scope() {
        if (sample_ != nullptr) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started_);
            sample_->elapsed_ns.fetch_add(static_cast<std::uint64_t>(elapsed.count()),
                                          std::memory_order_relaxed);
            sample_->calls.fetch_add(1, std::memory_order_relaxed);
        }
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    using Clock = std::chrono::steady_clock;
    Sample* sample_{nullptr};
    Clock::time_point started_;
};

} // namespace openstitch::pipeline_profile
