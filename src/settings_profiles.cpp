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


#include "settings_profiles.hpp"

using json = nlohmann::json;

namespace
{

constexpr const char *AIRCRAFT_PROFILES_KEY = "aircraft_profiles";
constexpr const char *THRESHOLDS_KEY        = "thresholds";

bool read_assignment(const json &value, FlightLoggerLogic::ProfileOverride &out)
{
    if (value.is_string())
    {
        out.profile_name = value.get<std::string>();
        out.is_custom    = false;
        return !out.profile_name.empty();
    }
    if (!value.is_object() || !value.contains(THRESHOLDS_KEY) || !value[THRESHOLDS_KEY].is_array() ||
        value[THRESHOLDS_KEY].size() != 4)
        return false;

    const auto &thresholds = value[THRESHOLDS_KEY];
    for (int index = 0; index < 4; ++index)
    {
        if (!thresholds[index].is_number_integer())
            return false;
        out.thresholds[index] = thresholds[index].get<int>();
    }
    out.is_custom = true;
    // Thresholds that do not validate are dropped here rather than at the rating, so a
    // hand-edited file cannot silently leave a landing graded against nonsense.
    return FlightLoggerLogic::are_valid_thresholds(out.thresholds);
}

} // namespace

std::map<std::string, FlightLoggerLogic::ProfileOverride> SettingsProfiles::read(const json &settings)
{
    std::map<std::string, FlightLoggerLogic::ProfileOverride> assignments;
    if (!settings.contains(AIRCRAFT_PROFILES_KEY) || !settings[AIRCRAFT_PROFILES_KEY].is_object())
        return assignments;

    for (const auto &entry : settings[AIRCRAFT_PROFILES_KEY].items())
    {
        FlightLoggerLogic::ProfileOverride parsed;
        if (read_assignment(entry.value(), parsed))
            assignments[entry.key()] = parsed;
    }
    return assignments;
}

json SettingsProfiles::write(const std::map<std::string, FlightLoggerLogic::ProfileOverride> &assignments)
{
    json out = json::object();
    for (const auto &[aircraft_icao, assignment] : assignments)
    {
        if (assignment.is_custom)
            out[aircraft_icao] = json{{THRESHOLDS_KEY, assignment.thresholds}};
        else
            out[aircraft_icao] = assignment.profile_name;
    }
    return out;
}
