// SPDX-License-Identifier: Apache-2.0
#include "crash_report.hpp"

#include "openstitch/core/log.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dbghelp.h>
#endif

namespace openstitch::desktop {

namespace fs = std::filesystem;

fs::path crashDumpDirectory() {
    const fs::path base = openstitch::user_data_directory();
    return base.empty() ? fs::path() : base / "crash";
}

namespace {

bool isDump(const fs::path& p) {
    return p.extension() == ".dmp";
}

fs::path seenMarker(const fs::path& dump) {
    return fs::path(dump).concat(".seen");
}

std::vector<fs::path> listDumps(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && isDump(it->path())) {
            out.push_back(it->path());
        }
    }
    // Les noms portent l'horodatage : l'ordre lexicographique est l'ordre chronologique.
    std::sort(out.begin(), out.end());
    return out;
}

#ifdef _WIN32
// Chemin pré-calculé : aucune allocation dans le gestionnaire de crash.
wchar_t g_dumpDir[MAX_PATH] = {};
bool g_installed = false;
LONG g_writing = 0;

void writeDump(EXCEPTION_POINTERS* info) {
    if (InterlockedExchange(&g_writing, 1) != 0) {
        return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t path[MAX_PATH + 64];
    swprintf_s(path, L"%s\\crash-%04d%02d%02d-%02d%02d%02d.dmp", g_dumpDir, t.wYear, t.wMonth,
               t.wDay, t.wHour, t.wMinute, t.wSecond);
    HANDLE file =
        CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = info;
    mei.ClientPointers = FALSE;
    // Pile et modules seulement (MiniDumpNormal) : pas de tas, donc aucune donnée de projet.
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal,
                      info != nullptr ? &mei : nullptr, nullptr, nullptr);
    CloseHandle(file);
}

LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* info) {
    spdlog::critical("exception non geree (code {:#x}) : minidump ecrit",
                     info != nullptr ? info->ExceptionRecord->ExceptionCode : 0UL);
    spdlog::default_logger()->flush();
    writeDump(info);
    return EXCEPTION_EXECUTE_HANDLER;
}

void terminateHandler() {
    spdlog::critical("std::terminate : minidump ecrit");
    spdlog::default_logger()->flush();
    writeDump(nullptr);
    std::abort();
}
#endif

} // namespace

bool installCrashHandler(const fs::path& dir) {
#ifdef _WIN32
    if (g_installed) {
        return true;
    }
    if (dir.empty()) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::wstring w = dir.wstring();
    if (w.size() >= MAX_PATH - 1) {
        return false;
    }
    wcsncpy_s(g_dumpDir, w.c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(unhandledFilter);
    std::set_terminate(terminateHandler);
    g_installed = true;
    return true;
#else
    (void)dir;
    return false;
#endif
}

std::optional<fs::path> findRecentUnseenDump(const fs::path& dir, std::chrono::seconds maxAge) {
    const auto dumps = listDumps(dir);
    const auto now = fs::file_time_type::clock::now();
    for (auto it = dumps.rbegin(); it != dumps.rend(); ++it) {
        std::error_code ec;
        if (fs::exists(seenMarker(*it), ec)) {
            continue;
        }
        const auto written = fs::last_write_time(*it, ec);
        if (!ec && now - written <= maxAge) {
            return *it;
        }
        return std::nullopt; // le plus récent non vu est déjà trop ancien
    }
    return std::nullopt;
}

std::optional<fs::path> findLatestDump(const fs::path& dir) {
    const auto dumps = listDumps(dir);
    if (dumps.empty()) {
        return std::nullopt;
    }
    return dumps.back();
}

void markDumpSeen(const fs::path& dump) {
    std::ofstream(seenMarker(dump)) << "seen\n";
}

} // namespace openstitch::desktop
