// SPDX-License-Identifier: Apache-2.0
// Journal d'usage local de Studio : format, écriture, rotation mensuelle, désactivation.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "usage_log.hpp"

using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;
using namespace std::chrono;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int counter = 0;
        path = fs::temp_directory_path() /
               ("openstitch-usage-test-" +
                std::to_string(system_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(++counter));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

system_clock::time_point at(int y, unsigned m, unsigned d, int hh, int mm, int ss, int ms) {
    const auto date = sys_days{year{y} / month{m} / day{d}};
    return date + hours{hh} + minutes{mm} + seconds{ss} + milliseconds{ms};
}

std::vector<std::string> readLines(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

UsageLogOptions options(const fs::path& dir, system_clock::time_point* clock) {
    UsageLogOptions o;
    o.dir = dir;
    o.version = "9.9 test";
    o.now = [clock] { return *clock; };
    return o;
}

} // namespace

TEST_CASE("usage log: a line carries version, session, kind and id", "[desktop_v2][usage]") {
    TempDir tmp;
    auto clock = at(2026, 11, 12, 10, 0, 0, 123);
    UsageLog log(options(tmp.path, &clock));
    const auto line = log.formatLine("tool", "draw.ellipse", {});
    CHECK(line.starts_with(R"({"v":1,"t":"2026-11-12T10:00:00.123","sid":")"));
    CHECK(line.find(R"("app":"studio","ver":"9.9 test")") != std::string::npos);
    CHECK(line.ends_with(R"("k":"tool","id":"draw.ellipse"})"));
    CHECK(log.sessionId().size() == 16);
    CHECK(line.find(log.sessionId()) != std::string::npos);
}

TEST_CASE("usage log: events are appended to the monthly file", "[desktop_v2][usage]") {
    TempDir tmp;
    auto clock = at(2026, 11, 30, 23, 59, 59, 0);
    UsageLog log(options(tmp.path, &clock));
    log.record("click", "gesture_satin");
    log.record("action", "fill.guides", "ok");
    const auto november = tmp.path / "usage-2026-11.jsonl";
    CHECK(log.currentFile() == november);
    const auto lines = readLines(november);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].find(R"("k":"click","id":"gesture_satin"})") != std::string::npos);
    CHECK(lines[1].find(R"("id":"fill.guides","d":"ok"})") != std::string::npos);

    clock = at(2026, 12, 1, 0, 0, 1, 0); // le mois suivant : un autre fichier
    log.record("space", "points");
    CHECK(readLines(tmp.path / "usage-2026-12.jsonl").size() == 1);
    CHECK(readLines(november).size() == 2);
}

TEST_CASE("usage log: a second launch appends and never rewrites", "[desktop_v2][usage]") {
    TempDir tmp;
    auto clock = at(2026, 11, 12, 10, 0, 0, 0);
    {
        UsageLog first(options(tmp.path, &clock));
        first.record("click", "a");
    }
    UsageLog second(options(tmp.path, &clock));
    second.record("click", "b");
    const auto lines = readLines(tmp.path / "usage-2026-11.jsonl");
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].find(R"("id":"a")") != std::string::npos);
    CHECK(lines[1].find(R"("id":"b")") != std::string::npos);
}

TEST_CASE("usage log: disabled or empty events write nothing", "[desktop_v2][usage]") {
    TempDir tmp;
    auto clock = at(2026, 11, 12, 10, 0, 0, 0);
    auto off = options(tmp.path, &clock);
    off.enabled = false;
    UsageLog disabled(off);
    disabled.record("click", "a");
    CHECK_FALSE(fs::exists(tmp.path / "usage-2026-11.jsonl"));

    UsageLog log(options(tmp.path, &clock));
    log.record("", "a");
    log.record("click", "");
    CHECK_FALSE(fs::exists(tmp.path / "usage-2026-11.jsonl"));
}

