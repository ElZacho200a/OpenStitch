// SPDX-License-Identifier: Apache-2.0
// Analyse complète par un backend (repli explicite, annulation) avec un backend factice EN MÉMOIRE.
#include "openstitch/ai_segmentation/backend_run.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <deque>
#include <fstream>
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
    const auto start = request.find("\"id\":\"") + 6;
    return request.substr(start, request.find('"', start) - start);
}

class Scripted final : public ITransport {
public:
    using Responder = std::function<void(const std::string&, Scripted&)>;
    explicit Scripted(Responder r) : responder_(std::move(r)) {}
    void emit(std::string line) {
        std::lock_guard lock(mutex_);
        out_.push_back(std::move(line));
    }
    AiResult<void> write_line(std::string_view line) override {
        responder_(std::string(line), *this);
        return {};
    }
    AiResult<std::optional<std::string>> read_line(std::chrono::milliseconds timeout) override {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            {
                std::lock_guard lock(mutex_);
                if (!out_.empty()) {
                    auto l = std::move(out_.front());
                    out_.pop_front();
                    return std::optional<std::string>(std::move(l));
                }
                if (closed_) {
                    return std::unexpected(AiError{AiErrorCode::WorkerCrashed, "fermé", ""});
                }
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return std::optional<std::string>{};
            }
            std::this_thread::sleep_for(2ms);
        }
    }
    void terminate() override {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }

private:
    Responder responder_;
    std::mutex mutex_;
    std::deque<std::string> out_;
    bool closed_{false};
};

struct Behavior {
    const char* prompts{R"(["automatic"])"};
    const char* health{"ready"};
    std::string masks; // contenu JSON de `masks`
    std::string transform{
        R"({"source_width":8,"source_height":4,"width":8,"height":4,"scale":1.0,"crop":null})"};
    std::string extra; // champs ajoutés au résultat (ex. detections)
    bool slow{false};  // aucune réponse au predict tant qu'il n'y a pas de cancel
    bool fail{false};
};

TransportFactory factory_for(Behavior b) {
    return [b](const BackendConfig&) -> AiResult<std::unique_ptr<ITransport>> {
        auto t = std::make_unique<Scripted>([b](const std::string& req, Scripted& self) {
            const std::string id = id_of(req);
            if (has(req, "\"type\":\"capabilities\"")) {
                self.emit(
                    R"({"id":")" + id +
                    R"(","type":"capabilities","backend":{"backend_id":"fake","backend_revision":"1","display_name":"Faux","model_id":"fake-small","model_hash":"0123456789abcdef0123","prompts":)" +
                    b.prompts +
                    R"(,"devices":["cpu"],"license_code":"Apache-2.0","license_weights":"Apache-2.0","experimental":false}})");
            } else if (has(req, "\"type\":\"health\"")) {
                self.emit(R"({"id":")" + id + R"(","type":"health","state":")" + b.health +
                          R"(","detail":"poids absents"})");
            } else if (has(req, "\"type\":\"predict\"")) {
                if (b.fail) {
                    self.emit(
                        R"({"id":")" + id +
                        R"(","type":"error","code":"INFERENCE_FAILED","message":"échec simulé","details":"d"})");
                } else if (!b.slow) {
                    self.emit(R"({"id":")" + id +
                              R"(","type":"result","image_sha256":"aa","transform":)" +
                              b.transform + R"(,"masks":[)" + b.masks + "]" + b.extra +
                              R"(,"elapsed_ms":7,"warnings":["w1"]})");
                }
            } else if (has(req, "\"type\":\"cancel\"")) {
                const auto s = req.find("\"target_id\":\"") + 13;
                const std::string target = req.substr(s, req.find('"', s) - s);
                self.emit(R"({"id":")" + target + R"(","type":"cancelled","target_id":")" + target +
                          R"("})");
            } else if (has(req, "\"type\":\"shutdown\"")) {
                self.emit(R"({"id":")" + id + R"(","type":"bye"})");
            }
        });
        t->emit(
            R"({"type":"hello","protocol":"openstitch.ai.backend/1","backend_id":"fake","pid":1})");
        return t;
    };
}

