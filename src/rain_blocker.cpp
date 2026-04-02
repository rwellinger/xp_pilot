#include "rain_blocker.hpp"
#include <XPLM/XPLMDataAccess.h>
#include <XPLM/XPLMProcessing.h>

// Hysteresis thresholds (groundspeed in m/s)
static constexpr float GS_OFF_MPS = 61.7f; // 120 kts
static constexpr float GS_ON_MPS  = 41.2f; // 80 kts

static XPLMDataRef s_rain_dr         = nullptr;
static XPLMDataRef s_gs_dr           = nullptr;
static bool        s_rain_suppressed = false;
static bool        s_enabled         = true;

static float FlightLoopCB(float, float, int, void *)
{
    if (!s_rain_dr || !s_gs_dr)
        return 1.0f;

    if (!s_enabled)
    {
        if (s_rain_suppressed)
        {
            XPLMSetDatai(s_rain_dr, 0);
            s_rain_suppressed = false;
        }
        return 1.0f;
    }

    float gs = XPLMGetDataf(s_gs_dr);

    if (!s_rain_suppressed && gs > GS_OFF_MPS)
    {
        XPLMSetDatai(s_rain_dr, 1);
        s_rain_suppressed = true;
    }
    else if (s_rain_suppressed && gs < GS_ON_MPS)
    {
        XPLMSetDatai(s_rain_dr, 0);
        s_rain_suppressed = false;
    }

    return 1.0f; // called every second
}

void RainBlocker::init()
{
    s_rain_dr         = XPLMFindDataRef("sim/private/controls/rain/kill_3d_rain");
    s_gs_dr           = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
    s_rain_suppressed = false;
    XPLMRegisterFlightLoopCallback(FlightLoopCB, 1.0f, nullptr);
}

void RainBlocker::toggle()
{
    s_enabled = !s_enabled;
    if (!s_enabled && s_rain_suppressed)
    {
        XPLMSetDatai(s_rain_dr, 0);
        s_rain_suppressed = false;
    }
}

bool RainBlocker::enabled() { return s_enabled; }

void RainBlocker::stop()
{
    XPLMUnregisterFlightLoopCallback(FlightLoopCB, nullptr);
    // Restore rain on unload
    if (s_rain_dr && s_rain_suppressed)
    {
        XPLMSetDatai(s_rain_dr, 0);
    }
}
