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

#include "settings.hpp"
#include "airport_lookup.hpp"
#include "auto_qnh.hpp"
#include "flight_logger.hpp"
#include "settings_migration.hpp"
#include "ui_landing_popup.hpp"
#include "ui_overlay.hpp"
#include "ui_theme.hpp"
#include <XPLM/XPLMUtilities.h>
#include <fstream>
#include <map>
#include <json.hpp>
#include <string>

using json = nlohmann::json;

namespace
{

// One JSON key bound to the accessor pair of the module that owns the setting.
// load() and save() both walk these tables, so the two directions cannot drift
// apart and a new setting is one line rather than two.
template <typename T> struct Binding
{
    const char *key;
    T (*get)();
    void (*set)(T);
    T default_value;
};

constexpr Binding<bool> BOOLEANS[] = {
    {"auto_qnh", AutoQNH::enabled, AutoQNH::set_enabled, false},
    {"qnh_messages", AutoQNH::messages_enabled, AutoQNH::set_messages_enabled, true},
    {"write_logs", FlightLogger::write_enabled, FlightLogger::set_write_enabled, true},
    {"html_report", FlightLogger::html_report_enabled, FlightLogger::set_html_report_enabled, true},
    {"log_messages", Overlay::enabled, Overlay::set_enabled, true},
    {"landing_popup", LandingPopup::enabled, LandingPopup::set_enabled, true},
    {"runway_analysis", AirportLookup::analysis_enabled, AirportLookup::set_analysis_enabled, true},
};

constexpr Binding<int> INTEGERS[] = {
    {"qnh_transition_altitude_ft", AutoQNH::transition_altitude_ft, AutoQNH::set_transition_altitude_ft, 18000},
};

constexpr Binding<float> FLOATS[] = {
    {"ui_scale", Theme::ui_scale, Theme::set_ui_scale, 1.0f},
};

// The popup position is stored by name so the file stays readable and an unknown
// value degrades to the default. Its own table entry would need a third accessor
// shape for one setting, so it is spelled out instead.
constexpr const char *POPUP_POSITION_KEY = "popup_position";

// Aircraft -> profile, an object rather than a scalar, so it needs its own branch in
// both directions just as the popup position does.
constexpr const char *AIRCRAFT_PROFILES_KEY = "aircraft_profiles";

std::map<std::string, std::string> read_profile_overrides(const json &j)
{
    std::map<std::string, std::string> overrides;
    if (!j.contains(AIRCRAFT_PROFILES_KEY) || !j[AIRCRAFT_PROFILES_KEY].is_object())
        return overrides;
    // Hand-edited file: skip anything that is not a name, rather than throwing the
    // whole settings file away over one bad entry.
    for (const auto &entry : j[AIRCRAFT_PROFILES_KEY].items())
        if (entry.value().is_string())
            overrides[entry.key()] = entry.value().get<std::string>();
    return overrides;
}

constexpr const char *SETTINGS_FILENAME = "xp_pilot.prf";

std::string settings_path() { return FlightLogger::preferences_dir() + SETTINGS_FILENAME; }

// Where the settings lived before they moved to Output/preferences/.
std::string legacy_settings_path() { return FlightLogger::output_dir() + "settings.json"; }

void migrate_from_legacy_location()
{
    switch (Settings::migrate_settings_file(legacy_settings_path(), settings_path()))
    {
    case Settings::MigrationOutcome::NothingToMigrate:
        break;
    case Settings::MigrationOutcome::Migrated:
        XPLMDebugString(("[xp_pilot] settings moved to " + settings_path() + "\n").c_str());
        break;
    case Settings::MigrationOutcome::OldFileMalformed:
        XPLMDebugString(("[xp_pilot] WARNING: cannot read " + legacy_settings_path() +
                         " - starting from defaults. The old file was left in place so you can recover it by hand.\n")
                            .c_str());
        break;
    case Settings::MigrationOutcome::MoveFailed:
        XPLMDebugString(("[xp_pilot] WARNING: cannot write " + settings_path() + " - settings left at " +
                         legacy_settings_path() + " and not loaded.\n")
                            .c_str());
        break;
    }
}

} // namespace

void Settings::load()
{
    migrate_from_legacy_location();

    std::ifstream f(settings_path());
    if (!f.is_open())
        return;
    try
    {
        json j;
        f >> j;
        for (const auto &binding : BOOLEANS)
            binding.set(j.value(binding.key, binding.default_value));
        for (const auto &binding : INTEGERS)
            binding.set(j.value(binding.key, binding.default_value));
        for (const auto &binding : FLOATS)
            binding.set(j.value(binding.key, binding.default_value));

        LandingPopup::set_position(popup_position_from_string(
            j.value(POPUP_POSITION_KEY, popup_position_to_string(POPUP_POSITION_DEFAULT))));

        FlightLogger::set_profile_overrides(read_profile_overrides(j));
    }
    catch (...)
    {
        XPLMDebugString(("[xp_pilot] Failed to parse " + settings_path() + "\n").c_str());
    }
}

void Settings::save()
{
    json j;
    for (const auto &binding : BOOLEANS)
        j[binding.key] = binding.get();
    for (const auto &binding : INTEGERS)
        j[binding.key] = binding.get();
    for (const auto &binding : FLOATS)
        j[binding.key] = binding.get();
    j[POPUP_POSITION_KEY] = popup_position_to_string(LandingPopup::position());

    json overrides = json::object();
    for (const auto &[aircraft_icao, profile_name] : FlightLogger::profile_overrides())
        overrides[aircraft_icao] = profile_name;
    j[AIRCRAFT_PROFILES_KEY] = overrides;

    std::ofstream f(settings_path());
    if (f.is_open())
        f << j.dump(2);
}
