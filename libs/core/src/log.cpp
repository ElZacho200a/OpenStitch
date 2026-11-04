// SPDX-License-Identifier: Apache-2.0
#include "openstitch/core/log.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <deque>
#include <fstream>
#include <memory>
#include <system_error>

namespace openstitch {

namespace fs = std::filesystem;

namespace {
constexpr const char* kLogFileName = "openstitch.log";

[[maybe_unused]] fs::path env_path(const char* name) {
#ifdef _WIN32
    char* buf = nullptr;
    std::size_t len = 0;
    fs::path result;
    if (_dupenv_s(&buf, &len, name) == 0 && buf != nullptr && *buf != '\0') {
        result = fs::path(buf);
    }
    std::free(buf);
    return result;
#else
    // NOLINTNEXTLINE(concurrency-mt-unsafe) : lu au démarrage, avant tout thread.
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? fs::path(v) : fs::path();
#endif
}
} // namespace

void init_logging(bool verbose) {
    auto logger = spdlog::stderr_color_mt("openstitch");
    spdlog::set_default_logger(std::move(logger));
    spdlog::set_level(verbose ? spdlog::level::debug : spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
}

fs::path user_data_directory() {
#ifdef _WIN32
    if (fs::path p = env_path("LOCALAPPDATA"); !p.empty()) {
        return p / "OpenStitch";
    }
#else
    if (fs::path p = env_path("XDG_STATE_HOME"); !p.empty()) {
        return p / "openstitch";
    }
    if (fs::path h = env_path("HOME"); !h.empty()) {
        return h / ".local" / "state" / "openstitch";
    }
#endif
    return {};
}

fs::path user_log_directory() {
    const fs::path base = user_data_directory();
    return base.empty() ? fs::path() : base / "logs";
}

bool init_file_logging(const fs::path& dir, std::size_t max_bytes, std::size_t max_files) {
    if (dir.empty()) {
        return false;
    }
    try {
        std::error_code ec;
        fs::create_directories(dir, ec);
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            (dir / kLogFileName).string(), max_bytes, max_files > 1 ? max_files - 1 : 1);
        sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::default_logger()->sinks().push_back(std::move(sink));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<std::string> read_log_tail(const fs::path& dir, std::size_t max_lines) {
    std::ifstream in(dir / kLogFileName);
    std::deque<std::string> tail;
    std::string line;
    while (max_lines > 0 && std::getline(in, line)) {
        tail.push_back(line);
        if (tail.size() > max_lines) {
            tail.pop_front();
        }
    }
    return {tail.begin(), tail.end()};
}

} // namespace openstitch
