// SPDX-License-Identifier: Apache-2.0
#include <nlohmann/json.hpp>

#include "openstitch/agent/agent.hpp"

namespace openstitch::agent {

namespace {
using json = nlohmann::json;
}

std::vector<ToolSpec> parse_tool_specs(const std::string& text) {
    std::vector<ToolSpec> specs;
    const json list = json::parse(text, nullptr, false);
    if (!list.is_array()) {
        return specs;
    }
    for (const auto& item : list) {
        ToolSpec spec;
        spec.name = item.value("name", std::string{});
        spec.description = item.value("description", std::string{});
        spec.input_schema_json = item.contains("input_schema") ? item.at("input_schema").dump()
                                                               : "{\"type\":\"object\"}";
        if (!spec.name.empty()) {
            specs.push_back(std::move(spec));
        }
    }
    return specs;
}

ToolHostResult parse_tool_result(const std::string& text) {
    ToolHostResult result;
    const json doc = json::parse(text, nullptr, false);
    if (!doc.is_object()) {
        result.is_error = true;
        ContentBlock b;
        b.text = "résultat d'outil illisible";
        result.content.push_back(std::move(b));
        return result;
    }
    result.is_error = doc.value("isError", false);
    if (doc.contains("content") && doc.at("content").is_array()) {
        for (const auto& c : doc.at("content")) {
            ContentBlock b;
            if (c.value("type", std::string{}) == "image") {
                b.kind = ContentBlock::Kind::Image;
                b.media_type = c.value("mimeType", std::string("image/png"));
                b.data_b64 = c.value("data", std::string{});
            } else {
                b.text = c.value("text", std::string{});
            }
            result.content.push_back(std::move(b));
        }
    }
    return result;
}

std::string describe_arguments(const std::string& args_json, std::size_t limit) {
    const json args = json::parse(args_json.empty() ? "{}" : args_json, nullptr, false);
    if (!args.is_object() || args.empty()) {
        return {};
    }
    std::string out;
    for (const auto& [key, value] : args.items()) {
        if (!out.empty()) {
            out += ", ";
        }
        out += key + "=" + (value.is_string() ? value.get<std::string>() : value.dump());
        if (out.size() > limit) {
            out.resize(limit);
            out += "…";
            break;
        }
    }
    return out;
}

std::string first_text(const ToolHostResult& result) {
    for (const auto& b : result.content) {
        if (b.kind == ContentBlock::Kind::Text && !b.text.empty()) {
            return b.text;
        }
    }
    return {};
}

} // namespace openstitch::agent
