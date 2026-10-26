// SPDX-License-Identifier: Apache-2.0
// Transport processus réel contre le worker Python générique (backends factices, stdlib seule).
// Ignoré proprement (SKIP) si aucun interpréteur Python n'est utilisable.
#include "openstitch/ai_segmentation/backend_registry.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <thread>

using namespace openstitch::ai_segmentation;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

SessionOptions options() {
    SessionOptions o;
    o.start_timeout = 20s;
    o.request_timeout = 20s;
    o.task_timeout = 30s;
    o.cancel_grace = 10s;
    return o;
}

ProcessSpec worker_spec(const std::string& python, std::vector<std::string> extra) {
    ProcessSpec spec;
    spec.executable = python;
    spec.args = {OPENSTITCH_AI_WORKER};
    spec.args.insert(spec.args.end(), extra.begin(), extra.end());
    return spec;
}

std::string env_value(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t size = 0;
    std::string value;
    if (_dupenv_s(&buffer, &size, name) == 0 && buffer != nullptr) {
        value = buffer;
        std::free(buffer);
    }
    return value;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? value : "";
#endif
}

// Premier interpréteur capable de lancer le faux worker et d'en lire le `hello`.
std::optional<std::string> find_python() {
    std::vector<std::string> candidates;
    if (const std::string env = env_value("OPENSTITCH_TEST_PYTHON"); !env.empty()) {
        candidates.push_back(env);
    }
    candidates.insert(candidates.end(), {"python3", "python"});
    for (const auto& c : candidates) {
        auto transport = spawn_process(worker_spec(c, {"--backend", "fake-grid"}));
        if (!transport) {
            continue;
        }
        if (auto session = BackendSession::open(std::move(*transport), options())) {
            return c;
        }
    }
    return std::nullopt;
}

fs::path make_job(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    // signature PNG + IHDR (8x6) : le faux backend ne lit que les 24 premiers octets
    const unsigned char head[24] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13,
                                    'I',  'H', 'D', 'R', 0,    0,    0,    8,    0, 0, 0, 6};
    std::ofstream f(dir / "input.png", std::ios::binary);
    f.write(reinterpret_cast<const char*>(head), sizeof head);
    return dir;
}

} // namespace

TEST_CASE("process transport: discovery, predict and clean shutdown against a real worker") {
    const auto python = find_python();
    if (!python) {
        SKIP("aucun interpréteur Python utilisable (définir OPENSTITCH_TEST_PYTHON)");
    }
    auto transport = spawn_process(worker_spec(*python, {"--backend", "fake-grid"}));
    REQUIRE(transport.has_value());
    auto session = BackendSession::open(std::move(*transport), options());
    REQUIRE(session.has_value());
    CHECK(session->hello().backend_id == "fake-grid");

    const auto caps = session->capabilities();
    REQUIRE(caps.has_value());
    CHECK(caps->supports(PromptKind::Box));
    CHECK(caps->license_code == "Apache-2.0");

    const fs::path job = make_job("openstitch-ai-process-job");
    PredictRequest request;
    request.job_dir = job.string();
    request.prompts = {Prompt{PromptKind::Automatic, {}, {}},
                       Prompt{PromptKind::Box, std::array<int, 4>{1, 1, 3, 2}, {}}};
    std::vector<std::string> stages;
    const auto result =
        session->predict(request, [&](const ProgressMsg& p) { stages.push_back(p.stage); });
    REQUIRE(result.has_value());
    CHECK(result->masks.size() == 5);
    CHECK(result->transform.source_width == 8);
    CHECK(result->transform.source_height == 6);
    CHECK(result->image_sha256.size() == 64);
    CHECK_FALSE(stages.empty());
    for (const auto& m : result->masks) {
        CHECK(fs::exists(job / m.file));
    }
    fs::remove_all(job);
}

