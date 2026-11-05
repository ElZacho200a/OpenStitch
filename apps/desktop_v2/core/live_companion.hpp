// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- compagnon IA : un agent (MCP, `openstitch-mcp --live`) travaille sur le document
// OUVERT, pas sur une copie. Tout passe par `DocumentSession::liveBridge()` : la même pile
// d'annulation que l'utilisateur, une seule étape d'annulation par modification, une approbation
// explicite avant toute écriture, un refus si le document a changé pendant le calcul de l'agent.
// Cette classe ne contient aucune interface : elle relie le pont, la politique d'outils
// (`libs/mcp`) et un journal d'activité. Sans Qt.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "document_session.hpp"
#include "openstitch/mcp/live.hpp"

namespace openstitch::desktop_v2 {

// Une demande de l'agent et ce qu'elle est devenue.
struct CompanionEvent {
    enum class Outcome : std::uint8_t { Pending, Applied, Refused };
    std::string tool;
    std::string label;  // nom de l'étape d'annulation proposée
    std::string detail; // résumé lisible des opérations (rapport du lot)
    std::uint64_t revision{0};
    Outcome outcome{Outcome::Pending};
};

class LiveCompanion {
public:
    // `ask` : interface d'approbation (fil GUI). Elle reçoit la demande complète ; `true` =
    // appliquer.
    using Ask = std::function<bool(const mcp::ApprovalRequest&)>;
    using DocumentName = std::function<std::string()>;

    LiveCompanion(DocumentSession& session, Ask ask, DocumentName name);
    ~LiveCompanion();
    LiveCompanion(const LiveCompanion&) = delete;
    LiveCompanion& operator=(const LiveCompanion&) = delete;

    [[nodiscard]] mcp::LiveService& service() { return *service_; }
    [[nodiscard]] const std::vector<CompanionEvent>& events() const { return events_; }
    [[nodiscard]] std::size_t appliedCount() const;
    [[nodiscard]] std::size_t refusedCount() const;

    // Appelé à chaque nouvel événement du journal (fil GUI) : l'interface se met à jour.
    void setOnChanged(std::function<void()> fn) { onChanged_ = std::move(fn); }

    // L'application se ferme ou désactive le pont : plus aucun appel d'agent ne peut aboutir.
    void close();

private:
    class Gate;
    void record(CompanionEvent event);

    DocumentSession& session_;
    Ask ask_;
    DocumentName name_;
    std::unique_ptr<Gate> gate_;
    std::unique_ptr<mcp::LiveService> service_;
    std::vector<CompanionEvent> events_;
    std::function<void()> onChanged_;
};

} // namespace openstitch::desktop_v2
