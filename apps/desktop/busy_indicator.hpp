// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QProgressDialog>
#include <QTimer>

#include <chrono>
#include <future>
#include <initializer_list>
#include <utility>
#include <vector>

namespace openstitch::desktop {
// Attente modale partagée : calcul sur un instantané, mutation ensuite sur le fil UI.
class BusyIndicator {
public:
    BusyIndicator(QWidget* parent, const QString& text, std::initializer_list<QTimer*> pause = {})
        : dialog_(text, QString(), 0, 0, parent) {
        for (QTimer* timer : pause) {
            if (timer != nullptr && timer->isActive()) {
                timer->stop();
                paused_.push_back(timer);
            }
        }
        dialog_.setWindowTitle(QObject::tr("Veuillez patienter"));
        dialog_.setWindowModality(Qt::WindowModal);
        dialog_.setCancelButton(nullptr);
        dialog_.setMinimumDuration(0);
        dialog_.show();
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
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
    template <class F> auto run(F&& compute) {
        auto future = std::async(std::launch::async, std::forward<F>(compute));
        while (future.wait_for(std::chrono::milliseconds(25)) != std::future_status::ready) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        return future.get();
    }

private:
    QProgressDialog dialog_;
    std::vector<QTimer*> paused_;
};
} // namespace openstitch::desktop
