// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace openstitch {

// Nombre de fils de travail utilisés par `parallel_for` : les coeurs de la machine
// (le fil appelant travaille aussi, il compte parmi eux), borné à [1 ; 16]. La variable
// d'environnement OPENSTITCH_THREADS (entier >= 1) impose le nombre de fils : 1 = tout en
// séquentiel (mesures, diagnostic).
[[nodiscard]] inline std::size_t parallel_workers() {
    if (const char* forced = std::getenv("OPENSTITCH_THREADS")) {
        const long value = std::strtol(forced, nullptr, 10);
        if (value >= 1) {
            return std::clamp<std::size_t>(static_cast<std::size_t>(value), 1, 64);
        }
    }
    const std::size_t hw = std::thread::hardware_concurrency();
    return std::clamp<std::size_t>(hw == 0 ? 1 : hw, 1, 16);
}

// Exécute `fn(i)` pour i dans [0 ; count) sur plusieurs fils. Les itérations doivent être
// INDÉPENDANTES (aucune ne lit ce qu'une autre écrit) ; chacune écrit dans « sa » case d'un
// tableau pré-dimensionné, de sorte que le résultat final ne dépend ni du nombre de fils ni de
// l'ordonnancement : déterminisme préservé. En deçà de `min_parallel` itérations, tout se
// passe sur le fil appelant (pas de coût de création de fils). La première exception levée par
// une itération est relancée sur le fil appelant après la fin des autres.
template <typename Fn> void parallel_for(std::size_t count, Fn&& fn, std::size_t min_parallel = 2) {
    if (count == 0) {
        return;
    }
    const std::size_t workers = std::min(parallel_workers(), count);
    if (count < min_parallel || workers <= 1) {
        for (std::size_t i = 0; i < count; ++i) {
            fn(i);
        }
        return;
    }
    std::atomic<std::size_t> next{0};
    std::exception_ptr failure;
    std::mutex failureMutex;
    const auto run = [&] {
        for (std::size_t i = next.fetch_add(1); i < count; i = next.fetch_add(1)) {
            try {
                fn(i);
            } catch (...) {
                const std::lock_guard<std::mutex> lock(failureMutex);
                if (!failure) {
                    failure = std::current_exception();
                }
            }
        }
    };
    std::vector<std::thread> threads;
    threads.reserve(workers - 1);
    for (std::size_t t = 1; t < workers; ++t) {
        threads.emplace_back(run);
    }
    run();
    for (auto& thread : threads) {
        thread.join();
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
}

} // namespace openstitch
