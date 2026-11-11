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

#include "flight_state_machine.hpp"

#include <catch2/catch_amalgamated.hpp>

#include <algorithm>

using FlightStateMachine::GroundContact;
using FlightStateMachine::PauseTracker;
using FlightStateMachine::PlacementOutcome;
using FlightStateMachine::Transition;

namespace
{

constexpr float FRAME_SEC = 0.05f; // 20 fps

class FakeEnvironment final : public FlightStateMachine::Environment
{
  public:
    FlightStateMachine::AircraftType aircraft{"C172", false};
    std::string                      tail                            = "HB-ABC";
    std::string                      departure                       = "LSZH";
    std::string                      nearest                         = "LSZH";
    bool                             shutdown                        = false;
    mutable bool                     profile_requested_as_rotorcraft = false;

    FlightStateMachine::AircraftType identify_aircraft() const override { return aircraft; }
    std::string                      aircraft_tail() const override { return tail; }
    FlightLogger::ResolvedProfile    landing_profile(const std::string &, bool rotorcraft) const override
    {
        profile_requested_as_rotorcraft = rotorcraft;
        FlightLogger::ResolvedProfile profile;
        profile.name = rotorcraft ? "rotorcraft" : "light_ga";
        return profile;
    }
    std::string departure_airport() const override { return departure; }
    std::string nearest_airport() const override { return nearest; }
    bool        shutdown_triggered(const std::string &) const override { return shutdown; }

    std::array<int, 4> rating_thresholds(const FlightSession &) const override { return {-125, -250, -350, -600}; }
    void               describe_conditions(LandingData &landing, const FrameInput &) const override
    {
        landing.heading_true = 280.f;
        landing.wind_status  = "calm";
    }
    std::string landing_airport() const override { return "LSZB"; }
    void        apply_runway_fix(LandingData &landing) const override
    {
        ++runway_fixes;
        landing.runway_ident = "28";
    }
    void preload_runways() const override { ++runway_preloads; }

    mutable int runway_fixes    = 0;
    mutable int runway_preloads = 0;
};

FrameInput parked()
{
    FrameInput frame;
    frame.on_any_gear = true;
    frame.on_all_gear = true;
    frame.wall_time   = 1'700'000'000;
    return frame;
}

FrameInput taking_off_roll()
{
    FrameInput frame       = parked();
    frame.ground_speed_mps = 20.f;
    return frame;
}

FrameInput climbing()
{
    FrameInput frame       = parked();
    frame.on_any_gear      = false;
    frame.on_all_gear      = false;
    frame.ground_speed_mps = 40.f;
    frame.agl_m            = 100.f;
    frame.latitude         = 47.46;
    frame.longitude        = 8.55;
    return frame;
}

LandingData landing_at(const std::string &airport, time_t time)
{
    LandingData landing;
    landing.airport_icao = airport;
    landing.time         = time;
    return landing;
}

struct Recorder
{
    FlightSession   session;
    LandingDetector detector;
    GroundContact   contact;
    FakeEnvironment environment;

    Transition step(const FrameInput &frame, float elapsed_sec = FRAME_SEC)
    {
        FlightStateMachine::accumulate_flight_time(session, frame, elapsed_sec);
        FlightStateMachine::track_ground_contact(contact, frame, elapsed_sec);
        return FlightStateMachine::step(session, detector, contact, frame, environment);
    }

    // Holds one frame for a stretch of sim time; returns the first transition it causes.
    Transition hold(const FrameInput &frame, float seconds)
    {
        Transition first = Transition::None;
        for (float t = 0.f; t < seconds; t += FRAME_SEC)
        {
            const Transition transition = step(frame);
            if (first == Transition::None)
                first = transition;
        }
        return first;
    }

