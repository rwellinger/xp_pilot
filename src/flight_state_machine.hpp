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
#include "flight_session.hpp"
#include "html_report.hpp"
#include "landing_detector.hpp"

#include <array>
#include <cstdint>
#include <string>

// The flight recording state machine: when a flight starts, takes off, touches and goes,
// and ends. No XPLM dependency — the sim reaches it only through FrameInput and the
// Environment, so every transition can be driven by synthetic frames in unit tests.
// Side effects beyond the session (logging, overlays, saving) are the caller's, keyed
// on the Transition a step returns.

namespace FlightStateMachine
{

inline constexpr float GROUND_SPEED_ROLLING_MPS   = 15.4f; // 30 kts
inline constexpr float GROUND_SPEED_TAXI_STOP_MPS = 2.6f;  // 5 kts
inline constexpr float AGL_AIRBORNE_M             = 15.0f; // ~50 ft (fixed-wing climb-out)

// X-Plane places a freshly loaded aircraft slightly above the ground and lets it drop.
// For a helicopter, which starts recording the moment it is off the ground, that drop
// would be a flight with a hard landing — so lift-off only counts once it has stood
// on the ground this long since it was placed.
inline constexpr float ROTORCRAFT_GROUND_SETTLE_SEC = 3.0f;

inline constexpr double TRACK_SAMPLE_INTERVAL_SEC = 10.0;

// Continuous ground and air time in active sim time. Outlives a session: it describes the
// aircraft since it was placed, not the flight being recorded.
struct GroundContact
{
    float seconds_on_ground    = 0.f;
    float seconds_off_ground   = 0.f;
    bool  settled_since_placed = false;

    void note_placed() { *this = GroundContact{}; }
};

void track_ground_contact(GroundContact &contact, const FrameInput &frame, float elapsed_sec);

// AGL alone cannot tell a hovering helicopter from one standing on an elevated pad, so
// rotorcraft go by skid contact instead.
bool rotorcraft_lifted_off(const GroundContact &contact);

struct AircraftType
{
    std::string icao;
    bool        is_rotorcraft = false;
};

// What a transition needs to know beyond the frame. Queried only on the frames that need
// it, so lookups the sim makes expensive stay off the per-frame path.
class Environment
{
  public:
    virtual ~Environment() = default;

    virtual AircraftType                  identify_aircraft() const                                       = 0;
    virtual std::string                   aircraft_tail() const                                           = 0;
    virtual FlightLogger::ResolvedProfile landing_profile(const std::string &icao, bool rotorcraft) const = 0;
    // The airport cached while the aircraft stood still, else the nearest one.
    virtual std::string departure_airport() const                         = 0;
    virtual std::string nearest_airport() const                           = 0;
    virtual bool        shutdown_triggered(const std::string &icao) const = 0;

    // The thresholds the session's landings are rated against.
    virtual std::array<int, 4> rating_thresholds(const FlightSession &session) const = 0;
    // Heading, wind, configuration and weather at the moment of touchdown.
    virtual void        describe_conditions(LandingData &landing, const FrameInput &frame) const = 0;
    virtual std::string landing_airport() const                                                  = 0;
    virtual void        apply_runway_fix(LandingData &landing) const                             = 0;
    // Fetches the destination's runways ahead of the touchdown frame.
    virtual void preload_runways() const = 0;
};

enum class Transition : std::uint8_t
{
    None,
    IdleToRolling,
    IdleToAirborneRotorcraft,
    RollingToAirborne,
    AirborneToLanded,
    AirborneToLandedRotorcraft,
    TouchAndGo,
    LandedToShutdown,
    ShutdownToIdle
};

const char *transition_label(Transition transition);

// Advances the session by one frame: at most one transition, so a landing completed while
// airborne is not stepped as Landed in the same frame. Arms the landing detector whenever
// the aircraft becomes airborne; a completed landing is session.landings.back().
Transition step(FlightSession &session, LandingDetector &detector, const GroundContact &contact,
                const FrameInput &frame, const Environment &environment);

// Ends the airborne phase with a completed landing.
void record_landing(FlightSession &session, const LandingData &landing);

// Flight time runs only while a flight is in progress; active time additionally stops
// while the sim is paused or replaying.
void accumulate_flight_time(FlightSession &session, const FrameInput &frame, float elapsed_sec);

struct PauseTracker
{
    int        previous_paused = 0;
    PauseEvent pending;
};

// Records each pause with the position the aircraft was frozen at. Returns true on the
// frame a pause ends and lands in session.pauses. A pause begun in one session never
// carries over into the next.
bool record_pause_release(PauseTracker &tracker, FlightSession &session, const FrameInput &frame);

// A flight saved while the sim is still paused would count the pause in its paused time
// without a pause of its own. Closes it at end_time; returns true if one was open.
bool close_open_pause(PauseTracker &tracker, FlightSession &session, time_t end_time);

// One track point every TRACK_SAMPLE_INTERVAL_SEC of active flight time.
void record_track_sample(FlightSession &session, const FrameInput &frame);

enum class PlacementOutcome : std::uint8_t
{
    None,                // no flight in progress
    SaveFlight,          // repositioned after landing
    SaveUpToLastLanding, // repositioned in flight, after at least one landing
    Discard              // repositioned before any landing
};

// A new flight or an aircraft reload moves the aircraft without restarting the plugin,
// so a session still running belongs to the previous flight.
PlacementOutcome placement_outcome(const FlightSession &session);

} // namespace FlightStateMachine
