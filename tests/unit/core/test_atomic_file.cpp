// SPDX-License-Identifier: Apache-2.0
// HP-QA-021 : primitive d'ecriture atomique et durable.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include "openstitch/core/atomic_file.hpp"

namespace fs = std::filesystem;
using namespace openstitch;

namespace {

fs::path fresh_dir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("os_atomic_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int count_tmp(const fs::path& dir) {
    int n = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() == ".tmp") {
            ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("write_file_atomic cree puis remplace la cible") {
    const auto dir = fresh_dir("basic");
    const auto target = dir / "out.bin";
    REQUIRE(write_file_atomic(target, std::string_view("premier")).has_value());
    CHECK(slurp(target) == "premier");
    REQUIRE(write_file_atomic(target, std::string_view("second contenu")).has_value());
    CHECK(slurp(target) == "second contenu");
    CHECK(count_tmp(dir) == 0);
}

TEST_CASE("write_file_atomic echec force: dossier absent ou cible dossier") {
    const auto dir = fresh_dir("fail");
    const auto missing = dir / "absent" / "out.bin";
    auto r = write_file_atomic(missing, std::string_view("x"));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == "E_WRITE_FAILED");
    CHECK_FALSE(r.error().hint.empty());

    const auto asDir = dir / "cible";
    fs::create_directories(asDir);
    CHECK_FALSE(write_file_atomic(asDir, std::string_view("x")).has_value());
    CHECK(fs::is_directory(asDir));
    CHECK(count_tmp(dir) == 0);
}

TEST_CASE("write_file_atomic_with: un echec de l'ecrivain laisse l'ancien fichier intact") {
    const auto dir = fresh_dir("with");
    const auto target = dir / "out.bin";
    REQUIRE(write_file_atomic(target, std::string_view("ancien")).has_value());

    auto r = write_file_atomic_with(target, [](const fs::path& tmp) -> Result<void> {
        std::ofstream(tmp, std::ios::binary) << "partiel";
        return fail(ErrorCategory::Internal, "echec force");
    });
    REQUIRE_FALSE(r.has_value());
    CHECK(slurp(target) == "ancien");
    CHECK(count_tmp(dir) == 0);

    r = write_file_atomic_with(target, [](const fs::path& tmp) -> Result<void> {
        std::ofstream(tmp, std::ios::binary) << "nouveau";
        return {};
    });
    REQUIRE(r.has_value());
    CHECK(slurp(target) == "nouveau");
    CHECK(count_tmp(dir) == 0);
}

TEST_CASE("write_file_atomic cible verrouillee: l'ancien fichier reste intact en cas d'echec") {
    const auto dir = fresh_dir("locked");
    const auto target = dir / "out.bin";
    REQUIRE(write_file_atomic(target, std::string_view("ancien")).has_value());
    {
        // Un lecteur ouvert peut (Windows) bloquer le remplacement ; s'il echoue, rien n'est casse.
        std::ifstream holder(target, std::ios::binary);
        REQUIRE(holder.is_open());
        auto r = write_file_atomic(target, std::string_view("nouveau"));
        if (!r.has_value()) {
            holder.close();
            CHECK(slurp(target) == "ancien");
        }
    }
    CHECK(count_tmp(dir) == 0);
}

TEST_CASE("write_file_atomic deux ecritures simultanees ne corrompent pas la cible") {
    const auto dir = fresh_dir("concurrent");
    const auto target = dir / "out.bin";
    const std::string a(300000, 'A');
    const std::string b(250000, 'B');
    std::atomic<int> okCount{0};
    std::atomic<bool> corrupted{false};
    auto writer = [&](const std::string& content) {
        for (int i = 0; i < 25; ++i) {
            if (write_file_atomic(target, std::string_view(content)).has_value()) {
                ++okCount;
            }
        }
    };
    std::thread t1(writer, std::cref(a));
    std::thread t2(writer, std::cref(b));
    std::thread reader([&] {
        for (int i = 0; i < 60; ++i) {
            std::error_code ec;
            if (fs::exists(target, ec)) {
                const auto s = slurp(target);
                if (!s.empty() && s != a && s != b) {
                    corrupted = true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    t1.join();
    t2.join();
    reader.join();
    CHECK_FALSE(corrupted.load());
    CHECK(okCount.load() >= 2);
    const auto last = slurp(target);
    CHECK((last == a || last == b));
    CHECK(count_tmp(dir) == 0);
}