const char* kTwoMasks =
    R"({"id":"a","file":"masks/m0.png","bbox_xywh":[0,0,4,4],"area_px":16,"confidence":0.9},{"id":"b","file":"masks/m1.png","bbox_xywh":[4,0,4,4],"area_px":16})";

struct JobDir {
    std::filesystem::path path{std::filesystem::temp_directory_path() / "os_backend_run_test"};
    JobDir() {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path / "masks");
        std::ofstream(path / "masks" / "m0.png") << "x";
        std::ofstream(path / "masks" / "m1.png") << "x";
    }
    ~JobDir() { std::filesystem::remove_all(path); }
};

BackendRunRequest request_in(const JobDir& job) {
    BackendRunRequest r;
    r.backend.id = "fake";
    r.backend.display_name = "Faux SAM";
    r.job_dir = job.path;
    r.expected_size = std::pair{8, 4};
    r.session.start_timeout = 300ms;
    r.session.request_timeout = 300ms;
    r.session.task_timeout = 600ms;
    r.session.cancel_grace = 300ms;
    return r;
}

} // namespace

TEST_CASE("a healthy backend yields masks with the model that answered") {
    const JobDir job;
    Behavior b;
    b.masks = kTwoMasks;
    const auto out = run_backend_segmentation(request_in(job), factory_for(b));
    REQUIRE(out.status == BackendRunStatus::Completed);
    CHECK(out.report.model_id == "fake-small");
    CHECK(out.report.model_hash.value_or("") == "0123456789abcdef0123");
    CHECK(out.report.license_weights == "Apache-2.0");
    CHECK(out.report.warnings == std::vector<std::string>{"w1"});
    REQUIRE(out.masks.masks.size() == 2);
    CHECK(out.masks.image_width == 8);
    CHECK(out.masks.model_worker_id == "fake-small");
    CHECK(out.masks.masks[0].id == 0);
    CHECK(out.masks.masks[0].iou_reported);
    CHECK(out.masks.masks[0].predicted_iou == 0.9);
    CHECK_FALSE(out.masks.masks[1].iou_reported); // jamais synthétisé
    CHECK_FALSE(out.masks.masks[1].stability_reported);
}

