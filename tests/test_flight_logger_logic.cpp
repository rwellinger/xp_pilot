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

#include "../src/geo_longitude.hpp"
#include "flight_logger_logic.hpp"

#include <catch2/catch_amalgamated.hpp>

using FlightLoggerLogic::is_plausible_speed_sample;
using FlightLoggerLogic::MAX_IAS_STEP_PER_SAMPLE;
using FlightLoggerLogic::MAX_PLAUSIBLE_IAS_KTS;

TEST_CASE("first sample is accepted within the absolute bound", "[flight_logger][speed]")
{
    REQUIRE(is_plausible_speed_sample(0, 0, false) == true);
    REQUIRE(is_plausible_speed_sample(140, 0, false) == true);
    REQUIRE(is_plausible_speed_sample(MAX_PLAUSIBLE_IAS_KTS, 0, false) == true);
}

TEST_CASE("first sample beyond the absolute bound is rejected", "[flight_logger][speed]")
{
    REQUIRE(is_plausible_speed_sample(MAX_PLAUSIBLE_IAS_KTS + 1, 0, false) == false);
    REQUIRE(is_plausible_speed_sample(-1, 0, false) == false);
}

TEST_CASE("normal acceleration between two samples is accepted", "[flight_logger][speed]")
{
    REQUIRE(is_plausible_speed_sample(180, 150, true) == true);
    REQUIRE(is_plausible_speed_sample(120, 250, true) == true); // deceleration
    REQUIRE(is_plausible_speed_sample(250 + MAX_IAS_STEP_PER_SAMPLE, 250, true) == true);
}

TEST_CASE("a reposition spike between two samples is rejected", "[flight_logger][speed]")
{
    REQUIRE(is_plausible_speed_sample(250 + MAX_IAS_STEP_PER_SAMPLE + 1, 250, true) == false);
    REQUIRE(is_plausible_speed_sample(4000, 250, true) == false);
    REQUIRE(is_plausible_speed_sample(0, 250, true) == false); // instant drop to zero
}

TEST_CASE("bounds of an empty track are still divisible", "[flight_logger][track]")
{
    const auto bounds = FlightLoggerLogic::track_bounds({});
    REQUIRE(bounds.lat_max > bounds.lat_min);
    REQUIRE(bounds.lon_max > bounds.lon_min);
}

TEST_CASE("a single track point gets padded into a real box", "[flight_logger][track]")
{
    std::vector<TrackPoint> track(1);
    track[0].lat = 46.9;
    track[0].lon = 7.5;

    const auto bounds = FlightLoggerLogic::track_bounds(track);
    REQUIRE(bounds.lat_min == Catch::Approx(46.9 - FlightLoggerLogic::TRACK_BOUNDS_MIN_PADDING_DEG));
    REQUIRE(bounds.lat_max == Catch::Approx(46.9 + FlightLoggerLogic::TRACK_BOUNDS_MIN_PADDING_DEG));
    REQUIRE(bounds.lon_min == Catch::Approx(7.5 - FlightLoggerLogic::TRACK_BOUNDS_MIN_PADDING_DEG));
    REQUIRE(bounds.lon_max == Catch::Approx(7.5 + FlightLoggerLogic::TRACK_BOUNDS_MIN_PADDING_DEG));
}

TEST_CASE("bounds follow the track's own extent", "[flight_logger][track]")
{
    // The map frames the flight itself: a short circuit stays zoomed in, which keeps the
    // track legible even though nearby airspace boundaries then fall outside the frame.
    std::vector<TrackPoint> track(2);
    track[0].lat = 47.4;
    track[0].lon = 8.5;
    track[1].lat = 49.0;
    track[1].lon = 2.5;

    const auto bounds = FlightLoggerLogic::track_bounds(track);
    REQUIRE(bounds.lat_max - bounds.lat_min == Catch::Approx(1.6 + 2 * (1.6 * 0.05 + 0.001)));
}

TEST_CASE("bounds span the extremes of the track with padding", "[flight_logger][track]")
{
    std::vector<TrackPoint> track(3);
    track[0].lat = 46.0;
    track[0].lon = 7.0;
    track[1].lat = 47.0;
    track[1].lon = 6.0;
    track[2].lat = 46.5;
    track[2].lon = 8.0;

    const auto bounds = FlightLoggerLogic::track_bounds(track);
    REQUIRE(bounds.lat_min < 46.0);
    REQUIRE(bounds.lat_max > 47.0);
    REQUIRE(bounds.lon_min < 6.0);
    REQUIRE(bounds.lon_max > 8.0);
    // 1 degree of latitude spread, 5% padding on each side plus the minimum.
    REQUIRE(bounds.lat_max - bounds.lat_min == Catch::Approx(1.0 + 2 * (0.05 + 0.001)));
}

