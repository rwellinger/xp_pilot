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

#include "flight_logger.hpp"
#include "flight_logger_logic.hpp"
#include "html_report.hpp"
#include "runway_data.hpp"
#include "runway_geometry.hpp"
#include <XPLM/XPLMDataAccess.h>
#include <XPLM/XPLMDisplay.h>
#include <XPLM/XPLMGraphics.h>
#include <XPLM/XPLMNavigation.h>
#include <XPLM/XPLMPlugin.h>
#include <XPLM/XPLMProcessing.h>
#include <XPLM/XPLMUtilities.h>
#include <algorithm>
#include <cmath>
// Keep these explicit: MSVC needs them; Clang often pulls them in transitively and flags them unused.
#include <cstdio> // snprintf
#include <ctime>  // std::time, time_t
#include <deque>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <json.hpp>
#include <mutex>
#include <sstream>
#include <thread>

using json = nlohmann::json;

// ════════════════════════════════════════════════════════════════
// PROFILES
// ════════════════════════════════════════════════════════════════

struct ProfileEntry
{
    std::string match, profile_name, shutdown_trigger;
};

static std::map<std::string, std::array<int, 4>> s_profiles;
static std::map<std::string, std::string>        s_profile_category; // profile name -> "fixed_wing" | "rotorcraft"
static std::vector<ProfileEntry>                 s_icao_map;
static std::string                               s_default_shutdown = "engine";
static std::string                               s_config_dir; // bundled config (landing profiles), next to the plugin
static std::string                               s_output_dir; // user data (flights, reports, index, settings) in Output
static std::string                               s_apt_dat_path; // X-Plane's global airport database
static bool                                      s_lb_needs_refresh = true;

static void load_profiles()
{
    std::string path = s_config_dir + "flight_logger_profiles.json";

    std::ifstream f(path);
    if (!f.is_open())
    {
        XPLMDebugString("[xp_pilot] WARNING: flight_logger_profiles.json not found\n");
        // Fallback medium_ga profile
        s_profiles["medium_ga"] = {-125, -250, -350, -600};
        return;
    }

    try
    {
        json j;
        f >> j;
        if (j.contains("profiles"))
        {
            for (auto &[name, val] : j["profiles"].items())
            {
                std::array<int, 4> thr{};
                std::string        cat = "fixed_wing";
                if (val.is_array() && val.size() == 4)
                {
                    thr = {val[0].get<int>(), val[1].get<int>(), val[2].get<int>(), val[3].get<int>()};
                }
                else if (val.is_object() && val.contains("thresholds") && val["thresholds"].is_array() &&
                         val["thresholds"].size() == 4)
                {
                    auto &t = val["thresholds"];
                    thr     = {t[0].get<int>(), t[1].get<int>(), t[2].get<int>(), t[3].get<int>()};
                    if (val.contains("category"))
                        cat = val["category"].get<std::string>();
                }
                else
                {
                    continue;
                }
                s_profiles[name]         = thr;
                s_profile_category[name] = cat;
            }
        }
        if (j.contains("default_shutdown_trigger"))
            s_default_shutdown = j["default_shutdown_trigger"].get<std::string>();
        if (j.contains("aircraft"))
        {
            for (auto &e : j["aircraft"])
            {
                ProfileEntry pe;
                pe.match            = e.value("match", "");
                pe.profile_name     = e.value("profile", "medium_ga");
                pe.shutdown_trigger = e.value("shutdown_trigger", "");
                if (!pe.match.empty())
                    s_icao_map.push_back(pe);
            }
        }
        char msg[256];
        snprintf(msg, sizeof(msg), "[xp_pilot] Profiles loaded: %zu profiles, %zu aircraft\n", s_profiles.size(),
                 s_icao_map.size());
        XPLMDebugString(msg);
    }
    catch (...)
    {
        XPLMDebugString("[xp_pilot] ERROR parsing flight_logger_profiles.json\n");
        s_profiles["medium_ga"] = {-125, -250, -350, -600};
    }
}

std::string FlightLogger::get_profile_name(const std::string &plane_icao)
{
    for (auto &e : s_icao_map)
        if (plane_icao.find(e.match) != std::string::npos)
            return e.profile_name;
    return s_profiles.count("medium_ga") ? "medium_ga" : "fallback";
}

std::array<int, 4> FlightLogger::get_profile_thresholds(const std::string &name)
{
    auto it = s_profiles.find(name);
    if (it != s_profiles.end())
        return it->second;
    return {-125, -250, -350, -600};
}

std::string FlightLogger::get_profile_category(const std::string &name)
{
    auto it = s_profile_category.find(name);
    if (it != s_profile_category.end())
        return it->second;
    return "fixed_wing";
}

static std::string get_shutdown_trigger(const std::string &plane_icao)
{
    for (auto &e : s_icao_map)
        if (plane_icao.find(e.match) != std::string::npos && !e.shutdown_trigger.empty())
            return e.shutdown_trigger;
    return s_default_shutdown;
}

// ════════════════════════════════════════════════════════════════
// DATAREFS
// ════════════════════════════════════════════════════════════════

static XPLMDataRef dr_gs           = nullptr; // m/s
static XPLMDataRef dr_onground     = nullptr; // any gear
static XPLMDataRef dr_onground_all = nullptr; // all gear
static XPLMDataRef dr_agl          = nullptr; // m
static XPLMDataRef dr_beacon       = nullptr;
static XPLMDataRef dr_ias          = nullptr; // kts (KIAS)
static XPLMDataRef dr_vertfpm      = nullptr; // fpm
static XPLMDataRef dr_gforce       = nullptr;
static XPLMDataRef dr_Q            = nullptr; // pitch rate deg/s
static XPLMDataRef dr_Qrad         = nullptr; // pitch rate rad/s
static XPLMDataRef dr_localtime    = nullptr;
static XPLMDataRef dr_paused       = nullptr;
static XPLMDataRef dr_in_replay    = nullptr;
static XPLMDataRef dr_wind_spd     = nullptr; // kts
static XPLMDataRef dr_wind_dir     = nullptr; // deg mag
static XPLMDataRef dr_magpsi       = nullptr;
static XPLMDataRef dr_truepsi      = nullptr; // apt.dat is true-north referenced
static XPLMDataRef dr_lat          = nullptr; // double
static XPLMDataRef dr_lon          = nullptr; // double
static XPLMDataRef dr_elevation    = nullptr; // double, meters
static XPLMDataRef dr_eng_running  = nullptr; // int array
static XPLMDataRef dr_nav_light    = nullptr;
static XPLMDataRef dr_acf_icao     = nullptr; // byte
static XPLMDataRef dr_acf_tail     = nullptr; // byte

static void find_datarefs()
{
    dr_gs           = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
    dr_onground     = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
    dr_onground_all = XPLMFindDataRef("sim/flightmodel/failures/onground_all");
    dr_agl          = XPLMFindDataRef("sim/flightmodel/position/y_agl");
    dr_beacon       = XPLMFindDataRef("sim/cockpit2/switches/beacon_on");
    dr_ias          = XPLMFindDataRef("sim/flightmodel/position/indicated_airspeed");
    dr_vertfpm      = XPLMFindDataRef("sim/flightmodel/position/vh_ind_fpm");
    dr_gforce       = XPLMFindDataRef("sim/flightmodel2/misc/gforce_normal");
    dr_Q            = XPLMFindDataRef("sim/flightmodel/position/Q");
    dr_Qrad         = XPLMFindDataRef("sim/flightmodel/position/Qrad");
    dr_localtime    = XPLMFindDataRef("sim/time/local_time_sec");
    dr_paused       = XPLMFindDataRef("sim/time/paused");
    dr_in_replay    = XPLMFindDataRef("sim/time/is_in_replay");
    dr_wind_spd     = XPLMFindDataRef("sim/cockpit2/gauges/indicators/wind_speed_kts");
    dr_wind_dir     = XPLMFindDataRef("sim/cockpit2/gauges/indicators/wind_heading_deg_mag");
    dr_magpsi       = XPLMFindDataRef("sim/flightmodel/position/mag_psi");
    dr_truepsi      = XPLMFindDataRef("sim/flightmodel/position/true_psi");
    dr_lat          = XPLMFindDataRef("sim/flightmodel/position/latitude");
    dr_lon          = XPLMFindDataRef("sim/flightmodel/position/longitude");
    dr_elevation    = XPLMFindDataRef("sim/flightmodel/position/elevation");
    dr_eng_running  = XPLMFindDataRef("sim/flightmodel/engine/ENGN_running");
    dr_nav_light    = XPLMFindDataRef("sim/cockpit/electrical/nav_lights_on");
    dr_acf_icao     = XPLMFindDataRef("sim/aircraft/view/acf_ICAO");
    dr_acf_tail     = XPLMFindDataRef("sim/aircraft/view/acf_tailnum");
}

static float  dr_f(XPLMDataRef dr) { return dr ? XPLMGetDataf(dr) : 0.0f; }
static int    dr_i(XPLMDataRef dr) { return dr ? XPLMGetDatai(dr) : 0; }
static double dr_d(XPLMDataRef dr) { return dr ? XPLMGetDatad(dr) : 0.0; }

