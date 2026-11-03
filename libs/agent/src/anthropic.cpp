// SPDX-License-Identifier: Apache-2.0
// Fournisseur Anthropic : API Messages avec utilisation d'outils (ADR 0002).
#include <algorithm>
#include <thread>

#include <nlohmann/json.hpp>

#include "openstitch/agent/agent.hpp"

namespace openstitch::agent {

namespace {

using json = nlohmann::json;

json block_to_json(const ContentBlock& b) {
    switch (b.kind) {
    case ContentBlock::Kind::Text:
        return json{{"type", "text"}, {"text", b.text}};
    case ContentBlock::Kind::Image:
        return json{
            {"type", "image"},
            {"source", {{"type", "base64"}, {"media_type", b.media_type}, {"data", b.data_b64}}}};
    case ContentBlock::Kind::ToolUse: {
        json input = json::parse(b.input_json.empty() ? "{}" : b.input_json, nullptr, false);
        return json{{"type", "tool_use"},
                    {"id", b.id},
                    {"name", b.name},
                    {"input", input.is_object() ? input : json::object()}};
    }
    case ContentBlock::Kind::ToolResult: {
        json content = json::array();
        for (const auto& c : b.content) {
            content.push_back(block_to_json(c));
        }
        return json{{"type", "tool_result"},
                    {"tool_use_id", b.id},
                    {"is_error", b.is_error},
                    {"content", content}};
    }
    }
    return json::object();
}

ContentBlock block_from_json(const json& j) {
    ContentBlock b;
    const std::string type = j.value("type", std::string{});
    if (type == "text") {
        b.kind = ContentBlock::Kind::Text;
        b.text = j.value("text", std::string{});
    } else if (type == "tool_use") {
        b.kind = ContentBlock::Kind::ToolUse;
        b.id = j.value("id", std::string{});
        b.name = j.value("name", std::string{});
        b.input_json = j.contains("input") ? j.at("input").dump() : "{}";
    } else {
        // Blocs inconnus (réflexion, etc.) : conservés comme texte vide, ignorés à l'affichage.
        b.kind = ContentBlock::Kind::Text;
    }
    return b;
}

ProviderResult fail(const char* code, std::string message, bool retryable, int retry_after_ms = 0) {
    ProviderResult r;
    r.error = {code, std::move(message), retryable, retry_after_ms};
    return r;
}

std::string provider_message(const std::string& body) {
    const json j = json::parse(body, nullptr, false);
    if (j.is_object() && j.contains("error") && j.at("error").is_object()) {
        return j.at("error").value("message", std::string{});
    }
    return {};
}

} // namespace

AnthropicProvider::AnthropicProvider(IHttpClient& http, std::string api_key,
                                     AnthropicOptions options)
    : http_(http), api_key_(std::move(api_key)), options_(std::move(options)) {}

std::string AnthropicProvider::build_body(const ProviderRequest& request) const {
    json body{{"model", request.model.empty() ? options_.model : request.model},
              {"max_tokens", request.max_tokens}};
    if (!request.system.empty()) {
        body["system"] = request.system;
    }
    json messages = json::array();
    for (const auto& m : request.messages) {
        json blocks = json::array();
        for (const auto& b : m.blocks) {
            blocks.push_back(block_to_json(b));
        }
        messages.push_back(json{{"role", m.role}, {"content", blocks}});
    }
    body["messages"] = messages;
    if (!request.tools.empty()) {
        json tools = json::array();
        for (const auto& t : request.tools) {
            json schema = json::parse(t.input_schema_json, nullptr, false);
            tools.push_back(
                json{{"name", t.name},
                     {"description", t.description},
                     {"input_schema", schema.is_object() ? schema : json{{"type", "object"}}}});
        }
        body["tools"] = tools;
    }
    return body.dump();
}

ProviderResult AnthropicProvider::parse_response(int status, const std::string& body) {
    if (status == 401 || status == 403) {
        return fail(code::kAuth,
                    "clé API refusée par le fournisseur (vérifiez-la dans les réglages)", false);
    }
    if (status == 429) {
        return fail(code::kQuota, "limite de débit ou de crédit atteinte chez le fournisseur",
                    true);
    }
    if (status >= 500 || status == 529) {
        return fail(code::kProvider, "service du fournisseur momentanément indisponible", true);
    }
    if (status != 200) {
        std::string detail = provider_message(body);
        return fail(code::kProvider,
                    "requête refusée (HTTP " + std::to_string(status) + ")" +
                        (detail.empty() ? std::string{} : " : " + detail),
                    false);
    }
    const json j = json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("content") || !j.at("content").is_array()) {
        return fail(code::kBadResponse, "réponse du fournisseur illisible", false);
    }
    ProviderResult r;
    r.ok = true;
    for (const auto& block : j.at("content")) {
        r.response.blocks.push_back(block_from_json(block));
    }
    r.response.stop_reason = j.value("stop_reason", std::string{});
    if (j.contains("usage") && j.at("usage").is_object()) {
        r.response.usage.input_tokens = j.at("usage").value("input_tokens", std::uint64_t{0});
        r.response.usage.output_tokens = j.at("usage").value("output_tokens", std::uint64_t{0});
    }
    return r;
}

ProviderResult AnthropicProvider::complete(const ProviderRequest& request,
                                           const CancelToken& cancel) {
    if (api_key_.empty()) {
        return fail(code::kAuth, "aucune clé API configurée", false);
    }
    const std::string body = build_body(request);
    const std::vector<HttpHeader> headers = {{"x-api-key", api_key_},
                                             {"anthropic-version", options_.api_version},
                                             {"content-type", "application/json"}};
    const int attempts = 1 + std::clamp(options_.max_retries, 0, 5); // retry borné
    ProviderResult last;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (cancel.cancelled()) {
            return fail(code::kCancelled, "interrompu", false);
        }
        const HttpResponse http =
            http_.post(options_.endpoint, headers, body, options_.timeout, cancel);
        if (http.cancelled || cancel.cancelled()) {
            return fail(code::kCancelled, "interrompu", false);
        }
        if (http.timed_out) {
            last = fail(code::kTimeout, "délai dépassé en attendant le fournisseur", true);
        } else if (http.status == 0) {
            last = fail(code::kNetwork,
                        "fournisseur injoignable (réseau ?)" +
                            (http.error.empty() ? std::string{} : " : " + http.error),
                        true);
        } else {
            last = parse_response(http.status, http.body);
            if (last.error.retry_after_ms == 0) {
                last.error.retry_after_ms = http.retry_after_ms;
            }
        }
        if (last.ok || !last.error.retryable || attempt + 1 >= attempts) {
            break;
        }
        const int wait = last.error.retry_after_ms > 0 ? std::min(last.error.retry_after_ms, 30000)
                                                       : options_.backoff_ms * (1 << attempt);
        if (options_.sleeper) {
            options_.sleeper(wait, cancel);
        } else {
            // Attente fractionnée pour réagir vite à une interruption.
            for (int waited = 0; waited < wait && !cancel.cancelled(); waited += 50) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }
    if (!last.ok) {
        last.error.message = redact(std::move(last.error.message), {api_key_});
    }
    return last;
}

} // namespace openstitch::agent
