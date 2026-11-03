// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QProgressDialog>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <initializer_list>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <vector>

#include "openstitch/core/task_control.hpp"

namespace openstitch::desktop {
// Attente modale partagée : calcul sur un instantané, mutation ensuite sur le fil UI.
//
// HP-PERF-001 / HP-UX-006 : avec `cancellable`, un bouton « Annuler » demande l'arrêt coopératif
// du calcul (`std::stop_token`) et la barre devient déterminée dès que le calcul rapporte sa
// progression. Un calcul qui accepte un `const TaskContext&` reçoit le jeton et le rappel de
// progression ; l'annulation ne modifie jamais le document (l'appelant ignore le résultat).
class BusyIndicator {
public:
    BusyIndicator(QWidget* parent, const QString& text, std::initializer_list<QTimer*> pause = {},
                  bool cancellable = false)
        : dialog_(text, QString(), 0, 0, parent), cancellable_(cancellable) {
        for (QTimer* timer : pause) {
            if (timer != nullptr && timer->isActive()) {
                timer->stop();
                paused_.push_back(timer);
            }
        }
        dialog_.setWindowTitle(QObject::tr("Veuillez patienter"));
        dialog_.setWindowModality(Qt::WindowModal);
        if (cancellable_) {
            dialog_.setCancelButtonText(QObject::tr("Annuler"));
            dialog_.setAutoClose(false);
            dialog_.setAutoReset(false);
            QObject::connect(&dialog_, &QProgressDialog::canceled, &dialog_,
                             [this] { requestCancel(); });
        } else {
            dialog_.setCancelButton(nullptr);
        }
        dialog_.setMinimumDuration(0);
        dialog_.show();
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        QCoreApplication::processEvents(pumpFlags());
    }
    ~BusyIndicator() {
        QGuiApplication::restoreOverrideCursor();
        dialog_.close();
        for (QTimer* timer : paused_) {
            timer->start();
        }
    }
    BusyIndicator(const BusyIndicator&) = delete;
    BusyIndicator& operator=(const BusyIndicator&) = delete;

    // Demande l'arrêt coopératif (bouton « Annuler », ou test).
    void requestCancel() { stop_.request_stop(); }
    [[nodiscard]] bool cancelled() const noexcept { return stop_.stop_requested(); }
    [[nodiscard]] QProgressDialog& dialog() noexcept { return dialog_; }

    template <class F> auto run(F&& compute) {
        auto future =
            std::async(std::launch::async, [this, fn = std::forward<F>(compute)]() mutable {
                if constexpr (std::is_invocable_v<decltype(fn)&, const TaskContext&>) {
                    TaskContext task;
                    task.stop = stop_.get_token();
                    task.progress = [this](std::size_t done, std::size_t total) {
                        total_.store(total);
                        done_.store(done);
                    };
                    return fn(task);
                } else {
                    return fn();
                }
            });
        while (future.wait_for(std::chrono::milliseconds(25)) != std::future_status::ready) {
            refreshProgress();
            QCoreApplication::processEvents(pumpFlags());
        }
        refreshProgress();
        return future.get();
    }

private:
    [[nodiscard]] QEventLoop::ProcessEventsFlags pumpFlags() const {
        // Le bouton « Annuler » doit recevoir la souris ; le dialogue modal par fenêtre protège
        // le reste de l'interface.
        return cancellable_ ? QEventLoop::AllEvents : QEventLoop::ExcludeUserInputEvents;
    }
    void refreshProgress() {
        const std::size_t total = total_.load();
        if (total == 0) {
            return;
        }
        const int shown = static_cast<int>(std::min<std::size_t>(total, 1'000'000));
        if (dialog_.maximum() != shown) {
            dialog_.setRange(0, shown);
        }
        dialog_.setValue(static_cast<int>(std::min<std::size_t>(done_.load(), total)));
    }

    QProgressDialog dialog_;
    bool cancellable_{false};
    std::stop_source stop_;
    std::atomic<std::size_t> done_{0};
    std::atomic<std::size_t> total_{0};
    std::vector<QTimer*> paused_;
};
} // namespace openstitch::desktop
