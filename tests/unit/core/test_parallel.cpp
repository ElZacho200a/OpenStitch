// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <stdexcept>
#include <vector>

#include "openstitch/core/parallel.hpp"

using openstitch::parallel_for;

TEST_CASE("parallel_for visits every index exactly once", "[parallel]") {
    constexpr std::size_t n = 5000;
    std::vector<int> hits(n, 0);
    parallel_for(n, [&](std::size_t i) { hits[i] += 1; });
    for (std::size_t i = 0; i < n; ++i) {
        REQUIRE(hits[i] == 1);
    }
}

TEST_CASE("parallel_for result does not depend on scheduling", "[parallel]") {
    constexpr std::size_t n = 2000;
    std::vector<std::size_t> a(n), b(n);
    parallel_for(n, [&](std::size_t i) { a[i] = i * i + 7; });
    for (std::size_t i = 0; i < n; ++i) {
        b[i] = i * i + 7;
    }
    REQUIRE(a == b);
}

TEST_CASE("parallel_for handles empty and tiny ranges", "[parallel]") {
    std::atomic<int> count{0};
    parallel_for(0, [&](std::size_t) { ++count; });
    REQUIRE(count == 0);
    parallel_for(1, [&](std::size_t) { ++count; });
    REQUIRE(count == 1);
}

TEST_CASE("parallel_for rethrows the first exception after finishing", "[parallel]") {
    std::atomic<int> done{0};
    REQUIRE_THROWS_AS(parallel_for(64,
                                   [&](std::size_t i) {
                                       if (i == 13) {
                                           throw std::runtime_error("boom");
                                       }
                                       ++done;
                                   }),
                      std::runtime_error);
    REQUIRE(done == 63);
}
