#include "rain_blocker.hpp"
#include <XPLM/XPLMDataAccess.h>
#include <XPLM/XPLMProcessing.h>
#include <XPLM/XPLMUtilities.h>

// Hysteresis thresholds (groundspeed in m/s)
static constexpr float GS_OFF_MPS = 61.7f; // 120 kts
static constexpr float GS_ON_MPS  = 41.2f; // 80 kts

enum class RainMethod { NONE, KILL_3D, SCALE };

static XPLMDataRef  s_rain_dr         = nullptr;
static XPLMDataRef  s_gs_dr           = nullptr;
static RainMethod   s_method          = RainMethod::NONE;
static bool         s_rain_suppressed = false;
static bool         s_enabled         = true;

static void suppress_rain(bool suppress)
{
    if (s_method == RainMethod::KILL_3D)
        XPLMSetDatai(s_rain_dr, suppress ? 1 : 0);
    else if (s_method == RainMethod::SCALE)
        XPLMSetDataf(s_rain_dr, suppress ? 0.0f : 1.0f);
}

static float FlightLoopCB(float, float, int, void *)
{
    if (s_method == RainMethod::NONE || !s_gs_dr)
        return 1.0f;

    if (!s_enabled)
    {
        if (s_rain_suppressed)
        {
            suppress_rain(false);
            s_rain_suppressed = false;
        }
        return 1.0f;
    }

    float gs = XPLMGetDataf(s_gs_dr);

    if (!s_rain_suppressed && gs > GS_OFF_MPS)
    {
        suppress_rain(true);
        s_rain_suppressed = true;
    }
    else if (s_rain_suppressed && gs < GS_ON_MPS)
    {
        suppress_rain(false);
        s_rain_suppressed = false;
    }

    return 1.0f; // called every second
}

void RainBlocker::init()
{
    s_gs_dr           = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
    s_rain_suppressed = false;

    s_rain_dr = XPLMFindDataRef("sim/private/controls/rain/kill_3d_rain");
    if (s_rain_dr)
    {
        s_method = RainMethod::KILL_3D;
        XPLMDebugString("[xp_pilot] Rain blocker: using kill_3d_rain dataref\n");
    }
    else
    {
        s_rain_dr = XPLMFindDataRef("sim/private/controls/rain/scale");
        if (s_rain_dr)
        {
            s_method = RainMethod::SCALE;
            XPLMDebugString("[xp_pilot] Rain blocker: kill_3d_rain not found, using rain/scale fallback\n");
        }
        else
        {
            s_method = RainMethod::NONE;
            XPLMDebugString("[xp_pilot] Rain blocker: no supported rain dataref found — feature disabled\n");
        }
    }

    XPLMRegisterFlightLoopCallback(FlightLoopCB, 1.0f, nullptr);
}

void RainBlocker::toggle()
{
    s_enabled = !s_enabled;
    if (!s_enabled && s_rain_suppressed)
    {
        suppress_rain(false);
        s_rain_suppressed = false;
    }
}

bool RainBlocker::enabled() { return s_enabled; }

void RainBlocker::set_enabled(bool on)
{
    if (s_enabled == on)
        return;
    s_enabled = on;
    if (!s_enabled && s_rain_suppressed)
    {
        suppress_rain(false);
        s_rain_suppressed = false;
    }
}

void RainBlocker::stop()
{
    XPLMUnregisterFlightLoopCallback(FlightLoopCB, nullptr);
    if (s_rain_suppressed)
    {
        suppress_rain(false);
        s_rain_suppressed = false;
    }
}
