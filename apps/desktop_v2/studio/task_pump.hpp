// SPDX-License-Identifier: Apache-2.0
#pragma once

// Livraison des tâches à la demande : remplace les QTimer de 8/16 ms que chaque contrôleur gardait
// allumés en permanence pour appeler `TaskManager::pump()` (≈ 1 000 réveils/s du fil d'interface au
// repos). Le fil de travail poste UN événement (coalescé) quand une complétion est prête ; `pump()`
// s'exécute alors sur le fil de `owner`. Aucun réveil tant qu'aucune tâche ne se termine.

#include <QMetaObject>
#include <QObject>
#include <atomic>
#include <memory>

#include "task_manager.hpp"

namespace openstitch::desktop_v2::studio {

inline void attachTaskPump(TaskManager& tasks, QObject* owner) {
    auto pending = std::make_shared<std::atomic<bool>>(false);
    tasks.setCompletionNotifier([&tasks, owner, pending] {
        if (pending->exchange(true, std::memory_order_acq_rel)) {
            return; // un pump() est déjà planifié : il livrera aussi cette complétion
        }
        QMetaObject::invokeMethod(
            owner,
            [&tasks, pending] {
                pending->store(false, std::memory_order_release);
                tasks.pump();
            },
            Qt::QueuedConnection);
    });
}

} // namespace openstitch::desktop_v2::studio
