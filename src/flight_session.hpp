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

#include "flight_logger.hpp"
#include "html_report.hpp"

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// The state of one recorded flight, from the first roll to the save. No XPLM
// dependency, so the state machine that drives it can be exercised in unit tests.

enum class FlightState : std::uint8_t
{
    Idle,
    Rolling,
    Airborne,
    Landed,
    Shutdown
};

// Everything the state machine and landing detection read from the sim in one frame.
struct FrameInput
{
    float  ground_speed_mps   = 0;
    float  agl_m              = 0;
    float  local_time_sec     = 0;
    float  g_force            = 0;
    bool   on_any_gear        = false;
    bool   on_all_gear        = false;
    int    paused             = 0;
    int    in_replay          = 0;
    float  ias_kts            = 0;
    float  vertical_speed_fpm = 0;
    double latitude           = 0;
    double longitude          = 0;
    double elevation_m        = 0;
    float  roll_deg           = 0;
    float  yaw_rate_deg_s     = 0;
    float  pitch_deg          = 0;
    float  pitch_rate_deg_s   = 0;
    float  flap_handle_ratio  = 0;
    double sim_seconds        = 0; // monotonic sim clock
    time_t wall_time          = 0;
};

struct FlightSession
{
    FlightState state = FlightState::Idle;

    std::string departure_icao;
    double      departure_lat = 0, departure_lon = 0;
    std::string arrival_icao;
    std::string aircraft_icao;
    std::string aircraft_tail;
    bool        is_rotorcraft = false;
    // Resolved when recording starts and kept for the flight: the airframe datarefs the
    // classification reads describe the loaded aircraft, not the one a stored flight flew.
    FlightLogger::ResolvedProfile landing_profile;

    time_t start_time      = 0;
    time_t end_time        = 0;
    int    max_altitude_ft = 0;
    int    max_speed_kts   = 0;

    std::vector<TrackPoint>  track;
    std::vector<LandingData> landings;

    // Flight time excluding sim pause: only frames with sim/time/paused == 0 are counted,
    // so a flight parked in pause for hours no longer inflates the block time.
    double                  active_seconds     = 0.0;
    double                  total_seconds      = 0.0;
    double                  last_sample_active = 0.0;
    std::vector<PauseEvent> pauses;
    // Flight clocks as they stood at the most recent landing, so a session that ends in
    // the air can still be saved up to that landing.
    double active_seconds_at_last_landing = 0.0;
    double total_seconds_at_last_landing  = 0.0;

    bool in_progress() const
    {
        return state == FlightState::Rolling || state == FlightState::Airborne || state == FlightState::Landed;
    }

    void reset() { *this = FlightSession{}; }
};
