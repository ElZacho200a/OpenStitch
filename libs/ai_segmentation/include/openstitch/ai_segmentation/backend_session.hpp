// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "openstitch/ai_segmentation/backend_protocol.hpp"

namespace openstitch::ai_segmentation {

// Canal ligne par ligne vers un worker. Abstrait pour que la logique de session soit testée
// sans processus (backends factices en mémoire) ; l'implémentation réelle est
// `spawn_process` (process_transport.hpp).
class ITransport {
public:
    virtual ~ITransport() = default;
    virtual AiResult<void> write_line(std::string_view line) = 0;
    // nullopt = délai écoulé sans ligne complète. Erreur WorkerCrashed = flux fermé.
    virtual AiResult<std::optional<std::string>> read_line(std::chrono::milliseconds timeout) = 0;
    // Arrêt immédiat (idempotent).
    virtual void terminate() = 0;
};

struct SessionOptions {
    std::chrono::milliseconds start_timeout{15000};   // attente du `hello`
    std::chrono::milliseconds request_timeout{10000}; // capabilities / health
    std::chrono::milliseconds task_timeout{600000};   // predict
    std::chrono::milliseconds cancel_grace{5000}; // délai accordé après `cancel` avant arrêt forcé
};

// Client synchrone d'UN worker. Non copiable ; un seul appel à la fois (le protocole n'admet
// qu'une tâche active, une seconde reçoit BACKEND_BUSY).
class BackendSession {
public:
    // Lit le `hello` et refuse toute version majeure différente (ProtocolMismatch) : jamais de
    // dégradation silencieuse.
    [[nodiscard]] static AiResult<BackendSession> open(std::unique_ptr<ITransport> transport,
                                                       SessionOptions options = {});

    BackendSession(BackendSession&&) noexcept;
    BackendSession& operator=(BackendSession&&) noexcept;
    ~BackendSession(); // demande `shutdown` puis arrête le transport

    [[nodiscard]] const HelloMsg& hello() const;

    [[nodiscard]] AiResult<BackendCapabilities> capabilities();
    [[nodiscard]] AiResult<HealthMsg> health();

    using ProgressFn = std::function<void(const ProgressMsg&)>;
    // `cancel` est interrogé environ toutes les 50 ms ; une fois vrai, `cancel` est envoyé au
    // worker. Retourne Cancelled si le worker confirme, BackendTimeout (et arrête le worker) s'il
    // ne répond pas dans `cancel_grace`.
    [[nodiscard]] AiResult<PredictResult> predict(const PredictRequest& request,
                                                  const ProgressFn& progress = {},
                                                  const std::atomic<bool>* cancel = nullptr);

    // Fin propre : `shutdown` + arrêt du transport. Appelée par le destructeur.
    void close();

private:
    BackendSession(std::unique_ptr<ITransport> transport, SessionOptions options, HelloMsg hello);
    // Attend le premier message portant `id` ; ignore ceux d'autres requêtes (progress tardifs).
    [[nodiscard]] AiResult<WorkerMessage> await(const std::string& id,
                                                std::chrono::milliseconds timeout);
    [[nodiscard]] std::string next_id(std::string_view prefix);

    std::unique_ptr<ITransport> transport_;
    SessionOptions options_;
    HelloMsg hello_;
    int counter_{0};
};

} // namespace openstitch::ai_segmentation
