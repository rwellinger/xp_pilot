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

static constexpr float INHG_PER_PA   = 1.0f / 3386.389f;
static constexpr float THRESHOLD_ON  = 0.05f; // ~1.7 hPa — triggers warning / auto-set
static constexpr float THRESHOLD_OFF = 0.02f; // hysteresis: warning clears below this
static constexpr float FLIGHTLEVEL   = 29.92f;
static constexpr float FL_EPSILON    = 0.01f;

// Datarefs
static XPLMDataRef s_baro_pilot   = nullptr;
static XPLMDataRef s_baro_copilot = nullptr;
static XPLMDataRef s_sealevel_pas = nullptr;

// State
static bool s_auto_on        = false;
static bool s_warnings_on    = true;
static bool s_warning_active = false;

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
    if (!s_auto_on)
        return 2.0f;

    float qnh  = actual_qnh_inhg();
    float pqnh = pilot_qnh();

    bool on_fl     = (std::fabs(pqnh - FLIGHTLEVEL) < FL_EPSILON);
    bool big_drift = (std::fabs(pqnh - qnh) > THRESHOLD_ON);

    if (!on_fl && big_drift)
    {
        set_both_baros(qnh);
    }

    return 2.0f;
}

// ── Draw: warning text ────────────────────────────────────────────────────────

void AutoQNH::draw()
{
    if (!s_warnings_on)
        return;

    float qnh   = actual_qnh_inhg();
    float pqnh  = pilot_qnh();
    bool  on_fl = (std::fabs(pqnh - FLIGHTLEVEL) < FL_EPSILON);
    float drift = std::fabs(pqnh - qnh);

    // Hysteresis (skip when intentionally on flight level)
    if (on_fl)
    {
        s_warning_active = false;
    }
    else if (s_warning_active)
    {
        if (drift < THRESHOLD_OFF)
            s_warning_active = false;
    }
    else
    {
        if (drift > THRESHOLD_ON)
            s_warning_active = true;
    }

    if (!s_warning_active && !s_baro_copilot)
        return;

    XPLMSetGraphicsState(0, 0, 0, 1, 1, 0, 0);

    if (s_warning_active)
    {
        float c[4] = {1.0f, 0.6f, 0.0f, 1.0f};
        XPLMDrawString(c, 40, 60, const_cast<char *>("CHECK ALTIMETER - QNH not set"), nullptr, xplmFont_Proportional);
    }

    if (s_baro_copilot)
    {
        float cpqnh = XPLMGetDataf(s_baro_copilot);
        if (std::fabs(pqnh - cpqnh) > FL_EPSILON)
        {
            float c[4] = {1.0f, 0.3f, 0.3f, 1.0f};
            XPLMDrawString(c, 40, 40, const_cast<char *>("ALTIMETER DISAGREE - PF/PM mismatch"), nullptr,
                           xplmFont_Proportional);
        }
    }
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void AutoQNH::init()
{
    s_baro_pilot   = XPLMFindDataRef("sim/cockpit2/gauges/actuators/barometer_setting_in_hg_pilot");
    s_baro_copilot = XPLMFindDataRef("sim/cockpit2/gauges/actuators/barometer_setting_in_hg_copilot");
    s_sealevel_pas = XPLMFindDataRef("sim/weather/region/sealevel_pressure_pas");

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

void AutoQNH::toggle_auto() { s_auto_on = !s_auto_on; }
bool AutoQNH::auto_enabled() { return s_auto_on; }
void AutoQNH::toggle_warnings() { s_warnings_on = !s_warnings_on; }
bool AutoQNH::warnings_enabled() { return s_warnings_on; }
