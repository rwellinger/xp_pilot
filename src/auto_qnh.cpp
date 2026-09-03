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

#include "auto_qnh.hpp"
#include "auto_qnh_logic.hpp"
#include <XPLM/XPLMDataAccess.h>
#include <XPLM/XPLMGraphics.h>
#include <XPLM/XPLMProcessing.h>
#include <XPLM/XPLMUtilities.h>
#include <cmath>

using AutoQnhLogic::next_above_ta;
using AutoQnhLogic::next_qnh_warning_state;
using AutoQnhLogic::THRESHOLD_ON;

static constexpr float INHG_PER_PA   = 1.0f / 3386.389f;
static constexpr float FLIGHTLEVEL   = 29.92f;
static constexpr float FL_EPSILON    = 0.01f;
static constexpr int   TA_DEFAULT_FT = 18000; // FAA-standard; configurable per user / region

// Datarefs
static XPLMDataRef s_baro_pilot   = nullptr;
static XPLMDataRef s_baro_copilot = nullptr;
static XPLMDataRef s_sealevel_pas = nullptr;
static XPLMDataRef s_alt_ft_pilot = nullptr;

// State
static bool s_enabled             = false;
static bool s_warning_active      = false;
static bool s_messages_enabled    = true;
static bool s_above_ta            = false;
static int  s_transition_altitude = TA_DEFAULT_FT;

// Commands
static XPLMCommandRef s_cmd_qnh = nullptr;
static XPLMCommandRef s_cmd_fl  = nullptr;

// ── Helpers ───────────────────────────────────────────────────────────────────

static float actual_qnh_inhg()
{
    if (!s_sealevel_pas)
        return FLIGHTLEVEL;
    return XPLMGetDataf(s_sealevel_pas) * INHG_PER_PA;
}

static float pilot_qnh()
{
    if (!s_baro_pilot)
        return FLIGHTLEVEL;
    return XPLMGetDataf(s_baro_pilot);
}

static float pilot_altitude_ft()
{
    if (!s_alt_ft_pilot)
        return 0.0f;
    return XPLMGetDataf(s_alt_ft_pilot);
}

// Latching predicate so the warning/mode does not flicker as altitude oscillates
// across TA: enter "above" only +HYST above TA, leave only -HYST below TA.
static void set_both_baros(float inhg)
{
    if (s_baro_pilot)
        XPLMSetDataf(s_baro_pilot, inhg);
    if (s_baro_copilot)
        XPLMSetDataf(s_baro_copilot, inhg);
}

// ── Command handlers ──────────────────────────────────────────────────────────

static int CmdSetQNH(XPLMCommandRef, XPLMCommandPhase phase, void *)
{
    if (phase == xplm_CommandBegin)
        set_both_baros(actual_qnh_inhg());
    return 1;
}

static int CmdSetFL(XPLMCommandRef, XPLMCommandPhase phase, void *)
{
    if (phase == xplm_CommandBegin)
        set_both_baros(FLIGHTLEVEL);
    return 1;
}

// ── Flight loop: auto mode and warning state ─────────────────────────────────

// Runs every frame rather than on a timer: draw() must no longer compute anything,
// because the drawing callback it runs from is only registered while there is in fact
// a warning on screen — see draw_gate.hpp.
static float FlightLoopCB(float, float, int, void *)
{
    if (!s_enabled)
        return -1.f;

    s_above_ta = next_above_ta(pilot_altitude_ft(), s_transition_altitude, s_above_ta);

    const float qnh       = actual_qnh_inhg();
    const float pqnh      = pilot_qnh();
    const bool  on_fl     = (std::fabs(pqnh - FLIGHTLEVEL) < FL_EPSILON);
    const float drift     = std::fabs(pqnh - qnh);
    const bool  big_drift = (drift > THRESHOLD_ON);

    s_warning_active = next_qnh_warning_state(s_above_ta, on_fl, drift, s_warning_active);

    // Above transition altitude the pilot is responsible for setting STD 29.92.
    // Auto-correcting back to local QNH up here would fight the pilot.
    if (!s_above_ta && !on_fl && big_drift)
        set_both_baros(qnh);

    return -1.f;
}

// ── Draw: warning text ────────────────────────────────────────────────────────

static bool copilot_disagrees()
{
    if (!s_baro_copilot)
        return false;
    return std::fabs(pilot_qnh() - XPLMGetDataf(s_baro_copilot)) > FL_EPSILON;
}

static void draw_copilot_disagree_warning()
{
    if (!copilot_disagrees())
        return;
    float c[4] = {1.0f, 0.3f, 0.3f, 1.0f};
    XPLMDrawString(c, 40, 40, const_cast<char *>("ALTIMETER DISAGREE - PF/PM mismatch"), nullptr,
                   xplmFont_Proportional);
}

bool AutoQNH::warning_visible()
{
    return s_enabled && s_messages_enabled && (s_warning_active || copilot_disagrees());
}

void AutoQNH::draw()
{
    if (!warning_visible())
        return;

    XPLMSetGraphicsState(0, 0, 0, 1, 1, 0, 0);

    if (s_warning_active)
    {
        float       c[4] = {1.0f, 0.6f, 0.0f, 1.0f};
        const char *msg  = s_above_ta ? "CHECK ALTIMETER - SET STD 29.92" : "CHECK ALTIMETER - QNH not set";
        XPLMDrawString(c, 40, 60, const_cast<char *>(msg), nullptr, xplmFont_Proportional);
    }

    draw_copilot_disagree_warning();
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void AutoQNH::init()
{
    s_baro_pilot   = XPLMFindDataRef("sim/cockpit2/gauges/actuators/barometer_setting_in_hg_pilot");
    s_baro_copilot = XPLMFindDataRef("sim/cockpit2/gauges/actuators/barometer_setting_in_hg_copilot");
    s_sealevel_pas = XPLMFindDataRef("sim/weather/region/sealevel_pressure_pas");
    s_alt_ft_pilot = XPLMFindDataRef("sim/cockpit2/gauges/indicators/altitude_ft_pilot");

    s_cmd_qnh = XPLMCreateCommand("xp_pilot/qnh/set_qnh", "Set QNH (pilot + copilot)");
    s_cmd_fl  = XPLMCreateCommand("xp_pilot/qnh/set_flightlevel", "Set standard pressure 29.92");
    XPLMRegisterCommandHandler(s_cmd_qnh, CmdSetQNH, 1, nullptr);
    XPLMRegisterCommandHandler(s_cmd_fl, CmdSetFL, 1, nullptr);

    XPLMRegisterFlightLoopCallback(FlightLoopCB, -1.f, nullptr);
}

void AutoQNH::stop()
{
    XPLMUnregisterFlightLoopCallback(FlightLoopCB, nullptr);
    if (s_cmd_qnh)
        XPLMUnregisterCommandHandler(s_cmd_qnh, CmdSetQNH, 1, nullptr);
    if (s_cmd_fl)
        XPLMUnregisterCommandHandler(s_cmd_fl, CmdSetFL, 1, nullptr);
}

bool AutoQNH::enabled() { return s_enabled; }
void AutoQNH::set_enabled(bool on) { s_enabled = on; }
void AutoQNH::set_messages_enabled(bool on) { s_messages_enabled = on; }
bool AutoQNH::messages_enabled() { return s_messages_enabled; }

void AutoQNH::set_transition_altitude_ft(int ft)
{
    if (ft < 1000)
        ft = 1000;
    if (ft > 20000)
        ft = 20000;
    s_transition_altitude = ft;
}
int AutoQNH::transition_altitude_ft() { return s_transition_altitude; }
