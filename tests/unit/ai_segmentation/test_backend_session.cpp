// SPDX-License-Identifier: Apache-2.0
// Session, découverte et repli, avec deux backends factices EN MÉMOIRE (aucun processus).
#include "openstitch/ai_segmentation/backend_registry.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

using namespace openstitch::ai_segmentation;
using namespace std::chrono_literals;

namespace {

bool has(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

std::string id_of(const std::string& request) {
    const auto p = request.find("\"id\":\"");
    const auto start = p + 6;
    return request.substr(start, request.find('"', start) - start);
}

// Transport factice : le « backend » est une fonction qui répond à chaque requête écrite.
class ScriptedTransport final : public ITransport {
public:
    using Responder = std::function<void(const std::string& request, ScriptedTransport& self)>;
    explicit ScriptedTransport(Responder responder) : responder_(std::move(responder)) {}

    void emit(std::string line) {
        std::lock_guard lock(mutex_);
        out_.push_back(std::move(line));
    }
    void crash() {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }
    // Journal partagé : reste lisible après la destruction du transport par la session.
    std::shared_ptr<std::vector<std::string>> log() const { return log_; }
    std::shared_ptr<std::atomic<bool>> terminated_flag() const { return terminated_; }
    bool terminated() const { return terminated_->load(); }
    std::vector<std::string> writes() const {
        std::lock_guard lock(mutex_);
        return *log_;
    }

    AiResult<void> write_line(std::string_view line) override {
        {
            std::lock_guard lock(mutex_);
            if (closed_) {
                return std::unexpected(AiError{AiErrorCode::WorkerCrashed, "fermé", ""});
            }
            log_->emplace_back(line);
        }
        responder_(std::string(line), *this);
        return {};
    }
    AiResult<std::optional<std::string>> read_line(std::chrono::milliseconds timeout) override {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            {
                std::lock_guard lock(mutex_);
                if (!out_.empty()) {
                    std::string l = std::move(out_.front());
                    out_.pop_front();
                    return std::optional<std::string>(std::move(l));
                }
                if (closed_) {
                    return std::unexpected(AiError{AiErrorCode::WorkerCrashed, "flux fermé", ""});
                }
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return std::optional<std::string>{};
            }
            std::this_thread::sleep_for(2ms);
        }
    }
    void terminate() override {
        *terminated_ = true;
        std::lock_guard lock(mutex_);
        closed_ = true;
    }

private:
    Responder responder_;
    mutable std::mutex mutex_;
    std::deque<std::string> out_;
    std::shared_ptr<std::vector<std::string>> log_{std::make_shared<std::vector<std::string>>()};
    bool closed_{false};
    std::shared_ptr<std::atomic<bool>> terminated_{std::make_shared<std::atomic<bool>>(false)};
};

std::string caps_line(const std::string& id, const std::string& backend, const char* prompts) {
    return R"({"id":")" + id + R"(","type":"capabilities","backend":{"backend_id":")" + backend +
           R"(","backend_revision":"t1","display_name":")" + backend +
           R"(","model_id":"none","model_hash":null,"prompts":)" + prompts +
           R"(,"devices":["cpu"],"license_code":"Apache-2.0","license_weights":"n/a","experimental":true}})";
}

const char* kHello =
    R"({"type":"hello","protocol":"openstitch.ai.backend/1","backend_id":"fake","pid":1})";

// Backend factice n°1 : sain. Un `predict` renvoie progression puis deux masques.
std::unique_ptr<ScriptedTransport> make_fake_ok(const char* hello = kHello,
                                                const char* health = "ready") {
    auto t = std::make_unique<ScriptedTransport>([health](const std::string& req,
                                                          ScriptedTransport& self) {
        const std::string id = id_of(req);
        if (has(req, "\"type\":\"capabilities\"")) {
            self.emit(caps_line(id, "fake-ok", R"(["automatic","box"])"));
        } else if (has(req, "\"type\":\"health\"")) {
            self.emit(R"({"id":")" + id + R"(","type":"health","state":")" + health +
                      R"(","detail":"poids absents"})");
        } else if (has(req, "\"type\":\"predict\"")) {
            self.emit(R"({"id":")" + id +
                      R"(","type":"progress","stage":"running_inference","fraction":0.5})");
            self.emit(
                R"({"id":")" + id +
                R"(","type":"result","image_sha256":"aa","transform":{"source_width":8,"source_height":8,"width":8,"height":8,"scale":1.0,"crop":null},"masks":[{"id":"m0","file":"masks/mask_0000.png","bbox_xywh":[0,0,4,4],"area_px":16},{"id":"m1","file":"masks/mask_0001.png","bbox_xywh":[4,4,4,4],"area_px":16,"confidence":0.8}],"elapsed_ms":3,"warnings":[]})");
        } else if (has(req, "\"type\":\"shutdown\"")) {
            self.emit(R"({"id":")" + id + R"(","type":"bye"})");
        }
    });
    t->emit(hello);
    return t;
}

