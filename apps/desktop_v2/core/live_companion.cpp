// SPDX-License-Identifier: Apache-2.0
#include "live_companion.hpp"

#include <algorithm>

namespace openstitch::desktop_v2 {

// Passerelle entre la politique d'outils du MCP et l'interface : journalise chaque demande, puis
// délègue la décision à l'utilisateur. Sans interface d'approbation, l'agent ne modifie rien.
class LiveCompanion::Gate final : public mcp::IApprovalGate {
public:
    explicit Gate(LiveCompanion& owner) : owner_(owner) {}

    bool approve(const mcp::ApprovalRequest& request) override {
        CompanionEvent event;
        event.tool = request.tool;
        event.label = request.label;
        for (const auto& item : request.items) {
            if (!event.detail.empty()) {
                event.detail += "; ";
            }
            event.detail += item.text;
        }
        event.revision = request.revision;
        const bool allowed = owner_.ask_ ? owner_.ask_(request) : false;
        event.outcome =
            allowed ? CompanionEvent::Outcome::Applied : CompanionEvent::Outcome::Refused;
        owner_.record(std::move(event));
        return allowed;
    }

private:
    LiveCompanion& owner_;
};

LiveCompanion::LiveCompanion(DocumentSession& session, Ask ask, DocumentName name)
    : session_(session), ask_(std::move(ask)), name_(std::move(name)),
      gate_(std::make_unique<Gate>(*this)) {
    service_ = std::make_unique<mcp::LiveService>(
        session_.liveBridge(), *gate_,
        [this] {
            mcp::LiveDocumentInfo info;
            info.name = name_ ? name_() : std::string("(sans titre)");
            info.dirty = session_.dirty();
            for (const auto& target : session_.selection().targets()) {
                if (target.kind == TargetKind::EmbroideryObject) {
                    info.selection.push_back(target.id);
                }
            }
            return info;
        },
        // Une modification installée par le pont : l'interface suit comme après un Annuler.
        [this] { session_.notifyExternalChange(); });
}

LiveCompanion::~LiveCompanion() = default;

std::size_t LiveCompanion::appliedCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(
        events_, [](const auto& e) { return e.outcome == CompanionEvent::Outcome::Applied; }));
}

std::size_t LiveCompanion::refusedCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(
        events_, [](const auto& e) { return e.outcome == CompanionEvent::Outcome::Refused; }));
}

void LiveCompanion::close() {
    session_.liveBridge().close();
}

void LiveCompanion::record(CompanionEvent event) {
    events_.push_back(std::move(event));
    if (onChanged_) {
        onChanged_();
    }
}

} // namespace openstitch::desktop_v2