TEST_CASE("projection centres the map and keeps north above south", "[flight_logger][track]")
{
    const FlightLoggerLogic::GeoBounds bounds{46.0, 47.0, 7.0, 8.0};
    const auto     viewport = FlightLoggerLogic::make_viewport(bounds, 200.f, 100.f);
    float          x = 0, y = 0;

    // The centre of the bounds lands in the centre of the box, whatever the letterboxing.
    FlightLoggerLogic::project_to_pixel(viewport, 46.5, 7.5, x, y);
    REQUIRE(x == Catch::Approx(100.f).margin(0.5f));
    REQUIRE(y == Catch::Approx(50.f).margin(0.5f));

    float north_y = 0, south_y = 0, unused = 0;
    FlightLoggerLogic::project_to_pixel(viewport, 47.0, 7.5, unused, north_y);
    FlightLoggerLogic::project_to_pixel(viewport, 46.0, 7.5, unused, south_y);
    REQUIRE(north_y < south_y); // latitude grows upwards, pixel y downwards

    float west_x = 0, east_x = 0;
    FlightLoggerLogic::project_to_pixel(viewport, 46.5, 7.0, west_x, unused);
    FlightLoggerLogic::project_to_pixel(viewport, 46.5, 8.0, east_x, unused);
    REQUIRE(west_x < east_x);
}

TEST_CASE("km_per_pixel converts pixel distance back to ground distance", "[flight_logger][track]")
{
    // At 46 N one degree of longitude is 111.32 * cos(46) = 77.3 km on the ground.
    const FlightLoggerLogic::GeoBounds bounds{45.5, 46.5, 7.0, 8.0};
    const auto   viewport  = FlightLoggerLogic::make_viewport(bounds, 400.f, 400.f);
    const double km_per_px = FlightLoggerLogic::km_per_pixel(bounds, viewport);

    float west_x = 0, east_x = 0, unused = 0;
    FlightLoggerLogic::project_to_pixel(viewport, 46.0, 7.0, west_x, unused);
    FlightLoggerLogic::project_to_pixel(viewport, 46.0, 8.0, east_x, unused);

    const double measured_km = (east_x - west_x) * km_per_px;
    REQUIRE(measured_km == Catch::Approx(77.3).margin(1.0));
}

TEST_CASE("km_per_pixel falls as the map grows", "[flight_logger][track]")
{
    const FlightLoggerLogic::GeoBounds bounds{45.5, 46.5, 7.0, 8.0};

    const double small = FlightLoggerLogic::km_per_pixel(bounds, FlightLoggerLogic::make_viewport(bounds, 400.f, 400.f));
    const double large = FlightLoggerLogic::km_per_pixel(bounds, FlightLoggerLogic::make_viewport(bounds, 800.f, 800.f));

    REQUIRE(small > 0.0);
    REQUIRE(large == Catch::Approx(small / 2.0).epsilon(0.01));
}

// The regression this guards: a linear lat/lon stretch into a fixed-aspect box squashed
// north-south flights, the more so the further from the equator they happened.
TEST_CASE("projection preserves the aspect ratio and letterboxes the remainder", "[flight_logger][track]")
{
    // One degree of latitude covers more Mercator span than one of longitude at 46 N,
    // so a square-in-degrees box is taller than it is wide once projected.
    const FlightLoggerLogic::GeoBounds bounds{46.0, 47.0, 7.0, 8.0};
    const auto viewport = FlightLoggerLogic::make_viewport(bounds, 400.f, 400.f);

    float left_x = 0, right_x = 0, top_y = 0, bottom_y = 0, unused = 0;
    FlightLoggerLogic::project_to_pixel(viewport, 46.5, 7.0, left_x, unused);
    FlightLoggerLogic::project_to_pixel(viewport, 46.5, 8.0, right_x, unused);
    FlightLoggerLogic::project_to_pixel(viewport, 47.0, 7.5, unused, top_y);
    FlightLoggerLogic::project_to_pixel(viewport, 46.0, 7.5, unused, bottom_y);

    const float drawn_w = right_x - left_x;
    const float drawn_h = bottom_y - top_y;
    REQUIRE(drawn_h > drawn_w);                          // no squashing into the square box
    REQUIRE(drawn_h == Catch::Approx(400.f).margin(1.f)); // the taller axis fills the box
    REQUIRE(left_x > 0.f);                               // the narrower one is letterboxed
    REQUIRE(left_x == Catch::Approx(400.f - right_x).margin(0.5f)); // ...symmetrically

    // Same scale on both axes is what "undistorted" means.
    REQUIRE(viewport.scale > 0.0);
}