// Backend factice n°2 : capricieux. Le mode vient de `options.mode` de la requête predict.
std::unique_ptr<ScriptedTransport> make_fake_flaky() {
    auto t = std::make_unique<ScriptedTransport>([](const std::string& req,
                                                    ScriptedTransport& self) {
        const std::string id = id_of(req);
        if (has(req, "\"type\":\"capabilities\"")) {
            self.emit(caps_line(id, "fake-flaky", R"(["automatic"])"));
        } else if (has(req, "\"type\":\"health\"")) {
            self.emit(R"({"id":")" + id + R"(","type":"health","state":"ready","detail":""})");
        } else if (has(req, "\"type\":\"predict\"")) {
            if (has(req, "\"mode\":\"fail\"")) {
                self.emit(
                    R"({"id":")" + id +
                    R"(","type":"error","code":"INFERENCE_FAILED","message":"échec simulé","details":"mode=fail"})");
            } else if (has(req, "\"mode\":\"busy\"")) {
                self.emit(
                    R"({"id":")" + id +
                    R"(","type":"error","code":"BACKEND_BUSY","message":"occupé","details":""})");
            } else if (has(req, "\"mode\":\"noise\"")) {
                self.emit("Traceback (most recent call last):");
            } else if (has(req, "\"mode\":\"crash\"")) {
                self.crash();
            } // mode « slow » : aucune réponse tant qu'il n'y a pas de `cancel`
        } else if (has(req, "\"type\":\"cancel\"")) {
            const auto p = req.find("\"target_id\":\"");
            const auto s = p + 13;
            const std::string target = req.substr(s, req.find('"', s) - s);
            self.emit(R"({"id":")" + target + R"(","type":"cancelled","target_id":")" + target +
                      R"("})");
        }
    });
    t->emit(kHello);
    return t;
}

SessionOptions fast() {
    SessionOptions o;
    o.start_timeout = 200ms;
    o.request_timeout = 200ms;
    o.task_timeout = 400ms;
    o.cancel_grace = 200ms;
    return o;
}

PredictRequest predict_with(std::vector<std::pair<std::string, std::string>> options = {}) {
    PredictRequest r;
    r.id = "p1";
    r.job_dir = "job";
    r.options = std::move(options);
    return r;
}

} // namespace

TEST_CASE("session opens, reads capabilities and health, predicts with progress") {
    auto session = BackendSession::open(make_fake_ok(), fast());
    REQUIRE(session.has_value());
    CHECK(session->hello().backend_id == "fake");
    const auto caps = session->capabilities();
    REQUIRE(caps.has_value());
    CHECK(caps->supports(PromptKind::Box));
    CHECK(caps->license_weights == "n/a");
    const auto health = session->health();
    REQUIRE(health.has_value());
    CHECK(health->state == "ready");

    std::vector<std::string> stages;
    const auto result =
        session->predict(predict_with(), [&](const ProgressMsg& p) { stages.push_back(p.stage); });
    REQUIRE(result.has_value());
    CHECK(stages == std::vector<std::string>{"running_inference"});
    REQUIRE(result->masks.size() == 2);
    CHECK_FALSE(result->masks[0].confidence.has_value());
    CHECK(result->masks[1].confidence == 0.8);
}

TEST_CASE("an incompatible protocol version is refused, never degraded") {
    auto t = make_fake_ok(
        R"({"type":"hello","protocol":"openstitch.ai.backend/2","backend_id":"future","pid":1})");
    auto* raw = t.get();
    const auto session = BackendSession::open(std::move(t), fast());
    REQUIRE_FALSE(session.has_value());
    CHECK(session.error().code == AiErrorCode::ProtocolMismatch);
    CHECK(raw != nullptr);
}

TEST_CASE("a foreign protocol or a missing hello is refused") {
    auto foreign = BackendSession::open(
        make_fake_ok(R"({"type":"hello","protocol":"sam.worker/1","backend_id":"x"})"), fast());
    REQUIRE_FALSE(foreign.has_value());
    CHECK(foreign.error().code == AiErrorCode::ProtocolMismatch);

    auto silent =
        std::make_unique<ScriptedTransport>([](const std::string&, ScriptedTransport&) {});
    const auto none = BackendSession::open(std::move(silent), fast());
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error().code == AiErrorCode::BackendTimeout);
}

