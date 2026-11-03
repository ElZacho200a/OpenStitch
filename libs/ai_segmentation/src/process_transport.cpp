// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/process_transport.hpp"

#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace openstitch::ai_segmentation {

namespace {

AiError error_of(AiErrorCode code, std::string detail) {
    return AiError{code, default_message(code), std::move(detail)};
}

// File de lignes alimentée par le thread lecteur.
struct LineQueue {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::string> lines;
    bool eof{false};
};

void push_chunk(LineQueue& q, std::string& partial, const char* data, std::size_t size) {
    partial.append(data, size);
    std::size_t pos = 0;
    std::size_t nl = 0;
    std::lock_guard lock(q.mutex);
    while ((nl = partial.find('\n', pos)) != std::string::npos) {
        std::string line = partial.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            q.lines.push_back(std::move(line));
        }
        pos = nl + 1;
    }
    partial.erase(0, pos);
    q.cv.notify_all();
}

void finish(LineQueue& q) {
    std::lock_guard lock(q.mutex);
    q.eof = true;
    q.cv.notify_all();
}

#ifdef _WIN32

std::wstring widen(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

// Quotage d'un argument selon les règles de CommandLineToArgvW.
std::wstring quote(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring out = L"\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(arg[i]);
        }
    }
    out.push_back(L'"');
    return out;
}

class ProcessTransport final : public ITransport {
public:
    ProcessTransport(HANDLE process, HANDLE in_write, HANDLE out_read)
        : process_(process), in_(in_write), out_(out_read) {
        reader_ = std::thread([this] {
            std::string partial;
            char buffer[4096];
            DWORD got = 0;
            while (ReadFile(out_, buffer, sizeof buffer, &got, nullptr) && got > 0) {
                push_chunk(queue_, partial, buffer, got);
            }
            finish(queue_);
        });
    }
    ~ProcessTransport() override {
        terminate();
        if (reader_.joinable()) {
            reader_.join();
        }
        if (out_ != nullptr) {
            CloseHandle(out_);
        }
        if (process_ != nullptr) {
            CloseHandle(process_);
        }
    }

    AiResult<void> write_line(std::string_view line) override {
        std::lock_guard lock(write_mutex_);
        if (in_ == nullptr) {
            return std::unexpected(error_of(AiErrorCode::WorkerCrashed, "stdin fermé"));
        }
        std::string data(line);
        data.push_back('\n');
        DWORD written = 0;
        if (!WriteFile(in_, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) ||
            written != data.size()) {
            return std::unexpected(
                error_of(AiErrorCode::WorkerCrashed, "écriture vers le worker impossible"));
        }
        return {};
    }

    AiResult<std::optional<std::string>> read_line(std::chrono::milliseconds timeout) override {
        std::unique_lock lock(queue_.mutex);
        queue_.cv.wait_for(lock, timeout, [this] { return !queue_.lines.empty() || queue_.eof; });
        if (!queue_.lines.empty()) {
            std::string line = std::move(queue_.lines.front());
            queue_.lines.pop_front();
            return std::optional<std::string>(std::move(line));
        }
        if (queue_.eof) {
            return std::unexpected(error_of(AiErrorCode::WorkerCrashed, "flux du worker fermé"));
        }
        return std::optional<std::string>{};
    }

    void terminate() override {
        std::lock_guard lock(write_mutex_);
        if (in_ != nullptr) {
            CloseHandle(in_);
            in_ = nullptr;
        }
        if (process_ != nullptr && !terminated_.exchange(true)) {
            if (WaitForSingleObject(process_, 300) == WAIT_TIMEOUT) {
                TerminateProcess(process_, 1);
            }
        }
    }

private:
    HANDLE process_;
    HANDLE in_;
    HANDLE out_;
    std::thread reader_;
    LineQueue queue_;
    std::mutex write_mutex_;
    std::atomic<bool> terminated_{false};
};

#else

class ProcessTransport final : public ITransport {
public:
    ProcessTransport(pid_t pid, int in_write, int out_read)
        : pid_(pid), in_(in_write), out_(out_read) {
        reader_ = std::thread([this] {
            std::string partial;
            char buffer[4096];
            ssize_t got = 0;
            while ((got = ::read(out_, buffer, sizeof buffer)) > 0 || (got < 0 && errno == EINTR)) {
                if (got > 0) {
                    push_chunk(queue_, partial, buffer, static_cast<std::size_t>(got));
                }
            }
            finish(queue_);
        });
    }
    ~ProcessTransport() override {
        terminate();
        if (reader_.joinable()) {
            reader_.join();
        }
        ::close(out_);
    }