// ── Pause total ───────────────────────────────────────────────────────────────
// The regression these guard: feeding the total from a whole-second wall clock while
// the active time accumulates continuously made the difference oscillate between 0
// and 1, so a flight that was never paused reported a phantom pause of one second.

TEST_CASE("a flight that was never paused reports no pause", "[flight_logger][pause]")
{
    REQUIRE(FlightLoggerLogic::paused_seconds(0.0, 0.0) == 0);
    REQUIRE(FlightLoggerLogic::paused_seconds(2843.5, 2843.5) == 0);
}

TEST_CASE("floating point noise is not reported as a pause", "[flight_logger][pause]")
{
    REQUIRE(FlightLoggerLogic::paused_seconds(100.0000001, 100.0) == 0);
    REQUIRE(FlightLoggerLogic::paused_seconds(100.49, 100.0) == 0);
}

TEST_CASE("a real pause is reported to the nearest second", "[flight_logger][pause]")
{
    REQUIRE(FlightLoggerLogic::paused_seconds(130.4, 100.2) == 30);
    REQUIRE(FlightLoggerLogic::paused_seconds(1300.0, 100.0) == 1200);
    REQUIRE(FlightLoggerLogic::paused_seconds(100.6, 100.0) == 1);
}

TEST_CASE("active time exceeding total time cannot go negative", "[flight_logger][pause]")
{
    REQUIRE(FlightLoggerLogic::paused_seconds(100.0, 140.0) == 0);
}

// ── Date line ─────────────────────────────────────────────────────────────────
// The regression these guard: longitude wraps at ±180, and a Pacific crossing steps
// from 179.99 to -179.99. Treated as plain numbers that spans the globe — Auckland to
// Los Angeles rendered as a world map with the track jumping between the two edges.
// Long-haul users fly these routes routinely.

TEST_CASE("unwrapped_near expresses longitude next to a reference", "[flight_logger][dateline]")
{
    CHECK(GeoLongitude::unwrapped_near(-179.0, 179.0) == Catch::Approx(181.0));
    CHECK(GeoLongitude::unwrapped_near(179.0, -179.0) == Catch::Approx(-181.0));
    CHECK(GeoLongitude::unwrapped_near(8.5, 7.0) == Catch::Approx(8.5)); // untouched nearby
    CHECK(GeoLongitude::unwrapped_near(-120.0, 210.0) == Catch::Approx(240.0));
}

TEST_CASE("track_bounds keeps a date-line crossing compact", "[flight_logger][dateline]")
{
    std::vector<TrackPoint> track(4); // Auckland heading east past the date line
    track[0].lat = -37.0; track[0].lon = 174.8;
    track[1].lat = -20.0; track[1].lon = 179.9;
    track[2].lat = 0.0;   track[2].lon = -179.9;
    track[3].lat = 21.3;  track[3].lon = -157.9; // Honolulu

    const auto bounds = FlightLoggerLogic::track_bounds(track);

    // Unwrapped the span is about 27 degrees; wrapped it would be the full 360.
    REQUIRE(bounds.lon_max - bounds.lon_min < 40.0);
    REQUIRE(bounds.lon_min > 170.0); // stays east of the date line rather than at -180
}

TEST_CASE("projection places both sides of the date line side by side", "[flight_logger][dateline]")
{
    std::vector<TrackPoint> track(2);
    track[0].lat = 0.0; track[0].lon = 179.0;
    track[1].lat = 0.0; track[1].lon = -179.0; // two degrees further east

    const auto bounds   = FlightLoggerLogic::track_bounds(track);
    const auto viewport = FlightLoggerLogic::make_viewport(bounds, 1000.f, 500.f);

    float west_x = 0, east_x = 0, unused = 0;
    FlightLoggerLogic::project_to_pixel(viewport, 0.0, 179.0, west_x, unused);
    FlightLoggerLogic::project_to_pixel(viewport, 0.0, -179.0, east_x, unused);

    REQUIRE(east_x > west_x);                     // continues east, does not jump back
    REQUIRE(east_x - west_x < 1000.f);            // and not across the whole map
}
