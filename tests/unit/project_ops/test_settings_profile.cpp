// SPDX-License-Identifier: Apache-2.0
// HP-FILE-014 : traduction d'un profil de réglages JSON en arguments de CLI.
#include <catch2/catch_test_macros.hpp>

#include "openstitch/project_ops/settings_profile.hpp"

namespace po = openstitch::project_ops;

namespace {

const std::vector<po::ProfileKey> kKnown{{"max-colors", false},
                                         {"dpi", false},
                                         {"dark-lines", true},
                                         {"hidden-travel-levels", false},
                                         {"skip-background", false}};

} // namespace

TEST_CASE("settings profile: keys translate to sorted CLI arguments", "[settings_profile]") {
    const auto args = po::settings_profile_args(
        R"({"max-colors": 6, "dpi": 150.5, "dark-lines": true, "skip-background": "no",
            "hidden-travel-levels": ["between", "within"]})",
        kKnown, {});
    REQUIRE(args.has_value());
    const std::vector<std::string> expected{"--dark-lines",
                                            "--dpi",
                                            "150.5",
                                            "--hidden-travel-levels",
                                            "between,within",
                                            "--max-colors",
                                            "6",
                                            "--skip-background",
                                            "no"};
    CHECK(*args == expected);
}

TEST_CASE("settings profile: false flag emits nothing, explicit option wins",
          "[settings_profile]") {
    const auto args = po::settings_profile_args(R"({"dark-lines": false, "max-colors": 4})", kKnown,
                                                {"max-colors"});
    REQUIRE(args.has_value());
    CHECK(args->empty());
}

TEST_CASE("settings profile: unknown key is refused with a hint", "[settings_profile]") {
    const auto args = po::settings_profile_args(R"({"max-color": 6})", kKnown, {});
    REQUIRE_FALSE(args.has_value());
    CHECK(args.error().message.find("max-color") != std::string::npos);
    CHECK(args.error().detail.find("max-colors") != std::string::npos);
}

TEST_CASE("settings profile: invalid JSON and wrong types are refused", "[settings_profile]") {
    CHECK_FALSE(po::settings_profile_args("{nope", kKnown, {}).has_value());
    CHECK_FALSE(po::settings_profile_args("[1,2]", kKnown, {}).has_value());
    CHECK_FALSE(po::settings_profile_args(R"({"dark-lines": 1})", kKnown, {}).has_value());
    CHECK_FALSE(po::settings_profile_args(R"({"dpi": {"a": 1}})", kKnown, {}).has_value());
}
