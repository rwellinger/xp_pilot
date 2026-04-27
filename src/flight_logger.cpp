#include "flight_logger.hpp"
#include "html_report.hpp"
#include <XPLM/XPLMDataAccess.h>
#include <XPLM/XPLMDisplay.h>
#include <XPLM/XPLMGraphics.h>
#include <XPLM/XPLMNavigation.h>
#include <XPLM/XPLMPlugin.h>
#include <XPLM/XPLMProcessing.h>
#include <XPLM/XPLMUtilities.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <json.hpp>
#include <sstream>

using json = nlohmann::json;

// ════════════════════════════════════════════════════════════════
// PROFILES
// ════════════════════════════════════════════════════════════════

struct ProfileEntry
{
    std::string match, profile_name, shutdown_trigger;
};

static std::map<std::string, std::array<int, 4>> s_profiles;
static std::vector<ProfileEntry>                 s_icao_map;
static std::string                               s_default_shutdown = "engine";
static std::string                               s_data_dir;
static bool                                      s_lb_needs_refresh = true;

static void load_profiles()
{
    std::string path = s_data_dir + "flight_logger_profiles.json";

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
            for (auto &[name, arr] : j["profiles"].items())
            {
                if (arr.is_array() && arr.size() == 4)
                {
                    s_profiles[name] = {arr[0].get<int>(), arr[1].get<int>(), arr[2].get<int>(), arr[3].get<int>()};
                }
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
static XPLMDataRef dr_ias          = nullptr; // m/s
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
    XPLMGetDatab(dr, buf, 0, (int)sizeof(buf) - 1);
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

    float      lat = (float)dr_d(dr_lat);
    float      lon = (float)dr_d(dr_lon);
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

static double monotonic_clock()
{
    static XPLMDataRef dr = XPLMFindDataRef("sim/time/total_running_time_sec");
    return (double)XPLMGetDataf(dr);
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
    int   y    = (int)((float)sh * 0.12f);
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

void FlightLogger::draw_popup()
{
    if (!popup_active())
        return;
    if (!s_landing_popup_enabled)
        return;

    int sw = 0, sh = 0;
    XPLMGetScreenSize(&sw, &sh);

    static auto rating_col = [](const std::string &r) -> ImVec4
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
    };

    const float popup_w = 430.f;
    ImGui::SetNextWindowPos(ImVec2(((float)sw - popup_w) * 0.5f, (float)sh * 0.12f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(popup_w, 0.f), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.38f, 0.42f, 0.48f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.70f, 0.80f, 0.90f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.f, 14.f));

    ImGui::Begin("##landing_popup", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);

    const float content_w = ImGui::GetContentRegionAvail().x;
    char        buf[128];

    auto center_text = [&](const char *txt)
    {
        float tw = ImGui::CalcTextSize(txt).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (content_w - tw) * 0.5f);
        ImGui::TextUnformatted(txt);
    };

    // Line 1: Vertical Speed + G-force
    snprintf(buf, sizeof(buf), "Vertical Speed: %.2fFPM / %.2fG", s_popup_ld.fpm, s_popup_ld.g_force);
    center_text(buf);

    // Line 2: Flare quality
    center_text(s_popup_ld.flare.c_str());

    // Line 3: Nose pitch rate + float time
    snprintf(buf, sizeof(buf), "Nose: %.2f deg/sec | Float: %.2f secs", s_popup_ld.pitch_rate, s_popup_ld.float_time);
    center_text(buf);

    // Line 4 (optional): Bounce-Anzahl, falls aufgetreten
    if (s_popup_ld.bounce_count > 0)
    {
        snprintf(buf, sizeof(buf), "Bounces: %d", s_popup_ld.bounce_count);
        center_text(buf);
    }

    ImGui::Spacing();

    // Line 5: Rating (colored, slightly larger)
    ImGui::PushStyleColor(ImGuiCol_Text, rating_col(s_popup_ld.rating));
    ImGui::SetWindowFontScale(1.2f);
    float tw = ImGui::CalcTextSize(s_popup_ld.rating.c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (content_w - tw) * 0.5f);
    ImGui::TextUnformatted(s_popup_ld.rating.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

static void show_popup(const LandingData &ld)
{
    s_popup_ld     = ld;
    s_popup_active = true;
    s_popup_until  = monotonic_clock() + 15.0;
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
        while ((int)vals.size() > size)
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
        return s / (float)vals.size();
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
    if (eff_fpm >= (float)p[0] && eff_fpm <= 0.f)
        return "BUTTER!";
    if (eff_fpm >= (float)p[1] && eff_fpm < (float)p[0])
        return "GREAT LANDING!";
    if (eff_fpm >= (float)p[2] && eff_fpm < (float)p[1])
        return "ACCEPTABLE";
    if (eff_fpm >= (float)p[3] && eff_fpm < (float)p[2])
        return "HARD LANDING!";
    return "WASTED!";
}

static void calc_wind(float spd, float wind_dir, float hdg, float &hw_out, float &xw_out)
{
    float angle = (wind_dir - hdg) * (float)M_PI / 180.f;
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
static time_t                   s_start_time      = 0;
static time_t                   s_end_time        = 0;
static int                      s_max_altitude_ft = 0;
static int                      s_max_speed_kts   = 0;
static std::vector<TrackPoint>  s_track;
static std::vector<LandingData> s_landings;
static std::string              s_last_gnd_apt;
static int                      s_prev_any_eng  = -1; // -1 = unknown
static time_t                   s_last_sample_t = 0;

static constexpr float GS_ROLLING_MPS   = 15.4f; // 30 kts
static constexpr float GS_TAXI_STOP_MPS = 2.6f;  // 5 kts
static constexpr float AGL_AIRBORNE_M   = 15.0f; // ~50 ft

static void session_reset()
{
    s_state = State::Idle;
    s_departure_icao.clear();
    s_arrival_icao.clear();
    s_aircraft_icao.clear();
    s_aircraft_tail.clear();
    s_start_time = s_end_time = 0;
    s_max_altitude_ft = s_max_speed_kts = 0;
    s_track.clear();
    s_landings.clear();
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
    if (f.gs <= GS_ROLLING_MPS || !f.on_gnd)
        return;

    s_aircraft_icao  = dr_str(dr_acf_icao);
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

    s_last_sample_t = std::time(nullptr);
    if (s_departure_icao.empty())
        s_departure_icao = get_airport_id();
    s_state = State::Airborne;
    landing_arm();
    if (s_write_enabled)
        show_overlay("REC  Flight recording started", 5.f);
    XPLMDebugString("[xp_pilot] State: Rolling -> Airborne\n");
}

// Sample track/max-stats every 10 seconds while airborne (writer only).
static void update_track_sample()
{
    if (!s_write_enabled)
        return;
    time_t now = std::time(nullptr);
    if (now - s_last_sample_t < 10)
        return;

    s_last_sample_t    = now;
    int        alt_ft  = (int)(dr_d(dr_elevation) * 3.28084);
    int        spd_kts = (int)(dr_f(dr_ias) * 1.94384f);
    int        vs      = (int)dr_f(dr_vertfpm);
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
    const bool is_bounce =
        s_ld_captured_valid && s_main_gear_lifted && s_max_agl_since_td < BOUNCE_AGL_LIMIT_FT;
    const bool is_hop = s_ld_captured_valid && !is_bounce;
    if (is_hop)
    {
        s_main_gear_lifted = false;
        s_max_agl_since_td = 0.f;
        return;
    }

    const float vertfpm  = dr_f(dr_vertfpm);
    const float wind_spd = dr_f(dr_wind_spd);
    const float wind_dir = dr_f(dr_wind_dir);
    const float magpsi   = dr_f(dr_magpsi);
    const float Q        = dr_f(dr_Q);
    const float Qrad     = dr_f(dr_Qrad);

    const float tspan = s_agl_buf.tspan();
    float       gVS   = vertfpm;
    if (tspan > 0.f)
        gVS = ((f.agl - s_agl_buf.avg()) / (tspan / 2.f)) * 196.85f;
    if (s_float_timer > 0.f && s_float_final == 0.f)
        s_float_final = (float)monotonic_clock() - s_float_timer;

    const float fpm_mag = std::abs(gVS);

    if (is_bounce)
    {
        ++s_bounce_count;
        s_main_gear_lifted = false;
        s_max_agl_since_td = 0.f;
        // Nur überschreiben, wenn dieser Touchdown härter war als der bisher schlechteste.
        if (fpm_mag <= s_worst_fpm_mag)
            return;
    }

    float hw = 0.f, xw = 0.f;
    calc_wind(wind_spd, wind_dir, magpsi, hw, xw);
    WindCondition wcond = (wind_spd < 3.f)   ? WindCondition::Calm
                          : (wind_spd < 6.f) ? WindCondition::Light
                                             : WindCondition::Steady;

    s_ld_captured.fpm            = gVS;
    s_ld_captured.g_force        = s_g_buf.avg();
    s_ld_captured.pitch_deg      = Q;
    s_ld_captured.pitch_rate     = Qrad;
    s_ld_captured.agl_ft         = f.agl * 3.28084f;
    s_ld_captured.float_time     = s_float_final;
    s_ld_captured.flare          = eval_flare(Q, Qrad);
    s_ld_captured.wind_speed_kts = (int)std::lround(wind_spd);
    s_ld_captured.wind_dir_mag   = (int)std::lround(wind_dir);
    s_ld_captured.wind_status    = wind_condition_to_string(wcond);
    s_ld_captured.headwind_kts   = (int)std::lround(hw);
    s_ld_captured.crosswind_kts  = (int)std::lround(xw);
    s_ld_captured.crosswind_side = (xw >= 0.f) ? "R" : "L";
    s_ld_captured_valid          = true;
    s_worst_fpm_mag              = fpm_mag;
}

// Finalize landing once the nose gear touches down after the mains.
static void finalize_landing_on_nose_gear(bool on_all)
{
    if (!s_ld_armed || !s_ld_captured_valid || s_prev_on_all || !on_all)
        return;

    auto pname   = FlightLogger::get_profile_name(s_aircraft_icao);
    auto pthresh = FlightLogger::get_profile_thresholds(pname);
    s_ld_captured.rating =
        eval_rating(s_ld_captured.fpm, (float)s_ld_captured.crosswind_kts, s_ld_captured.wind_status, pthresh);
    s_ld_captured.time         = std::time(nullptr);
    s_ld_captured.bounce_count = s_bounce_count;
    s_landings.push_back(s_ld_captured);
    s_arrival_icao = get_airport_id();
    s_state        = State::Landed;
    show_popup(s_ld_captured);
    landing_arm();
    XPLMDebugString("[xp_pilot] State: Airborne -> Landed\n");
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

    if (s_ld_armed && f.agl <= 15.f && s_float_timer == 0.f)
        s_float_timer = (float)monotonic_clock();

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
    if (f.agl > AGL_AIRBORNE_M)
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

static float triggers_cb(float, float, int, void *)
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
    std::string fdir = s_data_dir + "flights/";
    std::string path = fdir + base + ".json";
    // Avoid overwrite
    if (std::ifstream(path).good())
    {
        path = fdir + base + "_" + std::to_string(s_start_time) + ".json";
    }

    json obj;
    obj["version"]         = 1;
    obj["date"]            = date_buf;
    obj["start_utc"]       = sut;
    obj["end_utc"]         = eut;
    obj["departure_icao"]  = dep;
    obj["arrival_icao"]    = arr;
    obj["aircraft_icao"]   = icao;
    obj["aircraft_tail"]   = s_aircraft_tail;
    obj["start_time"]      = (long long)s_start_time;
    obj["end_time"]        = (long long)s_end_time;
    obj["block_time_min"]  = (int)((s_end_time - s_start_time) / 60);
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

    json ldg_arr = json::array();
    for (auto &ld : s_landings)
    {
        ldg_arr.push_back({{"fpm", ld.fpm},
                           {"g_force", ld.g_force},
                           {"pitch_deg", ld.pitch_deg},
                           {"pitch_rate", ld.pitch_rate},
                           {"agl_ft", ld.agl_ft},
                           {"float_time", ld.float_time},
                           {"time", (long long)ld.time},
                           {"wind_speed_kts", ld.wind_speed_kts},
                           {"wind_dir_mag", ld.wind_dir_mag},
                           {"wind_status", ld.wind_status},
                           {"headwind_kts", ld.headwind_kts},
                           {"crosswind_kts", ld.crosswind_kts},
                           {"crosswind_side", ld.crosswind_side},
                           {"bounce_count", ld.bounce_count},
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
    fd.departure_icao  = s_departure_icao;
    fd.arrival_icao    = s_arrival_icao;
    fd.aircraft_icao   = s_aircraft_icao;
    fd.aircraft_tail   = s_aircraft_tail;
    fd.start_time      = s_start_time;
    fd.end_time        = s_end_time;
    fd.block_time_min  = (int)((s_end_time - s_start_time) / 60);
    fd.max_altitude_ft = s_max_altitude_ft;
    fd.max_speed_kts   = s_max_speed_kts;
    fd.track           = s_track;
    fd.landings        = s_landings;

    if (s_html_report_enabled)
    {
        auto pname   = FlightLogger::get_profile_name(s_aircraft_icao);
        auto pthresh = FlightLogger::get_profile_thresholds(pname);
        HtmlReport::generate(fd, s_data_dir, filename, pname, pthresh);
        HtmlReport::generate_index(s_data_dir);
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

const std::string &FlightLogger::data_dir() { return s_data_dir; }
bool              &FlightLogger::lb_needs_refresh() { return s_lb_needs_refresh; }

void FlightLogger::regen_all_reports()
{
    std::string fdir = s_data_dir + "flights/";
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
        HtmlReport::generate(fd, s_data_dir, fname, pname, pthresh);
        ++count;
    }
    HtmlReport::generate_index(s_data_dir);
    char msg[64];
    snprintf(msg, sizeof(msg), "[xp_pilot] Regenerated %d reports\n", count);
    XPLMDebugString(msg);
    show_overlay(std::string("Regenerated ") + std::to_string(count) + " reports", 5.f, 0.2f, 1.f, 0.4f);
}

void FlightLogger::init()
{
    // Determine data directory relative to the plugin binary.
    // XPLMGetPluginInfo returns an HFS path on macOS (colon-separated,
    // e.g. "Macintosh HD:Users:foo:X-Plane 12:Resources:plugins:xp_pilot:mac_x64:xp_pilot.xpl").
    // Convert to POSIX by stripping the volume name and replacing colons with slashes.
    char pluginPathRaw[2048] = {};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, pluginPathRaw, nullptr, nullptr);
    std::string p(pluginPathRaw);
#if defined(__APPLE__)
    // macOS may return an HFS path (colon-separated, no slashes) — convert to POSIX
    if (p.find(':') != std::string::npos && p.find('/') == std::string::npos)
    {
        auto        colon = p.find(':');
        std::string posix = p.substr(colon + 1);
        for (char &c : posix)
            if (c == ':')
                c = '/';
        p = "/" + posix;
    }
#endif
    // Strip filename (xp_pilot.xpl) then platform directory (mac_x64 / win_x64)
    std::filesystem::path dataPath = std::filesystem::path(p).parent_path().parent_path() / "data";
    s_data_dir                     = dataPath.generic_string() + "/";
    std::filesystem::create_directories(dataPath / "flights");
    std::filesystem::create_directories(dataPath / "reports");

    find_datarefs();
    load_profiles();
    XPLMRegisterFlightLoopCallback(triggers_cb, -1.f, nullptr);
    XPLMDebugString("[xp_pilot] FlightLogger initialized\n");
}

void FlightLogger::stop() { XPLMUnregisterFlightLoopCallback(triggers_cb, nullptr); }