static std::string dr_str(XPLMDataRef dr)
{
    if (!dr)
        return "";
    char buf[64] = {};
    XPLMGetDatab(dr, buf, 0, static_cast<int>(sizeof(buf)) - 1);
    return buf;
}

static bool any_engine_running()
{
    if (!dr_eng_running)
        return false;
    int vals[8] = {};
    XPLMGetDatavi(dr_eng_running, vals, 0, 8);
    for (int v : vals)
        if (v > 0)
            return true;
    return false;
}

static bool nav_light_on() { return dr_nav_light ? XPLMGetDatai(dr_nav_light) != 0 : true; }

static bool shutdown_triggered(const std::string &plane_icao)
{
    auto trig = get_shutdown_trigger(plane_icao);
    if (trig == "engine")
        return !any_engine_running();
    if (trig == "nav_light")
        return !nav_light_on();
    // "beacon"
    return dr_i(dr_beacon) == 0;
}

// ════════════════════════════════════════════════════════════════
// AIRPORT LOOKUP
// ════════════════════════════════════════════════════════════════

static std::string get_airport_id()
{
    // XPLMFindNavAid is expensive — cache result with 5-second TTL
    static std::string cached_id;
    static time_t      last_check = 0;
    time_t             now        = std::time(nullptr);
    if (now - last_check < 5)
        return cached_id;
    last_check = now;

    float      lat = static_cast<float>(dr_d(dr_lat));
    float      lon = static_cast<float>(dr_d(dr_lon));
    XPLMNavRef ref = XPLMFindNavAid(nullptr, nullptr, &lat, &lon, nullptr, xplm_Nav_Airport);
    if (ref == XPLM_NAV_NOT_FOUND)
    {
        cached_id = "";
        return "";
    }
    char outID[32] = {};
    XPLMGetNavAidInfo(ref, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, outID, nullptr, nullptr);
    cached_id = outID;
    return cached_id;
}

// ════════════════════════════════════════════════════════════════
// OVERLAY
// ════════════════════════════════════════════════════════════════

static std::string s_overlay_text;
static double      s_overlay_until = 0;
static float       s_overlay_r = 1, s_overlay_g = 1, s_overlay_b = 1;

// ── Feature toggles (persisted via settings.json) ─────────────────────────────
static bool s_write_enabled         = true;
static bool s_html_report_enabled   = true;
static bool s_messages_enabled      = true;
static bool s_landing_popup_enabled = true;
static bool          s_runway_analysis_enabled = true;
static PopupPosition s_popup_position          = POPUP_POSITION_DEFAULT;

static double monotonic_clock()
{
    static XPLMDataRef dr = XPLMFindDataRef("sim/time/total_running_time_sec");
    return static_cast<double>(XPLMGetDataf(dr));
}

static void show_overlay(const std::string &text, float sec, float r = 1.f, float g = 1.f, float b = 1.f)
{
    if (!s_messages_enabled)
        return;
    s_overlay_text  = text;
    s_overlay_until = monotonic_clock() + sec;
    s_overlay_r     = r;
    s_overlay_g     = g;
    s_overlay_b     = b;
}

void FlightLogger::set_write_enabled(bool on) { s_write_enabled = on; }
bool FlightLogger::write_enabled() { return s_write_enabled; }
void FlightLogger::set_html_report_enabled(bool on) { s_html_report_enabled = on; }
bool FlightLogger::html_report_enabled() { return s_html_report_enabled; }
void FlightLogger::set_messages_enabled(bool on) { s_messages_enabled = on; }
bool FlightLogger::messages_enabled() { return s_messages_enabled; }
void FlightLogger::set_landing_popup_enabled(bool on) { s_landing_popup_enabled = on; }
bool FlightLogger::landing_popup_enabled() { return s_landing_popup_enabled; }
void FlightLogger::set_runway_analysis_enabled(bool on) { s_runway_analysis_enabled = on; }
bool FlightLogger::runway_analysis_enabled() { return s_runway_analysis_enabled; }
void          FlightLogger::set_popup_position(PopupPosition p) { s_popup_position = p; }
PopupPosition FlightLogger::popup_position() { return s_popup_position; }

void FlightLogger::draw_overlay()
{
    if (s_overlay_text.empty())
        return;
    if (monotonic_clock() > s_overlay_until)
    {
        s_overlay_text.clear();
        return;
    }

    int sw = 0, sh = 0;
    XPLMGetScreenSize(&sw, &sh);

    XPLMSetGraphicsState(0, 0, 0, 1, 1, 0, 0);
    float c[4] = {s_overlay_r, s_overlay_g, s_overlay_b, 1.0f};
    int   x    = sw / 2 - 150;
    int   y    = static_cast<int>(static_cast<float>(sh) * 0.12f);
    XPLMDrawString(c, x, y, const_cast<char *>(s_overlay_text.c_str()), nullptr, xplmFont_Proportional);
}

// ════════════════════════════════════════════════════════════════
// LANDING POPUP
// ════════════════════════════════════════════════════════════════

static LandingData s_popup_ld;
static bool        s_popup_active = false;
static double      s_popup_until  = 0;

bool FlightLogger::popup_active()
{
    if (s_popup_active && monotonic_clock() > s_popup_until)
        s_popup_active = false;
    return s_popup_active;
}

// Screen point the popup is pinned to, paired with the pivot below. The top row keeps
// the 12% inset the popup always had; the other edges use a flat margin.
static ImVec2 popup_anchor(float screen_w, float screen_h)
{
    constexpr float EDGE_MARGIN_PX = 40.f;
    const float     top_y          = screen_h * 0.12f;
    const float     bottom_y       = screen_h - EDGE_MARGIN_PX;

    switch (s_popup_position)
    {
    case PopupPosition::TopLeft:
        return {EDGE_MARGIN_PX, top_y};
    case PopupPosition::TopRight:
        return {screen_w - EDGE_MARGIN_PX, top_y};
    case PopupPosition::Center:
        return {screen_w * 0.5f, screen_h * 0.5f};
    case PopupPosition::BottomLeft:
        return {EDGE_MARGIN_PX, bottom_y};
    case PopupPosition::BottomCenter:
        return {screen_w * 0.5f, bottom_y};
    case PopupPosition::BottomRight:
        return {screen_w - EDGE_MARGIN_PX, bottom_y};
    case PopupPosition::TopCenter:
        break;
    }
    return {screen_w * 0.5f, top_y};
}

// Which corner of the window sits on the anchor: 0 = left/top, 1 = right/bottom.
static ImVec2 popup_pivot()
{
    switch (s_popup_position)
    {
    case PopupPosition::TopLeft:
        return {0.f, 0.f};
    case PopupPosition::TopRight:
        return {1.f, 0.f};
    case PopupPosition::Center:
        return {0.5f, 0.5f};
    case PopupPosition::BottomLeft:
        return {0.f, 1.f};
    case PopupPosition::BottomCenter:
        return {0.5f, 1.f};
    case PopupPosition::BottomRight:
        return {1.f, 1.f};
    case PopupPosition::TopCenter:
        break;
    }
    return {0.5f, 0.f};
}

static ImVec4 rating_col(const std::string &r)
{
    if (r == "BUTTER!")
        return {1.00f, 1.00f, 0.00f, 1.0f};
    if (r == "GREAT LANDING!")
        return {0.25f, 1.00f, 0.25f, 1.0f};
    if (r == "ACCEPTABLE")
        return {0.00f, 0.80f, 0.00f, 1.0f};
    if (r == "HARD LANDING!")
        return {1.00f, 0.50f, 0.00f, 1.0f};
    if (r == "WASTED!")
        return {1.00f, 0.13f, 0.13f, 1.0f};
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

// The rating headline on a tinted bar in its own colour.
static void draw_popup_rating_banner(const ImVec4 &col, float content_w)
{
    constexpr float BANNER_H = 40.f;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList  *dl     = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + content_w, origin.y + BANNER_H),
                      ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.16f)), 6.f);
    dl->AddRectFilled(origin, ImVec2(origin.x + 5.f, origin.y + BANNER_H), ImGui::GetColorU32(col), 6.f);

    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::SetWindowFontScale(1.45f);
    const float text_w = ImGui::CalcTextSize(s_popup_ld.rating.c_str()).x;
    const float text_h = ImGui::GetTextLineHeight();
    ImGui::SetCursorScreenPos(
        ImVec2(origin.x + (content_w - text_w) * 0.5f, origin.y + (BANNER_H - text_h) * 0.5f));
    ImGui::TextUnformatted(s_popup_ld.rating.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + BANNER_H));
}

// One metric as a dim label above its value, laid out in a row of equal columns.
static void draw_popup_metric_cell(const char *label, const char *value, const ImVec4 &value_col, float cell_w)
{
    const ImVec2 origin = ImGui::GetCursorPos();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.66f, 0.75f, 1.f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::SetCursorPosX(origin.x);
    ImGui::PushStyleColor(ImGuiCol_Text, value_col);
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(ImVec2(origin.x + cell_w, origin.y));
}

