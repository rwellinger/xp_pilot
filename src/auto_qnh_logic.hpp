#pragma once

// Pure decision logic for AutoQNH — no XPLM dependency, exposed so unit tests
// can exercise the state machines without standing up the plugin.

namespace AutoQnhLogic
{

inline constexpr float THRESHOLD_ON  = 0.05f;  // inHg — ~1.7 hPa, triggers warning / auto-set
inline constexpr float THRESHOLD_OFF = 0.02f;  // inHg — warning clears below this (hysteresis)
inline constexpr float TA_HYSTERESIS = 200.0f; // ft buffer around the transition altitude

// True if the aircraft is currently considered above the transition altitude,
// applying ±TA_HYSTERESIS so a hovering altitude can't flicker the state.
inline bool next_above_ta(float alt_ft, int ta_ft, bool was_above)
{
    if (was_above)
        return alt_ft > static_cast<float>(ta_ft) - TA_HYSTERESIS;
    return alt_ft >= static_cast<float>(ta_ft) + TA_HYSTERESIS;
}

// Decide whether the QNH-disagree warning should be visible this frame.
//
// Above TA: warning is on whenever the pilot is NOT on STD — drift vs. local
// QNH is irrelevant up there.
// Below TA: warning is silenced when the pilot is on STD (intentional); other-
// wise it latches above THRESHOLD_ON and releases only below THRESHOLD_OFF.
inline bool next_qnh_warning_state(bool above_ta, bool on_fl, float drift, bool was_active)
{
    if (above_ta)
        return !on_fl;
    if (on_fl)
        return false;
    if (was_active)
        return drift >= THRESHOLD_OFF;
    return drift > THRESHOLD_ON;
}

} // namespace AutoQnhLogic
