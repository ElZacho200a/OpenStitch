// SPDX-License-Identifier: Apache-2.0
// Découverte de l'hôte live et client de tube nommé / socket local (aucun port réseau).
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "openstitch/mcp/live.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace openstitch::mcp {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string env(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    std::string out;
    if (_dupenv_s(&value, &size, name) == 0 && value != nullptr) {
        out = value;
    }
    std::free(value);
    return out;
#else
    const char* v = std::getenv(name);
    return v != nullptr ? std::string(v) : std::string{};
#endif
}

} // namespace

fs::path live_directory() {
    if (const std::string over = env("OPENSTITCH_LIVE_DIR"); !over.empty()) {
        return fs::path(over);
    }
#ifdef _WIN32
    if (const std::string base = env("LOCALAPPDATA"); !base.empty()) {
        return fs::path(base) / "OpenStitch" / "live";
    }
    return fs::temp_directory_path() / "OpenStitch" / "live";
#else
    if (const std::string run = env("XDG_RUNTIME_DIR"); !run.empty()) {
        return fs::path(run) / "openstitch";
    }
    if (const std::string home = env("HOME"); !home.empty()) {
        return fs::path(home) / ".local" / "state" / "openstitch" / "live";
    }
    return fs::temp_directory_path() / "openstitch-live";
#endif
}

bool write_live_endpoint(const fs::path& dir, const LiveEndpoint& e) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        return false;
    }
#ifndef _WIN32
    fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
#endif
    const fs::path file = dir / (std::to_string(e.pid) + ".json");
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << json{{"pid", e.pid}, {"endpoint", e.endpoint}, {"token", e.token}}.dump();
    }
#ifndef _WIN32
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace,
                    ec); // 0600 : le jeton n'est lisible que par l'utilisateur
#endif
    return !ec;
}

void remove_live_endpoint(const fs::path& dir, int pid) {
    std::error_code ec;
    fs::remove(dir / (std::to_string(pid) + ".json"), ec);
}

std::optional<LiveEndpoint> find_live_endpoint(const fs::path& dir, int pid) {
    std::error_code ec;
    std::optional<LiveEndpoint> best;
    fs::file_time_type best_time{};
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec || entry.path().extension() != ".json") {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        const json doc = json::parse(in, nullptr, /*exceptions=*/false);
        if (doc.is_discarded() || !doc.is_object() || !doc.contains("endpoint") ||
            !doc.contains("token") || !doc.at("endpoint").is_string() ||
            !doc.at("token").is_string()) {
            continue;
        }
        LiveEndpoint e;
        e.pid = doc.value("pid", 0);
        e.endpoint = doc.at("endpoint").get<std::string>();
        e.token = doc.at("token").get<std::string>();
        if (pid > 0 && e.pid != pid) {
            continue;
        }
        const auto when = fs::last_write_time(entry.path(), ec);
        if (!best || when > best_time) {
            best = std::move(e);
            best_time = when;
        }
    }
    return best;
}

// --- Client local ----------------------------------------------------------------------------

struct LocalLineClient::Impl {
#ifdef _WIN32
    HANDLE handle{INVALID_HANDLE_VALUE};
#else
    int fd{-1};
#endif
    std::string buffer;

    ~Impl() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
#else
        if (fd >= 0) {
            ::close(fd);
        }
#endif
    }

    bool write_all(const std::string& data) {
#ifdef _WIN32
        DWORD done = 0;
        return WriteFile(handle, data.data(), static_cast<DWORD>(data.size()), &done, nullptr) &&
               done == data.size();
#else
        std::size_t sent = 0;
        while (sent < data.size()) {
            const ssize_t n = ::write(fd, data.data() + sent, data.size() - sent);
            if (n <= 0) {
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }
        return true;
#endif
    }

    // Lit jusqu'au prochain '\n' ; nullopt = connexion fermée.
    std::optional<std::string> read_line() {
        for (;;) {
            if (const auto nl = buffer.find('\n'); nl != std::string::npos) {
                std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                return line;
            }
            char chunk[4096];
#ifdef _WIN32
            DWORD got = 0;
            if (!ReadFile(handle, chunk, sizeof chunk, &got, nullptr) || got == 0) {
                return std::nullopt;
            }
            buffer.append(chunk, got);
#else
            const ssize_t n = ::read(fd, chunk, sizeof chunk);
            if (n <= 0) {
                return std::nullopt;
            }
            buffer.append(chunk, static_cast<std::size_t>(n));
#endif
        }
    }
};

LocalLineClient::~LocalLineClient() = default;

std::unique_ptr<LocalLineClient> LocalLineClient::connect(const std::string& endpoint) {
    std::unique_ptr<LocalLineClient> client(new LocalLineClient);
    client->impl_ = std::make_unique<Impl>();
#ifdef _WIN32
    HANDLE h = CreateFileA(endpoint.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    client->impl_->handle = h;
#else
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (endpoint.size() >= sizeof(addr.sun_path)) {
        return nullptr;
    }
    std::copy(endpoint.begin(), endpoint.end(), addr.sun_path);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return nullptr;
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        ::close(fd);
        return nullptr;
    }
    client->impl_->fd = fd;
#endif
    return client;
}

std::optional<std::string> LocalLineClient::exchange(const std::string& line) {
    if (!impl_ || !impl_->write_all(line + "\n")) {
        return std::nullopt;
    }
    return impl_->read_line();
}

} // namespace openstitch::mcp