static void draw_popup_metrics(float content_w)
{
    const ImVec4 white{0.92f, 0.94f, 0.98f, 1.f};
    const int    columns = s_popup_ld.is_rotorcraft ? 2 : 3;
    const float  cell_w  = content_w / static_cast<float>(columns);
    char         value[64];

    const ImVec2 row_origin = ImGui::GetCursorPos();

    snprintf(value, sizeof(value), "%.0f fpm", s_popup_ld.fpm);
    draw_popup_metric_cell("VERTICAL SPEED", value, rating_col(s_popup_ld.rating), cell_w);

    snprintf(value, sizeof(value), "%.2f G", s_popup_ld.g_force);
    draw_popup_metric_cell("G-FORCE", value, white, cell_w);

    if (!s_popup_ld.is_rotorcraft)
    {
        snprintf(value, sizeof(value), "%.1f s", s_popup_ld.float_time);
        draw_popup_metric_cell("FLOAT", value, white, cell_w);
    }

    // Two stacked rows of cells; the helper only advances horizontally.
    ImGui::SetCursorPos(ImVec2(row_origin.x, row_origin.y + ImGui::GetTextLineHeightWithSpacing() * 2.2f));

    if (s_popup_ld.ias_kts > 0.f)
    {
        snprintf(value, sizeof(value), "%.0f kts", s_popup_ld.ias_kts);
        draw_popup_metric_cell("TOUCHDOWN IAS", value, white, cell_w);

        snprintf(value, sizeof(value), "%.0f kts", s_popup_ld.ground_speed_kts);
        draw_popup_metric_cell("GROUND SPEED", value, white, cell_w);
    }

    if (s_popup_ld.bounce_count > 0)
    {
        snprintf(value, sizeof(value), "%d", s_popup_ld.bounce_count);
        draw_popup_metric_cell("BOUNCES", value, ImVec4(1.f, 0.5f, 0.2f, 1.f), cell_w);
    }
    else if (!s_popup_ld.is_rotorcraft)
    {
        snprintf(value, sizeof(value), "%d kts %s", std::abs(s_popup_ld.crosswind_kts),
                 s_popup_ld.crosswind_side.c_str());
        draw_popup_metric_cell("CROSSWIND", value, white, cell_w);
    }

    ImGui::SetCursorPos(ImVec2(row_origin.x, row_origin.y + ImGui::GetTextLineHeightWithSpacing() * 4.4f));

    if (!s_popup_ld.is_rotorcraft && !s_popup_ld.flare.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.80f, 0.88f, 1.f));
        ImGui::TextUnformatted(s_popup_ld.flare.c_str());
        ImGui::PopStyleColor();
    }
}

// Plan view of the runway with the touchdown marked. Same idea as the HTML report:
// along-track to scale, lateral deviation exaggerated so metres stay visible.
static void draw_popup_runway_diagram(float content_w)
{
    constexpr float STRIP_H        = 46.f;
    constexpr float LATERAL_SPAN_M = 20.f;
    constexpr float TOUCHDOWN_ZONE_M = 300.f;

    if (s_popup_ld.runway_length_m <= 0.f)
        return;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.66f, 0.75f, 1.f));
    char header[96];
    snprintf(header, sizeof(header), "RUNWAY %s  --  %.0f m usable", s_popup_ld.runway_ident.c_str(),
             s_popup_ld.runway_length_m);
    ImGui::TextUnformatted(header);
    ImGui::PopStyleColor();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList  *dl     = ImGui::GetWindowDrawList();
    const float  mid_y  = origin.y + STRIP_H * 0.5f;
    const float  half_h = STRIP_H * 0.5f;

    dl->AddRectFilled(origin, ImVec2(origin.x + content_w, origin.y + STRIP_H), IM_COL32(48, 48, 52, 255), 3.f);

    const float zone_w =
        std::min(content_w, TOUCHDOWN_ZONE_M / s_popup_ld.runway_length_m * content_w);
    dl->AddRectFilled(origin, ImVec2(origin.x + zone_w, origin.y + STRIP_H), IM_COL32(80, 78, 50, 255), 3.f);

    constexpr float DASH_PITCH_PX = 26.f;
    constexpr float DASH_LEN_PX   = 14.f;
    const float     dash_end_x    = origin.x + content_w - 12.f;
    const int       dash_count    = static_cast<int>((content_w - 24.f) / DASH_PITCH_PX);
    for (int i = 0; i < dash_count; ++i)
    {
        const float x = origin.x + 12.f + static_cast<float>(i) * DASH_PITCH_PX;
        dl->AddLine(ImVec2(x, mid_y), ImVec2(std::min(x + DASH_LEN_PX, dash_end_x), mid_y),
                    IM_COL32(215, 215, 215, 255), 1.5f);
    }

    dl->AddRectFilled(origin, ImVec2(origin.x + 4.f, origin.y + STRIP_H), IM_COL32(255, 255, 255, 255));

    const float along_pct =
        std::min(1.f, std::max(0.f, s_popup_ld.runway_distance_m / s_popup_ld.runway_length_m));
    const float offset_clamped =
        std::min(LATERAL_SPAN_M, std::max(-LATERAL_SPAN_M, s_popup_ld.runway_offset_m));
    const ImVec2 marker(origin.x + along_pct * content_w, mid_y + (offset_clamped / LATERAL_SPAN_M) * half_h);

    dl->AddCircleFilled(marker, 6.f, IM_COL32(224, 122, 60, 255));
    dl->AddCircle(marker, 6.f, IM_COL32(255, 255, 255, 255), 0, 1.5f);

    ImGui::Dummy(ImVec2(content_w, STRIP_H + 2.f));

    char caption[128];
    snprintf(caption, sizeof(caption), "%.0f m past threshold  |  %.0f m %s of centerline",
             s_popup_ld.runway_distance_m, std::abs(s_popup_ld.runway_offset_m),
             s_popup_ld.runway_offset_m > 0 ? "right" : "left");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.66f, 0.75f, 1.f));
    ImGui::TextUnformatted(caption);
    ImGui::PopStyleColor();
}

void FlightLogger::draw_popup()
{
    if (!popup_active())
        return;

    int sw = 0, sh = 0;
    XPLMGetScreenSize(&sw, &sh);

    const float  popup_w = 470.f;
    const ImVec2 anchor  = popup_anchor(static_cast<float>(sw), static_cast<float>(sh));
    const ImVec2 pivot   = popup_pivot();
    // Positioning by pivot lets the bottom and centre placements work without knowing
    // the auto-sized window height in advance.
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowSize(ImVec2(popup_w, 0.f), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.11f, 0.18f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Border, rating_col(s_popup_ld.rating));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.f, 14.f));

    ImGui::Begin("##landing_popup", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);

    const float content_w = ImGui::GetContentRegionAvail().x;

    draw_popup_rating_banner(rating_col(s_popup_ld.rating), content_w);
    ImGui::Spacing();
    draw_popup_metrics(content_w);
    if (!s_popup_ld.runway_ident.empty())
    {
        ImGui::Spacing();
        draw_popup_runway_diagram(content_w);
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

static void arm_popup(const LandingData &ld)
{
    s_popup_ld     = ld;
    s_popup_active = true;
    s_popup_until  = monotonic_clock() + 15.0;
}

// Automatic popup after touchdown. When the user turned it off the landing is still
// remembered, so the replay command can summon it on demand.
static void show_popup(const LandingData &ld)
{
    if (!s_landing_popup_enabled)
    {
        s_popup_ld = ld;
        return;
    }
    arm_popup(ld);
}

// Newest logged flight that actually contains a landing, so the popup can be replayed
// in a fresh X-Plane session. Filenames start with the date, so descending name order
// visits the most recent flights first.
static bool load_last_logged_landing(LandingData &out)
{
    const std::string        fdir = s_output_dir + "flights/";
    std::vector<std::string> fnames;

    std::error_code ec;
    auto            dit = std::filesystem::directory_iterator(fdir, ec);
    if (ec)
        return false;
    for (const auto &entry : dit)
    {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && name.size() > 5 && name.substr(name.size() - 5) == ".json")
            fnames.push_back(name);
    }
    std::sort(fnames.rbegin(), fnames.rend());

    for (const auto &fname : fnames)
    {
        std::ifstream f(fdir + fname);
        if (!f.is_open())
            continue;
        const std::string content((std::istreambuf_iterator<char>(f)), {});
        const FlightData  fd = parse_flight_json(content, fname);
        if (!fd.landings.empty())
        {
            out = fd.landings.back();
            return true;
        }
    }
    return false;
}

bool FlightLogger::replay_last_landing_popup()
{
    if (!s_popup_ld.rating.empty())
    {
        arm_popup(s_popup_ld);
        return true;
    }

    LandingData last;
    if (!load_last_logged_landing(last))
    {
        show_overlay("No landing recorded yet", 5.f, 1.f, 0.8f, 0.2f);
        return false;
    }
    arm_popup(last);
    return true;
}

