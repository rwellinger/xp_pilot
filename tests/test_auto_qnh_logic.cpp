#include "auto_qnh_logic.hpp"

#include <catch2/catch_amalgamated.hpp>

using AutoQnhLogic::next_above_ta;
using AutoQnhLogic::next_qnh_warning_state;
using AutoQnhLogic::TA_HYSTERESIS;
using AutoQnhLogic::THRESHOLD_OFF;
using AutoQnhLogic::THRESHOLD_ON;

// ── Transition altitude state machine ────────────────────────────────────────

TEST_CASE("next_above_ta: well below TA → false", "[auto_qnh][ta]")
{
    REQUIRE(next_above_ta(10000.0f, 18000, false) == false);
    REQUIRE(next_above_ta(10000.0f, 18000, true) == false);
}

TEST_CASE("next_above_ta: well above TA → true", "[auto_qnh][ta]")
{
    REQUIRE(next_above_ta(25000.0f, 18000, false) == true);
    REQUIRE(next_above_ta(25000.0f, 18000, true) == true);
}

TEST_CASE("next_above_ta: rising through TA needs +hysteresis to engage", "[auto_qnh][ta]")
{
    // Just below TA + hysteresis: stays below.
    REQUIRE(next_above_ta(18000.0f + TA_HYSTERESIS - 1.0f, 18000, false) == false);
    // At TA + hysteresis: engages.
    REQUIRE(next_above_ta(18000.0f + TA_HYSTERESIS, 18000, false) == true);
}

TEST_CASE("next_above_ta: descending out of TA needs -hysteresis to release", "[auto_qnh][ta]")
{
    // Just above TA - hysteresis: still considered above.
    REQUIRE(next_above_ta(18000.0f - TA_HYSTERESIS + 1.0f, 18000, true) == true);
    // At TA - hysteresis: releases.
    REQUIRE(next_above_ta(18000.0f - TA_HYSTERESIS, 18000, true) == false);
}

TEST_CASE("next_above_ta: hovering at TA does not flicker", "[auto_qnh][ta]")
{
    // Sitting exactly at the TA, the previous state must persist either way.
    REQUIRE(next_above_ta(18000.0f, 18000, false) == false);
    REQUIRE(next_above_ta(18000.0f, 18000, true) == true);
}

// ── QNH-disagree warning state machine ───────────────────────────────────────

TEST_CASE("warning above TA: on whenever pilot is not on STD", "[auto_qnh][warning]")
{
    // drift / was_active are irrelevant up here.
    REQUIRE(next_qnh_warning_state(true, false, 0.0f, false) == true);
    REQUIRE(next_qnh_warning_state(true, false, 0.5f, true) == true);
    REQUIRE(next_qnh_warning_state(true, true, 0.5f, true) == false);
}

TEST_CASE("warning below TA: silenced while pilot is on STD", "[auto_qnh][warning]")
{
    REQUIRE(next_qnh_warning_state(false, true, 0.0f, false) == false);
    REQUIRE(next_qnh_warning_state(false, true, 1.0f, true) == false);
}

TEST_CASE("warning below TA: drift below ON stays off", "[auto_qnh][warning]")
{
    REQUIRE(next_qnh_warning_state(false, false, THRESHOLD_ON - 0.001f, false) == false);
}

TEST_CASE("warning below TA: drift above ON triggers", "[auto_qnh][warning]")
{
    REQUIRE(next_qnh_warning_state(false, false, THRESHOLD_ON + 0.001f, false) == true);
}

TEST_CASE("warning below TA: latches between OFF and ON (hysteresis)", "[auto_qnh][warning]")
{
    // Active and drift sits in the dead-band → stays on.
    float dead_band = (THRESHOLD_ON + THRESHOLD_OFF) / 2.0f;
    REQUIRE(next_qnh_warning_state(false, false, dead_band, true) == true);
}

TEST_CASE("warning below TA: drops only once drift falls below OFF", "[auto_qnh][warning]")
{
    REQUIRE(next_qnh_warning_state(false, false, THRESHOLD_OFF, true) == true);
    REQUIRE(next_qnh_warning_state(false, false, THRESHOLD_OFF - 0.001f, true) == false);
}
