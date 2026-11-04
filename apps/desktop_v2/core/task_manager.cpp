// SPDX-License-Identifier: Apache-2.0
#include "task_manager.hpp"

#include <exception>

namespace openstitch::desktop_v2 {

struct TaskManager::Entry {
    TaskInfo info;
    std::atomic<std::size_t> done{0};
    std::atomic<std::size_t> total{0};
    std::function<void()> deliver;
    std::jthread worker;
};

TaskManager::~TaskManager() {
    std::vector<std::shared_ptr<Entry>> entries;
    {
        std::scoped_lock lock(mutex_);
        for (auto& [id, entry] : tasks_) {
            (void)id;
            entry->worker.request_stop();
            entries.push_back(entry);
        }
    }
    for (auto& e : entries) { // join hors verrou : `finish` a besoin du mutex
        if (e->worker.joinable()) {
            e->worker.join();
        }
    }
}

TaskId TaskManager::enqueue(std::string label,
                            std::function<std::optional<Error>(const TaskContext&)> work,
                            std::function<void()> deliver) {
    auto entry = std::make_shared<Entry>();
    std::scoped_lock lock(mutex_);
    const TaskId id = nextId_++;
    entry->info.id = id;
    entry->info.label = std::move(label);
    entry->deliver = std::move(deliver);
    tasks_[id] = entry;
    // Fil lancé sous verrou : `finish` (qui prend le même verrou) ne peut pas s'exécuter avant que
    // `entry->worker` soit affecté, et `cancel` ne voit jamais un jthread à moitié construit.
    entry->worker = std::jthread([this, id, entry, work = std::move(work)](std::stop_token stop) {
        TaskContext ctx;
        ctx.stop = stop;
        ctx.progress = [entry](std::size_t done, std::size_t total) {
            entry->done.store(done, std::memory_order_relaxed);
            entry->total.store(total, std::memory_order_relaxed);
        };
        std::optional<Error> error;
        try {
            error = work(ctx);
        } catch (const std::exception& e) {
            error = Error{ErrorCategory::Internal, e.what(), {}, {}, {}};
        } catch (...) {
            error = Error{ErrorCategory::Internal, "exception inconnue", {}, {}, {}};
        }
        finish(id, std::move(error));
    });
    return id;
}

void TaskManager::finish(TaskId id, std::optional<Error> error) {
    std::scoped_lock lock(mutex_);
    const auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        return;
    }
    auto& entry = *it->second;
    entry.info.done = entry.done.load();
    entry.info.total = entry.total.load();
    if (!error) {
        entry.info.state = TaskState::Succeeded;
    } else if (error->category == ErrorCategory::Cancelled) {
        entry.info.state = TaskState::Cancelled;
    } else {
        entry.info.state = TaskState::Failed;
        entry.info.message = error->message;
    }
    // Une tâche annulée ne livre aucun résultat : l'appelant n'applique jamais un état périmé
    // (principe H : tout changement reste maîtrisable).
    std::function<void()> deliver;
    if (entry.info.state != TaskState::Cancelled) {
        deliver = std::move(entry.deliver);
    }
    finished_.emplace_back(entry.info, std::move(deliver));
    cv_.notify_all();
}

bool TaskManager::cancel(TaskId id) {
    std::scoped_lock lock(mutex_);
    const auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        return false;
    }
    it->second->worker.request_stop();
    return true;
}

void TaskManager::cancelAll() {
    std::scoped_lock lock(mutex_);
    for (auto& [id, entry] : tasks_) {
        (void)id;
        entry->worker.request_stop();
    }
}

std::size_t TaskManager::pump() {
    std::deque<std::pair<TaskInfo, std::function<void()>>> batch;
    std::vector<std::shared_ptr<Entry>> reaped;
    {
        std::scoped_lock lock(mutex_);
        batch.swap(finished_);
        for (const auto& [info, deliver] : batch) {
            (void)deliver;
            if (const auto it = tasks_.find(info.id); it != tasks_.end()) {
                reaped.push_back(it->second);
                tasks_.erase(it);
            }
            history_.push_back(info);
            if (history_.size() > 50) {
                history_.pop_front();
            }
        }
    }
    for (auto& e : reaped) { // le travail est fini : le join est immédiat
        if (e->worker.joinable()) {
            e->worker.join();
        }
    }
    const std::size_t count = batch.size();
    for (auto& [info, deliver] : batch) {
        (void)info;
        if (deliver) {
            deliver();
        }
    }
    cv_.notify_all();
    return count;
}

std::vector<TaskInfo> TaskManager::running() const {
    std::scoped_lock lock(mutex_);
    std::vector<TaskInfo> out;
    for (const auto& [id, entry] : tasks_) {
        (void)id;
        if (entry->info.state != TaskState::Running) {
            continue; // terminée, en attente de pump()
        }
        TaskInfo info = entry->info;
        info.done = entry->done.load(std::memory_order_relaxed);
        info.total = entry->total.load(std::memory_order_relaxed);
        out.push_back(std::move(info));
    }
    return out;
}

std::vector<TaskInfo> TaskManager::history() const {
    std::scoped_lock lock(mutex_);
    return {history_.begin(), history_.end()};
}

bool TaskManager::idle() const {
    std::scoped_lock lock(mutex_);
    return tasks_.empty() && finished_.empty();
}

void TaskManager::waitIdle() {
    while (true) {
        pump();
        std::unique_lock lock(mutex_);
        if (tasks_.empty() && finished_.empty()) {
            return;
        }
        cv_.wait_for(lock, std::chrono::milliseconds(20));
    }
}

} // namespace openstitch::desktop_v2