    AiResult<void> write_line(std::string_view line) override {
        std::lock_guard lock(write_mutex_);
        if (in_ < 0) {
            return std::unexpected(error_of(AiErrorCode::WorkerCrashed, "stdin fermé"));
        }
        std::string data(line);
        data.push_back('\n');
        std::size_t done = 0;
        while (done < data.size()) {
            const ssize_t n = ::write(in_, data.data() + done, data.size() - done);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                return std::unexpected(
                    error_of(AiErrorCode::WorkerCrashed, "écriture vers le worker impossible"));
            }
            done += static_cast<std::size_t>(n);
        }
        return {};
    }

    AiResult<std::optional<std::string>> read_line(std::chrono::milliseconds timeout) override {
        std::unique_lock lock(queue_.mutex);
        queue_.cv.wait_for(lock, timeout, [this] { return !queue_.lines.empty() || queue_.eof; });
        if (!queue_.lines.empty()) {
            std::string line = std::move(queue_.lines.front());
            queue_.lines.pop_front();
            return std::optional<std::string>(std::move(line));
        }
        if (queue_.eof) {
            return std::unexpected(error_of(AiErrorCode::WorkerCrashed, "flux du worker fermé"));
        }
        return std::optional<std::string>{};
    }

    void terminate() override {
        std::lock_guard lock(write_mutex_);
        if (in_ >= 0) {
            ::close(in_);
            in_ = -1;
        }
        if (!terminated_.exchange(true)) {
            // Laisse le worker se terminer sur fermeture de stdin, puis force.
            for (int i = 0; i < 6; ++i) {
                int status = 0;
                if (::waitpid(pid_, &status, WNOHANG) == pid_) {
                    return;
                }
                ::usleep(50 * 1000);
            }
            ::kill(pid_, SIGKILL);
            int status = 0;
            ::waitpid(pid_, &status, 0);
        }
    }

private:
    pid_t pid_;
    int in_;
    int out_;
    std::thread reader_;
    LineQueue queue_;
    std::mutex write_mutex_;
    std::atomic<bool> terminated_{false};
};

#endif

} // namespace

AiResult<std::unique_ptr<ITransport>> spawn_process(const ProcessSpec& spec) {
    if (spec.executable.empty()) {
        return std::unexpected(error_of(AiErrorCode::WorkerNotConfigured, "exécutable vide"));
    }
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE in_read = nullptr;
    HANDLE in_write = nullptr;
    HANDLE out_read = nullptr;
    HANDLE out_write = nullptr;
    if (!CreatePipe(&in_read, &in_write, &sa, 0) || !CreatePipe(&out_read, &out_write, &sa, 0)) {
        return std::unexpected(error_of(AiErrorCode::WorkerStartFailed, "CreatePipe a échoué"));
    }
    SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);

    std::wstring command = quote(widen(spec.executable));
    for (const std::string& a : spec.args) {
        command += L" " + quote(widen(a));
    }
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_read;
    si.hStdOutput = out_write;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    const std::wstring cwd = spec.working_dir.wstring();
    const BOOL ok =
        CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                       cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(in_read);
    CloseHandle(out_write);
    if (!ok) {
        const DWORD err = GetLastError();
        CloseHandle(in_write);
        CloseHandle(out_read);
        return std::unexpected(
            error_of(err == ERROR_FILE_NOT_FOUND ? AiErrorCode::PythonNotFound
                                                 : AiErrorCode::WorkerStartFailed,
                     spec.executable + " (code Windows " + std::to_string(err) + ")"));
    }
    CloseHandle(pi.hThread);
    return std::unique_ptr<ITransport>(new ProcessTransport(pi.hProcess, in_write, out_read));
#else
    static const bool sigpipe_ignored = [] {
        std::signal(SIGPIPE, SIG_IGN); // un worker mort donne EPIPE, pas la mort d'OpenStitch
        return true;
    }();
    (void)sigpipe_ignored;
    int to_child[2];
    int from_child[2];
    if (::pipe(to_child) != 0 || ::pipe(from_child) != 0) {
        return std::unexpected(error_of(AiErrorCode::WorkerStartFailed, "pipe a échoué"));
    }
    // Les extrémités du parent ne doivent pas fuir vers d'autres workers lancés plus tard.
    ::fcntl(to_child[1], F_SETFD, FD_CLOEXEC);
    ::fcntl(from_child[0], F_SETFD, FD_CLOEXEC);
    std::vector<std::string> storage;
    storage.push_back(spec.executable);
    storage.insert(storage.end(), spec.args.begin(), spec.args.end());
    std::vector<char*> argv;
    for (std::string& s : storage) {
        argv.push_back(s.data());
    }
    argv.push_back(nullptr);
    const std::string cwd = spec.working_dir.string();
    const pid_t pid = ::fork();
    if (pid < 0) {
        return std::unexpected(error_of(AiErrorCode::WorkerStartFailed, "fork a échoué"));
    }
    if (pid == 0) {
        ::dup2(to_child[0], STDIN_FILENO);
        ::dup2(from_child[1], STDOUT_FILENO);
        ::close(to_child[0]);
        ::close(to_child[1]);
        ::close(from_child[0]);
        ::close(from_child[1]);
        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) {
            ::_exit(126);
        }
        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }
    ::close(to_child[0]);
    ::close(from_child[1]);
    return std::unique_ptr<ITransport>(new ProcessTransport(pid, to_child[1], from_child[0]));
#endif
}

} // namespace openstitch::ai_segmentation
