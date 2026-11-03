// SPDX-License-Identifier: Apache-2.0
// openstitch-mcp : serveur MCP sur stdio (un message JSON-RPC par ligne). Le canal de protocole
// est une copie privée de stdout ; le descripteur 1 est redirigé vers stderr, de sorte qu'une
// bibliothèque tierce (OpenCV journalise sur stdout) ne puisse jamais corrompre le flux JSON-RPC.
//
//   openstitch-mcp            serveur stdio (lancé par le client MCP, sans argument)
//   openstitch-mcp --version  version du serveur et du protocole
//   openstitch-mcp --doctor   auto-test en mémoire (poignée de main, outils, rendu), code 0 / 2
//   openstitch-mcp --live [--pid N]
//                             mode live : les outils s'exécutent sur le projet OUVERT dans
//                             OpenStitch Desktop (tube nommé local, jeton ; aucun port réseau)
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define OS_DUP _dup
#define OS_DUP2 _dup2
#define OS_FDOPEN _fdopen
#else
#include <unistd.h>
#define OS_DUP dup
#define OS_DUP2 dup2
#define OS_FDOPEN fdopen
#endif

#include "openstitch/mcp/live.hpp"
#include "openstitch/mcp/server.hpp"

namespace {

// Transport vers le desktop qui (re)découvre l'hôte à la demande : le serveur MCP peut démarrer
// avant l'application, et celle-ci peut être relancée (nouveau jeton) sans redémarrer le client.
class DiscoveringTransport final : public openstitch::mcp::ILiveTransport {
public:
    explicit DiscoveringTransport(int pid) : pid_(pid) {}

    // Relu à chaque requête (peu coûteux) : un desktop relancé change de jeton.
    std::string token() const {
        const auto found =
            openstitch::mcp::find_live_endpoint(openstitch::mcp::live_directory(), pid_);
        return found ? found->token : std::string{};
    }

    std::optional<std::string> exchange(const std::string& line) override {
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (!client_) {
                const auto found =
                    openstitch::mcp::find_live_endpoint(openstitch::mcp::live_directory(), pid_);
                if (!found) {
                    return std::nullopt;
                }
                client_ = openstitch::mcp::LocalLineClient::connect(found->endpoint);
                if (!client_) {
                    return std::nullopt;
                }
            }
            if (const auto reply = client_->exchange(line)) {
                return reply;
            }
            client_.reset(); // connexion perdue : une nouvelle tentative redécouvre l'hôte
        }
        return std::nullopt;
    }

private:
    int pid_;
    std::unique_ptr<openstitch::mcp::LocalLineClient> client_;
};

int usage(const char* program) {
    std::fprintf(stderr,
                 "usage : %s [--version | --doctor | --live [--pid N]]\n"
                 "Sans argument : serveur MCP (JSON-RPC 2.0) sur stdio, à lancer depuis un client "
                 "MCP.\n",
                 program);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    bool live = false;
    int livePid = 0;
    if (argc > 1) {
        if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
            std::printf("%s %s\n", openstitch::mcp::kServerName, openstitch::mcp::kServerVersion);
            return 0;
        }
        if (argc == 2 && std::strcmp(argv[1], "--doctor") == 0) {
            bool ok = false;
            const std::string report = openstitch::mcp::self_test_json(ok);
            std::printf("%s\n", report.c_str());
            return ok ? 0 : 2;
        }
        if (std::strcmp(argv[1], "--live") != 0) {
            return usage(argv[0]);
        }
        if (argc == 4 && std::strcmp(argv[2], "--pid") == 0) {
            livePid = std::atoi(argv[3]);
        } else if (argc != 2) {
            return usage(argv[0]);
        }
        live = true;
    }

    std::FILE* protocol = nullptr;
    const int saved = OS_DUP(1);
    if (saved >= 0) {
        protocol = OS_FDOPEN(saved, "wb");
        OS_DUP2(2, 1);
    }
    if (protocol == nullptr) {
        std::fputs("openstitch-mcp : impossible de réserver le canal de protocole\n", stderr);
        return 1;
    }

    DiscoveringTransport transport(livePid);
    openstitch::mcp::LiveRelay relay(transport, [&transport] { return transport.token(); });
    openstitch::mcp::Server server(live ? &relay : nullptr);
    constexpr std::size_t kMaxRequestBytes = 8 * 1024 * 1024;

    // Un fil lit stdin en continu ; un fil de travail execute les requetes dans l'ordre. Ainsi un
    // `notifications/cancelled` recu pendant un outil long est traite aussitot (HP-PERF-020).
    std::mutex out_mutex;
    const auto emit = [&](const std::string& message) {
        const std::lock_guard lock(out_mutex);
        std::fwrite(message.data(), 1, message.size(), protocol);
        std::fputc('\n', protocol);
        std::fflush(protocol);
    };
    server.set_notifier(emit);

    std::mutex queue_mutex;
    std::condition_variable queue_ready;
    std::deque<std::pair<std::string, bool>> queue; // (ligne, trop grande)
    bool closed = false;
    std::thread worker([&] {
        while (true) {
            std::pair<std::string, bool> job;
            {
                std::unique_lock lock(queue_mutex);
                queue_ready.wait(lock, [&] { return closed || !queue.empty(); });
                if (queue.empty()) {
                    return;
                }
                job = std::move(queue.front());
                queue.pop_front();
            }
            std::optional<std::string> response;
            try {
                response =
                    job.second
                        ? std::optional<std::string>(
                              R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"requete de plus de 8 Mio"}})")
                        : server.handle(job.first);
            } catch (const std::exception& e) { // dernier rempart : le serveur ne doit pas mourir
                response = std::string(R"({"jsonrpc":"2.0","id":null,"error":{"code":-32603,)"
                                       R"("message":"erreur interne du serveur"}})");
                std::fputs(e.what(), stderr);
                std::fputc('\n', stderr);
            }
            if (response) {
                emit(*response);
            }
        }
    });

    std::string line;
    while (true) {
        line.clear();
        bool too_large = false;
        int next = 0;
        while ((next = std::cin.get()) != std::char_traits<char>::eof() && next != '\n') {
            if (line.size() < kMaxRequestBytes) {
                line.push_back(static_cast<char>(next));
            } else {
                too_large = true;
            }
        }
        if (next == std::char_traits<char>::eof() && line.empty() && !too_large) {
            break;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() && !too_large) {
            if (next == std::char_traits<char>::eof()) {
                break;
            }
            continue;
        }
        // Annulation : traitee tout de suite, hors file (notification : pas de reponse).
        if (!too_large && line.find("notifications/cancelled") != std::string::npos) {
            try {
                (void)server.handle(line);
            } catch (const std::exception&) {
            }
        } else {
            {
                const std::lock_guard lock(queue_mutex);
                queue.emplace_back(line, too_large);
            }
            queue_ready.notify_one();
        }
        if (next == std::char_traits<char>::eof()) {
            break;
        }
    }
    {
        const std::lock_guard lock(queue_mutex);
        closed = true;
    }
    queue_ready.notify_one();
    worker.join();
    return 0;
}
