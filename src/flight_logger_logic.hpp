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

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

// Pure sampling and track-geometry logic for the flight logger — no XPLM dependency,
// exposed so unit tests can exercise it without standing up the plugin.

namespace FlightLoggerLogic
{

inline constexpr int MAX_PLAUSIBLE_IAS_KTS  = 1000; // beyond any airliner/fighter in the sim
inline constexpr int MAX_IAS_STEP_PER_SAMPLE = 150; // kts a 10 s sample interval can plausibly gain

// Reject speed samples that a real airframe cannot produce between two 10 s samples.
// A reposition, an aircraft reload or the first frame after leaving a paused/replayed
// state can hand out a single garbage IAS reading, and the running maximum would keep
// it for the rest of the flight.
//
// has_previous is false for the first sample of a flight, where only the absolute
// bound can be checked.
inline bool is_plausible_speed_sample(int speed_kts, int previous_speed_kts, bool has_previous)
{
    if (speed_kts < 0 || speed_kts > MAX_PLAUSIBLE_IAS_KTS)
        return false;
    if (!has_previous)
        return true;
    return std::abs(speed_kts - previous_speed_kts) <= MAX_IAS_STEP_PER_SAMPLE;
}

// Geographic extent of a track, already padded for display. Degenerate tracks (empty,
// or every sample at the same spot) still yield a non-zero span, so callers can divide
// by it without guarding.
struct GeoBounds
{
    double lat_min = 0, lat_max = 0, lon_min = 0, lon_max = 0;
};

// Pause total: whatever the flight clock ran beyond the active (unpaused) time.
// Both counters are fed from the same per-frame delta, so their difference carries no
// quantisation noise and is exactly zero for a flight that was never paused. Feeding
// one of them from a whole-second wall clock instead would make this oscillate
// between 0 and 1 every second.
inline int paused_seconds(double total_seconds, double active_seconds)
{
    const double paused = total_seconds - active_seconds;
    return paused > 0.0 ? static_cast<int>(std::lround(paused)) : 0;
}

inline constexpr double TRACK_BOUNDS_PADDING_FRACTION = 0.05;
inline constexpr double TRACK_BOUNDS_MIN_PADDING_DEG  = 0.001;

inline GeoBounds track_bounds(const std::vector<TrackPoint> &track)
{
    GeoBounds bounds;
    if (!track.empty())
    {
        bounds.lat_min = bounds.lat_max = track.front().lat;
        bounds.lon_min = bounds.lon_max = track.front().lon;
        for (const auto &point : track)
        {
            bounds.lat_min = std::min(bounds.lat_min, point.lat);
            bounds.lat_max = std::max(bounds.lat_max, point.lat);
            bounds.lon_min = std::min(bounds.lon_min, point.lon);
            bounds.lon_max = std::max(bounds.lon_max, point.lon);
        }
    }

    const double lat_padding =
        (bounds.lat_max - bounds.lat_min) * TRACK_BOUNDS_PADDING_FRACTION + TRACK_BOUNDS_MIN_PADDING_DEG;
    const double lon_padding =
        (bounds.lon_max - bounds.lon_min) * TRACK_BOUNDS_PADDING_FRACTION + TRACK_BOUNDS_MIN_PADDING_DEG;
    bounds.lat_min -= lat_padding;
    bounds.lat_max += lat_padding;
    bounds.lon_min -= lon_padding;
    bounds.lon_max += lon_padding;
    return bounds;
}

// Map a position into a width x height box, y growing downwards as pixels do.
inline void project_to_pixel(const GeoBounds &bounds, double lat, double lon, double width, double height,
                             float &out_x, float &out_y)
{
    out_x = static_cast<float>((lon - bounds.lon_min) / (bounds.lon_max - bounds.lon_min) * width);
    out_y = static_cast<float>((1.0 - (lat - bounds.lat_min) / (bounds.lat_max - bounds.lat_min)) * height);
}

} // namespace FlightLoggerLogic