// ════════════════════════════════════════════════════════════════
// LANDING DETECTION
// ════════════════════════════════════════════════════════════════

// Ring buffer for windowed averaging
struct RingBuf
{
    std::deque<float> vals;
    std::deque<float> times;
    int               size;
    RingBuf(int n) noexcept : size(n) {}
    void push(float v, float t)
    {
        vals.push_front(v);
        times.push_front(t);
        while (static_cast<int>(vals.size()) > size)
        {
            vals.pop_back();
            times.pop_back();
        }
    }
    float avg() const
    {
        if (vals.empty())
            return 0.f;
        float s = 0.f;
        for (auto v : vals)
            s += v;
        return s / static_cast<float>(vals.size());
    }
    float tspan() const
    {
        if (times.size() < 2)
            return 0.f;
        return times.front() - times.back();
    }
    void clear()
    {
        vals.clear();
        times.clear();
    }
};

static RingBuf     s_agl_buf{30};
static RingBuf     s_g_buf{30};
static float       s_float_timer = 0;
static float       s_float_final = 0;
static bool        s_prev_on_any = false;
static bool        s_prev_on_all = false;
static bool        s_ld_armed    = false;
static LandingData s_ld_captured;
static bool        s_ld_captured_valid = false;
static int         s_bounce_count      = 0;     // Touchdowns nach dem ersten, vor Bug-Touchdown
static float       s_worst_fpm_mag     = 0.f;   // |fpm| des bisher schlechtesten Touchdowns
static bool        s_main_gear_lifted  = false; // wahr, sobald Hauptfahrwerk nach erstem Touchdown abgehoben hat
static float       s_max_agl_since_td  = 0.f;   // [ft] max. AGL seit letztem Hauptfahrwerk-Touchdown

// 50-ft-Gate: Zustand des Anflugs beim Kreuzen von 50 ft AGL im Sinkflug.
static constexpr float GATE_AGL_M = 15.24f; // 50 ft
static bool            s_gate_captured = false;
static float           s_gate_ias_kts  = 0.f;
static float           s_gate_fpm      = 0.f;
static float           s_prev_agl_m    = 0.f; // Vorframe-Werte für die Gate-Interpolation
static float           s_prev_ias_kts  = 0.f;
static float           s_prev_vs_fpm   = 0.f;

static std::string eval_flare(float Q, float Qrad)
{
    float qrate = std::abs(Q);
    if (qrate <= 1.f)
        return "Very good flare";
    std::string r = (qrate > 2.f) ? "Poor, " : "Good, but ";
    r += (Q < 0) ? "late" : "early";
    if (std::abs(Qrad) > 1.f)
        r = "Aggressive, " + r;
    return r + " flare";
}

static std::string eval_rating(float fpm, float crosswind_kts, const std::string &wind_status,
                               const std::array<int, 4> &p)
{
    float xw_abs = std::min(std::abs(crosswind_kts), 30.f);
    float scale  = 1.f;
    switch (wind_condition_from_string(wind_status))
    {
    case WindCondition::Calm:
        scale = 0.f;
        break;
    case WindCondition::Light:
        scale = 0.5f;
        break;
    case WindCondition::Steady:
        scale = 1.f;
        break;
    }
    float xw_factor = 1.f + (xw_abs / 30.f) * 0.4f * scale;
    float eff_fpm   = fpm / xw_factor;
    if (eff_fpm >= static_cast<float>(p[0]) && eff_fpm <= 0.f)
        return "BUTTER!";
    if (eff_fpm >= static_cast<float>(p[1]) && eff_fpm < static_cast<float>(p[0]))
        return "GREAT LANDING!";
    if (eff_fpm >= static_cast<float>(p[2]) && eff_fpm < static_cast<float>(p[1]))
        return "ACCEPTABLE";
    if (eff_fpm >= static_cast<float>(p[3]) && eff_fpm < static_cast<float>(p[2]))
        return "HARD LANDING!";
    return "WASTED!";
}

static void calc_wind(float spd, float wind_dir, float hdg, float &hw_out, float &xw_out)
{
    float angle = (wind_dir - hdg) * static_cast<float>(M_PI) / 180.f;
    hw_out      = spd * std::cos(angle);
    xw_out      = spd * std::sin(angle);
}

static void landing_arm()
{
    s_agl_buf.clear();
    s_g_buf.clear();
    s_float_timer       = 0;
    s_float_final       = 0;
    s_ld_armed          = true;
    s_ld_captured       = {};
    s_ld_captured_valid = false;
    s_prev_on_any       = false;
    s_prev_on_all       = false;
    s_bounce_count      = 0;
    s_worst_fpm_mag     = 0.f;
    s_main_gear_lifted  = false;
    s_max_agl_since_td  = 0.f;
    s_gate_captured     = false;
    s_gate_ias_kts      = 0.f;
    s_gate_fpm          = 0.f;
    s_prev_agl_m        = 0.f;
    s_prev_ias_kts      = 0.f;
    s_prev_vs_fpm       = 0.f;
}

// ════════════════════════════════════════════════════════════════
// STATE MACHINE
// ════════════════════════════════════════════════════════════════

enum class State : uint8_t
{
    Idle,
    Rolling,
    Airborne,
    Landed,
    Shutdown
};

static State                    s_state = State::Idle;
static std::string              s_departure_icao;
static std::string              s_arrival_icao;
static std::string              s_aircraft_icao;
static std::string              s_aircraft_tail;
static bool                     s_is_rotorcraft   = false;
static time_t                   s_start_time      = 0;
static time_t                   s_end_time        = 0;
static int                      s_max_altitude_ft = 0;
static int                      s_max_speed_kts   = 0;
static std::vector<TrackPoint>  s_track;
static std::vector<LandingData> s_landings;
static std::string              s_last_gnd_apt;
static int                      s_prev_any_eng = -1; // -1 = unknown

// Flight time excluding sim pause: only frames with sim/time/paused == 0 are counted,
// so a flight parked in pause for hours no longer inflates the block time.
static double                  s_active_seconds     = 0.0;
static double                  s_last_sample_active = 0.0;
static std::vector<PauseEvent> s_pauses;

static constexpr float GS_ROLLING_MPS     = 15.4f; // 30 kts
static constexpr float GS_TAXI_STOP_MPS   = 2.6f;  // 5 kts
static constexpr float AGL_AIRBORNE_M     = 15.0f; // ~50 ft (fixed-wing climb-out)
static constexpr float AGL_AIRBORNE_HELI_M = 3.0f; // ~10 ft (rotorcraft lift-off / touch-and-go)

static float agl_airborne_threshold() { return s_is_rotorcraft ? AGL_AIRBORNE_HELI_M : AGL_AIRBORNE_M; }

static void session_reset()
{
    s_state = State::Idle;
    s_departure_icao.clear();
    s_arrival_icao.clear();
    s_aircraft_icao.clear();
    s_aircraft_tail.clear();
    s_is_rotorcraft   = false;
    s_start_time = s_end_time = 0;
    s_active_seconds = s_last_sample_active = 0.0;
    s_max_altitude_ft = s_max_speed_kts = 0;
    s_track.clear();
    s_landings.clear();
    s_pauses.clear();
}

// Block time is the accumulated active (unpaused) flight time; the pause total is
// what the wall clock ran beyond it.
static int block_time_seconds() { return static_cast<int>(std::lround(s_active_seconds)); }
static int block_time_minutes() { return static_cast<int>(s_active_seconds / 60.0); }

static int paused_seconds()
{
    const double paused = static_cast<double>(s_end_time - s_start_time) - s_active_seconds;
    return paused > 0.0 ? static_cast<int>(std::lround(paused)) : 0;
}

static void finalize_flight();

// ── Per-frame data shared across state handlers ──────────────────────────────

struct Frame
{
    float gs        = 0;
    float agl       = 0;
    float localtime = 0;
    float gforce    = 0;
    bool  on_gnd    = false;
    int   paused    = 0;
    int   in_replay = 0;
};

static Frame read_frame()
{
    Frame f;
    f.gs        = dr_f(dr_gs);
    f.on_gnd    = dr_i(dr_onground) != 0;
    f.agl       = dr_f(dr_agl);
    f.localtime = dr_f(dr_localtime);
    f.gforce    = dr_f(dr_gforce);
    f.paused    = dr_i(dr_paused);
    f.in_replay = dr_i(dr_in_replay);
    return f;
}

// ── Airport caching ──────────────────────────────────────────────────────────

static void cache_airport_when_stationary(const Frame &f)
{
    if (!f.on_gnd || f.gs >= GS_TAXI_STOP_MPS)
        return;
    auto apt = get_airport_id();
    if (!apt.empty())
        s_last_gnd_apt = apt;
}

