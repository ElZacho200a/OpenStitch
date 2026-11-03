// SPDX-License-Identifier: Apache-2.0
#include "openstitch/core/atomic_file.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <system_error>
#include <thread>

#include "openstitch/core/input_limits.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace openstitch {

namespace {

namespace fs = std::filesystem;

std::string utf8(const fs::path& path) {
    const auto u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::uint64_t process_id() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

// Nom unique dans le dossier de la cible : pid, compteur, horloge -> deux processus ou deux
// threads ne partagent jamais un temporaire.
fs::path unique_temp_path(const fs::path& target) {
    static std::atomic<std::uint64_t> counter{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    char suffix[96];
    std::snprintf(suffix, sizeof(suffix), ".%llx-%llx-%llx.tmp",
                  static_cast<unsigned long long>(process_id()),
                  static_cast<unsigned long long>(counter.fetch_add(1)),
                  static_cast<unsigned long long>(ticks) & 0xffffffffULL);
    fs::path tmp = target;
    tmp += suffix;
    return tmp;
}

void remove_quietly(const fs::path& path) {
    std::error_code ec;
    fs::remove(path, ec);
}

Error write_error(const fs::path& target, std::string detail) {
    return Error{ErrorCategory::Internal,
                 "Échec d'écriture : " + utf8(target) +
                     " (disque plein, dossier inaccessible ou fichier verrouillé ?)",
                 std::move(detail), limits::code::kWriteFailed,
                 "Libérez de l'espace ou choisissez un autre dossier ; l'ancien fichier est "
                 "conservé."};
}

#ifdef _WIN32

std::string last_error_text(const char* what) {
    return std::string(what) + " (code Windows " + std::to_string(GetLastError()) + ")";
}

// Écrit `bytes` dans un nouveau fichier `tmp` (CREATE_NEW) puis FlushFileBuffers.
Result<void> write_bytes_durable(const fs::path& tmp, const fs::path& target,
                                 std::span<const std::uint8_t> bytes) {
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return std::unexpected(write_error(target, last_error_text("CreateFileW")));
    }
    std::size_t done = 0;
    while (done < bytes.size()) {
        const DWORD chunk =
            static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, std::size_t{1} << 28));
        DWORD written = 0;
        if (!WriteFile(h, bytes.data() + done, chunk, &written, nullptr) || written == 0) {
            const auto detail = last_error_text("WriteFile");
            CloseHandle(h);
            return std::unexpected(write_error(target, detail));
        }
        done += written;
    }
    if (!FlushFileBuffers(h)) {
        const auto detail = last_error_text("FlushFileBuffers");
        CloseHandle(h);
        return std::unexpected(write_error(target, detail));
    }
    if (!CloseHandle(h)) {
        return std::unexpected(write_error(target, last_error_text("CloseHandle")));
    }
    return {};
}

// Vide sur disque un fichier écrit par un tiers (ex. minizip) : rouvre puis FlushFileBuffers.
Result<void> flush_existing(const fs::path& tmp, const fs::path& target) {
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return std::unexpected(write_error(target, last_error_text("CreateFileW(flush)")));
    }
    const BOOL ok = FlushFileBuffers(h);
    const auto detail = ok ? std::string() : last_error_text("FlushFileBuffers");
    CloseHandle(h);
    if (!ok) {
        return std::unexpected(write_error(target, detail));
    }
    return {};
}

