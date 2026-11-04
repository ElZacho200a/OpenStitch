// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- gestionnaire de tâches longues. Remplace les appels `BusyIndicator::run`
// dispersés : toute tâche est nommée, annulable (std::stop_token via core::TaskContext), rapporte
// sa progression et livre son résultat sur le fil appelant de `pump()` (le fil d'interface), jamais
// depuis un fil de travail. Sans Qt : l'interface appelle `pump()` depuis un QTimer.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"

namespace openstitch::desktop_v2 {

using TaskId = std::uint64_t;

enum class TaskState : std::uint8_t { Running, Succeeded, Failed, Cancelled };

struct TaskInfo {
    TaskId id{0};
    std::string label;
    TaskState state{TaskState::Running};
    std::size_t done{0};
    std::size_t total{0};
    std::string message; // message d'erreur montrable si Failed
};

class TaskManager {
public:
    TaskManager() = default;
    TaskManager(const TaskManager&) = delete;
    TaskManager& operator=(const TaskManager&) = delete;
    ~TaskManager();

    // `work` s'exécute sur un fil de travail et ne doit toucher ni le document vivant ni un widget
    // (il opère sur des données capturées par valeur). `done` est appelé par `pump()` sur le fil
    // appelant, une seule fois, avec le résultat ; jamais si la tâche a été annulée.
    template <typename T>
    TaskId submit(std::string label, std::function<Result<T>(const TaskContext&)> work,
                  std::function<void(Result<T>&&)> done) {
        auto slot = std::make_shared<std::optional<Result<T>>>();
        return enqueue(
            std::move(label),
            [work = std::move(work), slot](const TaskContext& ctx) -> std::optional<Error> {
                slot->emplace(work(ctx)); // *slot : optional ; **slot : le Result
                if (**slot) {
                    return std::nullopt;
                }
                return (*slot)->error();
            },
            [done = std::move(done), slot] {
                if (done && slot->has_value()) {
                    done(std::move(**slot));
                }
            });
    }

    bool cancel(TaskId id);
    void cancelAll();

    // Livre les complétions en attente sur le fil appelant ; retourne leur nombre.
    std::size_t pump();

    [[nodiscard]] std::vector<TaskInfo> running() const;
    [[nodiscard]] std::vector<TaskInfo> history() const; // dernières tâches terminées (50 max)
    [[nodiscard]] bool idle() const;                     // plus rien en cours NI à livrer
    void waitIdle();                                     // bloque jusqu'à idle() (tests, arrêt)

private:
    struct Entry;
    TaskId enqueue(std::string label, std::function<std::optional<Error>(const TaskContext&)> work,
                   std::function<void()> deliver);
    void finish(TaskId id, std::optional<Error> error);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::map<TaskId, std::shared_ptr<Entry>> tasks_;
    std::deque<std::pair<TaskInfo, std::function<void()>>> finished_;
    std::deque<TaskInfo> history_;
    TaskId nextId_{1};
};

} // namespace openstitch::desktop_v2