static void handle_engine_edge_detection(bool on_gnd)
{
    int cur_eng = any_engine_running() ? 1 : 0;
    if (s_prev_any_eng == -1 || !on_gnd)
    {
        s_prev_any_eng = cur_eng;
        return;
    }

    const bool started_up = (s_prev_any_eng == 0 && cur_eng == 1);
    const bool shut_down  = (s_prev_any_eng == 1 && cur_eng == 0);
    s_prev_any_eng        = cur_eng;
    if (!started_up && !shut_down)
        return;

    auto apt = get_airport_id();
    if (!apt.empty())
        s_last_gnd_apt = apt;
    if (s_write_enabled)
    {
        const char *label = started_up ? "DEP cached: " : "ARR cached: ";
        show_overlay(std::string(label) + (apt.empty() ? "?" : apt), 4.f, 0.2f, 1.f, 0.4f);
    }
}

// ── State handlers ───────────────────────────────────────────────────────────

static void handle_idle_state(const Frame &f)
{
    const std::string icao    = dr_str(dr_acf_icao);
    const std::string pname   = FlightLogger::get_profile_name(icao);
    const bool        is_heli = FlightLogger::get_profile_category(pname) == "rotorcraft";

    if (is_heli)
    {
        // Helicopters skip the Rolling phase — flight starts the moment the skids leave the ground.
        if (f.on_gnd && f.agl <= AGL_AIRBORNE_HELI_M)
            return;

        s_is_rotorcraft  = true;
        s_aircraft_icao  = icao;
        s_aircraft_tail  = dr_str(dr_acf_tail);
        s_departure_icao = !s_last_gnd_apt.empty() ? s_last_gnd_apt : get_airport_id();
        s_start_time     = std::time(nullptr);
        s_state          = State::Airborne;
        landing_arm();
        if (s_write_enabled)
            show_overlay("REC  Flight recording started", 5.f);
        XPLMDebugString("[xp_pilot] State: Idle -> Airborne (rotorcraft, skipped Rolling)\n");
        return;
    }

    if (f.gs <= GS_ROLLING_MPS || !f.on_gnd)
        return;

    s_is_rotorcraft  = false;
    s_aircraft_icao  = icao;
    s_aircraft_tail  = dr_str(dr_acf_tail);
    s_departure_icao = !s_last_gnd_apt.empty() ? s_last_gnd_apt : get_airport_id();
    s_start_time     = std::time(nullptr);
    s_state          = State::Rolling;
    XPLMDebugString("[xp_pilot] State: Idle -> Rolling\n");
}

static void handle_rolling_state(const Frame &f)
{
    if (f.agl <= AGL_AIRBORNE_M)
        return;

    s_last_sample_active = s_active_seconds;
    if (s_departure_icao.empty())
        s_departure_icao = get_airport_id();
    s_state = State::Airborne;
    landing_arm();
    if (s_write_enabled)
        show_overlay("REC  Flight recording started", 5.f);
    XPLMDebugString("[xp_pilot] State: Rolling -> Airborne\n");
}

// Sample track/max-stats every 10 seconds of active flight time while airborne (writer
// only). Pausing the sim pauses the sampler, which keeps the report's altitude/speed
// charts — they derive their time axis from the sample index — on a true 10 s grid.
static void update_track_sample()
{
    if (!s_write_enabled)
        return;
    if (s_active_seconds - s_last_sample_active < 10.0)
        return;

    s_last_sample_active = s_active_seconds;
    time_t     now       = std::time(nullptr);
    int        alt_ft    = static_cast<int>(dr_d(dr_elevation) * 3.28084);
    int        spd_kts = static_cast<int>(std::lround(dr_f(dr_ias)));
    int        vs      = static_cast<int>(dr_f(dr_vertfpm));

    // Dropping an implausible sample leaves a 10 s hole in the chart's time axis. That
    // is the cheaper error: a garbage reading would otherwise stick in the maximum for
    // the rest of the flight.
    const bool has_previous = !s_track.empty();
    if (!FlightLoggerLogic::is_plausible_speed_sample(spd_kts, has_previous ? s_track.back().spd_kts : 0,
                                                      has_previous))
        return;

    TrackPoint tp;
    tp.t       = now;
    tp.lat     = dr_d(dr_lat);
    tp.lon     = dr_d(dr_lon);
    tp.alt_ft  = alt_ft;
    tp.spd_kts = spd_kts;
    tp.vs_fpm  = vs;
    s_track.push_back(tp);
    if (alt_ft > s_max_altitude_ft)
        s_max_altitude_ft = alt_ft;
    if (spd_kts > s_max_speed_kts)
        s_max_speed_kts = spd_kts;
}

// ── Runway preload ───────────────────────────────────────────────────────────
// Scanning apt.dat costs ~100 ms, far too much for the frame in which the aircraft
// touches down. So the destination's runways are fetched on a worker thread during
// the approach; the touchdown itself then only does arithmetic on the cached data.

static std::mutex          s_runway_cache_mutex;
static std::string         s_runway_cache_icao;  // guarded by s_runway_cache_mutex
static std::vector<Runway> s_runway_cache;       // guarded by s_runway_cache_mutex
static std::thread         s_runway_loader;
static std::string         s_runway_loader_icao; // flight-loop thread only

static void request_runway_preload(const std::string &icao)
{
    if (!s_runway_analysis_enabled || icao.empty() || icao == s_runway_loader_icao)
        return;
    if (s_runway_loader.joinable())
        return; // a load is still in flight; the next approach will pick this one up

    // The worker reads s_runway_loader_icao and s_apt_dat_path rather than capturing
    // them: constructing captures could throw, and an exception escaping a thread
    // entry point terminates X-Plane. Both are written before the thread starts and
    // only rewritten after it has been joined.
    s_runway_loader_icao = icao;
    s_runway_loader      = std::thread(
        []() noexcept
        {
            try
            {
                const std::string &wanted = s_runway_loader_icao;
                auto               found  = RunwayData::load_runways(s_apt_dat_path, {wanted});
                auto               it     = found.find(wanted);

                std::lock_guard<std::mutex> lock(s_runway_cache_mutex);
                s_runway_cache_icao = wanted;
                s_runway_cache      = (it != found.end()) ? it->second : std::vector<Runway>();
            }
            catch (...)
            {
                XPLMDebugString("[xp_pilot] Runway preload failed\n");
            }
        });
}

// Wait for a pending preload so the cache is complete. Called off the flight loop.
static void join_runway_loader()
{
    if (s_runway_loader.joinable())
        s_runway_loader.join();
    s_runway_loader_icao.clear();
}

static std::vector<Runway> cached_runways_for(const std::string &icao)
{
    std::lock_guard<std::mutex> lock(s_runway_cache_mutex);
    return (icao == s_runway_cache_icao) ? s_runway_cache : std::vector<Runway>();
}

// Place a touchdown on its runway. Leaves the landing untouched when the airport is
// not in the cache — the shutdown pass fills those in.
static void apply_runway_fix(LandingData &ld)
{
    if (!s_runway_analysis_enabled || ld.is_rotorcraft || ld.airport_icao.empty())
        return;

    const std::vector<Runway> runways = cached_runways_for(ld.airport_icao);
    if (runways.empty())
        return;

    const RunwayFix fix  = RunwayGeometry::locate_touchdown(runways, ld.lat, ld.lon, ld.heading_true);
    ld.runway_ident      = fix.runway_ident;
    ld.runway_offset_m   = fix.centerline_offset_m;
    ld.runway_distance_m = fix.distance_from_thr_m;
    ld.runway_length_m   = fix.runway_length_m;
}

// Vertical speed derived from the AGL ring buffer — smoother than the raw dataref,
// which spikes on gear compression. Falls back to the dataref while the buffer fills.
static float smoothed_vertical_speed_fpm(float agl_m)
{
    const float tspan = s_agl_buf.tspan();
    if (tspan <= 0.f)
        return dr_f(dr_vertfpm);
    return ((agl_m - s_agl_buf.avg()) / (tspan / 2.f)) * 196.85f;
}

// Fill the airframe-agnostic landing metrics (vertical speed, G, wind). Fixed-wing
// callers layer pitch/flare/float on top; rotorcraft callers don't.
static void fill_landing_metrics(LandingData &ld, const Frame &f)
{
    const float wind_spd = dr_f(dr_wind_spd);
    const float wind_dir = dr_f(dr_wind_dir);
    const float magpsi   = dr_f(dr_magpsi);

    const float gVS = smoothed_vertical_speed_fpm(f.agl);

    float hw = 0.f, xw = 0.f;
    calc_wind(wind_spd, wind_dir, magpsi, hw, xw);
    WindCondition wcond = (wind_spd < 3.f)   ? WindCondition::Calm
                          : (wind_spd < 6.f) ? WindCondition::Light
                                             : WindCondition::Steady;

    ld.fpm              = gVS;
    ld.g_force          = s_g_buf.avg();
    ld.agl_ft           = f.agl * 3.28084f;
    ld.ias_kts          = dr_f(dr_ias);
    ld.ground_speed_kts = dr_f(dr_gs) * 1.94384f;
    ld.lat              = dr_d(dr_lat);
    ld.lon              = dr_d(dr_lon);
    ld.heading_true     = dr_f(dr_truepsi);
    ld.wind_speed_kts = static_cast<int>(std::lround(wind_spd));
    ld.wind_dir_mag   = static_cast<int>(std::lround(wind_dir));
    ld.wind_status    = wind_condition_to_string(wcond);
    ld.headwind_kts   = static_cast<int>(std::lround(hw));
    ld.crosswind_kts  = static_cast<int>(std::lround(xw));
    ld.crosswind_side = (xw >= 0.f) ? "R" : "L";
}

