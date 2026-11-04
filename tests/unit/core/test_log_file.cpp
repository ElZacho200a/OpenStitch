// SPDX-License-Identifier: Apache-2.0
// HP-QA-002 : journal fichier a rotation bornee.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "openstitch/core/log.hpp"

namespace fs = std::filesystem;

TEST_CASE("file log is written and tail is readable", "[log]") {
    const fs::path dir = fs::temp_directory_path() / "os_log_test_write";
    fs::remove_all(dir);
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("os_test_write"));
    REQUIRE(openstitch::init_file_logging(dir));
    spdlog::info("ligne un");
    spdlog::info("ligne deux");
    spdlog::default_logger()->flush();
    const auto tail = openstitch::read_log_tail(dir, 1);
    REQUIRE(tail.size() == 1);
    CHECK(tail[0].find("ligne deux") != std::string::npos);
    CHECK(openstitch::read_log_tail(dir, 10).size() == 2);
    spdlog::default_logger()->sinks().clear();
    fs::remove_all(dir);
}

TEST_CASE("file log rotation stays bounded", "[log]") {
    const fs::path dir = fs::temp_directory_path() / "os_log_test_rotate";
    fs::remove_all(dir);
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("os_test_rotate"));
    REQUIRE(openstitch::init_file_logging(dir, 2048, 2));
    for (int i = 0; i < 400; ++i) {
        spdlog::info("remplissage {:0>4} xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", i);
    }
    spdlog::default_logger()->flush();
    int files = 0;
    std::uintmax_t total = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        ++files;
        total += e.file_size();
    }
    CHECK(files <= 2);
    CHECK(total <= 2 * 2048 + 512);
    spdlog::default_logger()->sinks().clear();
    fs::remove_all(dir);
}

TEST_CASE("file log refuses an empty directory", "[log]") {
    CHECK_FALSE(openstitch::init_file_logging({}));
}
