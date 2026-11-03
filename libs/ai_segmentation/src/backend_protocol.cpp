// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/backend_protocol.hpp"

#include <algorithm>
#include <charconv>
#include <nlohmann/json.hpp>

namespace openstitch::ai_segmentation {

namespace {

using json = nlohmann::json;

AiError invalid(std::string detail) {
    return AiError{AiErrorCode::InvalidWorkerResponse,
                   default_message(AiErrorCode::InvalidWorkerResponse), std::move(detail)};
}

std::string str(const json& j, const char* key, std::string fallback = {}) {
    const auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : fallback;
}

std::optional<double> opt_number(const json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number()) {
        return std::nullopt;
    }
    return it->get<double>();
}

std::int64_t integer(const json& j, const char* key, std::int64_t fallback = 0) {
    const auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? static_cast<std::int64_t>(it->get<double>())
                                              : fallback;
}

std::array<int, 4> int4(const json& j, const char* key) {
    std::array<int, 4> out{};
    const auto it = j.find(key);
    if (it != j.end() && it->is_array() && it->size() == 4) {
        for (std::size_t i = 0; i < 4; ++i) {
            out[i] = static_cast<int>((*it)[i].is_number() ? (*it)[i].get<double>() : 0.0);
        }
    }
    return out;
}

BackendCapabilities parse_capabilities(const json& b) {
    BackendCapabilities c;
    c.backend_id = str(b, "backend_id");
    c.backend_revision = str(b, "backend_revision");
    c.display_name = str(b, "display_name", c.backend_id);
    c.model_id = str(b, "model_id");
    if (const auto it = b.find("model_hash"); it != b.end() && it->is_string()) {
        c.model_hash = it->get<std::string>();
    }
    if (const auto it = b.find("prompts"); it != b.end() && it->is_array()) {
        for (const auto& p : *it) {
            if (p.is_string()) {
                if (const auto kind = prompt_kind_from_name(p.get<std::string>())) {
                    c.prompts.push_back(
                        *kind); // les invites inconnues sont ignorées, pas inventées
                }
            }
        }
    }
    if (const auto it = b.find("devices"); it != b.end() && it->is_array()) {
        for (const auto& d : *it) {
            if (d.is_string()) {
                c.devices.push_back(d.get<std::string>());
            }
        }
    }
    c.license_code = str(b, "license_code");
    c.license_weights = str(b, "license_weights");
    c.experimental = b.value("experimental", false);
    return c;
}

PredictResult parse_result(const json& j) {
    PredictResult r;
    r.request_id = str(j, "id");
    r.image_sha256 = str(j, "image_sha256");
    if (const auto it = j.find("transform"); it != j.end() && it->is_object()) {
        r.transform.source_width = static_cast<int>(integer(*it, "source_width"));
        r.transform.source_height = static_cast<int>(integer(*it, "source_height"));
        r.transform.width = static_cast<int>(integer(*it, "width"));
        r.transform.height = static_cast<int>(integer(*it, "height"));
        r.transform.scale = opt_number(*it, "scale").value_or(1.0);
        if (const auto c = it->find("crop"); c != it->end() && c->is_array()) {
            r.transform.crop = int4(*it, "crop");
        }
    }
    if (const auto it = j.find("masks"); it != j.end() && it->is_array()) {
        for (const auto& m : *it) {
            MaskProposal p;
            p.id = str(m, "id");
            p.file = str(m, "file");
            p.bbox_xywh = int4(m, "bbox_xywh");
            p.area_px = integer(m, "area_px");
            p.confidence = opt_number(m, "confidence");
            if (const auto pi = opt_number(m, "prompt_index")) {
                p.prompt_index = static_cast<int>(*pi);
            }
            r.masks.push_back(std::move(p));
        }
    }
    if (const auto it = j.find("detections"); it != j.end() && it->is_array()) {
        for (const auto& d : *it) {
            Detection det;
            det.id = str(d, "id");
            det.label = str(d, "label");
            det.bbox_xywh = int4(d, "bbox_xywh");
            if (const auto pi = opt_number(d, "prompt_index")) {
                det.prompt_index = static_cast<int>(*pi);
            }
            r.detections.push_back(std::move(det));
        }
    }
    r.elapsed_ms = integer(j, "elapsed_ms");
    if (const auto it = j.find("warnings"); it != j.end() && it->is_array()) {
        for (const auto& w : *it) {
            if (w.is_string()) {
                r.warnings.push_back(w.get<std::string>());
            }
        }
    }
    return r;
}

json base(std::string_view id, std::string_view type) {
    return json{{"id", std::string(id)},
                {"type", std::string(type)},
                {"protocol", std::string(kBackendProtocol)}};
}

// Lignes ASCII pures : indépendantes de la page de code de stdin du worker (chemins accentués).
std::string dump_line(const json& j) {
    return j.dump(-1, ' ', /*ensure_ascii=*/true);
}

json option_value(const std::string& value) {
    if (value == "true" || value == "false") {
        return value == "true";
    }
    std::int64_t number = 0;
    const auto* end = value.data() + value.size();
    if (!value.empty() && std::from_chars(value.data(), end, number).ptr == end) {
        return number;
    }
    return value;
}

} // namespace

