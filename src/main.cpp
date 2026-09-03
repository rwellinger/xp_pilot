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

#include "map_overlay_cache.hpp"
#include "auto_qnh.hpp"
#include "draw_gate.hpp"
#include "flight_logger.hpp"
#include "logbook_ui.hpp"
#include "ui_theme.hpp"
#include "settings.hpp"
#include <XPLM/XPLMGraphics.h>
#include <XPLM/XPLMMenus.h>
#include <XPLM/XPLMPlugin.h>
#include <XPLM/XPLMUtilities.h>
// Keep these explicit: MSVC needs them; Clang often pulls them in transitively and flags them unused.
#include <cstdint> // intptr_t
#include <cstdio>  // snprintf
#include <cstring> // strncpy
#include <filesystem>

// ── Menu + Commands ──────────────────────────────────────────────────────────

static XPLMCommandRef s_cmd_logbook     = nullptr;
static XPLMCommandRef s_cmd_show_landing = nullptr;
static XPLMCommandRef s_cmd_reset_layout = nullptr;

static XPLMMenuID s_plugin_menu       = nullptr;
static int        s_logbook_item      = -1;
static int        s_show_landing_item = -1;
static int        s_reset_layout_item = -1;

// Resetting the interface has to work from outside the ImGui window: a window scaled
// larger than the screen cannot be reached with the mouse, and the plugin menu can.
static void reset_ui_layout()
{
    LogbookUI::reset_layout();
    Settings::save();
}

static void PluginMenuHandler(void *, void *item_ref)
{
    if (item_ref == &s_show_landing_item)
        FlightLogger::replay_last_landing_popup();
    else if (item_ref == &s_reset_layout_item)
        reset_ui_layout();
    else
        LogbookUI::toggle();
}

static int CmdLogbook(XPLMCommandRef, XPLMCommandPhase phase, void *)
{
    if (phase == xplm_CommandBegin)
        LogbookUI::toggle();
    return 1;
}

static int CmdShowLanding(XPLMCommandRef, XPLMCommandPhase phase, void *)
{
    if (phase == xplm_CommandBegin)
        FlightLogger::replay_last_landing_popup();
    return 1;
}

static int CmdResetLayout(XPLMCommandRef, XPLMCommandPhase phase, void *)
{
    if (phase == xplm_CommandBegin)
        reset_ui_layout();
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

    snprintf(outName, 255, "XP Pilot Suite v%s", XP_PILOT_VERSION);
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

        // Everything drawn beneath the track comes from local files: X-Plane's own OpenAir
        // database for airspaces, and a Natural Earth extract shipped in the plugin's
        // data/ for water. No network, no API key. A missing file leaves that layer empty.
        {
            char system_path[2048] = {};
            XPLMGetSystemPath(system_path);
            MapOverlayCache::init((std::filesystem::path(system_path) / "Resources" / "default data" / "airspaces" /
                                   "airspace.txt")
                                      .generic_string(),
                                  FlightLogger::config_dir() + "coastlines.dat",
                                  FlightLogger::config_dir() + "cities.dat");
        }
        XPLMDebugString("[xp_pilot] XPluginStart: Settings::load\n");
        Settings::load();

        DrawGate::init();

        s_cmd_logbook = XPLMCreateCommand("xp_pilot/logbook/toggle", "Toggle Flight Logbook");
        XPLMRegisterCommandHandler(s_cmd_logbook, CmdLogbook, 1, nullptr);
        s_cmd_show_landing =
            XPLMCreateCommand("xp_pilot/logbook/show_last_landing", "Show last landing rating popup");
        XPLMRegisterCommandHandler(s_cmd_show_landing, CmdShowLanding, 1, nullptr);
        s_cmd_reset_layout = XPLMCreateCommand("xp_pilot/ui/reset_layout", "Reset UI scale and window size");
        XPLMRegisterCommandHandler(s_cmd_reset_layout, CmdResetLayout, 1, nullptr);

        XPLMMenuID plugins_menu = XPLMFindPluginsMenu();
        int        sub          = XPLMAppendMenuItem(plugins_menu, "XP Pilot Suite", nullptr, 0);
        s_plugin_menu           = XPLMCreateMenu("XP Pilot Suite", plugins_menu, sub, PluginMenuHandler, nullptr);
        s_logbook_item          = XPLMAppendMenuItem(s_plugin_menu, "Open / Close Logbook", &s_logbook_item, 0);
        s_show_landing_item =
            XPLMAppendMenuItem(s_plugin_menu, "Show Last Landing Rating", &s_show_landing_item, 0);
        s_reset_layout_item =
            XPLMAppendMenuItem(s_plugin_menu, "Reset UI Scale & Window Size", &s_reset_layout_item, 0);

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
    DrawGate::stop();
    LogbookUI::stop();
    MapOverlayCache::stop();
    FlightLogger::stop();
    AutoQNH::stop();
    if (s_cmd_logbook)
        XPLMUnregisterCommandHandler(s_cmd_logbook, CmdLogbook, 1, nullptr);
    if (s_cmd_show_landing)
        XPLMUnregisterCommandHandler(s_cmd_show_landing, CmdShowLanding, 1, nullptr);
    if (s_cmd_reset_layout)
        XPLMUnregisterCommandHandler(s_cmd_reset_layout, CmdResetLayout, 1, nullptr);
    XPLMDebugString("[xp_pilot] Plugin unloaded.\n");
}

PLUGIN_API int  XPluginEnable() { return 1; }
PLUGIN_API void XPluginDisable() {}
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int message, void *param)
{
    // Index 0 is the user's own aircraft; the AI planes carry none of the datarefs the
    // landing profile is read from.
    if (message == XPLM_MSG_PLANE_LOADED && reinterpret_cast<intptr_t>(param) == 0)
        FlightLogger::note_aircraft_changed();
}
