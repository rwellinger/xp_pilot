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
#include "html_report.hpp"
#include <array>
#include <map>
#include <string>
#include <vector>

// Where the landing popup appears on screen. Stored in the settings file by name so the
// file stays readable and an unknown value degrades to the default.
enum class PopupPosition
{
    TopLeft,
    TopCenter,
    TopRight,
    Center,
    BottomLeft,
    BottomCenter,
    BottomRight
};

inline constexpr PopupPosition POPUP_POSITION_DEFAULT = PopupPosition::TopCenter;

// Order matches the enum; also drives the settings dropdown.
inline const std::array<const char *, 7> &popup_position_keys()
{
    static const std::array<const char *, 7> keys{"top_left",    "top_center",    "top_right",  "center",
                                                  "bottom_left", "bottom_center", "bottom_right"};
    return keys;
}

inline const std::array<const char *, 7> &popup_position_labels()
{
    static const std::array<const char *, 7> labels{"Top left",    "Top center",    "Top right",  "Center",
                                                    "Bottom left", "Bottom center", "Bottom right"};
    return labels;
}

inline const char *popup_position_to_string(PopupPosition p)
{
    const auto  &keys  = popup_position_keys();
    const size_t index = static_cast<size_t>(p);
    return keys[index < keys.size() ? index : static_cast<size_t>(POPUP_POSITION_DEFAULT)];
}

inline PopupPosition popup_position_from_string(const std::string &s)
{
    const auto &keys = popup_position_keys();
    for (size_t i = 0; i < keys.size(); ++i)
        if (s == keys[i])
            return static_cast<PopupPosition>(i);
    return POPUP_POSITION_DEFAULT;
}

namespace FlightLogger
{

// ── Public lifecycle ──────────────────────────────────────────────────────────
void init();
void stop();

// Note that the user's aircraft changed. The airframe data and the landing profile it
// resolves to are logged from the next flight loop, not here: X-Plane blocks while a
// plugin sits in the load message. Needs init() to have run.
void note_aircraft_changed();

// Re-show the most recent landing popup — bound to a command so it can be summoned
// for screenshots. Falls back to the newest logged flight when this X-Plane session
// has no landing yet. Returns false when there is nothing to show.
bool replay_last_landing_popup();

// ── Live flight snapshot ──────────────────────────────────────────────────────
// The flight as recorded so far, for display while it is still running. Returned by
// value: session_reset() clears the underlying vectors the moment a flight ends.
struct LiveFlight
{
    bool       in_progress = false;
    FlightData flight; // everything known so far; end_time is "now"

    // Instantaneous values, read straight from the datarefs on each call.
    double latitude = 0, longitude = 0;
    int    altitude_ft = 0, indicated_airspeed_kts = 0, vertical_speed_fpm = 0;
    float  agl_ft = 0, heading_true = 0;
};

LiveFlight live_flight();

// ── Logbook access ────────────────────────────────────────────────────────────
// User data root (flights, reports, index) under X-Plane's Output dir.
const std::string &output_dir();

// X-Plane's Output/preferences/, where the settings file lives.
const std::string &preferences_dir();

// Bundled read-only config shipped next to the plugin binary (<plugin>/data/).
const std::string &config_dir();
bool              &lb_needs_refresh();
void               regen_all_reports();

// ── Landing profiles ──────────────────────────────────────────────────────────

// Where the profile a landing is rated against came from. Shown on the settings screen,
// because a rating that looks wrong is only explicable together with its source.
enum class ProfileSource
{
    UserOverride,   // assigned by the user, by name or as custom thresholds
    IcaoList,       // named in the bundled flight_logger_profiles.json
    AirframeClass,  // derived from mass, engine count and engine type
    Fallback,       // no usable airframe data — medium_ga stands
};

// The thresholds a landing is actually rated against, and how they were arrived at.
struct ResolvedProfile
{
    std::string        name; // a bundled profile name, or CUSTOM_PROFILE_NAME
    std::array<int, 4> thresholds{};
    ProfileSource      source    = ProfileSource::Fallback;
    bool               is_custom = false;
};

// The name a custom set of thresholds carries in reports and in the flight JSON. Not a
// bundled profile, so it never resolves through the profile table.
inline constexpr const char *CUSTOM_PROFILE_NAME = "custom";

// Landing profiles the user assigned per aircraft, keyed by ICAO code. They take
// precedence over the bundled ICAO list and over the airframe classification, so a type
// either gets wrong can be corrected without touching the shipped config. An entry
// naming a profile that does not exist, or carrying thresholds that do not validate, is
// ignored — the normal lookup stays in charge rather than a landing losing its rating.
void set_profile_overrides(std::map<std::string, FlightLoggerLogic::ProfileOverride> overrides);
const std::map<std::string, FlightLoggerLogic::ProfileOverride> &profile_overrides();

// Assign or remove the override for one aircraft. Neither persists on its own — the
// caller saves the settings, as every other setting on that screen does.
void set_profile_override(const std::string &aircraft_icao, const FlightLoggerLogic::ProfileOverride &override_entry);
void clear_profile_override(const std::string &aircraft_icao);

// The bundled profiles by name, sorted, for the assignment dropdown.
std::vector<std::string> available_profile_names();

// Thresholds of one bundled profile; the medium_ga fallback for a name that does not
// exist.
std::array<int, 4> profile_thresholds(const std::string &profile_name);

// The aircraft loaded in the sim right now, read straight from the datarefs, together
// with the profile it currently resolves to. The settings screen needs this between
// flights, where the session values are empty.
struct CurrentAircraft
{
    std::string     icao;
    std::string     tail;
    bool            is_rotorcraft = false;
    ResolvedProfile profile;
};

CurrentAircraft current_aircraft();

void set_write_enabled(bool on);
bool write_enabled();
void set_html_report_enabled(bool on);
bool html_report_enabled();

} // namespace FlightLogger
