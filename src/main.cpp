#include "auto_qnh.hpp"
#include "flight_logger.hpp"
#include "logbook_ui.hpp"
#include "rain_blocker.hpp"
#include <XPLM/XPLMDisplay.h>
#include <XPLM/XPLMGraphics.h>
#include <XPLM/XPLMMenus.h>
#include <XPLM/XPLMPlugin.h>
#include <XPLM/XPLMUtilities.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

// ── Draw callback: overlay + popup (registered once) ─────────────────────────

static int DrawCallback(XPLMDrawingPhase, int, void *)
{
    AutoQNH::draw();
    FlightLogger::draw_overlay();
    FlightLogger::draw_popup();
    LogbookUI::draw();
    return 1;
}

// ── Commands ──────────────────────────────────────────────────────────────────

static XPLMCommandRef s_cmd_logbook = nullptr;
static XPLMCommandRef s_cmd_rain    = nullptr;

static XPLMMenuID s_plugin_menu   = 0;
static int        s_auto_qnh_item = -1;
static int        s_logbook_item  = -1;
static int        s_rain_item     = -1;

static void PluginMenuHandler(void *, void *item_ref)
{
    if ((intptr_t)item_ref == 0)
    {
        AutoQNH::toggle_auto();
        XPLMCheckMenuItem(s_plugin_menu, s_auto_qnh_item,
                          AutoQNH::auto_enabled() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    }
    else if ((intptr_t)item_ref == 1)
    {
        LogbookUI::toggle();
    }
    else if ((intptr_t)item_ref == 2)
    {
        RainBlocker::toggle();
        XPLMCheckMenuItem(s_plugin_menu, s_rain_item, RainBlocker::enabled() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    }
}

static int CmdLogbook(XPLMCommandRef, XPLMCommandPhase phase, void *)
{
    if (phase == xplm_CommandBegin)
        LogbookUI::toggle();
    return 1;
}

static int CmdRain(XPLMCommandRef, XPLMCommandPhase phase, void *)
{
    if (phase == xplm_CommandBegin)
    {
        RainBlocker::toggle();
        XPLMCheckMenuItem(s_plugin_menu, s_rain_item, RainBlocker::enabled() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    }
    return 1;
}

// ════════════════════════════════════════════════════════════════
// X-Plane Plugin entry points
// ════════════════════════════════════════════════════════════════

PLUGIN_API int XPluginStart(char *outName, char *outSig, char *outDesc)
{
    snprintf(outName, 255, "xp_pilot v%s", XP_PILOT_VERSION);
    strncpy(outSig, "thWelly.xp_pilot", 255);
    snprintf(outDesc, 255, "Flight Logger + Auto QNH + Rain Blocker v%s", XP_PILOT_VERSION);

    // Initialise all modules
    FlightLogger::init();
    RainBlocker::init();
    AutoQNH::init();
    LogbookUI::init();

    // Draw callback for overlays (after windows, no blend)
    XPLMRegisterDrawCallback(DrawCallback, xplm_Phase_Window, 1, nullptr);

    // Commands
    s_cmd_logbook = XPLMCreateCommand("xp_pilot/logbook/toggle", "Toggle Flight Logbook");
    s_cmd_rain    = XPLMCreateCommand("xp_pilot/rain_blocker/toggle", "Toggle Star Wars Mode (Rain Blocker)");
    XPLMRegisterCommandHandler(s_cmd_logbook, CmdLogbook, 1, nullptr);
    XPLMRegisterCommandHandler(s_cmd_rain, CmdRain, 1, nullptr);

    // Plugin menu (single "xp_pilot" submenu for all items)
    XPLMMenuID plugins_menu = XPLMFindPluginsMenu();
    int        sub          = XPLMAppendMenuItem(plugins_menu, "xp_pilot", nullptr, 0);
    s_plugin_menu           = XPLMCreateMenu("xp_pilot", plugins_menu, sub, PluginMenuHandler, nullptr);
    s_auto_qnh_item         = XPLMAppendMenuItem(s_plugin_menu, "Auto QNH", (void *)0, 0);
    s_logbook_item          = XPLMAppendMenuItem(s_plugin_menu, "Open / Close Logbook", (void *)1, 0);
    s_rain_item             = XPLMAppendMenuItem(s_plugin_menu, "Star Wars Mode", (void *)2, 0);
    XPLMCheckMenuItem(s_plugin_menu, s_auto_qnh_item, xplm_Menu_Unchecked);
    XPLMCheckMenuItem(s_plugin_menu, s_rain_item, xplm_Menu_Checked);

    char banner[128];
    snprintf(banner, sizeof(banner), "[xp_pilot] *** xp_pilot v%s by thWelly ***\n", XP_PILOT_VERSION);
    XPLMDebugString(banner);
    return 1;
}

PLUGIN_API void XPluginStop()
{
    LogbookUI::stop();
    FlightLogger::stop();
    AutoQNH::stop();
    RainBlocker::stop();
    if (s_cmd_logbook)
        XPLMUnregisterCommandHandler(s_cmd_logbook, CmdLogbook, 1, nullptr);
    if (s_cmd_rain)
        XPLMUnregisterCommandHandler(s_cmd_rain, CmdRain, 1, nullptr);
    XPLMUnregisterDrawCallback(DrawCallback, xplm_Phase_Window, 1, nullptr);
    XPLMDebugString("[xp_pilot] Plugin unloaded.\n");
}

PLUGIN_API int  XPluginEnable() { return 1; }
PLUGIN_API void XPluginDisable() {}
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void *) {}
