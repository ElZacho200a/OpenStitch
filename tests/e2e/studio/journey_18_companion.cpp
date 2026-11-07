// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"

#include <QTimer>
#include <deque>
#include <nlohmann/json.hpp>

#include "transform_batch.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

namespace {
// Only the external language model is scripted. MCP planning, previews, commands and the
// approval loop are real production implementations; this provider cannot use a network.
class OfflineProvider final : public agent::IAgentProvider {
public:
    explicit OfflineProvider(std::shared_ptr<std::deque<agent::ProviderResult>> replies)
        : replies_(std::move(replies)) {}
    std::string name() const override { return "Offline E2E fixture"; }
    agent::ProviderCapabilities capabilities() const override { return {}; }
    agent::ProviderResult complete(const agent::ProviderRequest&,
                                   const agent::CancelToken&) override {
        if (replies_->empty()) {
            agent::ProviderResult error;
            error.error = {agent::code::kNetwork, "Offline fixture exhausted", false, 0};
            return error;
        }
        auto result = replies_->front();
        replies_->pop_front();
        return result;
    }

private:
    std::shared_ptr<std::deque<agent::ProviderResult>> replies_;
};

agent::ProviderResult toolReply(const std::string& arguments) {
    agent::ProviderResult result;
    result.ok = true;
    agent::ContentBlock block;
    block.kind = agent::ContentBlock::Kind::ToolUse;
    block.id = "offline-e2e-call";
    block.name = "project_apply";
    block.input_json = arguments;
    result.response.blocks.push_back(block);
    return result;
}

agent::ProviderResult finalReply() {
    agent::ProviderResult result;
    result.ok = true;
    agent::ContentBlock block;
    block.text = "Offline fixture complete";
    result.response.blocks.push_back(block);
    return result;
}
} // namespace

class Journey18 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        auto replies = std::make_shared<std::deque<agent::ProviderResult>>();
        Harness h({}, [replies](const std::string&, const std::string&) {
            return std::static_pointer_cast<agent::IAgentProvider>(
                std::make_shared<OfflineProvider>(replies));
        });
        QVERIFY(h.ready());
        QVERIFY(h.rectangles());
        QVERIFY(h.prepareSavePath());
        QVERIFY(h.companion.setApiKey(QStringLiteral("sk-ant-TEST-OFFLINE-0000000000000000")));
        h.companion.setProviderEnabled(true);
        QVERIFY(h.companion.providerReady());
        const auto id = h.session.project().embroidery_objects.front().id.value;
        const auto batch = nlohmann::json::parse(batchTranslate({id}, 2, 1));
        const auto request = nlohmann::json{{"operations", batch.at("operations")}}.dump();
        const auto before = h.bytes();
        const auto depth = h.steps();
        replies->push_back(toolReply(request));
        replies->push_back(finalReply());
        bool sawProposal = false;
        bool previewPresent = false;
        bool unchangedBeforeApproval = false;
        bool clickedApproval = false;
        QTimer approve;
        approve.setInterval(10);
        connect(&approve, &QTimer::timeout, this, [&] {
            if (!h.companion.proposalPending())
                return;
            sawProposal = true;
            const auto proposal = h.companion.proposal();
            previewPresent = !proposal.value(QStringLiteral("before")).toString().isEmpty() &&
                             !proposal.value(QStringLiteral("after")).toString().isEmpty();
            unchangedBeforeApproval = h.bytes() == before && h.steps() == depth;
            clickedApproval = h.click("companionApprove");
            if (clickedApproval)
                approve.stop();
        });
        QVERIFY(h.click("companionButton"));
        approve.start();
        QVERIFY(h.type("companionInput", QStringLiteral("Translate the selected fixture object")));
        QTRY_VERIFY_WITH_TIMEOUT(!h.companion.busy() && h.companion.events().size() == 1, 30000);
        approve.stop();
        QVERIFY(sawProposal && previewPresent && unchangedBeforeApproval && clickedApproval);
        QCOMPARE(h.steps(), depth + 1);
        QCOMPARE(h.companion.events().front().toMap().value(QStringLiteral("outcome")).toString(),
                 QStringLiteral("applied"));
        QVERIFY(h.settle());
        QVERIFY(h.bytes() != before);
        QVERIFY(h.saveRoundTrip());
        QVERIFY(h.click("companionUndo"));
        QVERIFY(h.settle());
        QCOMPARE(h.bytes(), before);
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey18>("18");
}
#include "journey_18_companion.moc"