Result<void> replace_target(const fs::path& tmp, const fs::path& target) {
    std::error_code ec;
    if (fs::is_directory(target, ec)) {
        return std::unexpected(write_error(target, "la cible est un dossier"));
    }
    // Deux écritures simultanées ou un lecteur antivirus peuvent refuser brièvement le
    // remplacement : quelques tentatives rapprochées avant d'abandonner.
    DWORD lastError = 0;
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (MoveFileExW(tmp.c_str(), target.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return {};
        }
        lastError = GetLastError();
        if (lastError != ERROR_ACCESS_DENIED && lastError != ERROR_SHARING_VIOLATION &&
            lastError != ERROR_LOCK_VIOLATION) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool busy = lastError == ERROR_ACCESS_DENIED || lastError == ERROR_SHARING_VIOLATION ||
                      lastError == ERROR_LOCK_VIOLATION;
    return fail_coded(
        ErrorCategory::Internal, busy ? limits::code::kWriteTargetBusy : limits::code::kWriteFailed,
        "Impossible de finaliser l'écriture : " + utf8(target) +
            (busy ? " (fichier ouvert dans un autre programme ?)" : ""),
        "Fermez le programme qui utilise ce fichier puis recommencez ; l'ancien fichier est "
        "conservé.",
        "MoveFileExW code Windows " + std::to_string(lastError));
}

#else

std::string errno_text(const char* what) {
    const std::error_code ec(errno, std::generic_category());
    return std::string(what) + " : " + ec.message();
}

Result<void> write_bytes_durable(const fs::path& tmp, const fs::path& target,
                                 std::span<const std::uint8_t> bytes) {
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        return std::unexpected(write_error(target, errno_text("open")));
    }
    std::size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            const auto detail = errno_text("write");
            ::close(fd);
            return std::unexpected(write_error(target, detail));
        }
        done += static_cast<std::size_t>(n);
    }
    if (::fsync(fd) != 0) {
        const auto detail = errno_text("fsync");
        ::close(fd);
        return std::unexpected(write_error(target, detail));
    }
    if (::close(fd) != 0) {
        return std::unexpected(write_error(target, errno_text("close")));
    }
    return {};
}

Result<void> flush_existing(const fs::path& tmp, const fs::path& target) {
    const int fd = ::open(tmp.c_str(), O_RDWR);
    if (fd < 0) {
        return std::unexpected(write_error(target, errno_text("open(flush)")));
    }
    const int rc = ::fsync(fd);
    const auto detail = rc == 0 ? std::string() : errno_text("fsync");
    ::close(fd);
    if (rc != 0) {
        return std::unexpected(write_error(target, detail));
    }
    return {};
}

Result<void> replace_target(const fs::path& tmp, const fs::path& target) {
    std::error_code ec;
    if (fs::is_directory(target, ec)) {
        return std::unexpected(write_error(target, "la cible est un dossier"));
    }
    if (::rename(tmp.c_str(), target.c_str()) != 0) {
        return fail_coded(ErrorCategory::Internal, limits::code::kWriteFailed,
                          "Impossible de finaliser l'écriture : " + utf8(target),
                          "Vérifiez les droits du dossier ; l'ancien fichier est conservé.",
                          errno_text("rename"));
    }
    // Rend le renommage durable : fsync du dossier (au mieux, l'échec n'invalide pas le fichier).
    fs::path dir = target.parent_path();
    if (dir.empty()) {
        dir = ".";
    }
    const int dfd = ::open(dir.c_str(), O_RDONLY);
    if (dfd >= 0) {
        (void)::fsync(dfd);
        ::close(dfd);
    }
    return {};
}

#endif

} // namespace

Result<void> write_file_atomic_with(const fs::path& target, const TempFileWriter& writer) {
    const fs::path tmp = unique_temp_path(target);
    if (auto written = writer(tmp); !written) {
        remove_quietly(tmp);
        return written;
    }
    if (auto flushed = flush_existing(tmp, target); !flushed) {
        remove_quietly(tmp);
        return flushed;
    }
    if (auto replaced = replace_target(tmp, target); !replaced) {
        remove_quietly(tmp);
        return replaced;
    }
    return {};
}

Result<void> write_file_atomic(const fs::path& target, std::span<const std::uint8_t> bytes) {
    const fs::path tmp = unique_temp_path(target);
    if (auto written = write_bytes_durable(tmp, target, bytes); !written) {
        remove_quietly(tmp);
        return written;
    }
    if (auto replaced = replace_target(tmp, target); !replaced) {
        remove_quietly(tmp);
        return replaced;
    }
    return {};
}

Result<void> write_file_atomic(const fs::path& target, std::string_view text) {
    return write_file_atomic(
        target, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),
                                              text.size()));
}

} // namespace openstitch
