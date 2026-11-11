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

#include "flight_session.hpp"
#include "landing_detector.hpp"

#include <catch2/catch_amalgamated.hpp>

TEST_CASE("Only Rolling, Airborne and Landed count as a flight in progress", "[flight_session]")
{
    FlightSession session;
    CHECK_FALSE(session.in_progress());
    for (auto state : {FlightState::Rolling, FlightState::Airborne, FlightState::Landed})
    {
        session.state = state;
        CHECK(session.in_progress());
    }
    session.state = FlightState::Shutdown;
    CHECK_FALSE(session.in_progress());
}

TEST_CASE("Resetting a session clears every recorded field", "[flight_session]")
{
    FlightSession session;
    session.state          = FlightState::Landed;
    session.departure_icao = "LSZH";
    session.active_seconds = 1200.0;
    session.landings.push_back(LandingData{});
    session.track.push_back(TrackPoint{});
    session.pauses.push_back(PauseEvent{});

    session.reset();

    CHECK(session.state == FlightState::Idle);
    CHECK(session.departure_icao.empty());
    CHECK(session.active_seconds == 0.0);
    CHECK(session.landings.empty());
    CHECK(session.track.empty());
    CHECK(session.pauses.empty());
}

TEST_CASE("Ring buffer averages only its most recent samples", "[landing_detector]")
{
    RingBuffer buffer{3};
    CHECK(buffer.average() == 0.f);
    CHECK(buffer.time_span() == 0.f);

    for (int i = 1; i <= 5; ++i)
        buffer.push(static_cast<float>(i), static_cast<float>(i) * 0.5f);

    CHECK(buffer.average() == Catch::Approx(4.f)); // 3, 4, 5
    CHECK(buffer.time_span() == Catch::Approx(1.f));
}

TEST_CASE("Arming the detector clears the previous approach and seeds gear contact", "[landing_detector]")
{
    LandingDetector detector;
    detector.captured_valid = true;
    detector.bounce_count   = 2;
    detector.gate_captured  = true;
    detector.agl_buffer.push(10.f, 1.f);

    detector.arm(true, false);

    CHECK(detector.armed);
    CHECK(detector.previous_on_any);
    CHECK_FALSE(detector.previous_on_all);
    CHECK_FALSE(detector.captured_valid);
    CHECK(detector.bounce_count == 0);
    CHECK_FALSE(detector.gate_captured);
    CHECK(detector.agl_buffer.values.empty());
}
