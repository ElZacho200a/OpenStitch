// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "openstitch/thread_palette/catalog.hpp"

using namespace openstitch::thread_palette;

TEST_CASE("all_charts returns at least one chart") {
    REQUIRE(!all_charts().empty());
}

TEST_CASE("all_charts order is stable across two calls") {
    const auto first = all_charts();
    const auto second = all_charts();
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].chart_id == second[i].chart_id);
    }
}

TEST_CASE("every ThreadChart has a non-empty source_note") {
    for (const ThreadChart& chart : all_charts()) {
        CHECK(!chart.source_note.empty());
    }
}

TEST_CASE("find_chart resolves a known chart_id") {
    const ThreadChart* chart = find_chart("madeira_polyneon");
    REQUIRE(chart != nullptr);
    CHECK(chart->chart_id == "madeira_polyneon");
    CHECK(!chart->threads.empty());
}

TEST_CASE("find_chart returns nullptr for an unknown chart_id") {
    CHECK(find_chart("does_not_exist") == nullptr);
}

TEST_CASE("find_by_code resolves a known thread in Madeira Polyneon") {
    const auto thread = find_by_code("madeira_polyneon", "1919");
    REQUIRE(thread.has_value());
    CHECK(thread->key.chart_id == "madeira_polyneon");
    CHECK(thread->key.code == "1919");
    CHECK(!thread->name.empty());
}

TEST_CASE("find_by_code returns empty optional for an unknown code") {
    CHECK_FALSE(find_by_code("madeira_polyneon", "9999999").has_value());
    CHECK_FALSE(find_by_code("does_not_exist", "1919").has_value());
}

TEST_CASE("search_by_name is case insensitive and matches substrings") {
    const auto lower = search_by_name("red");
    const auto upper = search_by_name("RED");
    const auto mixed = search_by_name("ReD");
    REQUIRE(!lower.empty());
    CHECK(lower.size() == upper.size());
    CHECK(lower.size() == mixed.size());
    for (const Thread& t : lower) {
        std::string lowered_name = t.name;
        std::transform(lowered_name.begin(), lowered_name.end(), lowered_name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        CHECK(lowered_name.find("red") != std::string::npos);
    }
}

TEST_CASE("search_by_name returns results in registry order") {
    const auto results = search_by_name("e"); // large substring, matches many entries
    REQUIRE(results.size() > 1);

    // Reconstruit l'ordre attendu en balayant le registre dans l'ordre et
    // compare : search_by_name ne doit pas re-trier.
    std::vector<ThreadKey> expected_order;
    for (const ThreadChart& chart : all_charts()) {
        for (const Thread& thread : chart.threads) {
            std::string lowered_name = thread.name;
            std::transform(lowered_name.begin(), lowered_name.end(), lowered_name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lowered_name.find('e') != std::string::npos) {
                expected_order.push_back(thread.key);
            }
        }
    }

    REQUIRE(results.size() == expected_order.size());
    for (std::size_t i = 0; i < results.size(); ++i) {
        CHECK(results[i].key == expected_order[i]);
    }
}

TEST_CASE("all_charts contains exactly Madeira Polyneon and Isacord in declaration order") {
    const auto charts = all_charts();
    REQUIRE(charts.size() == 2);
    CHECK(charts[0].chart_id == "madeira_polyneon");
    CHECK(charts[1].chart_id == "isacord_40");
}

TEST_CASE("find_by_code resolves a known thread in Isacord 40") {
    const auto thread = find_by_code("isacord_40", "1900");
    REQUIRE(thread.has_value());
    CHECK(thread->key.chart_id == "isacord_40");
    CHECK(thread->key.code == "1900");
    CHECK(!thread->name.empty());
}
