// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/backend_protocol.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace openstitch::ai_segmentation;

TEST_CASE("protocol major is parsed strictly") {
    CHECK(backend_protocol_major("openstitch.ai.backend/1") == 1);
    CHECK(backend_protocol_major("openstitch.ai.backend/12") == 12);
    CHECK_FALSE(backend_protocol_major("openstitch.ai.backend/").has_value());
    CHECK_FALSE(backend_protocol_major("openstitch.ai.backend/1x").has_value());
    CHECK_FALSE(backend_protocol_major("sam.worker/1").has_value());
}

TEST_CASE("prompt kinds round-trip and unknown names are rejected") {
    for (const auto kind : {PromptKind::Automatic, PromptKind::Box, PromptKind::Point,
                            PromptKind::Text, PromptKind::Exemplar}) {
        CHECK(prompt_kind_from_name(prompt_kind_name(kind)) == kind);
    }
    CHECK_FALSE(prompt_kind_from_name("telepathy").has_value());
}

TEST_CASE("hello and capabilities are parsed with every contract field") {
    const auto hello = parse_worker_message(
        R"({"type":"hello","protocol":"openstitch.ai.backend/1","backend_id":"sam2","pid":42})");
    REQUIRE(hello.has_value());
    const auto& h = std::get<HelloMsg>(*hello);
    CHECK(h.backend_id == "sam2");
    CHECK(h.pid == 42);

    const auto caps = parse_worker_message(
        R"({"id":"c1","type":"capabilities","backend":{"backend_id":"sam2","backend_revision":"sam2.1","display_name":"SAM 2.1","model_id":"small","model_hash":"abc","prompts":["automatic","box","telepathy"],"devices":["cpu"],"license_code":"Apache-2.0","license_weights":"Apache-2.0","experimental":false}})");
    REQUIRE(caps.has_value());
    const auto& c = std::get<CapabilitiesMsg>(*caps);
    CHECK(c.id == "c1");
    CHECK(c.backend.model_hash == "abc");
    CHECK(c.backend.license_weights == "Apache-2.0");
    CHECK(c.backend.supports(PromptKind::Box));
    CHECK(c.backend.prompts.size() == 2); // l'invite inconnue est ignorée, pas inventée
}

TEST_CASE("a null model hash stays absent") {
    const auto caps = parse_worker_message(
        R"({"id":"c","type":"capabilities","backend":{"backend_id":"x","model_hash":null}})");
    REQUIRE(caps.has_value());
    CHECK_FALSE(std::get<CapabilitiesMsg>(*caps).backend.model_hash.has_value());
}

TEST_CASE("result carries transform, masks and optional confidence") {
    const auto msg = parse_worker_message(
        R"({"id":"p1","type":"result","image_sha256":"ff","transform":{"source_width":320,"source_height":240,"width":160,"height":120,"scale":0.5,"crop":null},"masks":[{"id":"m0","file":"masks/mask_0000.png","bbox_xywh":[1,2,3,4],"area_px":12,"confidence":0.9,"prompt_index":0},{"id":"m1","file":"masks/mask_0001.png","bbox_xywh":[0,0,1,1],"area_px":1}],"elapsed_ms":7,"warnings":["w"]})");
    REQUIRE(msg.has_value());
    const auto& r = std::get<PredictResult>(*msg);
    CHECK(r.request_id == "p1");
    CHECK(r.transform.scale == 0.5);
    CHECK_FALSE(r.transform.crop.has_value());
    REQUIRE(r.masks.size() == 2);
    CHECK(r.masks[0].confidence == 0.9);
    CHECK_FALSE(r.masks[1].confidence.has_value()); // jamais de confiance synthétisée
    CHECK(r.masks[0].bbox_xywh == std::array<int, 4>{1, 2, 3, 4});
    CHECK(r.warnings.size() == 1);
}

TEST_CASE("typed errors map to AiErrorCode and unknown codes are never swallowed") {
    const auto known = parse_worker_message(
        R"({"id":"p","type":"error","code":"UNSUPPORTED_PROMPT","message":"non","details":"d"})");
    REQUIRE(known.has_value());
    CHECK(std::get<ErrorMsg>(*known).error.code == AiErrorCode::UnsupportedPrompt);
    const auto unknown = parse_worker_message(
        R"({"id":"p","type":"error","code":"MOON_PHASE","message":"","details":"d"})");
    REQUIRE(unknown.has_value());
    const auto& e = std::get<ErrorMsg>(*unknown);
    CHECK(e.error.code == AiErrorCode::InvalidWorkerResponse);
    CHECK(e.error.detail.find("MOON_PHASE") != std::string::npos);
    CHECK_FALSE(e.error.message.empty());
}

TEST_CASE("non-protocol lines are refused") {
    for (const char* line :
         {"hello world", "[1,2]", "{\"type\":\"warp\"}", "", "{\"type\":\"capabilities\"}"}) {
        const auto r = parse_worker_message(line);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code == AiErrorCode::InvalidWorkerResponse);
    }
}

TEST_CASE("requests are single ASCII lines carrying the protocol version") {
    PredictRequest req;
    req.id = "p1";
    req.job_dir = "C:/Users/Zachée/job";
    req.prompts = {Prompt{PromptKind::Box, std::array<int, 4>{1, 2, 3, 4}, {}}};
    req.options = {{"delay_ms", "250"}, {"flag", "true"}, {"profile", "balanced"}};
    const std::string line = serialize_predict_request(req);
    CHECK(line.find('\n') == std::string::npos);
    for (const char c : line) {
        CHECK(static_cast<unsigned char>(c) < 0x80); // chemins accentués échappés
    }
    CHECK(line.find("\"protocol\":\"openstitch.ai.backend/1\"") != std::string::npos);
    CHECK(line.find("\"kind\":\"box\"") != std::string::npos);
    CHECK(line.find("\"box_xywh\":[1,2,3,4]") != std::string::npos);
    CHECK(line.find("\"delay_ms\":250") != std::string::npos);
    CHECK(line.find("\"flag\":true") != std::string::npos);
    CHECK(line.find("\"profile\":\"balanced\"") != std::string::npos);
    CHECK(serialize_cancel_request("c1", "p1").find("\"target_id\":\"p1\"") != std::string::npos);
}
