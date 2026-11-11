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

#include "landing_detector.hpp"
#include "flight_logger_logic.hpp"

#include <cmath>
#include <utility>

namespace LandingDetection
{

namespace
{

constexpr float METERS_TO_FEET        = 3.28084f;
constexpr float MPS_TO_KNOTS          = 1.94384f;
constexpr float METERS_PER_SEC_TO_FPM = 196.85f;

void reset_lift_tracking(LandingDetector &detector)
{
    detector.main_gear_lifted           = false;
    detector.max_agl_since_touchdown_ft = 0.f;
}

} // namespace

void observe_frame(LandingDetector &detector, const FrameInput &frame)
{
    if (frame.paused != 0)
        return;
    detector.agl_buffer.push(frame.agl_m, frame.local_time_sec);
    detector.g_force_buffer.push(frame.g_force, frame.local_time_sec);
    detector.roll_buffer.push(frame.roll_deg, frame.local_time_sec);
    detector.yaw_rate_buffer.push(frame.yaw_rate_deg_s, frame.local_time_sec);
}

float smoothed_vertical_speed_fpm(const LandingDetector &detector, const FrameInput &frame)
{
    const float time_span = detector.agl_buffer.time_span();
    if (time_span <= 0.f)
        return frame.vertical_speed_fpm;
    return ((frame.agl_m - detector.agl_buffer.average()) / (time_span / 2.f)) * METERS_PER_SEC_TO_FPM;
}

// Interpolated between the two frames straddling the gate: at 20 fps and 700 fpm a single
// frame spans roughly 3 ft, enough to skew the reading.
void track_fifty_foot_gate(LandingDetector &detector, const FrameInput &frame, float vertical_speed_fpm)
{
    const bool crossing_gate =
        !detector.gate_captured && detector.armed && detector.previous_agl_m > GATE_AGL_M && frame.agl_m <= GATE_AGL_M;
    if (crossing_gate)
    {
        const float descent = detector.previous_agl_m - frame.agl_m;
        const float ratio   = (descent > 0.f) ? (detector.previous_agl_m - GATE_AGL_M) / descent : 0.f;

        detector.gate_ias_kts = detector.previous_ias_kts + (frame.ias_kts - detector.previous_ias_kts) * ratio;
        detector.gate_fpm     = detector.previous_vs_fpm + (vertical_speed_fpm - detector.previous_vs_fpm) * ratio;
        // The handle rather than the surfaces: a flap selection made at the gate is a late
        // change even while the surfaces are still travelling.
        detector.gate_flap_ratio = frame.flap_handle_ratio;
        detector.gate_captured   = true;
    }
    detector.previous_agl_m   = frame.agl_m;
    detector.previous_ias_kts = frame.ias_kts;
    detector.previous_vs_fpm  = vertical_speed_fpm;

    if (detector.armed && frame.agl_m <= GATE_AGL_M && detector.float_timer == 0.f)
        detector.float_timer = static_cast<float>(frame.sim_seconds);
}

// A hop high enough to be a mini touch-and-go keeps the first touchdown as the rating; a
// real touch-and-go (above 50 ft) produces a landing entry of its own anyway.
TouchdownKind classify_main_gear_touchdown(const LandingDetector &detector, const FrameInput &frame)
{
    if (!detector.armed || detector.previous_on_any || !frame.on_any_gear)
        return TouchdownKind::None;
    if (!detector.captured_valid)
        return TouchdownKind::First;
    if (detector.main_gear_lifted && detector.max_agl_since_touchdown_ft < BOUNCE_AGL_LIMIT_FT)
        return TouchdownKind::Bounce;
    return TouchdownKind::Hop;
}

void record_main_gear_touchdown(LandingDetector &detector, TouchdownKind kind, LandingData candidate,
                                const FrameInput &frame)
{
    if (kind == TouchdownKind::None)
        return;
    if (kind == TouchdownKind::Hop)
    {
        reset_lift_tracking(detector);
        return;
    }

    if (detector.float_timer > 0.f && detector.float_final == 0.f)
        detector.float_final = static_cast<float>(frame.sim_seconds) - detector.float_timer;
    candidate.pitch_deg    = frame.pitch_deg;
    candidate.pitch_rate   = frame.pitch_rate_deg_s;
    candidate.float_time   = detector.float_final;
    candidate.flare        = flare_label(frame.pitch_deg, frame.pitch_rate_deg_s);
    candidate.gate_ias_kts = detector.gate_ias_kts;
    candidate.gate_fpm     = detector.gate_fpm;

    const float fpm_magnitude = std::abs(candidate.fpm);

    if (kind == TouchdownKind::Bounce)
    {
        ++detector.bounce_count;
        reset_lift_tracking(detector);
        if (fpm_magnitude <= detector.worst_fpm_magnitude)
            return;
    }

    detector.captured            = std::move(candidate);
    detector.captured_valid      = true;
    detector.worst_fpm_magnitude = fpm_magnitude;
}

void track_main_gear_lift(LandingDetector &detector, const FrameInput &frame)
{
    if (!detector.captured_valid || frame.on_any_gear)
        return;
    detector.main_gear_lifted = true;
    const float agl_ft        = frame.agl_m * METERS_TO_FEET;
    if (agl_ft > detector.max_agl_since_touchdown_ft)
        detector.max_agl_since_touchdown_ft = agl_ft;
}

bool nose_gear_settled(const LandingDetector &detector, const FrameInput &frame)
{
    return detector.armed && detector.captured_valid && !detector.previous_on_all && frame.on_all_gear;
}

bool skids_touched_down(const LandingDetector &detector, const FrameInput &frame)
{
    return detector.armed && !detector.previous_on_all && frame.on_all_gear;
}

void remember_gear_contact(LandingDetector &detector, const FrameInput &frame)
{
    detector.previous_on_any = frame.on_any_gear;
    detector.previous_on_all = frame.on_all_gear;
}

void fill_touchdown_kinematics(LandingData &landing, const LandingDetector &detector, const FrameInput &frame)
{
    landing.fpm              = smoothed_vertical_speed_fpm(detector, frame);
    landing.g_force          = detector.g_force_buffer.average();
    landing.agl_ft           = frame.agl_m * METERS_TO_FEET;
    landing.ias_kts          = frame.ias_kts;
    landing.ground_speed_kts = frame.ground_speed_mps * MPS_TO_KNOTS;
    landing.bank_deg         = detector.roll_buffer.average();
    landing.yaw_rate_deg_s   = detector.yaw_rate_buffer.average();
    landing.lat              = frame.latitude;
    landing.lon              = frame.longitude;
    landing.gate_flap_ratio  = detector.gate_flap_ratio;
}

std::string flare_label(float pitch_deg, float pitch_rate_deg_s)
{
    const float pitch_magnitude = std::abs(pitch_deg);
    if (pitch_magnitude <= 1.f)
        return "Very good flare";
    std::string label = (pitch_magnitude > 2.f) ? "Poor, " : "Good, but ";
    label += (pitch_deg < 0) ? "late" : "early";
    if (std::abs(pitch_rate_deg_s) > 1.f)
        label = "Aggressive, " + label;
    return label + " flare";
}

std::string fixed_wing_rating(const LandingData &landing, const std::array<int, 4> &thresholds)
{
    FlightLoggerLogic::FixedWingTouchdown touchdown;
    touchdown.descent_fpm   = landing.fpm;
    touchdown.g_force       = landing.g_force;
    touchdown.crosswind_kts = static_cast<float>(landing.crosswind_kts);
    touchdown.wind          = wind_condition_from_string(landing.wind_status);
    return FlightLoggerLogic::rating_label(FlightLoggerLogic::fixed_wing_rating_index(touchdown, thresholds));
}

std::string rotorcraft_rating(const LandingData &landing, const std::array<int, 4> &thresholds)
{
    FlightLoggerLogic::RotorcraftTouchdown touchdown;
    touchdown.descent_fpm    = landing.fpm;
    touchdown.drift_kts      = landing.ground_speed_kts;
    touchdown.bank_deg       = landing.bank_deg;
    touchdown.yaw_rate_deg_s = landing.yaw_rate_deg_s;
    touchdown.g_force        = landing.g_force;
    return FlightLoggerLogic::rating_label(FlightLoggerLogic::rotorcraft_rating_index(touchdown, thresholds));
}

} // namespace LandingDetection
