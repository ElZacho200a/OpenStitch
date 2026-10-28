// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace openstitch::mcp {

class LiveRelay;

inline constexpr const char* kServerName = "openstitch-mcp";
inline constexpr const char* kServerVersion = "1.1.0";

// Serveur MCP (Model Context Protocol, JSON-RPC 2.0) qui expose `libs/project_ops` à un agent IA
// sous forme d'outils typés. Une session garde UN projet ouvert en mémoire : l'agent enchaîne
// numérisation, inspection, lots d'opérations, contrôle qualité et rendu sans relancer de
// processus ni recharger le `.osp`. Aucun algorithme n'est réimplémenté (tout passe par
// `project_ops`) ; aucune E/S sur stdout/stderr ici : le transport (stdio) vit dans `apps/mcp`.
class Server {
public:
    Server();
    // Mode live (`openstitch-mcp --live`) : les appels d'outils sont relayés à OpenStitch Desktop
    // et s'exécutent sur le projet OUVERT ; `relay` n'est pas possédé et doit survivre au serveur.
    explicit Server(LiveRelay* relay);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Traite UN message JSON-RPC (une ligne). Retourne la réponse à émettre, ou `nullopt` pour
    // une notification (aucune réponse attendue).
    [[nodiscard]] std::optional<std::string> handle(std::string_view message);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Auto-test de bout en bout, en mémoire (aucun fichier, aucun réseau) : poignée de main
// `initialize`, `tools/list`, puis un petit projet (création, forme, contrôle qualité, rendu PNG,
// guide). Retourne le rapport JSON `openstitch.mcp-doctor/1` ; `ok` dit si tout est passé.
[[nodiscard]] std::string self_test_json(bool& ok);

} // namespace openstitch::mcp
