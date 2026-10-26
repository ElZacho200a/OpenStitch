// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/backend_session.hpp"

#include <utility>

namespace openstitch::ai_segmentation {

namespace {

using Clock = std::chrono::steady_clock;

AiError make_error(AiErrorCode code, std::string detail = {}) {
    return AiError{code, default_message(code), std::move(detail)};
}

} // namespace

BackendSession::BackendSession(std::unique_ptr<ITransport> transport, SessionOptions options,
                               HelloMsg hello)
    : transport_(std::move(transport)), options_(options), hello_(std::move(hello)) {}

BackendSession::BackendSession(BackendSession&&) noexcept = default;
BackendSession& BackendSession::operator=(BackendSession&& other) noexcept {
    if (this != &other) {
        close();
        transport_ = std::move(other.transport_);
        options_ = other.options_;
        hello_ = std::move(other.hello_);
        counter_ = other.counter_;
    }
    return *this;
}

BackendSession::~BackendSession() {
    close();
}

AiResult<BackendSession> BackendSession::open(std::unique_ptr<ITransport> transport,
                                              SessionOptions options) {
    if (!transport) {
        return std::unexpected(make_error(AiErrorCode::WorkerStartFailed, "transport nul"));
    }
    const auto deadline = Clock::now() + options.start_timeout;
    while (true) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() <= 0) {
            transport->terminate();
            return std::unexpected(make_error(AiErrorCode::BackendTimeout, "aucun `hello` reçu"));
        }
        auto line = transport->read_line(left);
        if (!line) {
            transport->terminate();
            return std::unexpected(line.error());
        }
        if (!line->has_value()) {
            continue;
        }
        auto message = parse_worker_message(**line);
        if (!message) {
            transport->terminate();
            return std::unexpected(message.error());
        }
        const auto* hello = std::get_if<HelloMsg>(&*message);
        if (hello == nullptr) {
            transport->terminate();
            return std::unexpected(make_error(AiErrorCode::InvalidWorkerResponse,
                                              "premier message différent de `hello`"));
        }
        const auto major = backend_protocol_major(hello->protocol);
        if (!major || *major != kBackendProtocolMajor) {
            transport->terminate();
            return std::unexpected(make_error(AiErrorCode::ProtocolMismatch,
                                              "worker : " + hello->protocol +
                                                  ", attendu : " + std::string(kBackendProtocol)));
        }
        return BackendSession(std::move(transport), options, *hello);
    }
}

const HelloMsg& BackendSession::hello() const {
    return hello_;
}

std::string BackendSession::next_id(std::string_view prefix) {
    return std::string(prefix) + "-" + std::to_string(++counter_);
}

AiResult<WorkerMessage> BackendSession::await(const std::string& id,
                                              std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (true) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() <= 0) {
            transport_->terminate();
            return std::unexpected(make_error(AiErrorCode::BackendTimeout, "requête " + id));
        }
        auto line = transport_->read_line(left);
        if (!line) {
            return std::unexpected(line.error());
        }
        if (!line->has_value()) {
            continue;
        }
        auto message = parse_worker_message(**line);
        if (!message) {
            transport_->terminate(); // sortie parasite : le flux n'est plus fiable
            return std::unexpected(message.error());
        }
        const std::string mid = message_id(*message);
        if (mid == id || mid.empty()) {
            if (const auto* err = std::get_if<ErrorMsg>(&*message)) {
                return std::unexpected(err->error);
            }
            return message;
        }
    }
}

AiResult<BackendCapabilities> BackendSession::capabilities() {
    const std::string id = next_id("cap");
    if (auto w = transport_->write_line(serialize_capabilities_request(id)); !w) {
        return std::unexpected(w.error());
    }
    auto reply = await(id, options_.request_timeout);
    if (!reply) {
        return std::unexpected(reply.error());
    }
    if (const auto* caps = std::get_if<CapabilitiesMsg>(&*reply)) {
        return caps->backend;
    }
    return std::unexpected(
        make_error(AiErrorCode::InvalidWorkerResponse, "réponse inattendue à `capabilities`"));
}

AiResult<HealthMsg> BackendSession::health() {
    const std::string id = next_id("health");
    if (auto w = transport_->write_line(serialize_health_request(id)); !w) {
        return std::unexpected(w.error());
    }
    auto reply = await(id, options_.request_timeout);
    if (!reply) {
        return std::unexpected(reply.error());
    }
    if (const auto* health = std::get_if<HealthMsg>(&*reply)) {
        return *health;
    }
    return std::unexpected(
        make_error(AiErrorCode::InvalidWorkerResponse, "réponse inattendue à `health`"));
}

AiResult<PredictResult> BackendSession::predict(const PredictRequest& request,
                                                const ProgressFn& progress,
                                                const std::atomic<bool>* cancel) {
    PredictRequest sent = request;
    if (sent.id.empty()) {
        sent.id = next_id("predict");
    }
    if (auto w = transport_->write_line(serialize_predict_request(sent)); !w) {
        return std::unexpected(w.error());
    }
    const auto task_deadline = Clock::now() + options_.task_timeout;
    std::optional<Clock::time_point> cancel_deadline;
    constexpr std::chrono::milliseconds slice{50};
    while (true) {
        const auto now = Clock::now();
        if (now >= task_deadline || (cancel_deadline && now >= *cancel_deadline)) {
            transport_->terminate();
            return std::unexpected(
                make_error(AiErrorCode::BackendTimeout,
                           cancel_deadline ? "annulation non confirmée" : "tâche trop longue"));
        }
        if (cancel != nullptr && cancel->load() && !cancel_deadline) {
            if (auto w =
                    transport_->write_line(serialize_cancel_request(next_id("cancel"), sent.id));
                !w) {
                return std::unexpected(w.error());
            }
            cancel_deadline = now + options_.cancel_grace;
        }
        auto line = transport_->read_line(slice);
        if (!line) {
            return std::unexpected(line.error());
        }
        if (!line->has_value()) {
            continue;
        }
        auto message = parse_worker_message(**line);
        if (!message) {
            transport_->terminate();
            return std::unexpected(message.error());
        }
        const std::string mid = message_id(*message);
        if (mid != sent.id && !mid.empty()) {
            continue;
        }
        if (const auto* p = std::get_if<ProgressMsg>(&*message)) {
            if (progress) {
                progress(*p);
            }
        } else if (auto* result = std::get_if<PredictResult>(&*message)) {
            return std::move(*result);
        } else if (std::get_if<CancelledMsg>(&*message) != nullptr) {
            return std::unexpected(make_error(AiErrorCode::Cancelled));
        } else if (const auto* err = std::get_if<ErrorMsg>(&*message)) {
            return std::unexpected(err->error);
        }
    }
}

void BackendSession::close() {
    if (!transport_) {
        return;
    }
    (void)transport_->write_line(serialize_shutdown_request(next_id("bye")));
    // courte attente du `bye` ; le transport est arrêté dans tous les cas
    for (int i = 0; i < 20; ++i) {
        auto line = transport_->read_line(std::chrono::milliseconds{50});
        if (!line || (line->has_value() && line->value().find("\"bye\"") != std::string::npos)) {
            break;
        }
    }
    transport_->terminate();
    transport_.reset();
}

} // namespace openstitch::ai_segmentation