TEST_CASE("typed backend errors reach the caller unchanged") {
    auto session = BackendSession::open(make_fake_flaky(), fast());
    REQUIRE(session.has_value());
    const auto failed = session->predict(predict_with({{"mode", "fail"}}));
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().code == AiErrorCode::InferenceFailed);
    CHECK(failed.error().message == "échec simulé");
    const auto busy = session->predict(predict_with({{"mode", "busy"}}));
    REQUIRE_FALSE(busy.has_value());
    CHECK(busy.error().code == AiErrorCode::BackendBusy);
    CHECK(session->capabilities().has_value()); // la session reste utilisable après une erreur
}

TEST_CASE("cancellation is forwarded and confirmed by the worker") {
    auto t = make_fake_flaky();
    auto* raw = t.get();
    auto session = BackendSession::open(std::move(t), [] {
        auto o = fast();
        o.task_timeout = 5000ms;
        return o;
    }());
    REQUIRE(session.has_value());
    std::atomic<bool> cancel{false};
    std::thread canceller([&] {
        std::this_thread::sleep_for(120ms);
        cancel = true;
    });
    const auto r = session->predict(predict_with({{"mode", "slow"}}), {}, &cancel);
    canceller.join();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == AiErrorCode::Cancelled);
    bool sent_cancel = false;
    for (const auto& w : raw->writes()) {
        sent_cancel =
            sent_cancel || (has(w, "\"type\":\"cancel\"") && has(w, "\"target_id\":\"p1\""));
    }
    CHECK(sent_cancel);
    CHECK_FALSE(raw->terminated());
}

TEST_CASE("an unresponsive worker times out and is stopped") {
    auto t = make_fake_flaky();
    auto* raw = t.get();
    auto session = BackendSession::open(std::move(t), fast());
    REQUIRE(session.has_value());
    const auto r = session->predict(predict_with({{"mode", "slow"}}));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == AiErrorCode::BackendTimeout);
    CHECK(raw->terminated());
}

TEST_CASE("stdout noise and crashes are explicit errors") {
    auto noisy = BackendSession::open(make_fake_flaky(), fast());
    REQUIRE(noisy.has_value());
    const auto n = noisy->predict(predict_with({{"mode", "noise"}}));
    REQUIRE_FALSE(n.has_value());
    CHECK(n.error().code == AiErrorCode::InvalidWorkerResponse);

    auto crashing = BackendSession::open(make_fake_flaky(), fast());
    REQUIRE(crashing.has_value());
    const auto c = crashing->predict(predict_with({{"mode", "crash"}}));
    REQUIRE_FALSE(c.has_value());
    CHECK(c.error().code == AiErrorCode::WorkerCrashed);
}

TEST_CASE("closing a session sends shutdown") {
    auto t = make_fake_ok();
    const auto log = t->log();
    {
        auto session = BackendSession::open(std::move(t), fast());
        REQUIRE(session.has_value());
    }
    bool shutdown = false;
    for (const auto& w : *log) {
        shutdown = shutdown || has(w, "\"type\":\"shutdown\"");
    }
    CHECK(shutdown);
}

namespace {

BackendSettings two_backends() {
    BackendSettings s;
    s.preferred_id = "fake-ok";
    for (const char* id : {"fake-ok", "fake-flaky"}) {
        BackendConfig c;
        c.id = id;
        c.display_name = id;
        c.launch.executable = id;
        s.backends.push_back(c);
    }
    return s;
}

} // namespace

TEST_CASE("discovery reports each declared backend in order") {
    int launched = 0;
    const auto statuses = discover_backends(
        two_backends(),
        [&](const BackendConfig& c) -> AiResult<std::unique_ptr<ITransport>> {
            ++launched;
            return std::unique_ptr<ITransport>(c.id == "fake-ok" ? make_fake_ok()
                                                                 : make_fake_flaky());
        },
        fast());
    REQUIRE(statuses.size() == 2);
    CHECK(launched == 2);
    CHECK(statuses[0].config.id == "fake-ok");
    CHECK(statuses[0].state == BackendState::Available);
    REQUIRE(statuses[0].capabilities.has_value());
    CHECK(statuses[0].capabilities->supports(PromptKind::Box));
    CHECK(statuses[1].config.id == "fake-flaky");
}

