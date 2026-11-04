// SPDX-License-Identifier: Apache-2.0
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>

#include "task_manager.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

TEST_CASE("task result is delivered on the pumping thread only", "[desktop_v2][tasks]") {
    TaskManager tm;
    std::thread::id workerThread;
    std::thread::id doneThread;
    int value = 0;
    tm.submit<int>(
        "calcul",
        [&](const TaskContext&) -> Result<int> {
            workerThread = std::this_thread::get_id();
            return 42;
        },
        [&](Result<int>&& r) {
            doneThread = std::this_thread::get_id();
            value = *r;
        });
    tm.waitIdle();
    CHECK(value == 42);
    CHECK(workerThread != std::this_thread::get_id());
    CHECK(doneThread == std::this_thread::get_id());
    REQUIRE(tm.history().size() == 1);
    CHECK(tm.history()[0].state == TaskState::Succeeded);
}

TEST_CASE("task failure reports a message and still delivers the error", "[desktop_v2][tasks]") {
    TaskManager tm;
    std::string seen;
    tm.submit<int>(
        "echec",
        [](const TaskContext&) -> Result<int> {
            return fail(ErrorCategory::UserInput, "entrée refusée");
        },
        [&](Result<int>&& r) { seen = r ? "ok" : r.error().message; });
    tm.waitIdle();
    CHECK(seen == "entrée refusée");
    CHECK(tm.history()[0].state == TaskState::Failed);
    CHECK(tm.history()[0].message == "entrée refusée");
}

TEST_CASE("cancelled task never delivers a stale result", "[desktop_v2][tasks]") {
    TaskManager tm;
    std::atomic<bool> started{false};
    bool delivered = false;
    const TaskId id = tm.submit<int>(
        "long",
        [&](const TaskContext& ctx) -> Result<int> {
            started = true;
            while (!ctx.cancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return cancelled_error();
        },
        [&](Result<int>&&) { delivered = true; });
    while (!started) {
        std::this_thread::yield();
    }
    CHECK(tm.cancel(id));
    tm.waitIdle();
    CHECK_FALSE(delivered);
    CHECK(tm.history()[0].state == TaskState::Cancelled);
}

TEST_CASE("task progress is visible while running", "[desktop_v2][tasks]") {
    TaskManager tm;
    std::atomic<bool> reported{false};
    std::atomic<bool> release{false};
    tm.submit<int>("progression",
                   [&](const TaskContext& ctx) -> Result<int> {
                       ctx.report(3, 10);
                       reported = true;
                       while (!release) {
                           std::this_thread::yield();
                       }
                       return 1;
                   },
                   {});
    while (!reported) {
        std::this_thread::yield();
    }
    const auto running = tm.running();
    REQUIRE(running.size() == 1);
    CHECK(running[0].done == 3);
    CHECK(running[0].total == 10);
    CHECK(running[0].label == "progression");
    release = true;
    tm.waitIdle();
    CHECK(tm.idle());
}

TEST_CASE("exception in a task becomes an internal failure", "[desktop_v2][tasks]") {
    TaskManager tm;
    tm.submit<int>(
        "boom", [](const TaskContext&) -> Result<int> { throw std::runtime_error("kaboom"); }, {});
    tm.waitIdle();
    REQUIRE(tm.history().size() == 1);
    CHECK(tm.history()[0].state == TaskState::Failed);
    CHECK(tm.history()[0].message == "kaboom");
}

TEST_CASE("destroying the manager cancels and joins running tasks", "[desktop_v2][tasks]") {
    std::atomic<bool> sawStop{false};
    std::atomic<bool> started{false};
    {
        TaskManager tm;
        tm.submit<int>("orpheline",
                       [&](const TaskContext& ctx) -> Result<int> {
                           started = true;
                           while (!ctx.cancelled()) {
                               std::this_thread::yield();
                           }
                           sawStop = true;
                           return cancelled_error();
                       },
                       {});
        while (!started) {
            std::this_thread::yield();
        }
    }
    CHECK(sawStop);
}