TEST_CASE("process transport: cancellation stops a long task and the worker keeps answering") {
    const auto python = find_python();
    if (!python) {
        SKIP("aucun interpréteur Python utilisable (définir OPENSTITCH_TEST_PYTHON)");
    }
    auto transport = spawn_process(worker_spec(*python, {"--backend", "fake-grid"}));
    REQUIRE(transport.has_value());
    auto session = BackendSession::open(std::move(*transport), options());
    REQUIRE(session.has_value());
    const fs::path job = make_job("openstitch-ai-process-cancel");
    PredictRequest request;
    request.job_dir = job.string();
    request.options = {{"delay_ms", "20000"}};
    std::atomic<bool> cancel{false};
    std::thread canceller([&] {
        std::this_thread::sleep_for(400ms);
        cancel = true;
    });
    const auto started = std::chrono::steady_clock::now();
    const auto result = session->predict(request, {}, &cancel);
    canceller.join();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AiErrorCode::Cancelled);
    CHECK(std::chrono::steady_clock::now() - started < 15s);
    const auto health = session->health();
    REQUIRE(health.has_value());
    CHECK(health->state == "ready");
    fs::remove_all(job);
}

TEST_CASE(
    "process transport: crash, incompatible version and missing executable are typed errors") {
    const auto python = find_python();
    if (!python) {
        SKIP("aucun interpréteur Python utilisable (définir OPENSTITCH_TEST_PYTHON)");
    }
    {
        auto transport = spawn_process(worker_spec(*python, {"--backend", "fake-flaky"}));
        REQUIRE(transport.has_value());
        auto session = BackendSession::open(std::move(*transport), options());
        REQUIRE(session.has_value());
        const fs::path job = make_job("openstitch-ai-process-crash");
        PredictRequest request;
        request.job_dir = job.string();
        request.options = {{"mode", "crash"}};
        const auto result = session->predict(request);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == AiErrorCode::WorkerCrashed);
        fs::remove_all(job);
    }
    {
        auto transport =
            spawn_process(worker_spec(*python, {"--backend", "fake-grid", "--advertise-protocol",
                                                "openstitch.ai.backend/9"}));
        REQUIRE(transport.has_value());
        const auto session = BackendSession::open(std::move(*transport), options());
        REQUIRE_FALSE(session.has_value());
        CHECK(session.error().code == AiErrorCode::ProtocolMismatch);
    }
    {
        ProcessSpec missing;
        missing.executable = "openstitch-no-such-interpreter-xyz";
        const auto transport = spawn_process(missing);
        if (transport) { // POSIX : l'échec d'exec n'apparaît qu'à la lecture du hello
            auto session = BackendSession::open(
                std::move(const_cast<std::unique_ptr<ITransport>&>(*transport)), options());
            CHECK_FALSE(session.has_value());
        } else {
            CHECK(transport.error().code != AiErrorCode::Cancelled);
        }
    }
}

TEST_CASE(
    "process transport: discovery with the real factory reports availability and falls back") {
    const auto python = find_python();
    if (!python) {
        SKIP("aucun interpréteur Python utilisable (définir OPENSTITCH_TEST_PYTHON)");
    }
    BackendSettings settings;
    settings.preferred_id = "grid";
    BackendConfig grid;
    grid.id = "grid";
    grid.launch = worker_spec(*python, {"--backend", "fake-grid"});
    BackendConfig future;
    future.id = "future";
    future.launch = worker_spec(
        *python, {"--backend", "fake-grid", "--advertise-protocol", "openstitch.ai.backend/9"});
    BackendConfig ghost;
    ghost.id = "ghost";
    ghost.launch.executable = "openstitch-no-such-interpreter-xyz";
    settings.backends = {grid, future, ghost};

    const auto statuses = discover_backends(settings, process_transport_factory(), options());
    REQUIRE(statuses.size() == 3);
    CHECK(statuses[0].state == BackendState::Available);
    CHECK(statuses[1].state == BackendState::Incompatible);
    CHECK(statuses[2].state == BackendState::Failed);
    CHECK_FALSE(resolve_backend(statuses, "grid", PromptKind::Box).classical_fallback);
    CHECK(resolve_backend(statuses, "future").classical_fallback);
    CHECK(resolve_backend(statuses, "ghost").classical_fallback);
}
