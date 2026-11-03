// SPDX-License-Identifier: Apache-2.0
// HP-PERF-001 : contexte de tache (annulation cooperative + progression), sans Qt.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <stop_token>

#include "openstitch/core/task_control.hpp"

TEST_CASE("TaskContext par defaut ne s'annule jamais et ignore la progression", "[task]") {
    const openstitch::TaskContext task;
    CHECK_FALSE(task.cancelled());
    task.report(1, 2); // sans rappel : aucun effet
}

TEST_CASE("TaskContext relaie l'annulation et la progression", "[task]") {
    std::stop_source source;
    std::size_t doneSeen = 0;
    std::size_t totalSeen = 0;
    openstitch::TaskContext task;
    task.stop = source.get_token();
    task.progress = [&](std::size_t done, std::size_t total) {
        doneSeen = done;
        totalSeen = total;
    };
    CHECK_FALSE(task.cancelled());
    task.report(3, 7);
    CHECK(doneSeen == 3);
    CHECK(totalSeen == 7);
    source.request_stop();
    CHECK(task.cancelled());
    CHECK(openstitch::cancelled_error().error().category == openstitch::ErrorCategory::Cancelled);
}
