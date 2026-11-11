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

#include <catch2/catch_amalgamated.hpp>

using LandingDetection::TouchdownKind;

namespace
{

LandingDetector armed_in_the_air()
{
    LandingDetector detector;
    detector.arm(false, false);
    return detector;
}

FrameInput main_gear_down()
{
    FrameInput frame;
    frame.on_any_gear = true;
    return frame;
}

LandingData touchdown_at(float fpm)
{
    LandingData candidate;
    candidate.fpm = fpm;
    return candidate;
}

} // namespace

TEST_CASE("The 50-ft gate is interpolated between the frames straddling it", "[landing_detector]")
{
    LandingDetector detector = armed_in_the_air();
    FrameInput      above;
    above.agl_m   = GATE_AGL_M + 1.f;
    above.ias_kts = 70.f;
    LandingDetection::track_fifty_foot_gate(detector, above, -600.f);
    CHECK_FALSE(detector.gate_captured);

    FrameInput below;
    below.agl_m             = GATE_AGL_M - 1.f;
    below.ias_kts           = 68.f;
    below.flap_handle_ratio = 0.75f;
    LandingDetection::track_fifty_foot_gate(detector, below, -700.f);

    REQUIRE(detector.gate_captured);
    CHECK(detector.gate_ias_kts == Catch::Approx(69.f));
    CHECK(detector.gate_fpm == Catch::Approx(-650.f));
    CHECK(detector.gate_flap_ratio == Catch::Approx(0.75f));
}

TEST_CASE("The gate is not captured climbing through it or before the detector is armed", "[landing_detector]")
{
    FrameInput low;
    low.agl_m = GATE_AGL_M - 1.f;
    FrameInput high;
    high.agl_m = GATE_AGL_M + 1.f;

    LandingDetector climbing = armed_in_the_air();
    LandingDetection::track_fifty_foot_gate(climbing, low, 500.f);
    LandingDetection::track_fifty_foot_gate(climbing, high, 500.f);
    CHECK_FALSE(climbing.gate_captured);

    LandingDetector unarmed;
    LandingDetection::track_fifty_foot_gate(unarmed, high, -500.f);
    LandingDetection::track_fifty_foot_gate(unarmed, low, -500.f);
    CHECK_FALSE(unarmed.gate_captured);
}

TEST_CASE("The float time runs from below the gate to the main-gear touchdown", "[landing_detector]")
{
    LandingDetector detector = armed_in_the_air();
    FrameInput      below_gate;
    below_gate.agl_m       = 5.f;
    below_gate.sim_seconds = 100.0;
    LandingDetection::track_fifty_foot_gate(detector, below_gate, -400.f);
    CHECK(detector.float_timer == Catch::Approx(100.f));

    FrameInput touchdown  = main_gear_down();
    touchdown.sim_seconds = 104.5;
    LandingDetection::record_main_gear_touchdown(detector, TouchdownKind::First, touchdown_at(-300.f), touchdown);
    CHECK(detector.captured.float_time == Catch::Approx(4.5f));
}

TEST_CASE("A main-gear touchdown is classified as first, bounce or hop", "[landing_detector]")
{
    LandingDetector detector = armed_in_the_air();
    CHECK(LandingDetection::classify_main_gear_touchdown(detector, FrameInput{}) == TouchdownKind::None);
    CHECK(LandingDetection::classify_main_gear_touchdown(detector, main_gear_down()) == TouchdownKind::First);

    detector.captured_valid             = true;
    detector.main_gear_lifted           = true;
    detector.max_agl_since_touchdown_ft = LandingDetection::BOUNCE_AGL_LIMIT_FT - 1.f;
    CHECK(LandingDetection::classify_main_gear_touchdown(detector, main_gear_down()) == TouchdownKind::Bounce);

    detector.max_agl_since_touchdown_ft = LandingDetection::BOUNCE_AGL_LIMIT_FT;
    CHECK(LandingDetection::classify_main_gear_touchdown(detector, main_gear_down()) == TouchdownKind::Hop);

    detector.previous_on_any = true;
    CHECK(LandingDetection::classify_main_gear_touchdown(detector, main_gear_down()) == TouchdownKind::None);

    LandingDetector unarmed;
    CHECK(LandingDetection::classify_main_gear_touchdown(unarmed, main_gear_down()) == TouchdownKind::None);
}

