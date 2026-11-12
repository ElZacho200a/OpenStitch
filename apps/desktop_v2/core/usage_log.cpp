// SPDX-License-Identifier: Apache-2.0
#include "usage_log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <fstream>
#include <random>
#include <system_error>

namespace openstitch::desktop_v2 {

namespace {

constexpr std::string_view kOs =
#if defined(_WIN32)
    "windows";
#elif defined(__APPLE__)
    "macos";
#else
    "linux";
#endif

std::string randomSessionId() {
    std::random_device device;
    std::uint64_t value = (static_cast<std::uint64_t>(device()) << 32) ^ device();
    return std::format("{:016x}", value);
}

bool falsy(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text == "0" || text == "false" || text == "off" || text == "non" || text == "no";
}

} // namespace

std::string usageJsonEscape(std::string_view text, std::size_t maxBytes) {
    std::string out;
    out.reserve(std::min(text.size(), maxBytes) + 8);
    for (const char raw : text) {
        if (out.size() >= maxBytes) {
            break;
        }
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned>(c));
            } else {
                out += raw;
            }
        }
    }
    return out;
}

std::filesystem::path defaultUsageDir(const GetEnv& getenv,
                                      const std::filesystem::path& genericDataDir) {
    if (const auto custom = getenv("OPENSTITCH_USAGE_DIR"); !custom.empty()) {
        return std::filesystem::path(custom);
    }
    return genericDataDir / "OpenStitch" / "usage";
}

bool usageLogEnabled(const GetEnv& getenv, const std::filesystem::path& dir) {
    if (const auto flag = getenv("OPENSTITCH_USAGE_LOG"); !flag.empty() && falsy(flag)) {
        return false;
    }
    std::error_code ec;
    return !std::filesystem::exists(dir / "DISABLED", ec);
}

UsageLog::UsageLog(UsageLogOptions options)
    : options_(std::move(options)), sessionId_(randomSessionId()), enabled_(options_.enabled) {
    if (!options_.now) {
        options_.now = [] { return std::chrono::system_clock::now(); };
    }
}

std::chrono::system_clock::time_point UsageLog::timestamp() const {
    return options_.now();
}

std::filesystem::path UsageLog::currentFile() const {
    const auto day = std::chrono::floor<std::chrono::days>(timestamp());
    const std::chrono::year_month_day ymd{day};
    return options_.dir / std::format("usage-{:04d}-{:02d}.jsonl", static_cast<int>(ymd.year()),
                                      static_cast<unsigned>(ymd.month()));
}

std::string UsageLog::formatLine(std::string_view kind, std::string_view id,
                                 std::string_view detail) const {
    const auto ms = std::chrono::floor<std::chrono::milliseconds>(timestamp());
    std::string line = std::format(
        R"({{"v":1,"t":"{:%FT%T}","sid":"{}","app":"{}","ver":"{}","os":"{}","k":"{}","id":"{}")",
        ms, sessionId_, usageJsonEscape(options_.app, 32), usageJsonEscape(options_.version, 48),
        kOs, usageJsonEscape(kind, 32), usageJsonEscape(id));
    if (!detail.empty()) {
        line += std::format(R"(,"d":"{}")", usageJsonEscape(detail));
    }
    line += '}';
    return line;
}

void UsageLog::record(std::string_view kind, std::string_view id, std::string_view detail) {
    if (!enabled_ || kind.empty() || id.empty()) {
        return;
    }
    try {
        const std::scoped_lock lock(mutex_);
        std::error_code ec;
        std::filesystem::create_directories(options_.dir, ec);
        std::ofstream out(currentFile(), std::ios::app | std::ios::binary);
        if (!out) {
            return;
        }
        out << formatLine(kind, id, detail) << '\n';
    } catch (...) {
        // Le journal ne doit jamais gêner le travail.
    }
}

} // namespace openstitch::desktop_v2
