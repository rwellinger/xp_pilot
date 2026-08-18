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

TEST_CASE("projection maps the box corners to pixel corners", "[flight_logger][track]")
{
    FlightLoggerLogic::GeoBounds bounds{46.0, 47.0, 7.0, 8.0};
    float                        x = 0, y = 0;

    // North-west corner is the top-left pixel; latitude grows upwards, y grows downwards.
    FlightLoggerLogic::project_to_pixel(bounds, 47.0, 7.0, 200.0, 100.0, x, y);
    REQUIRE(x == Catch::Approx(0.f));
    REQUIRE(y == Catch::Approx(0.f));

    FlightLoggerLogic::project_to_pixel(bounds, 46.0, 8.0, 200.0, 100.0, x, y);
    REQUIRE(x == Catch::Approx(200.f));
    REQUIRE(y == Catch::Approx(100.f));

    FlightLoggerLogic::project_to_pixel(bounds, 46.5, 7.5, 200.0, 100.0, x, y);
    REQUIRE(x == Catch::Approx(100.f));
    REQUIRE(y == Catch::Approx(50.f));
}