// Record landing metrics when main gear touches down. On bounces (multiple main-gear
// touchdowns before the nose gear settles), the *worst* touchdown's metrics win — so
// the rating reflects the hardest impact rather than the cushioned final settle.
static void capture_main_gear_touchdown(const Frame &f, bool on_any)
{
    if (!s_ld_armed || s_prev_on_any || !on_any)
        return;

    // Bounce-Klassifikation: Wenn schon ein Touchdown erfasst ist, prüfen ob das ein
    // echter Bounce war (Hauptfahrwerk hat kurz abgehoben, AGL < 5 ft) oder ein
    // Hop/Mini-T&G (AGL >= 5 ft). Bei einem Hop ignorieren wir den zweiten Touchdown
    // — der erste bleibt das offizielle Rating, ein späterer Touch-and-Go (AGL > 50 ft)
    // erzeugt sowieso einen separaten Landing-Eintrag.
    constexpr float BOUNCE_AGL_LIMIT_FT = 5.f;
    const bool      is_bounce = s_ld_captured_valid && s_main_gear_lifted && s_max_agl_since_td < BOUNCE_AGL_LIMIT_FT;
    const bool      is_hop    = s_ld_captured_valid && !is_bounce;
    if (is_hop)
    {
        s_main_gear_lifted = false;
        s_max_agl_since_td = 0.f;
        return;
    }

    LandingData candidate;
    fill_landing_metrics(candidate, f);

    const float Q    = dr_f(dr_Q);
    const float Qrad = dr_f(dr_Qrad);
    if (s_float_timer > 0.f && s_float_final == 0.f)
        s_float_final = static_cast<float>(monotonic_clock()) - s_float_timer;
    candidate.pitch_deg    = Q;
    candidate.pitch_rate   = Qrad;
    candidate.float_time   = s_float_final;
    candidate.flare        = eval_flare(Q, Qrad);
    candidate.gate_ias_kts = s_gate_ias_kts;
    candidate.gate_fpm     = s_gate_fpm;

    const float fpm_mag = std::abs(candidate.fpm);

    if (is_bounce)
    {
        ++s_bounce_count;
        s_main_gear_lifted = false;
        s_max_agl_since_td = 0.f;
        // Nur überschreiben, wenn dieser Touchdown härter war als der bisher schlechteste.
        if (fpm_mag <= s_worst_fpm_mag)
            return;
    }

    s_ld_captured       = candidate;
    s_ld_captured_valid = true;
    s_worst_fpm_mag     = fpm_mag;
}

// Finalize landing once the nose gear touches down after the mains.
static void finalize_landing_on_nose_gear(bool on_all)
{
    if (!s_ld_armed || !s_ld_captured_valid || s_prev_on_all || !on_all)
        return;

    auto pname   = FlightLogger::get_profile_name(s_aircraft_icao);
    auto pthresh = FlightLogger::get_profile_thresholds(pname);
    s_ld_captured.rating =
        eval_rating(s_ld_captured.fpm, static_cast<float>(s_ld_captured.crosswind_kts), s_ld_captured.wind_status, pthresh);
    s_ld_captured.time         = std::time(nullptr);
    s_ld_captured.bounce_count = s_bounce_count;
    s_ld_captured.airport_icao = get_airport_id();
    apply_runway_fix(s_ld_captured);
    s_landings.push_back(s_ld_captured);
    s_arrival_icao = s_ld_captured.airport_icao;
    s_state        = State::Landed;
    show_popup(s_ld_captured);
    landing_arm();
    XPLMDebugString("[xp_pilot] State: Airborne -> Landed\n");
}

// Skid-landing helicopters touch down with both contact points simultaneously, so
// there is no main-gear-then-nose-gear sequence. Capture and finalize the landing
// in one step on the rising edge of onground_all. Pitch/flare/float don't apply.
static void capture_helicopter_touchdown(const Frame &f, bool on_all)
{
    if (!s_ld_armed || s_prev_on_all || !on_all)
        return;

    fill_landing_metrics(s_ld_captured, f);
    s_ld_captured.is_rotorcraft = true;

    auto pname   = FlightLogger::get_profile_name(s_aircraft_icao);
    auto pthresh = FlightLogger::get_profile_thresholds(pname);
    s_ld_captured.rating       = eval_rating(s_ld_captured.fpm, static_cast<float>(s_ld_captured.crosswind_kts),
                                             s_ld_captured.wind_status, pthresh);
    s_ld_captured.time         = std::time(nullptr);
    s_ld_captured.bounce_count = 0;
    s_ld_captured.airport_icao = get_airport_id();
    s_ld_captured_valid        = true;
    apply_runway_fix(s_ld_captured);

    s_landings.push_back(s_ld_captured);
    s_arrival_icao = s_ld_captured.airport_icao;
    s_state        = State::Landed;
    show_popup(s_ld_captured);
    landing_arm();
    XPLMDebugString("[xp_pilot] State: Airborne -> Landed (rotorcraft)\n");
}

// Snapshot speed and descent rate at the 50-ft gate — the point a landing is actually
// flown against. Interpolated between the two frames straddling the gate: at 20 fps and
// 700 fpm a single frame spans roughly 3 ft, enough to skew the reading.
static void capture_fifty_foot_gate(const Frame &f, float ias_kts, float vs_fpm)
{
    if (s_gate_captured || !s_ld_armed)
        return;
    if (s_prev_agl_m <= GATE_AGL_M || f.agl > GATE_AGL_M)
        return;

    const float descent = s_prev_agl_m - f.agl;
    const float ratio   = (descent > 0.f) ? (s_prev_agl_m - GATE_AGL_M) / descent : 0.f;

    s_gate_ias_kts  = s_prev_ias_kts + (ias_kts - s_prev_ias_kts) * ratio;
    s_gate_fpm      = s_prev_vs_fpm + (vs_fpm - s_prev_vs_fpm) * ratio;
    s_gate_captured = true;
}

static void handle_airborne_state(const Frame &f)
{
    update_track_sample();

    const bool on_any = dr_i(dr_onground) != 0;
    const bool on_all = dr_i(dr_onground_all) != 0;

    if (f.paused == 0)
    {
        s_agl_buf.push(f.agl, f.localtime);
        s_g_buf.push(f.gforce, f.localtime);
    }

    // Fetch the destination's runways early enough that the touchdown frame finds
    // them cached. ~2000 ft AGL leaves roughly a minute of margin.
    constexpr float APPROACH_PRELOAD_AGL_M = 610.f;
    if (f.agl < APPROACH_PRELOAD_AGL_M)
        request_runway_preload(get_airport_id());

    if (s_is_rotorcraft)
    {
        capture_helicopter_touchdown(f, on_all);
        s_prev_on_any = on_any;
        s_prev_on_all = on_all;
        return;
    }

    const float ias_kts = dr_f(dr_ias);
    const float vs_fpm  = smoothed_vertical_speed_fpm(f.agl);
    capture_fifty_foot_gate(f, ias_kts, vs_fpm);
    s_prev_agl_m   = f.agl;
    s_prev_ias_kts = ias_kts;
    s_prev_vs_fpm  = vs_fpm;

    if (s_ld_armed && f.agl <= GATE_AGL_M && s_float_timer == 0.f)
        s_float_timer = static_cast<float>(monotonic_clock());

    capture_main_gear_touchdown(f, on_any);

    // Track lift-off und max. AGL zwischen Hauptfahrwerk-Touchdowns für Bounce-Erkennung.
    if (s_ld_captured_valid && !on_any)
    {
        s_main_gear_lifted     = true;
        const float agl_ft_now = f.agl * 3.28084f;
        if (agl_ft_now > s_max_agl_since_td)
            s_max_agl_since_td = agl_ft_now;
    }

    finalize_landing_on_nose_gear(on_all);

    s_prev_on_any = on_any;
    s_prev_on_all = on_all;
}

static void handle_landed_state(const Frame &f)
{
    if (f.agl > agl_airborne_threshold())
    {
        s_state = State::Airborne;
        landing_arm();
        if (s_write_enabled)
            show_overlay("REC  Touch-and-Go", 4.f);
        XPLMDebugString("[xp_pilot] State: Landed -> Airborne (T&G)\n");
        return;
    }

    auto apt = get_airport_id();
    if (!apt.empty())
        s_arrival_icao = apt;

    if (f.gs < GS_TAXI_STOP_MPS && shutdown_triggered(s_aircraft_icao))
    {
        s_end_time = std::time(nullptr);
        s_state    = State::Shutdown;
        XPLMDebugString("[xp_pilot] State: Landed -> Shutdown\n");
        finalize_flight();
    }
}

static void handle_shutdown_state()
{
    session_reset();
    XPLMDebugString("[xp_pilot] State: Shutdown -> Idle\n");
}

