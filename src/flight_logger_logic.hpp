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

#include "geo_longitude.hpp"
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
        // Longitudes are unwrapped against the first point, so a track crossing the date
        // line keeps growing east instead of snapping back and spanning the globe.
        const double reference = track.front().lon;
        for (const auto &point : track)
        {
            const double lon = GeoLongitude::unwrapped_near(point.lon, reference);
            bounds.lat_min   = std::min(bounds.lat_min, point.lat);
            bounds.lat_max   = std::max(bounds.lat_max, point.lat);
            bounds.lon_min   = std::min(bounds.lon_min, lon);
            bounds.lon_max   = std::max(bounds.lon_max, lon);
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

// ── Map projection ────────────────────────────────────────────────────────────
// Web Mercator, so the track keeps the shape a pilot recognises from a chart. Both
// axes carry the same scale; whatever is left over becomes a letterbox margin. A plain
// linear lat/lon stretch would squash north-south flights the further from the equator
// they happen.

namespace detail
{
constexpr double DEGREES_TO_RADIANS = 3.14159265358979323846 / 180.0;
constexpr double QUARTER_TURN_RAD   = 3.14159265358979323846 / 4.0;
// Mercator diverges at the poles; Web Mercator's usual cutoff keeps the maths finite.
constexpr double MERCATOR_LAT_LIMIT_DEG = 85.05112878;
// Good to ~0.1% — plenty for a scale bar that snaps to round distances anyway.
constexpr double KM_PER_DEGREE_LONGITUDE_AT_EQUATOR = 111.320;

inline double mercator_x(double lon_degrees) { return lon_degrees * DEGREES_TO_RADIANS; }

inline double mercator_y(double lat_degrees)
{
    const double clamped = std::clamp(lat_degrees, -MERCATOR_LAT_LIMIT_DEG, MERCATOR_LAT_LIMIT_DEG);
    return std::log(std::tan(QUARTER_TURN_RAD + clamped * DEGREES_TO_RADIANS / 2.0));
}
} // namespace detail

// A projected map fitted into a width x height box, centred with equal margins.
struct MapViewport
{
    double x_left     = 0; // Mercator x along the left edge of the fitted map
    double y_top      = 0; // Mercator y along its top edge
    double scale      = 1; // pixels per Mercator unit — the same on both axes
    double lon_centre = 0; // reference for unwrapping longitudes onto this map
    float  offset_x   = 0; // letterbox margins centring the map in its box
    float  offset_y   = 0;
};

inline MapViewport make_viewport(const GeoBounds &bounds, float width, float height)
{
    MapViewport viewport;
    viewport.lon_centre = (bounds.lon_min + bounds.lon_max) / 2.0;
    viewport.x_left     = detail::mercator_x(bounds.lon_min);
    viewport.y_top  = detail::mercator_y(bounds.lat_max);

    // Guard against a degenerate span; track_bounds always pads, so this only bites on
    // hand-built bounds.
    const double span_x = std::max(detail::mercator_x(bounds.lon_max) - viewport.x_left, 1e-12);
    const double span_y = std::max(viewport.y_top - detail::mercator_y(bounds.lat_min), 1e-12);

    viewport.scale    = std::min(width / span_x, height / span_y);
    viewport.offset_x = static_cast<float>((width - span_x * viewport.scale) / 2.0);
    viewport.offset_y = static_cast<float>((height - span_y * viewport.scale) / 2.0);
    return viewport;
}

// Ground distance one horizontal pixel covers, measured at the centre latitude —
// what a scale bar needs to label itself.
inline double km_per_pixel(const GeoBounds &bounds, const MapViewport &viewport)
{
    const double px_per_degree = detail::DEGREES_TO_RADIANS * viewport.scale;
    if (px_per_degree <= 0.0)
        return 0.0;
    const double mid_lat_rad = (bounds.lat_min + bounds.lat_max) / 2.0 * detail::DEGREES_TO_RADIANS;
    return detail::KM_PER_DEGREE_LONGITUDE_AT_EQUATOR * std::cos(mid_lat_rad) / px_per_degree;
}

// Drop points that would land on the pixel their predecessor already covers. Natural
// Earth outlines carry far more detail than a 1000 px map can show, and ImGui indexes
// vertices with 16 bits — a wide view once overflowed that limit and drew the map as
// garbage. Thinning removes the cause rather than capping the symptom.
inline constexpr float min_pixel_step = 1.5f;

// Map a position into the viewport, y growing downwards as pixels do.
inline void project_to_pixel(const MapViewport &viewport, double lat, double lon, float &out_x, float &out_y)
{
    // Unwrapping here fixes the overlays too: an American coastline at -120 lands at
    // +240 on a map centred past the date line, instead of vanishing off the left edge.
    const double unwrapped = GeoLongitude::unwrapped_near(lon, viewport.lon_centre);
    out_x = viewport.offset_x + static_cast<float>((detail::mercator_x(unwrapped) - viewport.x_left) * viewport.scale);
    out_y = viewport.offset_y + static_cast<float>((viewport.y_top - detail::mercator_y(lat)) * viewport.scale);
}

} // namespace FlightLoggerLogic