TEST_CASE("every failure becomes an explicit fallback to CIELAB, never silence") {
    const JobDir job;
    SECTION("backend cannot be launched") {
        const auto out = run_backend_segmentation(
            request_in(job), [](const BackendConfig&) -> AiResult<std::unique_ptr<ITransport>> {
                return std::unexpected(AiError{AiErrorCode::WorkerStartFailed, "introuvable", "x"});
            });
        CHECK(out.status == BackendRunStatus::FellBack);
        CHECK(has(out.fallback_reason, "Faux SAM"));
        CHECK(has(out.fallback_reason, "CIELAB"));
        CHECK(out.error_code == AiErrorCode::WorkerStartFailed);
    }
    SECTION("weights missing (health unavailable)") {
        Behavior b;
        b.health = "unavailable";
        const auto out = run_backend_segmentation(request_in(job), factory_for(b));
        CHECK(out.status == BackendRunStatus::FellBack);
        CHECK(has(out.fallback_reason, "poids absents"));
        CHECK(out.error_code == AiErrorCode::BackendUnavailable);
    }
    SECTION("prompt not supported") {
        Behavior b;
        b.prompts = R"(["box"])";
        const auto out = run_backend_segmentation(request_in(job), factory_for(b));
        CHECK(out.status == BackendRunStatus::FellBack);
        CHECK(has(out.fallback_reason, "automatic"));
    }
    SECTION("inference error") {
        Behavior b;
        b.fail = true;
        const auto out = run_backend_segmentation(request_in(job), factory_for(b));
        CHECK(out.status == BackendRunStatus::FellBack);
        CHECK(out.error_code == AiErrorCode::InferenceFailed);
        CHECK(has(out.detail, "d"));
    }
    SECTION("planner without masks") {
        Behavior b;
        b.extra =
            R"(,"detections":[{"id":"d0","label":"a boat","bbox_xywh":[0,0,4,4],"prompt_index":0}])";
        const auto out = run_backend_segmentation(request_in(job), factory_for(b));
        CHECK(out.status == BackendRunStatus::FellBack);
        CHECK(has(out.fallback_reason, "boîtes"));
    }
    SECTION("no mask at all") {
        const auto out = run_backend_segmentation(request_in(job), factory_for(Behavior{}));
        CHECK(out.status == BackendRunStatus::FellBack);
        CHECK(has(out.fallback_reason, "aucun masque"));
    }
    SECTION("mask resolution differs from the sent image") {
        Behavior b;
        b.masks = kTwoMasks;
        auto request = request_in(job);
        request.expected_size = std::pair{16, 8};
        const auto out = run_backend_segmentation(request, factory_for(b));
        CHECK(out.status == BackendRunStatus::FellBack);
        CHECK(out.error_code == AiErrorCode::InvalidWorkerResponse);
    }
    SECTION("mask file escaping the job directory") {
        Behavior b;
        b.masks = R"({"id":"a","file":"../secret.png","bbox_xywh":[0,0,1,1],"area_px":1})";
        const auto out = run_backend_segmentation(request_in(job), factory_for(b));
        CHECK(out.status == BackendRunStatus::FellBack);
        CHECK(out.error_code == AiErrorCode::InvalidWorkerResponse);
    }
    SECTION("mask file absent from disk") {
        Behavior b;
        b.masks = R"({"id":"a","file":"masks/absent.png","bbox_xywh":[0,0,1,1],"area_px":1})";
        const auto out = run_backend_segmentation(request_in(job), factory_for(b));
        CHECK(out.status == BackendRunStatus::FellBack);
    }
    SECTION("disabled backend is not launched") {
        auto request = request_in(job);
        request.backend.enabled = false;
        bool launched = false;
        const auto out = run_backend_segmentation(
            request, [&](const BackendConfig&) -> AiResult<std::unique_ptr<ITransport>> {
                launched = true;
                return std::unexpected(AiError{});
            });
        CHECK(out.status == BackendRunStatus::FellBack);
        CHECK_FALSE(launched);
    }
}

TEST_CASE("cancellation is neither a result nor a fallback") {
    const JobDir job;
    SECTION("already requested: the backend is never launched") {
        std::atomic<bool> cancel{true};
        bool launched = false;
        const auto out = run_backend_segmentation(
            request_in(job),
            [&](const BackendConfig&) -> AiResult<std::unique_ptr<ITransport>> {
                launched = true;
                return std::unexpected(AiError{});
            },
            {}, &cancel);
        CHECK(out.status == BackendRunStatus::Cancelled);
        CHECK_FALSE(launched);
    }
    SECTION("requested during predict") {
        Behavior b;
        b.slow = true;
        std::atomic<bool> cancel{false};
        std::thread later([&] {
            std::this_thread::sleep_for(100ms);
            cancel = true;
        });
        const auto out = run_backend_segmentation(request_in(job), factory_for(b), {}, &cancel);
        later.join();
        CHECK(out.status == BackendRunStatus::Cancelled);
        CHECK(out.masks.masks.empty());
    }
}

TEST_CASE("status line carries state, model, fingerprint and licences") {
    BackendStatus s;
    s.config.id = "sam2";
    s.config.display_name = "SAM 2.1 small";
    s.state = BackendState::Available;
    s.diagnostics = "prêt";
    BackendCapabilities c;
    c.model_id = "sam2.1_small";
    c.model_hash = "6d1aa6f3aaaaaaaaaaaa";
    c.license_code = "Apache-2.0";
    c.license_weights = "Apache-2.0";
    s.capabilities = c;
    const auto line = backend_status_line(s);
    CHECK(has(line, "SAM 2.1 small"));
    CHECK(has(line, "available"));
    CHECK(has(line, "sam2.1_small"));
    CHECK(has(line, "6d1aa6f3aaaa"));
    CHECK_FALSE(has(line, "aaaaaaaaaaaa"));
    s.capabilities->model_hash.reset();
    CHECK(has(backend_status_line(s), "non fournie"));
}