static bool flight_in_progress()
{
    return s_state == State::Rolling || s_state == State::Airborne || s_state == State::Landed;
}

// Flight-loop callbacks keep firing while the sim is paused or in replay, so gate the
// accumulator on those datarefs instead of trusting the wall clock. Freezing the
// accumulator also freezes the track sampler, which keeps replayed frames out of the
// track and out of the max altitude/speed statistics.
static void accumulate_active_time(const Frame &f, float elapsed_real_sec)
{
    if (f.paused == 0 && f.in_replay == 0 && flight_in_progress())
        s_active_seconds += elapsed_real_sec;
}

// Records each pause with the position the aircraft was frozen at, so the report can
// mark it on the route, and logs it so an unexpected block time can be traced from
// Log.txt without picking the flight JSON apart.
static void record_pause_release(const Frame &f)
{
    static int        prev_paused = 0;
    static PauseEvent pending;

    if (flight_in_progress())
    {
        if (f.paused != 0 && prev_paused == 0)
        {
            pending     = PauseEvent{};
            pending.t   = std::time(nullptr);
            pending.lat = dr_d(dr_lat);
            pending.lon = dr_d(dr_lon);
        }
        else if (f.paused == 0 && prev_paused != 0 && pending.t != 0)
        {
            pending.sec = static_cast<int>(std::time(nullptr) - pending.t);
            s_pauses.push_back(pending);
            pending = PauseEvent{};

            char msg[128];
            snprintf(msg, sizeof(msg), "[xp_pilot] Sim resumed after %d s (excluded from block time)\n",
                     s_pauses.back().sec);
            XPLMDebugString(msg);
        }
    }
    prev_paused = f.paused;
}

static float triggers_cb(float elapsed_real_sec, float, int, void *)
{
    // Both logger features off → no state machine work this frame.
    // Reset any mid-flight state so re-enabling starts cleanly from Idle.
    const bool logger_active = s_write_enabled || s_landing_popup_enabled;
    if (!logger_active)
    {
        if (s_state != State::Idle)
            session_reset();
        return -1.f;
    }

    const Frame f = read_frame();
    accumulate_active_time(f, elapsed_real_sec);
    record_pause_release(f);
    cache_airport_when_stationary(f);
    handle_engine_edge_detection(f.on_gnd);

    switch (s_state)
    {
    case State::Idle:
        handle_idle_state(f);
        break;
    case State::Rolling:
        handle_rolling_state(f);
        break;
    case State::Airborne:
        handle_airborne_state(f);
        break;
    case State::Landed:
        handle_landed_state(f);
        break;
    case State::Shutdown:
        handle_shutdown_state();
        break;
    }
    return -1.f;
}

// ════════════════════════════════════════════════════════════════
// RUNWAY ANALYSIS
// ════════════════════════════════════════════════════════════════

// Catch the touchdowns the approach preload didn't cover — a touch-and-go at a field
// the aircraft descended into too quickly, or a second airport later in the flight.
// Runs at shutdown, where a file scan costs nothing.
static void resolve_runways_for_landings()
{
    join_runway_loader();
    if (!s_runway_analysis_enabled || s_landings.empty())
        return;

    std::set<std::string> icaos;
    for (const auto &ld : s_landings)
    {
        // A rotorcraft set-down has no runway to be measured against.
        if (!ld.is_rotorcraft && ld.runway_ident.empty() && !ld.airport_icao.empty())
            icaos.insert(ld.airport_icao);
    }
    if (icaos.empty())
        return;

    const auto runways_by_airport = RunwayData::load_runways(s_apt_dat_path, icaos);
    if (runways_by_airport.empty())
    {
        XPLMDebugString("[xp_pilot] Runway analysis: no airport data found\n");
        return;
    }

    for (auto &ld : s_landings)
    {
        auto it = runways_by_airport.find(ld.airport_icao);
        if (ld.is_rotorcraft || !ld.runway_ident.empty() || it == runways_by_airport.end())
            continue;

        const RunwayFix fix = RunwayGeometry::locate_touchdown(it->second, ld.lat, ld.lon, ld.heading_true);
        ld.runway_ident      = fix.runway_ident;
        ld.runway_offset_m   = fix.centerline_offset_m;
        ld.runway_distance_m = fix.distance_from_thr_m;
        ld.runway_length_m   = fix.runway_length_m;
    }
}

// ════════════════════════════════════════════════════════════════
// JSON SAVE + FINALIZE
// ════════════════════════════════════════════════════════════════

static std::string save_flight()
{
    std::string dep  = s_departure_icao.empty() ? "ZZZZ" : s_departure_icao;
    std::string arr  = s_arrival_icao.empty() ? "ZZZZ" : s_arrival_icao;
    std::string icao = s_aircraft_icao.empty() ? "UNKN" : s_aircraft_icao;

    char       date_buf[16];
    struct tm *tm = gmtime(&s_start_time);
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tm);
    char sut[8], eut[8];
    strftime(sut, sizeof(sut), "%H:%M", tm);
    struct tm *tm2 = gmtime(&s_end_time);
    strftime(eut, sizeof(eut), "%H:%M", tm2);

    char base[256];
    snprintf(base, sizeof(base), "%s_%s_%s_%s", date_buf, dep.c_str(), arr.c_str(), icao.c_str());
    std::string fdir = s_output_dir + "flights/";
    std::string path = fdir + base + ".json";
    // Avoid overwrite
    if (std::ifstream(path).good())
    {
        path = fdir + base + "_" + std::to_string(s_start_time) + ".json";
    }

    json obj;
    obj["version"]         = 4;
    obj["date"]            = date_buf;
    obj["start_utc"]       = sut;
    obj["end_utc"]         = eut;
    obj["departure_icao"]  = dep;
    obj["arrival_icao"]    = arr;
    obj["aircraft_icao"]     = icao;
    obj["aircraft_tail"]     = s_aircraft_tail;
    obj["aircraft_category"] = s_is_rotorcraft ? "rotorcraft" : "fixed_wing";
    obj["start_time"]        = (long long)s_start_time;
    obj["end_time"]        = (long long)s_end_time;
    obj["block_time_min"]  = block_time_minutes();
    obj["block_time_sec"]  = block_time_seconds();
    obj["paused_sec"]      = paused_seconds();
    obj["max_altitude_ft"] = s_max_altitude_ft;
    obj["max_speed_kts"]   = s_max_speed_kts;
    obj["fuel_used_kg"]    = 0;

    json track_arr = json::array();
    for (auto &tp : s_track)
    {
        track_arr.push_back({{"t", tp.t},
                             {"lat", tp.lat},
                             {"lon", tp.lon},
                             {"alt", tp.alt_ft},
                             {"spd", tp.spd_kts},
                             {"vs", tp.vs_fpm}});
    }
    obj["track"] = track_arr;

    json pause_arr = json::array();
    for (auto &p : s_pauses)
    {
        pause_arr.push_back({{"t", (long long)p.t}, {"sec", p.sec}, {"lat", p.lat}, {"lon", p.lon}});
    }
    obj["pauses"] = pause_arr;

    json ldg_arr = json::array();
    for (auto &ld : s_landings)
    {
        ldg_arr.push_back({{"fpm", ld.fpm},
                           {"g_force", ld.g_force},
                           {"pitch_deg", ld.pitch_deg},
                           {"pitch_rate", ld.pitch_rate},
                           {"agl_ft", ld.agl_ft},
                           {"gate_ias_kts", ld.gate_ias_kts},
                           {"gate_fpm", ld.gate_fpm},
                           {"float_time", ld.float_time},
                           {"ias_kts", ld.ias_kts},
                           {"ground_speed_kts", ld.ground_speed_kts},
                           {"lat", ld.lat},
                           {"lon", ld.lon},
                           {"heading_true", ld.heading_true},
                           {"airport_icao", ld.airport_icao},
                           {"runway_ident", ld.runway_ident},
                           {"runway_offset_m", ld.runway_offset_m},
                           {"runway_distance_m", ld.runway_distance_m},
                           {"runway_length_m", ld.runway_length_m},
                           {"time", (long long)ld.time},
                           {"wind_speed_kts", ld.wind_speed_kts},
                           {"wind_dir_mag", ld.wind_dir_mag},
                           {"wind_status", ld.wind_status},
                           {"headwind_kts", ld.headwind_kts},
                           {"crosswind_kts", ld.crosswind_kts},
                           {"crosswind_side", ld.crosswind_side},
                           {"bounce_count", ld.bounce_count},
                           {"is_rotorcraft", ld.is_rotorcraft},
                           {"flare", ld.flare},
                           {"rating", ld.rating}});
    }
    obj["landings"] = ldg_arr;

    std::ofstream f(path);
    if (!f.is_open())
        return "";
    f << obj.dump();

    return path.substr(path.rfind('/') + 1);
}

