// SPDX-License-Identifier: Apache-2.0
// Boucle d'agent : modèle ↔ outils OpenStitch autorisés, interruptible, bornée.
#include <algorithm>

#include "openstitch/agent/agent.hpp"

namespace openstitch::agent {

namespace {

std::string truncated(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) {
        return text;
    }
    return text.substr(0, limit) + "\n… (résultat tronqué : " + std::to_string(text.size()) +
           " caractères)";
}

ContentBlock text_block(std::string text) {
    ContentBlock b;
    b.kind = ContentBlock::Kind::Text;
    b.text = std::move(text);
    return b;
}

ContentBlock error_result(const std::string& id, const std::string& message) {
    ContentBlock r;
    r.kind = ContentBlock::Kind::ToolResult;
    r.id = id;
    r.is_error = true;
    r.content.push_back(text_block(message));
    return r;
}

} // namespace

AgentRunner::AgentRunner(IAgentProvider& provider, IToolHost& host, ToolPolicy policy,
                         RunnerOptions options)
    : provider_(provider), host_(host), policy_(std::move(policy)), options_(std::move(options)) {}

RunOutcome AgentRunner::run(std::vector<Message>& history, const std::string& user_text,
                            IAgentObserver& observer, const CancelToken& cancel) {
    RunOutcome outcome;
    history.push_back({"user", {text_block(user_text)}});

    // L'agent ne voit que les outils autorisés par la politique.
    std::vector<ToolSpec> tools;
    for (auto& spec : host_.tools()) {
        if (policy_.permits(spec.name)) {
            tools.push_back(std::move(spec));
        }
    }

    for (int step = 0; step < std::max(1, options_.max_steps); ++step) {
        if (cancel.cancelled()) {
            outcome.status = RunOutcome::Status::Cancelled;
            return outcome;
        }
        ProviderRequest request;
        request.model = options_.model;
        request.system = options_.system;
        request.messages = history;
        request.tools = tools;
        request.max_tokens = options_.max_tokens;
        ProviderResult result = provider_.complete(request, cancel);
        ++outcome.steps;
        if (!result.ok) {
            outcome.error = result.error;
            outcome.status = result.error.code == code::kCancelled ? RunOutcome::Status::Cancelled
                                                                   : RunOutcome::Status::Failed;
            return outcome;
        }
        outcome.usage.add(result.response.usage);
        observer.on_usage(result.response.usage, outcome.usage);

        std::vector<ContentBlock> uses;
        for (const auto& block : result.response.blocks) {
            if (block.kind == ContentBlock::Kind::Text && !block.text.empty()) {
                observer.on_text(block.text);
            } else if (block.kind == ContentBlock::Kind::ToolUse) {
                uses.push_back(block);
            }
        }
        history.push_back({"assistant", result.response.blocks});
        if (uses.empty()) {
            return outcome; // réponse finale
        }

        // Exécute chaque appel d'outil ; un appel non autorisé n'est JAMAIS exécuté. Si
        // l'utilisateur interrompt, chaque appel reçoit un résultat d'erreur pour garder un
        // historique valide (tout tool_use doit avoir son tool_result).
        Message results{"user", {}};
        bool interrupted = false;
        for (const auto& use : uses) {
            if (cancel.cancelled()) {
                interrupted = true;
                results.blocks.push_back(error_result(use.id, "interrompu par l'utilisateur"));
                continue;
            }
            ToolHostResult tool_result;
            std::string refusal;
            if (!policy_.permits(use.name)) {
                refusal = "outil non autorisé : « " + use.name + " »";
                tool_result.is_error = true;
                tool_result.content.push_back(text_block(refusal));
            } else {
                tool_result = host_.call(use.name, use.input_json);
                for (auto& c : tool_result.content) {
                    if (c.kind == ContentBlock::Kind::Text) {
                        c.text = truncated(c.text, options_.max_result_chars);
                    }
                }
                if (tool_result.is_error) {
                    refusal = "l'outil a signalé une erreur";
                }
            }
            observer.on_tool_call(use.name, use.input_json, tool_result, refusal);
            ContentBlock block;
            block.kind = ContentBlock::Kind::ToolResult;
            block.id = use.id;
            block.is_error = tool_result.is_error;
            block.content = std::move(tool_result.content);
            results.blocks.push_back(std::move(block));
        }
        history.push_back(std::move(results));
        if (interrupted || cancel.cancelled()) {
            outcome.status = RunOutcome::Status::Cancelled;
            return outcome;
        }
        if (options_.token_budget > 0 &&
            outcome.usage.input_tokens + outcome.usage.output_tokens >= options_.token_budget) {
            outcome.status = RunOutcome::Status::BudgetExceeded;
            return outcome;
        }
    }
    outcome.status = RunOutcome::Status::StepLimit;
    outcome.error = {code::kStepLimit,
                     "trop d'allers-retours avec les outils : la demande est interrompue", false,
                     0};
    return outcome;
}

} // namespace openstitch::agent