TEST_CASE("one broken backend never breaks the discovery of the others") {
    BackendSettings s;
    for (const char* id : {"no-weights", "future", "broken", "off", "after"}) {
        BackendConfig c;
        c.id = id;
        c.launch.executable = id;
        c.enabled = std::string(id) != "off";
        s.backends.push_back(c);
    }
    int launched = 0;
    const auto statuses = discover_backends(
        s,
        [&](const BackendConfig& c) -> AiResult<std::unique_ptr<ITransport>> {
            ++launched;
            if (c.id == "no-weights") {
                return std::unique_ptr<ITransport>(make_fake_ok(kHello, "unavailable"));
            }
            if (c.id == "future") {
                return std::unique_ptr<ITransport>(make_fake_ok(
                    R"({"type":"hello","protocol":"openstitch.ai.backend/9","backend_id":"f"})"));
            }
            if (c.id == "broken") {
                return std::unexpected(
                    AiError{AiErrorCode::PythonNotFound, "Python introuvable", "python"});
            }
            return std::unique_ptr<ITransport>(make_fake_ok());
        },
        fast());
    REQUIRE(statuses.size() == 5);
    CHECK(launched == 4); // le backend désactivé n'est jamais lancé
    CHECK(statuses[0].state == BackendState::Unavailable);
    CHECK(statuses[0].diagnostics == "poids absents");
    CHECK(statuses[1].state == BackendState::Incompatible);
    CHECK(statuses[1].error_code == AiErrorCode::ProtocolMismatch);
    CHECK(statuses[2].state == BackendState::Failed);
    CHECK(statuses[2].error_code == AiErrorCode::PythonNotFound);
    CHECK(statuses[3].state == BackendState::Disabled);
    CHECK(statuses[4].state == BackendState::Available);
}

TEST_CASE("resolution falls back to classical segmentation with a reason") {
    const auto factory = [](const BackendConfig& c) -> AiResult<std::unique_ptr<ITransport>> {
        return std::unique_ptr<ITransport>(c.id == "fake-ok" ? make_fake_ok() : make_fake_flaky());
    };
    const auto statuses = discover_backends(two_backends(), factory, fast());

    const auto ok = resolve_backend(statuses, "fake-ok", PromptKind::Box);
    CHECK(ok.backend_id == "fake-ok");
    CHECK_FALSE(ok.classical_fallback);

    const auto no_prompt = resolve_backend(statuses, "fake-flaky", PromptKind::Box);
    CHECK_FALSE(no_prompt.backend_id.has_value());
    CHECK(no_prompt.classical_fallback);
    CHECK(no_prompt.reason.find("box") != std::string::npos);

    const auto none = resolve_backend(statuses, "", PromptKind::Automatic);
    CHECK(none.classical_fallback);
    const auto missing = resolve_backend(statuses, "ghost", PromptKind::Automatic);
    CHECK(missing.classical_fallback);
    CHECK(missing.reason.find("ghost") != std::string::npos);

    const auto dead = discover_backends(
        two_backends(),
        [](const BackendConfig&) -> AiResult<std::unique_ptr<ITransport>> {
            return std::unexpected(AiError{AiErrorCode::WorkerStartFailed, "x", ""});
        },
        fast());
    const auto fallback = resolve_backend(dead, "fake-ok", PromptKind::Automatic);
    CHECK_FALSE(fallback.backend_id.has_value());
    CHECK(fallback.classical_fallback);
}

TEST_CASE("settings are parsed strictly") {
    const auto ok = parse_backend_settings(
        R"({"schema":"openstitch.ai.backends/1","preferred":"a","backends":[{"id":"a","command":"python","args":["w.py","--x"],"cwd":"d"},{"id":"b","command":"p","enabled":false}]})");
    REQUIRE(ok.has_value());
    CHECK(ok->preferred_id == "a");
    REQUIRE(ok->backends.size() == 2);
    CHECK(ok->backends[0].launch.args == std::vector<std::string>{"w.py", "--x"});
    CHECK_FALSE(ok->backends[1].enabled);
    for (
        const char* bad :
        {"not json", R"({"schema":"x","backends":[]})", R"({"schema":"openstitch.ai.backends/1"})",
         R"({"schema":"openstitch.ai.backends/1","backends":[{"id":"a"}]})",
         R"({"schema":"openstitch.ai.backends/1","backends":[{"id":"a","command":"p"},{"id":"a","command":"q"}]})"}) {
        CHECK_FALSE(parse_backend_settings(bad).has_value());
    }
}

TEST_CASE("the diagnostics report is JSON with the resolution") {
    const auto statuses = discover_backends(
        two_backends(),
        [](const BackendConfig& c) -> AiResult<std::unique_ptr<ITransport>> {
            return std::unique_ptr<ITransport>(c.id == "fake-ok" ? make_fake_ok()
                                                                 : make_fake_flaky());
        },
        fast());
    const auto report = backends_report_json(statuses, resolve_backend(statuses, "fake-ok"));
    CHECK(report.find("openstitch.ai.backends-report/1") != std::string::npos);
    CHECK(report.find("\"classical_fallback\": false") != std::string::npos);
    CHECK(report.find("\"license_weights\": \"n/a\"") != std::string::npos);
}
