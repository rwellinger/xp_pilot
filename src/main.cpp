#include "auto_qnh.hpp"
#include "flight_logger.hpp"
#include "logbook_ui.hpp"
#include "settings.hpp"
#include <XPLM/XPLMDisplay.h>
#include <XPLM/XPLMGraphics.h>
#include <XPLM/XPLMMenus.h>
#include <XPLM/XPLMPlugin.h>
#include <XPLM/XPLMUtilities.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <json.hpp>

using json = nlohmann::json;

// ── Settings persistence ─────────────────────────────────────────────────────

static std::string settings_path() { return FlightLogger::data_dir() + "settings.json"; }

static void load_settings()
{
    std::ifstream f(settings_path());
    if (!f.is_open())
        return;
    try
    {
        json j;
        f >> j;
        AutoQNH::set_enabled(j.value("auto_qnh", false));
        AutoQNH::set_messages_enabled(j.value("qnh_messages", true));
        AutoQNH::set_transition_altitude_ft(j.value("qnh_transition_altitude_ft", 18000));
        FlightLogger::set_write_enabled(j.value("write_logs", true));
        FlightLogger::set_html_report_enabled(j.value("html_report", true));
        FlightLogger::set_messages_enabled(j.value("log_messages", true));
        FlightLogger::set_landing_popup_enabled(j.value("landing_popup", true));
    }
    catch (...)
    {
        XPLMDebugString("[xp_pilot] Failed to parse settings.json\n");
    }
}

void Settings::save()
{
    json j;
    j["auto_qnh"]                  = AutoQNH::enabled();
    j["qnh_messages"]              = AutoQNH::messages_enabled();
    j["qnh_transition_altitude_ft"] = AutoQNH::transition_altitude_ft();
    j["write_logs"]    = FlightLogger::write_enabled();
    j["html_report"]   = FlightLogger::html_report_enabled();
    j["log_messages"]  = FlightLogger::messages_enabled();
    j["landing_popup"] = FlightLogger::landing_popup_enabled();
    std::ofstream f(settings_path());
    if (f.is_open())
        f << j.dump(2);
}

// ── Draw callback: overlay + popup (registered once) ─────────────────────────

static int DrawCallback(XPLMDrawingPhase, int, void *)
{
    AutoQNH::draw();
    FlightLogger::draw_overlay();
    LogbookUI::draw();
    return 1;
}

// ── Menu + Commands ──────────────────────────────────────────────────────────

static XPLMCommandRef s_cmd_logbook = nullptr;

static XPLMMenuID s_plugin_menu  = 0;
static int        s_logbook_item = -1;

static void PluginMenuHandler(void *, void *item_ref)
{
    if ((intptr_t)item_ref == 1)
    {
        LogbookUI::toggle();
    }
}

static int CmdLogbook(XPLMCommandRef, XPLMCommandPhase phase, void *)
{
    if (phase == xplm_CommandBegin)
        LogbookUI::toggle();
    return 1;
}

// ════════════════════════════════════════════════════════════════
// X-Plane Plugin entry points
// ════════════════════════════════════════════════════════════════

PLUGIN_API int XPluginStart(char *outName, char *outSig, char *outDesc)
{
    // Use POSIX paths on all platforms. On macOS this is required for X-Plane installs
    // on external volumes — without it the SDK returns HFS paths that lose the
    // /Volumes/<name>/ mount prefix, causing all plugin file I/O to hit the read-only
    // system root.
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    snprintf(outName, 255, "xp_pilot v%s", XP_PILOT_VERSION);
    strncpy(outSig, "thWelly.xp_pilot", 255);
    snprintf(outDesc, 255, "Flight Logger + Auto QNH v%s", XP_PILOT_VERSION);

    // XPluginStart must never throw — an uncaught exception here will crash X-Plane
    // before any other plugin gets a chance to load. Trace each step so we can
    // pinpoint the failing module from the user's Log.txt if init aborts.
    try
    {
        XPLMDebugString("[xp_pilot] XPluginStart: entry\n");

        XPLMDebugString("[xp_pilot] XPluginStart: FlightLogger::init\n");
        FlightLogger::init();
        XPLMDebugString("[xp_pilot] XPluginStart: AutoQNH::init\n");
        AutoQNH::init();
        XPLMDebugString("[xp_pilot] XPluginStart: LogbookUI::init\n");
        LogbookUI::init();
        XPLMDebugString("[xp_pilot] XPluginStart: load_settings\n");
        load_settings();

        XPLMRegisterDrawCallback(DrawCallback, xplm_Phase_Window, 1, nullptr);

        s_cmd_logbook = XPLMCreateCommand("xp_pilot/logbook/toggle", "Toggle Flight Logbook");
        XPLMRegisterCommandHandler(s_cmd_logbook, CmdLogbook, 1, nullptr);

        XPLMMenuID plugins_menu = XPLMFindPluginsMenu();
        int        sub          = XPLMAppendMenuItem(plugins_menu, "xp_pilot", nullptr, 0);
        s_plugin_menu           = XPLMCreateMenu("xp_pilot", plugins_menu, sub, PluginMenuHandler, nullptr);
        s_logbook_item          = XPLMAppendMenuItem(s_plugin_menu, "Open / Close Logbook", (void *)1, 0);

        char banner[128];
        snprintf(banner, sizeof(banner), "[xp_pilot] *** xp_pilot v%s by thWelly ***\n", XP_PILOT_VERSION);
        XPLMDebugString(banner);
        return 1;
    }
    catch (const std::exception &e)
    {
        XPLMDebugString(("[xp_pilot] FATAL: XPluginStart threw: " + std::string(e.what()) + "\n").c_str());
        return 0;
    }
    catch (...)
    {
        XPLMDebugString("[xp_pilot] FATAL: XPluginStart threw unknown exception\n");
        return 0;
    }
}

PLUGIN_API void XPluginStop()
{
    LogbookUI::stop();
    FlightLogger::stop();
    AutoQNH::stop();
    if (s_cmd_logbook)
        XPLMUnregisterCommandHandler(s_cmd_logbook, CmdLogbook, 1, nullptr);
    XPLMUnregisterDrawCallback(DrawCallback, xplm_Phase_Window, 1, nullptr);
    XPLMDebugString("[xp_pilot] Plugin unloaded.\n");
}

PLUGIN_API int  XPluginEnable() { return 1; }
PLUGIN_API void XPluginDisable() {}
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void *) {}