TEST_CASE("usage log: text is escaped and bounded", "[desktop_v2][usage]") {
    CHECK(usageJsonEscape("a\"b\\c\nd") == R"(a\"b\\c\nd)");
    CHECK(usageJsonEscape(std::string("x\x01y")) == "x\\u0001y");
    CHECK(usageJsonEscape(std::string(500, 'z')).size() == 160);
    TempDir tmp;
    auto clock = at(2026, 11, 12, 10, 0, 0, 0);
    UsageLog log(options(tmp.path, &clock));
    log.record("click", "bad\"id\nwith newline");
    const auto lines = readLines(log.currentFile());
    REQUIRE(lines.size() == 1); // le saut de ligne de l'identifiant n'ouvre pas une 2e ligne
    CHECK(lines[0].find(R"(bad\"id\nwith newline)") != std::string::npos);
}

TEST_CASE("usage log: folder and switches come from the environment", "[desktop_v2][usage]") {
    std::map<std::string, std::string> env;
    const GetEnv get = [&](std::string_view key) {
        const auto it = env.find(std::string(key));
        return it == env.end() ? std::string{} : it->second;
    };
    CHECK(defaultUsageDir(get, "/data") == fs::path("/data") / "OpenStitch" / "usage");
    env["OPENSTITCH_USAGE_DIR"] = "/shared/kpi";
    CHECK(defaultUsageDir(get, "/data") == fs::path("/shared/kpi"));

    TempDir tmp;
    CHECK(usageLogEnabled(get, tmp.path));
    env["OPENSTITCH_USAGE_LOG"] = "0";
    CHECK_FALSE(usageLogEnabled(get, tmp.path));
    env["OPENSTITCH_USAGE_LOG"] = "OFF";
    CHECK_FALSE(usageLogEnabled(get, tmp.path));
    env["OPENSTITCH_USAGE_LOG"] = "1";
    CHECK(usageLogEnabled(get, tmp.path));
    std::ofstream(tmp.path / "DISABLED").put('\n');
    CHECK_FALSE(usageLogEnabled(get, tmp.path));
}

TEST_CASE("usage log: the marker file switches the log off and on", "[desktop_v2][usage]") {
    TempDir tmp;
    const GetEnv none = [](std::string_view) { return std::string{}; };
    CHECK(usageLogEnabled(none, tmp.path));
    CHECK(usageLogSetMarker(tmp.path, false));
    CHECK(fs::exists(tmp.path / "DISABLED"));
    CHECK_FALSE(usageLogEnabled(none, tmp.path));
    CHECK(usageLogSetMarker(tmp.path, true));
    CHECK_FALSE(fs::exists(tmp.path / "DISABLED"));
    CHECK(usageLogEnabled(none, tmp.path));
    CHECK(usageLogSetMarker(tmp.path, true)); // already on: still a success
}

TEST_CASE("usage log: the environment lock is reported separately", "[desktop_v2][usage]") {
    std::map<std::string, std::string> env;
    const GetEnv get = [&](std::string_view key) {
        const auto it = env.find(std::string(key));
        return it == env.end() ? std::string{} : it->second;
    };
    CHECK_FALSE(usageLogDisabledByEnvironment(get));
    env["OPENSTITCH_USAGE_LOG"] = "off";
    CHECK(usageLogDisabledByEnvironment(get));
    env["OPENSTITCH_USAGE_LOG"] = "1";
    CHECK_FALSE(usageLogDisabledByEnvironment(get));
}

TEST_CASE("usage log: setEnabled stops and resumes recording at runtime", "[desktop_v2][usage]") {
    TempDir tmp;
    auto clock = at(2026, 11, 12, 10, 0, 0, 0);
    UsageLog log(options(tmp.path, &clock));
    log.record("click", "before");
    log.setEnabled(false);
    log.record("click", "during");
    log.setEnabled(true);
    log.record("click", "after");
    const auto lines = readLines(log.currentFile());
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].find(R"("id":"before")") != std::string::npos);
    CHECK(lines[1].find(R"("id":"after")") != std::string::npos);
    CHECK(log.dir() == tmp.path);
}
