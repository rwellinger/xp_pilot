/*
 * xp_pilot - Flight Logger and Auto QNH plugin for X-Plane 12
 * Copyright (C) 2026 thWelly
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */


#include "../src/settings_profiles.hpp"
#include <catch_amalgamated.hpp>
#include <json.hpp>

using json = nlohmann::json;
using FlightLoggerLogic::ProfileOverride;

namespace
{
json settings_with(const json &aircraft_profiles) { return json{{"aircraft_profiles", aircraft_profiles}}; }
} // namespace

TEST_CASE("A profile name written by earlier versions still loads")
{
    // Compatibility surface: every assignment made before custom thresholds existed is
    // a bare string, and those must keep working untouched.
    const auto assignments = SettingsProfiles::read(settings_with({{"B77W", "heavy_jet"}, {"C208", "turboprop"}}));

    REQUIRE(assignments.size() == 2);
    CHECK_FALSE(assignments.at("B77W").is_custom);
    CHECK(assignments.at("B77W").profile_name == "heavy_jet");
    CHECK(assignments.at("C208").profile_name == "turboprop");
}

TEST_CASE("Custom thresholds load from the object form")
{
    const auto assignments =
        SettingsProfiles::read(settings_with({{"C208", {{"thresholds", {-150, -275, -400, -650}}}}}));

    REQUIRE(assignments.count("C208") == 1);
    CHECK(assignments.at("C208").is_custom);
    CHECK(assignments.at("C208").thresholds == std::array<int, 4>{-150, -275, -400, -650});
}

TEST_CASE("Both forms coexist in one file")
{
    const auto assignments = SettingsProfiles::read(
        settings_with({{"B77W", "heavy_jet"}, {"C208", {{"thresholds", {-150, -275, -400, -650}}}}}));

    REQUIRE(assignments.size() == 2);
    CHECK_FALSE(assignments.at("B77W").is_custom);
    CHECK(assignments.at("C208").is_custom);
}

TEST_CASE("An unusable entry is dropped without costing the others")
{
    // A hand-edited file is the only way to produce these: ascending thresholds, the
    // wrong number of them, a non-numeric value, and a value of the wrong type entirely.
    const auto assignments = SettingsProfiles::read(settings_with({
        {"AAAA", {{"thresholds", {-500, -300, -200, -100}}}},
        {"BBBB", {{"thresholds", {-100, -200, -300}}}},
        {"CCCC", {{"thresholds", {"-100", -200, -300, -500}}}},
        {"DDDD", 42},
        {"EEEE", ""},
        {"C172", "light_ga"},
    }));

    REQUIRE(assignments.size() == 1);
    CHECK(assignments.at("C172").profile_name == "light_ga");
}

TEST_CASE("A settings file with no assignments reads as none")
{
    CHECK(SettingsProfiles::read(json::object()).empty());
    CHECK(SettingsProfiles::read(settings_with(json::array())).empty());
}

TEST_CASE("Assignments round-trip through write and read")
{
    std::map<std::string, ProfileOverride> assignments;
    assignments["B77W"] = ProfileOverride{"heavy_jet", {}, false};
    assignments["C208"] = ProfileOverride{"", {-150, -275, -400, -650}, true};

    const auto reloaded = SettingsProfiles::read(settings_with(SettingsProfiles::write(assignments)));

    REQUIRE(reloaded.size() == 2);
    CHECK(reloaded.at("B77W").profile_name == "heavy_jet");
    CHECK(reloaded.at("C208").is_custom);
    CHECK(reloaded.at("C208").thresholds == assignments["C208"].thresholds);
}

TEST_CASE("A custom assignment is written as an object, a named one as a string")
{
    std::map<std::string, ProfileOverride> assignments;
    assignments["B77W"] = ProfileOverride{"heavy_jet", {}, false};
    assignments["C208"] = ProfileOverride{"", {-150, -275, -400, -650}, true};

    const json written = SettingsProfiles::write(assignments);

    CHECK(written["B77W"] == "heavy_jet");
    CHECK(written["C208"] == json{{"thresholds", {-150, -275, -400, -650}}});
}
