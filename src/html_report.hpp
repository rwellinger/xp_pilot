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
#include <array>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// ── Wind classification ──────────────────────────────────────────────────────

enum class WindCondition : std::uint8_t
{
    Calm,
    Light,
    Steady
};

WindCondition wind_condition_from_string(const std::string &s);
const char   *wind_condition_to_string(WindCondition c);

// ── Data structures shared between flight_logger and logbook_ui ───────────────

struct TrackPoint
{
    time_t t   = 0;
    double lat = 0, lon = 0;
    int    alt_ft = 0, spd_kts = 0, vs_fpm = 0;
};

// One sim pause during a flight, with the position the aircraft was frozen at.
struct PauseEvent
{
    time_t t   = 0; // when the pause started
    int    sec = 0;
    double lat = 0, lon = 0;
};

struct LandingData
{
    float       fpm            = 0;
    float       g_force        = 0;
    float       pitch_deg      = 0;
    float       pitch_rate     = 0;
    float       agl_ft         = 0;
    float       float_time     = 0;
    time_t      time           = 0;
    int         wind_speed_kts = 0;
    int         wind_dir_mag   = 0;
    int         headwind_kts   = 0;
    int         crosswind_kts  = 0;
    int         bounce_count   = 0;
    bool        is_rotorcraft  = false;
    std::string flare;
    std::string rating;
    std::string wind_status;
    std::string crosswind_side;
};

struct FlightData
{
    std::string              filename;  // basename, e.g. "2026-04-01_LSZB_LSGG_DA42.json"
    std::string              date;      // "YYYY-MM-DD"
    std::string              start_utc; // "HH:MM"
    std::string              end_utc;   // "HH:MM"
    std::string              departure_icao;
    std::string              arrival_icao;
    std::string              aircraft_icao;
    std::string              aircraft_tail;
    std::string              aircraft_category = "fixed_wing"; // "fixed_wing" | "rotorcraft"
    time_t                   start_time      = 0;
    time_t                   end_time        = 0;
    int                      block_time_min  = 0; // net of sim pause
    int                      paused_sec      = 0; // 0 for flights logged before v2
    int                      block_time_sec  = 0; // exact active time; falls back to the minute value
    int                      max_altitude_ft = 0;
    int                      max_speed_kts   = 0;
    std::vector<TrackPoint>  track;
    std::vector<LandingData> landings;
    std::vector<PauseEvent>  pauses;
};

// The pauses to display: the recorded ones, or — for flights logged before they were
// tracked individually — the ones reconstructed from the gaps between track points.
std::vector<PauseEvent> resolve_pauses(const FlightData &fd);

// ── HTML report / index generation ───────────────────────────────────────────

namespace HtmlReport
{
// Render one HTML flight report. Returns report filename (basename) or "".
std::string generate(const FlightData &fd, const std::string &data_dir, const std::string &json_filename,
                     const std::string &profile_name, const std::array<int, 4> &profile_thresholds);

// Regenerate index.html from all JSON files in data_dir/flights/.
void generate_index(const std::string &data_dir);
} // namespace HtmlReport

// ── JSON parsing (for logbook and report regeneration) ─────────────────────

FlightData parse_flight_json(const std::string &json, const std::string &filename);
