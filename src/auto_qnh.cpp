#include "auto_qnh.hpp"
#include <XPLM/XPLMDataAccess.h>
#include <XPLM/XPLMDisplay.h>
#include <XPLM/XPLMGraphics.h>
#include <XPLM/XPLMMenus.h>
#include <XPLM/XPLMPlugin.h>
#include <XPLM/XPLMProcessing.h>
#include <XPLM/XPLMUtilities.h>
#include <cmath>
#include <cstring>

static constexpr float INHG_PER_PA    = 1.0f / 3386.389f;
static constexpr float THRESHOLD_ON   = 0.05f; // ~1.7 hPa — triggers warning / auto-set
static constexpr float THRESHOLD_OFF  = 0.02f; // hysteresis: warning clears below this
static constexpr float FLIGHTLEVEL    = 29.92f;
static constexpr float FL_EPSILON     = 0.01f;
static constexpr int   TA_DEFAULT_FT  = 18000; // FAA-standard; configurable per user / region
static constexpr float TA_HYSTERESIS  = 200.0f; // ft buffer around TA to prevent flicker

// Datarefs
static XPLMDataRef s_baro_pilot   = nullptr;
static XPLMDataRef s_baro_copilot = nullptr;
static XPLMDataRef s_sealevel_pas = nullptr;
static XPLMDataRef s_alt_ft_pilot = nullptr;

// State
static bool s_enabled              = false;
static bool s_warning_active       = false;
static bool s_messages_enabled     = true;
static bool s_above_ta             = false;
static int  s_transition_altitude  = TA_DEFAULT_FT;

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
static bool next_above_ta(float alt, int ta, bool was_above)
{
    if (was_above)
        return alt > (float)ta - TA_HYSTERESIS;
    return alt >= (float)ta + TA_HYSTERESIS;
}

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
    if (phase != xplm_CommandBegin)
        return 1;
    set_both_baros(actual_qnh_inhg());
    return 1;
}

static int CmdSetFL(XPLMCommandRef, XPLMCommandPhase phase, void *)
{
    if (phase != xplm_CommandBegin)
        return 1;
    set_both_baros(FLIGHTLEVEL);
    return 1;
}

// ── Flight loop: auto mode ────────────────────────────────────────────────────

static float FlightLoopCB(float, float, int, void *)
{
    if (!s_enabled)
        return 2.0f;

    s_above_ta = next_above_ta(pilot_altitude_ft(), s_transition_altitude, s_above_ta);

    // Above transition altitude the pilot is responsible for setting STD 29.92.
    // Auto-correcting back to local QNH up here would fight the pilot.
    if (s_above_ta)
        return 2.0f;

    const float qnh       = actual_qnh_inhg();
    const float pqnh      = pilot_qnh();
    const bool  on_fl     = (std::fabs(pqnh - FLIGHTLEVEL) < FL_EPSILON);
    const bool  big_drift = (std::fabs(pqnh - qnh) > THRESHOLD_ON);

    if (!on_fl && big_drift)
    {
        set_both_baros(qnh);
    }

    return 2.0f;
}

// ── Draw: warning text ────────────────────────────────────────────────────────

// Below TA: warning silenced when pilot is on STD (intentional); otherwise it
// latches above THRESHOLD_ON and releases below THRESHOLD_OFF.
// Above TA: warning is on whenever pilot is NOT on STD — drift vs. local QNH
// is irrelevant up here.
static bool next_qnh_warning_state(bool above_ta, bool on_fl, float drift, bool was_active)
{
    if (above_ta)
        return !on_fl;
    if (on_fl)
        return false;
    if (was_active)
        return drift >= THRESHOLD_OFF;
    return drift > THRESHOLD_ON;
}

static void draw_copilot_disagree_warning(float pqnh)
{
    if (!s_baro_copilot)
        return;
    float cpqnh = XPLMGetDataf(s_baro_copilot);
    if (std::fabs(pqnh - cpqnh) <= FL_EPSILON)
        return;
    float c[4] = {1.0f, 0.3f, 0.3f, 1.0f};
    XPLMDrawString(c, 40, 40, const_cast<char *>("ALTIMETER DISAGREE - PF/PM mismatch"), nullptr,
                   xplmFont_Proportional);
}

void AutoQNH::draw()
{
    if (!s_enabled)
        return;

    const float qnh   = actual_qnh_inhg();
    const float pqnh  = pilot_qnh();
    const bool  on_fl = (std::fabs(pqnh - FLIGHTLEVEL) < FL_EPSILON);
    const float drift = std::fabs(pqnh - qnh);

    s_above_ta       = next_above_ta(pilot_altitude_ft(), s_transition_altitude, s_above_ta);
    s_warning_active = next_qnh_warning_state(s_above_ta, on_fl, drift, s_warning_active);

    if (!s_warning_active && !s_baro_copilot)
        return;
    if (!s_messages_enabled)
        return;

    XPLMSetGraphicsState(0, 0, 0, 1, 1, 0, 0);

    if (s_warning_active)
    {
        float       c[4] = {1.0f, 0.6f, 0.0f, 1.0f};
        const char *msg  = s_above_ta ? "CHECK ALTIMETER - SET STD 29.92" : "CHECK ALTIMETER - QNH not set";
        XPLMDrawString(c, 40, 60, const_cast<char *>(msg), nullptr, xplmFont_Proportional);
    }

    draw_copilot_disagree_warning(pqnh);
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

    XPLMRegisterFlightLoopCallback(FlightLoopCB, 2.0f, nullptr);
}

void AutoQNH::stop()
{
    XPLMUnregisterFlightLoopCallback(FlightLoopCB, nullptr);
    if (s_cmd_qnh)
        XPLMUnregisterCommandHandler(s_cmd_qnh, CmdSetQNH, 1, nullptr);
    if (s_cmd_fl)
        XPLMUnregisterCommandHandler(s_cmd_fl, CmdSetFL, 1, nullptr);
}

void AutoQNH::toggle() { s_enabled = !s_enabled; }
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
