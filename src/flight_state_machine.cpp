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
#include "flight_logger_logic.hpp"

#include <cmath>
#include <utility>

namespace FlightStateMachine
{

namespace
{

constexpr double METERS_TO_FEET = 3.28084;

void record_liftoff_point(FlightSession &session, const FrameInput &frame)
{
    session.departure_lat = frame.latitude;
    session.departure_lon = frame.longitude;
}

void begin_session(FlightSession &session, const AircraftType &aircraft, const FrameInput &frame,
                   const Environment &environment)
{
    session.is_rotorcraft   = aircraft.is_rotorcraft;
    session.aircraft_icao   = aircraft.icao;
    session.landing_profile = environment.landing_profile(aircraft.icao, aircraft.is_rotorcraft);
    session.aircraft_tail   = environment.aircraft_tail();
    session.departure_icao  = environment.departure_airport();
    session.start_time      = frame.wall_time;
}

void become_airborne(FlightSession &session, LandingDetector &detector, const FrameInput &frame)
{
    session.state = FlightState::Airborne;
    detector.arm(frame.on_any_gear, frame.on_all_gear);
}

Transition step_idle(FlightSession &session, LandingDetector &detector, const GroundContact &contact,
                     const FrameInput &frame, const Environment &environment)
{
    const AircraftType aircraft = environment.identify_aircraft();

    if (aircraft.is_rotorcraft)
    {
        // Helicopters skip the Rolling phase — flight starts once the skids have left the ground.
        if (!rotorcraft_lifted_off(contact) || !contact.settled_since_placed)
            return Transition::None;

        begin_session(session, aircraft, frame, environment);
        record_liftoff_point(session, frame);
        become_airborne(session, detector, frame);
        return Transition::IdleToAirborneRotorcraft;
    }

    if (frame.ground_speed_mps <= GROUND_SPEED_ROLLING_MPS || !frame.on_any_gear)
        return Transition::None;

    begin_session(session, aircraft, frame, environment);
    session.state = FlightState::Rolling;
    return Transition::IdleToRolling;
}

Transition step_rolling(FlightSession &session, LandingDetector &detector, const FrameInput &frame,
                        const Environment &environment)
{
    if (frame.agl_m <= AGL_AIRBORNE_M)
        return Transition::None;

    session.last_sample_active = session.active_seconds;
    if (session.departure_icao.empty())
        session.departure_icao = environment.nearest_airport();
    record_liftoff_point(session, frame);
    become_airborne(session, detector, frame);
    return Transition::RollingToAirborne;
}

// Ends the approach with the captured touchdown and arms the detector for the next one.
void complete_landing(FlightSession &session, LandingDetector &detector, const FrameInput &frame,
                      const Environment &environment)
{
    LandingData &landing = detector.captured;
    landing.time         = frame.wall_time;
    landing.airport_icao = environment.landing_airport();
    environment.apply_runway_fix(landing);
    record_landing(session, landing);
    detector.arm(frame.on_any_gear, frame.on_all_gear);
}

void describe_touchdown(LandingData &landing, const LandingDetector &detector, const FrameInput &frame,
                        const Environment &environment)
{
    LandingDetection::fill_touchdown_kinematics(landing, detector, frame);
    environment.describe_conditions(landing, frame);
}

Transition step_rotorcraft_approach(FlightSession &session, LandingDetector &detector, const FrameInput &frame,
                                    const Environment &environment)
{
    if (!LandingDetection::skids_touched_down(detector, frame))
    {
        LandingDetection::remember_gear_contact(detector, frame);
        return Transition::None;
    }

    LandingData &landing = detector.captured;
    describe_touchdown(landing, detector, frame, environment);
    landing.is_rotorcraft   = true;
    landing.rating          = LandingDetection::rotorcraft_rating(landing, environment.rating_thresholds(session));
    landing.bounce_count    = 0;
    detector.captured_valid = true;
    complete_landing(session, detector, frame, environment);
    return Transition::AirborneToLandedRotorcraft;
}

Transition step_fixed_wing_approach(FlightSession &session, LandingDetector &detector, const FrameInput &frame,
                                    const Environment &environment)
{
    LandingDetection::track_fifty_foot_gate(detector, frame,
                                            LandingDetection::smoothed_vertical_speed_fpm(detector, frame));

    const auto touchdown = LandingDetection::classify_main_gear_touchdown(detector, frame);
    if (touchdown != LandingDetection::TouchdownKind::None)
    {
        LandingData candidate;
        if (touchdown != LandingDetection::TouchdownKind::Hop)
            describe_touchdown(candidate, detector, frame, environment);
        LandingDetection::record_main_gear_touchdown(detector, touchdown, std::move(candidate), frame);
    }
    LandingDetection::track_main_gear_lift(detector, frame);

    if (!LandingDetection::nose_gear_settled(detector, frame))
    {
        LandingDetection::remember_gear_contact(detector, frame);
        return Transition::None;
    }

    LandingData &landing = detector.captured;
    landing.rating       = LandingDetection::fixed_wing_rating(landing, environment.rating_thresholds(session));
    landing.bounce_count = detector.bounce_count;
    complete_landing(session, detector, frame, environment);
    return Transition::AirborneToLanded;
}

Transition step_airborne(FlightSession &session, LandingDetector &detector, const FrameInput &frame,
                         const Environment &environment)
{
    LandingDetection::observe_frame(detector, frame);

    // ~2000 ft AGL leaves roughly a minute for the runways to load before touchdown.
    constexpr float APPROACH_PRELOAD_AGL_M = 610.f;
    if (session.is_rotorcraft)
        return step_rotorcraft_approach(session, detector, frame, environment);
    if (frame.agl_m < APPROACH_PRELOAD_AGL_M)
        environment.preload_runways();
    return step_fixed_wing_approach(session, detector, frame, environment);
}

bool lifted_off_again(const FlightSession &session, const GroundContact &contact, const FrameInput &frame)
{
    return session.is_rotorcraft ? rotorcraft_lifted_off(contact) : frame.agl_m > AGL_AIRBORNE_M;
}

Transition step_landed(FlightSession &session, LandingDetector &detector, const GroundContact &contact,
                       const FrameInput &frame, const Environment &environment)
{
    if (lifted_off_again(session, contact, frame))
    {
        if (!session.landings.empty())
            session.landings.back().liftoff_time = frame.wall_time;
        become_airborne(session, detector, frame);
        return Transition::TouchAndGo;
    }

    const std::string airport = environment.nearest_airport();
    if (!airport.empty())
        session.arrival_icao = airport;

    if (frame.ground_speed_mps >= GROUND_SPEED_TAXI_STOP_MPS || !environment.shutdown_triggered(session.aircraft_icao))
        return Transition::None;

    session.end_time = frame.wall_time;
    session.state    = FlightState::Shutdown;
    return Transition::LandedToShutdown;
}

} // namespace

void track_ground_contact(GroundContact &contact, const FrameInput &frame, float elapsed_sec)
{
    const bool sim_running    = frame.paused == 0 && frame.in_replay == 0;
    contact.seconds_on_ground = FlightLoggerLogic::continuous_active_seconds(
        contact.seconds_on_ground, frame.on_any_gear, sim_running, elapsed_sec);
    contact.seconds_off_ground = FlightLoggerLogic::continuous_active_seconds(
        contact.seconds_off_ground, !frame.on_any_gear, sim_running, elapsed_sec);
    if (contact.seconds_on_ground >= ROTORCRAFT_GROUND_SETTLE_SEC)
        contact.settled_since_placed = true;
}

bool rotorcraft_lifted_off(const GroundContact &contact)
{
    return contact.seconds_off_ground >= FlightLoggerLogic::ROTORCRAFT_LIFTOFF_CONFIRM_SEC;
}

const char *transition_label(Transition transition)
{
    switch (transition)
    {
    case Transition::IdleToRolling:
        return "Idle -> Rolling";
    case Transition::IdleToAirborneRotorcraft:
        return "Idle -> Airborne (rotorcraft, skipped Rolling)";
    case Transition::RollingToAirborne:
        return "Rolling -> Airborne";
    case Transition::AirborneToLanded:
        return "Airborne -> Landed";
    case Transition::AirborneToLandedRotorcraft:
        return "Airborne -> Landed (rotorcraft)";
    case Transition::TouchAndGo:
        return "Landed -> Airborne (T&G)";
    case Transition::LandedToShutdown:
        return "Landed -> Shutdown";
    case Transition::ShutdownToIdle:
        return "Shutdown -> Idle";
    case Transition::None:
        break;
    }
    return "";
}

Transition step(FlightSession &session, LandingDetector &detector, const GroundContact &contact,
                const FrameInput &frame, const Environment &environment)
{
    switch (session.state)
    {
    case FlightState::Idle:
        return step_idle(session, detector, contact, frame, environment);
    case FlightState::Rolling:
        return step_rolling(session, detector, frame, environment);
    case FlightState::Airborne:
        return step_airborne(session, detector, frame, environment);
    case FlightState::Landed:
        return step_landed(session, detector, contact, frame, environment);
    case FlightState::Shutdown:
        session.reset();
        return Transition::ShutdownToIdle;
    }
    return Transition::None;
}

void record_landing(FlightSession &session, const LandingData &landing)
{
    session.landings.push_back(landing);
    session.arrival_icao                   = landing.airport_icao;
    session.active_seconds_at_last_landing = session.active_seconds;
    session.total_seconds_at_last_landing  = session.total_seconds;
    session.state                          = FlightState::Landed;
}

// Flight-loop callbacks keep firing while the sim is paused or in replay, so the active
// accumulator is gated on those datarefs instead of trusting the wall clock. Freezing it
// also freezes the track sampler, which keeps replayed frames out of the track and out
// of the max altitude/speed statistics.
//
// The total counter runs unconditionally over the same frames, so that the pause total
// — their difference — stays free of the quantisation noise a whole-second wall clock
// would introduce.
void accumulate_flight_time(FlightSession &session, const FrameInput &frame, float elapsed_sec)
{
    if (!session.in_progress())
        return;

    session.total_seconds += elapsed_sec;
    if (frame.paused == 0 && frame.in_replay == 0)
        session.active_seconds += elapsed_sec;
}

bool record_pause_release(PauseTracker &tracker, FlightSession &session, const FrameInput &frame)
{
    bool released = false;
    if (!session.in_progress())
        tracker.pending = PauseEvent{};
    else
    {
        if (frame.paused != 0 && tracker.previous_paused == 0)
        {
            tracker.pending     = PauseEvent{};
            tracker.pending.t   = frame.wall_time;
            tracker.pending.lat = frame.latitude;
            tracker.pending.lon = frame.longitude;
        }
        else if (frame.paused == 0 && tracker.previous_paused != 0 && tracker.pending.t != 0)
        {
            tracker.pending.sec = static_cast<int>(frame.wall_time - tracker.pending.t);
            session.pauses.push_back(tracker.pending);
            tracker.pending = PauseEvent{};
            released        = true;
        }
    }
    tracker.previous_paused = frame.paused;
    return released;
}

bool close_open_pause(PauseTracker &tracker, FlightSession &session, time_t end_time)
{
    if (tracker.pending.t == 0)
        return false;
    tracker.pending.sec = static_cast<int>(end_time - tracker.pending.t);
    session.pauses.push_back(tracker.pending);
    tracker.pending = PauseEvent{};
    return true;
}

// Pausing the sim pauses the sampler, which keeps the report's altitude/speed charts —
// they derive their time axis from the sample index — on a true 10 s grid.
void record_track_sample(FlightSession &session, const FrameInput &frame)
{
    if (session.active_seconds - session.last_sample_active < TRACK_SAMPLE_INTERVAL_SEC)
        return;

    session.last_sample_active = session.active_seconds;
    const int altitude_ft      = static_cast<int>(frame.elevation_m * METERS_TO_FEET);
    const int speed_kts        = static_cast<int>(std::lround(frame.ias_kts));

    // Dropping an implausible sample leaves a 10 s hole in the chart's time axis. That
    // is the cheaper error: a garbage reading would otherwise stick in the maximum for
    // the rest of the flight.
    const bool has_previous = !session.track.empty();
    if (!FlightLoggerLogic::is_plausible_speed_sample(speed_kts, has_previous ? session.track.back().spd_kts : 0,
                                                      has_previous))
        return;

    TrackPoint point;
    point.t       = frame.wall_time;
    point.lat     = frame.latitude;
    point.lon     = frame.longitude;
    point.alt_ft  = altitude_ft;
    point.spd_kts = speed_kts;
    point.vs_fpm  = static_cast<int>(frame.vertical_speed_fpm);
    session.track.push_back(point);
    if (altitude_ft > session.max_altitude_ft)
        session.max_altitude_ft = altitude_ft;
    if (speed_kts > session.max_speed_kts)
        session.max_speed_kts = speed_kts;
}

PlacementOutcome placement_outcome(const FlightSession &session)
{
    if (!session.in_progress())
        return PlacementOutcome::None;
    if (session.state == FlightState::Landed)
        return PlacementOutcome::SaveFlight;
    if (!session.landings.empty())
        return PlacementOutcome::SaveUpToLastLanding;
    return PlacementOutcome::Discard;
}

} // namespace FlightStateMachine
