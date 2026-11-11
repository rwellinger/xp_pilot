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

#include "flight_session.hpp"
#include "html_report.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <string>

// Landing detection for one approach: smoothing windows, the 50-ft gate, bounce tracking
// and the touchdown captured so far. No XPLM dependency. The sim conditions at touchdown
// (wind, configuration, weather) are filled in by the caller; everything decided from the
// frame itself lives here.

// Windowed average over the most recent samples, newest first.
struct RingBuffer
{
    std::deque<float> values;
    std::deque<float> times;
    int               capacity;

    explicit RingBuffer(int size) noexcept : capacity(size) {}

    void push(float value, float time)
    {
        values.push_front(value);
        times.push_front(time);
        while (static_cast<int>(values.size()) > capacity)
        {
            values.pop_back();
            times.pop_back();
        }
    }

    float average() const
    {
        if (values.empty())
            return 0.f;
        float sum = 0.f;
        for (auto value : values)
            sum += value;
        return sum / static_cast<float>(values.size());
    }

    float time_span() const
    {
        if (times.size() < 2)
            return 0.f;
        return times.front() - times.back();
    }

    void clear()
    {
        values.clear();
        times.clear();
    }
};

inline constexpr int   LANDING_SMOOTHING_SAMPLES = 30;
inline constexpr float GATE_AGL_M                = 15.24f; // 50 ft

struct LandingDetector
{
    RingBuffer agl_buffer{LANDING_SMOOTHING_SAMPLES};
    RingBuffer g_force_buffer{LANDING_SMOOTHING_SAMPLES};
    // Attitude at the moment of contact decides a rotorcraft landing, and a single jittery
    // frame should not decide it — so both are smoothed the same way as the G-force.
    RingBuffer roll_buffer{LANDING_SMOOTHING_SAMPLES};
    RingBuffer yaw_rate_buffer{LANDING_SMOOTHING_SAMPLES};

    float float_timer     = 0;
    float float_final     = 0;
    bool  previous_on_any = false;
    bool  previous_on_all = false;
    bool  armed           = false;

    LandingData captured;
    bool        captured_valid             = false;
    int         bounce_count               = 0;     // main-gear touchdowns after the first, before the nose gear
    float       worst_fpm_magnitude        = 0.f;   // |fpm| of the hardest touchdown so far
    bool        main_gear_lifted           = false; // main gear has left the ground since the first touchdown
    float       max_agl_since_touchdown_ft = 0.f;   // highest AGL since the last main-gear touchdown

    // Approach state when crossing 50 ft AGL in the descent.
    bool  gate_captured   = false;
    float gate_ias_kts    = 0.f;
    float gate_fpm        = 0.f;
    float gate_flap_ratio = 0.f;
    // Previous-frame values for the gate interpolation.
    float previous_agl_m   = 0.f;
    float previous_ias_kts = 0.f;
    float previous_vs_fpm  = 0.f;

    // Seeded from the sim rather than assumed airborne: a touchdown is a rising edge of
    // gear contact, and an aircraft armed while still on the ground has not landed.
    void arm(bool on_any, bool on_all)
    {
        *this           = LandingDetector{};
        armed           = true;
        previous_on_any = on_any;
        previous_on_all = on_all;
    }
};

namespace LandingDetection
{

inline constexpr float BOUNCE_AGL_LIMIT_FT = 5.f;

enum class TouchdownKind : std::uint8_t
{
    None,
    First,  // the first main-gear contact of this approach
    Bounce, // main gear back down after lifting less than BOUNCE_AGL_LIMIT_FT
    Hop     // main gear back down after a higher hop: ignored, the first contact stands
};

// Feeds the smoothing windows. Paused frames repeat the last state and would only dilute them.
void observe_frame(LandingDetector &detector, const FrameInput &frame);

// Vertical speed derived from the AGL window — smoother than the raw dataref, which spikes
// on gear compression. Falls back to the dataref while the window fills.
float smoothed_vertical_speed_fpm(const LandingDetector &detector, const FrameInput &frame);

// Captures the approach at the 50-ft gate and starts the float timer below it.
void track_fifty_foot_gate(LandingDetector &detector, const FrameInput &frame, float vertical_speed_fpm);

TouchdownKind classify_main_gear_touchdown(const LandingDetector &detector, const FrameInput &frame);

// On bounces the *hardest* touchdown wins, so the rating reflects the worst impact rather
// than the cushioned final settle. The candidate carries the touchdown's metrics; it is
// ignored for a hop.
void record_main_gear_touchdown(LandingDetector &detector, TouchdownKind kind, LandingData candidate,
                                const FrameInput &frame);

// Lift-off and highest AGL between main-gear touchdowns, which tell a bounce from a hop.
void track_main_gear_lift(LandingDetector &detector, const FrameInput &frame);

// A fixed-wing landing completes once the nose gear follows the mains.
bool nose_gear_settled(const LandingDetector &detector, const FrameInput &frame);

// Skid-landing helicopters touch down with all contact points at once, so the landing
// is captured and completed on the rising edge of full ground contact.
bool skids_touched_down(const LandingDetector &detector, const FrameInput &frame);

void remember_gear_contact(LandingDetector &detector, const FrameInput &frame);

// The airframe-agnostic metrics taken from the frame and the smoothing windows.
void fill_touchdown_kinematics(LandingData &landing, const LandingDetector &detector, const FrameInput &frame);

std::string flare_label(float pitch_deg, float pitch_rate_deg_s);
std::string fixed_wing_rating(const LandingData &landing, const std::array<int, 4> &thresholds);
std::string rotorcraft_rating(const LandingData &landing, const std::array<int, 4> &thresholds);

} // namespace LandingDetection
