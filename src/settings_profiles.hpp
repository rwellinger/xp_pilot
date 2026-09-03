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


#pragma once
#include "flight_logger_logic.hpp"
#include <json.hpp>
#include <map>
#include <string>

// The "aircraft_profiles" entry of the settings file, in both directions. A value is
// either a profile name or an object carrying the user's own thresholds — the same
// string-or-object convention flight_logger_profiles.json uses for its profiles.
namespace SettingsProfiles
{

// Read the assignments, skipping any entry that is neither. The file is hand-editable,
// so one bad entry must not cost the user the rest of their assignments.
std::map<std::string, FlightLoggerLogic::ProfileOverride> read(const nlohmann::json &settings);

// The value to store under "aircraft_profiles".
nlohmann::json write(const std::map<std::string, FlightLoggerLogic::ProfileOverride> &assignments);

} // namespace SettingsProfiles