std::optional<int> backend_protocol_major(std::string_view protocol) {
    constexpr std::string_view prefix = "openstitch.ai.backend/";
    if (!protocol.starts_with(prefix)) {
        return std::nullopt;
    }
    const std::string_view tail = protocol.substr(prefix.size());
    int major = 0;
    const auto result = std::from_chars(tail.data(), tail.data() + tail.size(), major);
    if (tail.empty() || result.ec != std::errc{} || result.ptr != tail.data() + tail.size()) {
        return std::nullopt;
    }
    return major;
}

std::string_view prompt_kind_name(PromptKind kind) {
    switch (kind) {
    case PromptKind::Automatic:
        return "automatic";
    case PromptKind::Box:
        return "box";
    case PromptKind::Point:
        return "point";
    case PromptKind::Text:
        return "text";
    case PromptKind::Exemplar:
        return "exemplar";
    }
    return "automatic";
}

std::optional<PromptKind> prompt_kind_from_name(std::string_view name) {
    for (const PromptKind k : {PromptKind::Automatic, PromptKind::Box, PromptKind::Point,
                               PromptKind::Text, PromptKind::Exemplar}) {
        if (prompt_kind_name(k) == name) {
            return k;
        }
    }
    return std::nullopt;
}

bool BackendCapabilities::supports(PromptKind kind) const {
    return std::find(prompts.begin(), prompts.end(), kind) != prompts.end();
}

AiResult<WorkerMessage> parse_worker_message(std::string_view line) {
    json j = json::parse(line, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return std::unexpected(
            invalid("ligne hors protocole : " + std::string(line.substr(0, 120))));
    }
    const std::string type = str(j, "type");
    const std::string id = str(j, "id");
    if (type == "hello") {
        return HelloMsg{str(j, "protocol"), str(j, "backend_id"), integer(j, "pid")};
    }
    if (type == "capabilities") {
        const auto it = j.find("backend");
        if (it == j.end() || !it->is_object()) {
            return std::unexpected(invalid("capabilities sans objet `backend`"));
        }
        return CapabilitiesMsg{id, parse_capabilities(*it)};
    }
    if (type == "progress") {
        return ProgressMsg{id, str(j, "stage"), opt_number(j, "fraction")};
    }
    if (type == "result") {
        return parse_result(j);
    }
    if (type == "cancelled") {
        return CancelledMsg{id, str(j, "target_id", id)};
    }
    if (type == "health") {
        return HealthMsg{id, str(j, "state"), str(j, "detail")};
    }
    if (type == "error") {
        const std::string name = str(j, "code", "INVALID_WORKER_RESPONSE");
        const AiErrorCode code = ai_error_code_from_name(name);
        std::string details = str(j, "details");
        if (code == AiErrorCode::InvalidWorkerResponse && name != "INVALID_WORKER_RESPONSE") {
            details = "code inconnu « " + name + " » ; " + details; // jamais ignoré en silence
        }
        const std::string message = str(j, "message");
        return ErrorMsg{id, name,
                        AiError{code, message.empty() ? default_message(code) : message, details}};
    }
    if (type == "unloaded") {
        return UnloadedMsg{id};
    }
    if (type == "bye") {
        return ByeMsg{id};
    }
    return std::unexpected(invalid("type de message inconnu : " + type));
}

std::string message_id(const WorkerMessage& message) {
    return std::visit(
        [](const auto& m) -> std::string {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, HelloMsg>) {
                return {};
            } else if constexpr (std::is_same_v<T, PredictResult>) {
                return m.request_id;
            } else {
                return m.id;
            }
        },
        message);
}

std::string serialize_capabilities_request(std::string_view id) {
    return dump_line(base(id, "capabilities"));
}

std::string serialize_health_request(std::string_view id) {
    return dump_line(base(id, "health"));
}

std::string serialize_predict_request(const PredictRequest& request) {
    json j = base(request.id, "predict");
    j["job_dir"] = request.job_dir;
    j["image_file"] = request.image_file;
    json prompts = json::array();
    for (const Prompt& p : request.prompts) {
        json item{{"kind", std::string(prompt_kind_name(p.kind))}};
        if (p.box_xywh) {
            item["box_xywh"] = *p.box_xywh;
        }
        if (!p.text.empty()) {
            item["text"] = p.text;
        }
        prompts.push_back(std::move(item));
    }
    j["prompts"] = std::move(prompts);
    json options = json::object();
    for (const auto& [key, value] : request.options) {
        options[key] = option_value(value);
    }
    j["options"] = std::move(options);
    return dump_line(j);
}

std::string serialize_cancel_request(std::string_view id, std::string_view target_id) {
    json j = base(id, "cancel");
    j["target_id"] = std::string(target_id);
    return dump_line(j);
}

std::string serialize_unload_request(std::string_view id) {
    return dump_line(base(id, "unload"));
}

std::string serialize_shutdown_request(std::string_view id) {
    return dump_line(base(id, "shutdown"));
}

} // namespace openstitch::ai_segmentation
