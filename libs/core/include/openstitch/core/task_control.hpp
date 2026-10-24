// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <functional>
#include <stop_token>
#include <utility>

#include "openstitch/core/error.hpp"

namespace openstitch {

// Contrôle d'une tâche longue (HP-PERF-001), sans Qt : jeton d'annulation coopératif
// (`std::stop_token`) et rappel de progression. Un contexte par défaut ne s'annule jamais et ne
// rapporte rien : le calcul est alors identique à celui d'un appel synchrone, octet pour octet.
// `progress` peut être appelé depuis n'importe quel fil de travail : il doit être sûr vis-à-vis des
// fils (compteur atomique, pas d'accès direct à un widget).
struct TaskContext {
    std::stop_token stop;
    std::function<void(std::size_t done, std::size_t total)> progress;

    [[nodiscard]] bool cancelled() const noexcept { return stop.stop_requested(); }
    void report(std::size_t done, std::size_t total) const {
        if (progress) {
            progress(done, total);
        }
    }
};

[[nodiscard]] inline std::unexpected<Error> cancelled_error() {
    return fail(ErrorCategory::Cancelled, "Opération annulée");
}

} // namespace openstitch
