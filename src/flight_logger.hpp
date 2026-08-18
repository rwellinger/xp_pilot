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
#include "html_report.hpp"
#include <array>
#include <map>
#include <string>
#include <vector>

// Where the landing popup appears on screen. Stored in settings.json by name so the
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

// ── Draw callbacks (call from registered XPLM draw callback) ─────────────────
void draw_overlay();
void draw_popup();
bool popup_active();

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
// User data root (flights, reports, index, settings) under X-Plane's Output dir.
const std::string &output_dir();

// Bundled read-only config shipped next to the plugin binary (<plugin>/data/).
const std::string &config_dir();
bool              &lb_needs_refresh();
void               regen_all_reports();

// ── Settings ──────────────────────────────────────────────────────────────────
void set_write_enabled(bool on);
bool write_enabled();
void set_html_report_enabled(bool on);
bool html_report_enabled();
void set_messages_enabled(bool on);
bool messages_enabled();
void set_landing_popup_enabled(bool on);
bool landing_popup_enabled();
void set_runway_analysis_enabled(bool on);
bool runway_analysis_enabled();
void          set_popup_position(PopupPosition p);
PopupPosition popup_position();

// ── Profile access (for HTML report generation) ───────────────────────────────
std::string        get_profile_name(const std::string &plane_icao);
std::array<int, 4> get_profile_thresholds(const std::string &profile_name);
std::string        get_profile_category(const std::string &profile_name); // "fixed_wing" | "rotorcraft"

} // namespace FlightLogger