TEST_CASE("Of several bounces the hardest touchdown is kept and every bounce is counted", "[landing_detector]")
{
    LandingDetector detector = armed_in_the_air();
    LandingDetection::record_main_gear_touchdown(detector, TouchdownKind::First, touchdown_at(-300.f),
                                                 main_gear_down());

    detector.main_gear_lifted = true;
    LandingDetection::record_main_gear_touchdown(detector, TouchdownKind::Bounce, touchdown_at(-200.f),
                                                 main_gear_down());
    CHECK(detector.bounce_count == 1);
    CHECK(detector.captured.fpm == Catch::Approx(-300.f));
    CHECK_FALSE(detector.main_gear_lifted);

    LandingDetection::record_main_gear_touchdown(detector, TouchdownKind::Bounce, touchdown_at(-450.f),
                                                 main_gear_down());
    CHECK(detector.bounce_count == 2);
    CHECK(detector.captured.fpm == Catch::Approx(-450.f));
    CHECK(detector.worst_fpm_magnitude == Catch::Approx(450.f));
}

TEST_CASE("A hop keeps the first touchdown and restarts the lift tracking", "[landing_detector]")
{
    LandingDetector detector = armed_in_the_air();
    LandingDetection::record_main_gear_touchdown(detector, TouchdownKind::First, touchdown_at(-300.f),
                                                 main_gear_down());

    FrameInput lifted;
    lifted.agl_m = 3.f;
    LandingDetection::track_main_gear_lift(detector, lifted);
    REQUIRE(detector.main_gear_lifted);
    CHECK(detector.max_agl_since_touchdown_ft == Catch::Approx(9.84f).margin(0.01f));

    LandingDetection::record_main_gear_touchdown(detector, TouchdownKind::Hop, touchdown_at(-900.f), main_gear_down());
    CHECK(detector.captured.fpm == Catch::Approx(-300.f));
    CHECK(detector.bounce_count == 0);
    CHECK_FALSE(detector.main_gear_lifted);
    CHECK(detector.max_agl_since_touchdown_ft == 0.f);
}

TEST_CASE("Lift tracking only starts after a captured touchdown", "[landing_detector]")
{
    LandingDetector detector = armed_in_the_air();
    FrameInput      airborne;
    airborne.agl_m = 2.f;
    LandingDetection::track_main_gear_lift(detector, airborne);
    CHECK_FALSE(detector.main_gear_lifted);
}

TEST_CASE("Nose gear and skid contact complete a landing only on their rising edge", "[landing_detector]")
{
    LandingDetector detector = armed_in_the_air();
    FrameInput      all_down;
    all_down.on_any_gear = true;
    all_down.on_all_gear = true;

    CHECK_FALSE(LandingDetection::nose_gear_settled(detector, all_down)); // no main-gear touchdown yet
    CHECK(LandingDetection::skids_touched_down(detector, all_down));

    detector.captured_valid = true;
    CHECK(LandingDetection::nose_gear_settled(detector, all_down));

    LandingDetection::remember_gear_contact(detector, all_down);
    CHECK_FALSE(LandingDetection::nose_gear_settled(detector, all_down));
    CHECK_FALSE(LandingDetection::skids_touched_down(detector, all_down));
}

TEST_CASE("Paused frames stay out of the smoothing windows", "[landing_detector]")
{
    LandingDetector detector = armed_in_the_air();
    FrameInput      frame;
    frame.agl_m = 10.f;
    LandingDetection::observe_frame(detector, frame);
    frame.paused = 1;
    LandingDetection::observe_frame(detector, frame);
    CHECK(detector.agl_buffer.values.size() == 1);
}

TEST_CASE("Vertical speed comes from the AGL window once it spans time", "[landing_detector]")
{
    LandingDetector detector = armed_in_the_air();
    FrameInput      frame;
    frame.vertical_speed_fpm = -480.f;
    CHECK(LandingDetection::smoothed_vertical_speed_fpm(detector, frame) == Catch::Approx(-480.f));

    // 1 m/s down over one second.
    for (int i = 0; i <= 20; ++i)
    {
        frame.agl_m          = 20.f - static_cast<float>(i) * 0.05f;
        frame.local_time_sec = static_cast<float>(i) * 0.05f;
        LandingDetection::observe_frame(detector, frame);
    }
    CHECK(LandingDetection::smoothed_vertical_speed_fpm(detector, frame) == Catch::Approx(-196.85f).margin(1.f));
}

TEST_CASE("The flare label reads pitch and pitch rate at touchdown", "[landing_detector]")
{
    CHECK(LandingDetection::flare_label(0.5f, 0.f) == "Very good flare");
    CHECK(LandingDetection::flare_label(1.5f, 0.f) == "Good, but early flare");
    CHECK(LandingDetection::flare_label(-2.5f, 0.f) == "Poor, late flare");
    CHECK(LandingDetection::flare_label(-2.5f, 1.5f) == "Aggressive, Poor, late flare");
}