    void fly_fixed_wing_to_first_landing()
    {
        REQUIRE(step(taking_off_roll()) == Transition::IdleToRolling);
        REQUIRE(step(climbing()) == Transition::RollingToAirborne);
        FlightStateMachine::record_landing(session, landing_at("LSZB", 1'700'000'900));
    }
};

} // namespace

TEST_CASE("A fixed-wing flight runs Idle, Rolling, Airborne, Landed, Shutdown and back to Idle", "[state_machine]")
{
    Recorder recorder;

    CHECK(recorder.step(parked()) == Transition::None);
    CHECK(recorder.session.state == FlightState::Idle);

    REQUIRE(recorder.step(taking_off_roll()) == Transition::IdleToRolling);
    CHECK(recorder.session.state == FlightState::Rolling);
    CHECK(recorder.session.aircraft_icao == "C172");
    CHECK(recorder.session.aircraft_tail == "HB-ABC");
    CHECK(recorder.session.departure_icao == "LSZH");
    CHECK(recorder.session.landing_profile.name == "light_ga");
    CHECK(recorder.session.start_time == parked().wall_time);

    REQUIRE(recorder.step(climbing()) == Transition::RollingToAirborne);
    CHECK(recorder.session.state == FlightState::Airborne);
    CHECK(recorder.detector.armed);
    CHECK(recorder.session.departure_lat == Catch::Approx(47.46));
    CHECK(recorder.session.departure_lon == Catch::Approx(8.55));

    FlightStateMachine::record_landing(recorder.session, landing_at("LSZB", 1'700'000'900));
    CHECK(recorder.session.state == FlightState::Landed);
    CHECK(recorder.session.arrival_icao == "LSZB");

    FrameInput taxied_in          = parked();
    taxied_in.wall_time           = 1'700'001'200;
    recorder.environment.nearest  = "LSZB";
    recorder.environment.shutdown = true;
    REQUIRE(recorder.step(taxied_in) == Transition::LandedToShutdown);
    CHECK(recorder.session.state == FlightState::Shutdown);
    CHECK(recorder.session.end_time == taxied_in.wall_time);

    REQUIRE(recorder.step(parked()) == Transition::ShutdownToIdle);
    CHECK(recorder.session.state == FlightState::Idle);
    CHECK(recorder.session.landings.empty());
}

TEST_CASE("A fixed-wing recording only starts from a ground roll", "[state_machine]")
{
    Recorder   recorder;
    FrameInput fast_but_airborne  = taking_off_roll();
    fast_but_airborne.on_any_gear = false;

    CHECK(recorder.step(fast_but_airborne) == Transition::None);

    FrameInput rolling_exactly_at_threshold       = parked();
    rolling_exactly_at_threshold.ground_speed_mps = FlightStateMachine::GROUND_SPEED_ROLLING_MPS;
    CHECK(recorder.step(rolling_exactly_at_threshold) == Transition::None);
    CHECK(recorder.session.state == FlightState::Idle);
}

TEST_CASE("A rejected takeoff stays Rolling until the aircraft is repositioned", "[state_machine]")
{
    Recorder recorder;
    REQUIRE(recorder.step(taking_off_roll()) == Transition::IdleToRolling);

    CHECK(recorder.hold(parked(), 60.f) == Transition::None);
    CHECK(recorder.session.state == FlightState::Rolling);
    CHECK(FlightStateMachine::placement_outcome(recorder.session) == PlacementOutcome::Discard);
}

TEST_CASE("The departure falls back to the nearest airport when none was known at the roll", "[state_machine]")
{
    Recorder recorder;
    recorder.environment.departure = "";
    recorder.environment.nearest   = "LSZG";

    REQUIRE(recorder.step(taking_off_roll()) == Transition::IdleToRolling);
    CHECK(recorder.session.departure_icao.empty());
    REQUIRE(recorder.step(climbing()) == Transition::RollingToAirborne);
    CHECK(recorder.session.departure_icao == "LSZG");
}

TEST_CASE("A helicopter starts recording only after settling and a confirmed lift-off", "[state_machine]")
{
    Recorder recorder;
    recorder.environment.aircraft = {"EC35", true};

    FrameInput hovering  = parked();
    hovering.on_any_gear = false;
    hovering.on_all_gear = false;
    hovering.agl_m       = 2.f;

    SECTION("the drop after placement does not count as a flight")
    {
        CHECK(recorder.hold(hovering, 5.f) == Transition::None);
        CHECK(recorder.session.state == FlightState::Idle);
    }

    SECTION("a lift-off after standing on the skids starts the recording")
    {
        recorder.hold(parked(), FlightStateMachine::ROTORCRAFT_GROUND_SETTLE_SEC + 0.1f);
        REQUIRE(recorder.contact.settled_since_placed);

        CHECK(recorder.hold(hovering, 1.f) == Transition::None);
        CHECK(recorder.hold(hovering, 2.f) == Transition::IdleToAirborneRotorcraft);
        CHECK(recorder.session.state == FlightState::Airborne);
        CHECK(recorder.session.is_rotorcraft);
        CHECK(recorder.environment.profile_requested_as_rotorcraft);
        CHECK(recorder.detector.armed);
    }

    SECTION("a new placement requires settling again")
    {
        recorder.hold(parked(), FlightStateMachine::ROTORCRAFT_GROUND_SETTLE_SEC + 0.1f);
        recorder.contact.note_placed();
        CHECK(recorder.hold(hovering, 5.f) == Transition::None);
    }
}

TEST_CASE("A touch-and-go keeps the flight going and stamps the lift-off on the landing", "[state_machine]")
{
    Recorder recorder;
    recorder.fly_fixed_wing_to_first_landing();

    FrameInput rolling_on_runway       = parked();
    rolling_on_runway.ground_speed_mps = 30.f;
    CHECK(recorder.step(rolling_on_runway) == Transition::None);
    CHECK(recorder.session.state == FlightState::Landed);

    FrameInput climb_out = climbing();
    climb_out.wall_time  = 1'700'000'960;
    REQUIRE(recorder.step(climb_out) == Transition::TouchAndGo);
    CHECK(recorder.session.state == FlightState::Airborne);
    CHECK(recorder.session.landings.back().liftoff_time == climb_out.wall_time);
    CHECK(recorder.detector.armed);

    FlightStateMachine::record_landing(recorder.session, landing_at("LSZH", 1'700'001'500));
    CHECK(recorder.session.landings.size() == 2);
    CHECK(recorder.session.arrival_icao == "LSZH");
}

TEST_CASE("A helicopter touch-and-go goes by skid contact, not AGL", "[state_machine]")
{
    Recorder recorder;
    recorder.environment.aircraft = {"EC35", true};
    recorder.hold(parked(), FlightStateMachine::ROTORCRAFT_GROUND_SETTLE_SEC + 0.1f);
    FrameInput hovering  = parked();
    hovering.on_any_gear = false;
    hovering.on_all_gear = false;
    hovering.agl_m       = 2.f;
    REQUIRE(recorder.hold(hovering, 2.5f) == Transition::IdleToAirborneRotorcraft);

    FlightStateMachine::record_landing(recorder.session, landing_at("LSZH", 1'700'000'300));
    recorder.hold(parked(), 1.f);

    CHECK(recorder.hold(hovering, 1.f) == Transition::None);
    CHECK(recorder.hold(hovering, 1.5f) == Transition::TouchAndGo);
}

TEST_CASE("After landing, the arrival follows the nearest airport and taxiing does not end the flight",
          "[state_machine]")
{
    Recorder recorder;
    recorder.fly_fixed_wing_to_first_landing();
    recorder.environment.nearest  = "LSZR";
    recorder.environment.shutdown = true;

    FrameInput taxiing       = parked();
    taxiing.ground_speed_mps = FlightStateMachine::GROUND_SPEED_TAXI_STOP_MPS;
    CHECK(recorder.step(taxiing) == Transition::None);
    CHECK(recorder.session.arrival_icao == "LSZR");

    recorder.environment.nearest  = "";
    recorder.environment.shutdown = false;
    CHECK(recorder.step(parked()) == Transition::None);
    CHECK(recorder.session.arrival_icao == "LSZR");
}

TEST_CASE("A placement decides between saving and discarding the session", "[state_machine]")
{
    Recorder recorder;
    CHECK(FlightStateMachine::placement_outcome(recorder.session) == PlacementOutcome::None);

    recorder.step(taking_off_roll());
    recorder.step(climbing());
    CHECK(FlightStateMachine::placement_outcome(recorder.session) == PlacementOutcome::Discard);

    FlightStateMachine::record_landing(recorder.session, landing_at("LSZB", 1'700'000'900));
    CHECK(FlightStateMachine::placement_outcome(recorder.session) == PlacementOutcome::SaveFlight);

    recorder.step(climbing());
    REQUIRE(recorder.session.state == FlightState::Airborne);
    CHECK(FlightStateMachine::placement_outcome(recorder.session) == PlacementOutcome::SaveUpToLastLanding);
}

TEST_CASE("A landing remembers the flight clocks so a session ending in the air can be cut back", "[state_machine]")
{
    Recorder recorder;
    recorder.step(taking_off_roll());
    recorder.step(climbing());
    recorder.hold(climbing(), 10.f);
    FlightStateMachine::record_landing(recorder.session, landing_at("LSZB", 1'700'000'900));

    CHECK(recorder.session.active_seconds_at_last_landing == Catch::Approx(recorder.session.active_seconds));
    CHECK(recorder.session.total_seconds_at_last_landing == Catch::Approx(recorder.session.total_seconds));
}

TEST_CASE("Flight time runs only during a flight, and active time stops in pause and replay", "[state_machine]")
{
    FlightSession session;
    FrameInput    frame;

    FlightStateMachine::accumulate_flight_time(session, frame, 1.f);
    CHECK(session.total_seconds == 0.0);

    session.state = FlightState::Airborne;
    FlightStateMachine::accumulate_flight_time(session, frame, 1.f);
    frame.paused = 1;
    FlightStateMachine::accumulate_flight_time(session, frame, 1.f);
    frame.paused    = 0;
    frame.in_replay = 1;
    FlightStateMachine::accumulate_flight_time(session, frame, 1.f);

    CHECK(session.active_seconds == Catch::Approx(1.0));
    CHECK(session.total_seconds == Catch::Approx(3.0));
}

TEST_CASE("The frame that starts the roll is not counted as flight time", "[state_machine]")
{
    Recorder recorder;
    recorder.step(taking_off_roll(), 1.f);
    CHECK(recorder.session.state == FlightState::Rolling);
    CHECK(recorder.session.total_seconds == 0.0);

    recorder.step(taking_off_roll(), 1.f);
    CHECK(recorder.session.total_seconds == Catch::Approx(1.0));
}

TEST_CASE("A pause during a flight is recorded with its position and duration", "[state_machine]")
{
    FlightSession session;
    session.state = FlightState::Airborne;
    PauseTracker tracker;

    FrameInput frame = climbing();
    CHECK_FALSE(FlightStateMachine::record_pause_release(tracker, session, frame));

    frame.paused = 1;
    CHECK_FALSE(FlightStateMachine::record_pause_release(tracker, session, frame));
    frame.wall_time += 90;
    CHECK_FALSE(FlightStateMachine::record_pause_release(tracker, session, frame));

    frame.paused = 0;
    CHECK(FlightStateMachine::record_pause_release(tracker, session, frame));
    REQUIRE(session.pauses.size() == 1);
    CHECK(session.pauses[0].sec == 90);
    CHECK(session.pauses[0].lat == Catch::Approx(47.46));
    CHECK(session.pauses[0].lon == Catch::Approx(8.55));
}

TEST_CASE("A pause outside a flight is not recorded", "[state_machine]")
{
    FlightSession session;
    PauseTracker  tracker;
    FrameInput    frame = parked();

    frame.paused = 1;
    FlightStateMachine::record_pause_release(tracker, session, frame);
    frame.paused = 0;
    CHECK_FALSE(FlightStateMachine::record_pause_release(tracker, session, frame));
    CHECK(session.pauses.empty());
}

TEST_CASE("The track is sampled every 10 s of active time and skips implausible speeds", "[state_machine]")
{
    FlightSession session;
    session.state     = FlightState::Airborne;
    FrameInput frame  = climbing();
    frame.elevation_m = 1000.0;
    frame.ias_kts     = 100.f;

    session.active_seconds = 5.0;
    FlightStateMachine::record_track_sample(session, frame);
    CHECK(session.track.empty());

    session.active_seconds = 10.0;
    FlightStateMachine::record_track_sample(session, frame);
    REQUIRE(session.track.size() == 1);
    CHECK(session.track[0].alt_ft == 3280);
    CHECK(session.track[0].spd_kts == 100);
    CHECK(session.max_altitude_ft == 3280);
    CHECK(session.max_speed_kts == 100);

    frame.ias_kts          = 900.f; // a jump no aircraft makes in 10 s
    session.active_seconds = 20.0;
    FlightStateMachine::record_track_sample(session, frame);
    CHECK(session.track.size() == 1);
    CHECK(session.max_speed_kts == 100);
}

namespace
{

// A stabilised fixed-wing approach: 500 fpm down, 65 kts, at 20 fps.
struct Approach
{
    double t     = 1000.0;
    float  agl_m = 30.f;

    FrameInput frame(bool on_any, bool on_all)
    {
        FrameInput f         = climbing();
        f.agl_m              = agl_m;
        f.on_any_gear        = on_any;
        f.on_all_gear        = on_all;
        f.ias_kts            = 65.f;
        f.ground_speed_mps   = 33.f;
        f.vertical_speed_fpm = -500.f;
        f.local_time_sec     = static_cast<float>(t);
        f.sim_seconds        = t;
        f.wall_time          = 1'700'000'000 + static_cast<time_t>(t);
        f.flap_handle_ratio  = 0.5f;
        t += FRAME_SEC;
        return f;
    }

    Transition descend_to_touchdown(Recorder &recorder)
    {
        Transition last = Transition::None;
        while (agl_m > 0.f)
        {
            agl_m = std::max(0.f, agl_m - 2.54f * FRAME_SEC);
            last  = recorder.step(frame(agl_m <= 0.f, false));
        }
        return last;
    }

    Transition rise_and_settle(Recorder &recorder, float peak_m)
    {
        for (agl_m = 0.1f; agl_m < peak_m; agl_m += 0.1f)
            recorder.step(frame(false, false));
        agl_m = 0.f;
        return recorder.step(frame(true, false));
    }

    Transition nose_down(Recorder &recorder)
    {
        agl_m = 0.f;
        return recorder.step(frame(true, true));
    }
};

Recorder airborne_fixed_wing()
{
    Recorder recorder;
    recorder.step(taking_off_roll());
    recorder.step(climbing());
    return recorder;
}

} // namespace

TEST_CASE("A fixed-wing landing completes when the nose gear follows the mains", "[state_machine][landing]")
{
    Recorder recorder = airborne_fixed_wing();
    Approach approach;

    CHECK(approach.descend_to_touchdown(recorder) == Transition::None);
    CHECK(recorder.session.state == FlightState::Airborne);
    CHECK(recorder.detector.captured_valid);

    REQUIRE(approach.nose_down(recorder) == Transition::AirborneToLanded);
    CHECK(recorder.session.state == FlightState::Landed);
    REQUIRE(recorder.session.landings.size() == 1);

    const LandingData &landing = recorder.session.landings.back();
    CHECK(landing.airport_icao == "LSZB");
    CHECK(landing.runway_ident == "28");
    CHECK(landing.heading_true == Catch::Approx(280.f));
    CHECK_FALSE(landing.rating.empty());
    CHECK(landing.bounce_count == 0);
    CHECK(landing.fpm < 0.f);
    CHECK(landing.gate_ias_kts == Catch::Approx(65.f));
    CHECK(landing.gate_flap_ratio == Catch::Approx(0.5f));
    CHECK(landing.float_time > 0.f);
    CHECK(recorder.session.arrival_icao == "LSZB");
    CHECK(recorder.environment.runway_fixes == 1);
    CHECK(recorder.environment.runway_preloads > 0);

    // Re-armed for a touch-and-go.
    CHECK(recorder.detector.armed);
    CHECK_FALSE(recorder.detector.captured_valid);
}

TEST_CASE("A low bounce before the nose gear counts as a bounce of the same landing", "[state_machine][landing]")
{
    Recorder recorder = airborne_fixed_wing();
    Approach approach;
    approach.descend_to_touchdown(recorder);

    CHECK(approach.rise_and_settle(recorder, 1.f) == Transition::None); // ~3 ft
    REQUIRE(approach.nose_down(recorder) == Transition::AirborneToLanded);

    REQUIRE(recorder.session.landings.size() == 1);
    CHECK(recorder.session.landings.back().bounce_count == 1);
}

TEST_CASE("A hop above the bounce limit leaves the first touchdown as the rated one", "[state_machine][landing]")
{
    Recorder recorder = airborne_fixed_wing();
    Approach approach;
    approach.descend_to_touchdown(recorder);
    const float first_fpm = recorder.detector.captured.fpm;

    CHECK(approach.rise_and_settle(recorder, 3.f) == Transition::None); // ~10 ft
    REQUIRE(approach.nose_down(recorder) == Transition::AirborneToLanded);

    REQUIRE(recorder.session.landings.size() == 1);
    CHECK(recorder.session.landings.back().bounce_count == 0);
    CHECK(recorder.session.landings.back().fpm == Catch::Approx(first_fpm));
}

TEST_CASE("No landing is recorded while the aircraft never touches down", "[state_machine][landing]")
{
    Recorder   recorder = airborne_fixed_wing();
    FrameInput cruise   = climbing();
    cruise.agl_m        = 1500.f;
    CHECK(recorder.hold(cruise, 30.f) == Transition::None);
    CHECK(recorder.session.landings.empty());
    CHECK(recorder.environment.runway_preloads == 0);
}

TEST_CASE("A helicopter lands on the rising edge of full skid contact", "[state_machine][landing]")
{
    Recorder recorder;
    recorder.environment.aircraft = {"EC35", true};
    recorder.hold(parked(), FlightStateMachine::ROTORCRAFT_GROUND_SETTLE_SEC + 0.1f);
    FrameInput hovering  = parked();
    hovering.on_any_gear = false;
    hovering.on_all_gear = false;
    hovering.agl_m       = 2.f;
    REQUIRE(recorder.hold(hovering, 2.5f) == Transition::IdleToAirborneRotorcraft);

    FrameInput one_skid  = hovering;
    one_skid.on_any_gear = true;
    one_skid.agl_m       = 0.f;
    CHECK(recorder.step(one_skid) == Transition::None);

    REQUIRE(recorder.step(parked()) == Transition::AirborneToLandedRotorcraft);
    const LandingData &landing = recorder.session.landings.back();
    CHECK(landing.is_rotorcraft);
    CHECK(landing.bounce_count == 0);
    CHECK_FALSE(landing.rating.empty());
    CHECK(landing.airport_icao == "LSZB");
    CHECK(recorder.environment.runway_preloads == 0);
}
