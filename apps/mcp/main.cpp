// SPDX-License-Identifier: Apache-2.0
// openstitch-mcp : serveur MCP sur stdio (un message JSON-RPC par ligne). Le canal de protocole
// est une copie privée de stdout ; le descripteur 1 est redirigé vers stderr, de sorte qu'une
// bibliothèque tierce (OpenCV journalise sur stdout) ne puisse jamais corrompre le flux JSON-RPC.
#include <cstdio>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

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

#include "openstitch/mcp/server.hpp"

int main() {
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

    openstitch::mcp::Server server;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        std::optional<std::string> response;
        try {
            response = server.handle(line);
        } catch (const std::exception& e) { // dernier rempart : le serveur ne doit pas mourir
            response = std::string(R"({"jsonrpc":"2.0","id":null,"error":{"code":-32603,)"
                                   R"("message":"erreur interne du serveur"}})");
            std::fputs(e.what(), stderr);
            std::fputc('\n', stderr);
        }
        if (response) {
            std::fwrite(response->data(), 1, response->size(), protocol);
            std::fputc('\n', protocol);
            std::fflush(protocol);
        }
    }
    return 0;
}