static void finalize_flight()
{
    if (dr_i(dr_in_replay))
    {
        XPLMDebugString("[xp_pilot] Replay – skipping save\n");
        session_reset();
        return;
    }
    if (!s_write_enabled)
    {
        XPLMDebugString("[xp_pilot] Log writing disabled – skipping save\n");
        session_reset();
        return;
    }
    if (s_arrival_icao.empty() && !s_last_gnd_apt.empty())
        s_arrival_icao = s_last_gnd_apt;

    resolve_runways_for_landings();

    auto filename = save_flight();
    if (filename.empty())
    {
        show_overlay("! Flight save ERROR", 8.f, 1.f, 0.2f, 0.2f);
        session_reset();
        return;
    }

    // Build FlightData for report
    FlightData fd;
    fd.filename = filename;
    char       buf[16];
    struct tm *tm = gmtime(&s_start_time);
    strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
    fd.date = buf;
    strftime(buf, sizeof(buf), "%H:%M", tm);
    fd.start_utc = buf;
    tm           = gmtime(&s_end_time);
    strftime(buf, sizeof(buf), "%H:%M", tm);
    fd.end_utc         = buf;
    fd.departure_icao    = s_departure_icao;
    fd.arrival_icao      = s_arrival_icao;
    fd.aircraft_icao     = s_aircraft_icao;
    fd.aircraft_tail     = s_aircraft_tail;
    fd.aircraft_category = s_is_rotorcraft ? "rotorcraft" : "fixed_wing";
    fd.start_time        = s_start_time;
    fd.end_time        = s_end_time;
    fd.block_time_min  = block_time_minutes();
    fd.block_time_sec  = block_time_seconds();
    fd.paused_sec      = paused_seconds();
    fd.max_altitude_ft = s_max_altitude_ft;
    fd.max_speed_kts   = s_max_speed_kts;
    fd.track           = s_track;
    fd.landings        = s_landings;
    fd.pauses          = s_pauses;

    if (s_html_report_enabled)
    {
        auto pname   = FlightLogger::get_profile_name(s_aircraft_icao);
        auto pthresh = FlightLogger::get_profile_thresholds(pname);
        HtmlReport::generate(fd, s_output_dir, filename, pname, pthresh);
        HtmlReport::generate_index(s_output_dir);
    }

    std::string dep = s_departure_icao.empty() ? "?" : s_departure_icao;
    std::string arr = s_arrival_icao.empty() ? "?" : s_arrival_icao;
    show_overlay("Flight saved: " + dep + " -> " + arr, 8.f, 0.2f, 1.f, 0.4f);

    s_lb_needs_refresh = true;
    session_reset();
}

// ════════════════════════════════════════════════════════════════
// PUBLIC API
// ════════════════════════════════════════════════════════════════

const std::string &FlightLogger::output_dir() { return s_output_dir; }
bool              &FlightLogger::lb_needs_refresh() { return s_lb_needs_refresh; }

void FlightLogger::regen_all_reports()
{
    std::string fdir = s_output_dir + "flights/";
    // Collect .json filenames, sort for deterministic order
    std::vector<std::string> fnames;
    std::error_code          ec;
    auto                     dit = std::filesystem::directory_iterator(fdir, ec);
    if (ec)
    {
        XPLMDebugString(("[xp_pilot] regen: cannot open " + fdir + "\n").c_str());
        return;
    }
    for (auto &entry : dit)
    {
        if (entry.is_regular_file())
        {
            std::string n = entry.path().filename().string();
            if (n.size() > 5 && n.substr(n.size() - 5) == ".json")
                fnames.push_back(n);
        }
    }
    std::sort(fnames.begin(), fnames.end());

    int count = 0;
    for (auto &fname : fnames)
    {
        std::string   path = fdir + fname;
        std::ifstream f(path);
        if (!f.is_open())
            continue;
        std::string c((std::istreambuf_iterator<char>(f)), {});
        auto        fd      = parse_flight_json(c, fname);
        auto        pname   = get_profile_name(fd.aircraft_icao);
        auto        pthresh = get_profile_thresholds(pname);
        HtmlReport::generate(fd, s_output_dir, fname, pname, pthresh);
        ++count;
    }
    HtmlReport::generate_index(s_output_dir);
    char msg[64];
    snprintf(msg, sizeof(msg), "[xp_pilot] Regenerated %d reports\n", count);
    XPLMDebugString(msg);
    show_overlay(std::string("Regenerated ") + std::to_string(count) + " reports", 5.f, 0.2f, 1.f, 0.4f);
}

// Move one file, falling back to copy + remove when source and destination are on
// different volumes (rename then fails with cross_device_link). Soft-fails.
static void move_file(const std::filesystem::path &src, const std::filesystem::path &dst)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(src, ec))
        return;
    fs::create_directories(dst.parent_path(), ec);
    ec.clear();
    fs::rename(src, dst, ec);
    if (!ec)
        return;
    ec.clear();
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        XPLMDebugString(("[xp_pilot] migrate: cannot move " + src.filename().string() + ": " + ec.message() + "\n").c_str());
        return;
    }
    ec.clear();
    fs::remove(src, ec);
}

// One-time migration of user data from the old in-plugin location (<plugin>/data)
// to X-Plane's Output dir. Guarded by a marker so it runs only once. The bundled
// landing profiles stay in the plugin and are deliberately not moved.
static void migrate_user_data_to_output(const std::filesystem::path &config_dir,
                                        const std::filesystem::path &output_dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path  marker = output_dir / ".migrated";
    if (fs::exists(marker, ec))
        return;

    auto move_matching = [](const fs::path &src_dir, const fs::path &dst_dir, const std::string &ext) {
        std::error_code it_ec;
        auto            it = fs::directory_iterator(src_dir, it_ec);
        if (it_ec)
            return;
        for (auto &entry : it)
        {
            if (!entry.is_regular_file())
                continue;
            const std::string name = entry.path().filename().string();
            if (name.size() > ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
                move_file(entry.path(), dst_dir / name);
        }
    };

    move_matching(config_dir / "flights", output_dir / "flights", ".json");
    move_matching(config_dir / "flights" / "archived", output_dir / "flights" / "archived", ".json");
    move_matching(config_dir / "reports", output_dir / "reports", ".html");
    move_matching(config_dir / "reports" / "archived", output_dir / "reports" / "archived", ".html");
    move_file(config_dir / "index.html", output_dir / "index.html");
    move_file(config_dir / "settings.json", output_dir / "settings.json");

    std::ofstream(marker) << "migrated\n";
}

void FlightLogger::init()
{
    // Bundled config (landing profiles) lives next to the plugin binary.
    // XPLM_USE_NATIVE_PATHS is enabled in XPluginStart, so paths are POSIX on all
    // platforms — including external volumes mounted under /Volumes/.
    char pluginPathRaw[2048] = {};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, pluginPathRaw, nullptr, nullptr);

    // Strip filename (xp_pilot.xpl) then platform directory (mac_x64 / win_x64)
    std::filesystem::path configPath = std::filesystem::path(pluginPathRaw).parent_path().parent_path() / "data";
    s_config_dir                     = configPath.generic_string() + "/";

    // User data (flights, reports, index, settings) lives under X-Plane's Output dir
    // so it survives plugin updates. XPLMGetSystemPath returns the X-Plane root with
    // a trailing separator.
    char systemPathRaw[2048] = {};
    XPLMGetSystemPath(systemPathRaw);
    std::filesystem::path outputPath = std::filesystem::path(systemPathRaw) / "Output" / "x_pilot_reports";
    s_output_dir                     = outputPath.generic_string() + "/";
    s_apt_dat_path = (std::filesystem::path(systemPathRaw) / "Global Scenery" / "Global Airports" / "Earth nav data" /
                      "apt.dat")
                         .generic_string();
    XPLMDebugString(("[xp_pilot] config_dir: " + s_config_dir + "\n").c_str());
    XPLMDebugString(("[xp_pilot] output_dir: " + s_output_dir + "\n").c_str());

    // Use the error_code overload — the throwing variant kills X-Plane if writes
    // are blocked (sandbox, read-only volume, missing parent). Failing soft is OK:
    // logging still tries to write later and will simply skip if the dir is absent.
    // create_directories also creates the parent flights/ and reports/ dirs.
    std::error_code ec;
    std::filesystem::create_directories(outputPath / "flights" / "archived", ec);
    if (ec)
        XPLMDebugString(("[xp_pilot] WARNING: cannot create flights dir: " + ec.message() + "\n").c_str());
    ec.clear();
    std::filesystem::create_directories(outputPath / "reports" / "archived", ec);
    if (ec)
        XPLMDebugString(("[xp_pilot] WARNING: cannot create reports dir: " + ec.message() + "\n").c_str());

    migrate_user_data_to_output(configPath, outputPath);

    find_datarefs();
    load_profiles();
    XPLMRegisterFlightLoopCallback(triggers_cb, -1.f, nullptr);
    XPLMDebugString("[xp_pilot] FlightLogger initialized\n");
}

void FlightLogger::stop()
{
    XPLMUnregisterFlightLoopCallback(triggers_cb, nullptr);
    // The worker touches statics of this translation unit — it must not outlive unload.
    join_runway_loader();
}
