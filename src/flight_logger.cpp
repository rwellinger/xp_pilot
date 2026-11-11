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

#include "airport_lookup.hpp"
#include "city_data.hpp"
#include "flight_logger.hpp"
#include "flight_logger_logic.hpp"
#include "flight_session.hpp"
#include "flight_state_machine.hpp"
#include "flight_store.hpp"
#include "flight_store_migration.hpp"
#include "html_report.hpp"
#include "landing_detector.hpp"
#include "sim_clock.hpp"
#include "ui_landing_popup.hpp"
#include "ui_overlay.hpp"
#include <XPLM/XPLMDataAccess.h>
#include <XPLM/XPLMPlugin.h>
#include <XPLM/XPLMProcessing.h>
#include <XPLM/XPLMUtilities.h>
#include <algorithm>
#include <cmath>
// Keep these explicit: MSVC needs them; Clang often pulls them in transitively and flags them unused.
#include <cstdio> // snprintf
#include <ctime>  // std::time, time_t
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <sstream>
#include <utility>

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
static std::string                               s_output_dir; // user data (flights, reports, index) in Output
static std::string                               s_preferences_dir; // settings file, in X-Plane's Output/preferences
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

// The profile the ICAO list names for this aircraft, or an empty string when the list
// does not cover it. Callers that only need a usable name go through get_profile_name().
static std::string find_listed_profile(const std::string &plane_icao)
{
    for (auto &e : s_icao_map)
        if (FlightLoggerLogic::icao_matches_profile(plane_icao, e.match))
            return e.profile_name;
    return "";
}

static std::string get_profile_name(const std::string &plane_icao)
{
    std::string listed = find_listed_profile(plane_icao);
    if (!listed.empty())
        return listed;
    return s_profiles.count("medium_ga") ? "medium_ga" : "fallback";
}

static std::array<int, 4> get_profile_thresholds(const std::string &name)
{
    auto it = s_profiles.find(name);
    if (it != s_profiles.end())
        return it->second;
    return {-125, -250, -350, -600};
}

static std::string get_profile_category(const std::string &name)
{
    auto it = s_profile_category.find(name);
    if (it != s_profile_category.end())
        return it->second;
    return "fixed_wing";
}

static std::map<std::string, FlightLoggerLogic::ProfileOverride> s_profile_overrides;

void FlightLogger::set_profile_overrides(std::map<std::string, FlightLoggerLogic::ProfileOverride> overrides)
{
    s_profile_overrides = std::move(overrides);
}

const std::map<std::string, FlightLoggerLogic::ProfileOverride> &FlightLogger::profile_overrides()
{
    return s_profile_overrides;
}

void FlightLogger::set_profile_override(const std::string                        &aircraft_icao,
                                        const FlightLoggerLogic::ProfileOverride &override_entry)
{
    if (!aircraft_icao.empty())
        s_profile_overrides[aircraft_icao] = override_entry;
}

void FlightLogger::clear_profile_override(const std::string &aircraft_icao)
{
    s_profile_overrides.erase(aircraft_icao);
}

std::vector<std::string> FlightLogger::available_profile_names()
{
    std::vector<std::string> names;
    names.reserve(s_profiles.size());
    for (const auto &[name, thresholds] : s_profiles)
        names.push_back(name);
    return names;
}

std::array<int, 4> FlightLogger::profile_thresholds(const std::string &profile_name)
{
    return get_profile_thresholds(profile_name);
}

// Build the profile the user assigned to this aircraft, if they assigned a usable one.
// A name no profile carries and thresholds that do not validate are both ignored: a
// typo must not leave the rating without thresholds, it degrades to the normal lookup.
static bool find_valid_override(const std::string &plane_icao, FlightLogger::ResolvedProfile &out)
{
    const auto *chosen = FlightLoggerLogic::find_profile_override(s_profile_overrides, plane_icao);
    if (!chosen)
        return false;

    out.source = FlightLogger::ProfileSource::UserOverride;
    if (chosen->is_custom)
    {
        if (!FlightLoggerLogic::are_valid_thresholds(chosen->thresholds))
            return false;
        out.name       = FlightLogger::CUSTOM_PROFILE_NAME;
        out.thresholds = chosen->thresholds;
        out.is_custom  = true;
        return true;
    }

    if (!s_profiles.count(chosen->profile_name))
        return false;
    out.name       = chosen->profile_name;
    out.thresholds = get_profile_thresholds(chosen->profile_name);
    out.is_custom  = false;
    return true;
}

// A bundled profile as the resolution chain hands it on.
static FlightLogger::ResolvedProfile bundled_profile(const std::string &name, FlightLogger::ProfileSource source)
{
    FlightLogger::ResolvedProfile resolved;
    resolved.name       = name;
    resolved.thresholds = get_profile_thresholds(name);
    resolved.source     = source;
    return resolved;
}

// A helicopter recognised only by the sim keeps the generic fixed-wing profile, whose
// thresholds are far too lax for a set-down. Fall back to the turbine profile so the
// rating stays meaningful until the type earns its own entry.
static FlightLogger::ResolvedProfile get_rating_profile(const std::string &plane_icao, bool is_rotorcraft)
{
    FlightLogger::ResolvedProfile chosen;
    if (find_valid_override(plane_icao, chosen))
        return chosen;

    const std::string listed = find_listed_profile(plane_icao);
    const auto        source =
        listed.empty() ? FlightLogger::ProfileSource::Fallback : FlightLogger::ProfileSource::IcaoList;
    const std::string name = get_profile_name(plane_icao);
    if (!is_rotorcraft || get_profile_category(name) == "rotorcraft")
        return bundled_profile(name, source);
    return s_profiles.count("turbine_helicopter") ? bundled_profile("turbine_helicopter", source)
                                                  : bundled_profile(name, source);
}

static std::string get_shutdown_trigger(const std::string &plane_icao)
{
    for (auto &e : s_icao_map)
        if (FlightLoggerLogic::icao_matches_profile(plane_icao, e.match) && !e.shutdown_trigger.empty())
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
static XPLMDataRef dr_is_heli      = nullptr; // int, X-Plane's own airframe category
static XPLMDataRef dr_roll         = nullptr; // deg, bank angle
static XPLMDataRef dr_yaw_rate     = nullptr; // deg/sec

// Airframe facts used to place an unlisted aircraft in a landing profile
static XPLMDataRef dr_acf_mass_max = nullptr; // float, kg, maximum takeoff mass
static XPLMDataRef dr_acf_num_eng  = nullptr; // int, engine count
static XPLMDataRef dr_acf_eng_type = nullptr; // int array, per-engine type

// Configuration at touchdown
static XPLMDataRef dr_gear_deploy   = nullptr; // float[10], 0 = up, 1 = down
static XPLMDataRef dr_gear_type     = nullptr; // int[10], 0 = leg not present
static XPLMDataRef dr_gear_retract  = nullptr; // int, airframe has retractable gear
static XPLMDataRef dr_flap_deploy   = nullptr; // ratio, actual flap system position
static XPLMDataRef dr_flap_handle   = nullptr; // ratio, flap handle position
static XPLMDataRef dr_speedbrake    = nullptr; // ratio, negative = ARMED
static XPLMDataRef dr_autopilot     = nullptr; // enum: off=0, flight director=1, on=2

// Weather at touchdown
static XPLMDataRef dr_visibility_sm = nullptr; // statute miles
static XPLMDataRef dr_cloud_base    = nullptr; // float[3], MSL meters
static XPLMDataRef dr_cloud_cover   = nullptr; // float[3], 0..1
static XPLMDataRef dr_oat           = nullptr; // deg C
static XPLMDataRef dr_precip        = nullptr; // ratio, rain on the windshield

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
    dr_is_heli      = XPLMFindDataRef("sim/aircraft2/metadata/is_helicopter");
    dr_acf_mass_max = XPLMFindDataRef("sim/aircraft/weight/acf_m_max");
    dr_acf_num_eng  = XPLMFindDataRef("sim/aircraft/engine/acf_num_engines");
    dr_acf_eng_type = XPLMFindDataRef("sim/aircraft/prop/acf_en_type");
    dr_roll         = XPLMFindDataRef("sim/flightmodel/position/phi");
    dr_yaw_rate     = XPLMFindDataRef("sim/flightmodel/position/R");

    dr_gear_deploy  = XPLMFindDataRef("sim/flightmodel2/gear/deploy_ratio");
    dr_gear_type    = XPLMFindDataRef("sim/aircraft/parts/acf_gear_type");
    dr_gear_retract = XPLMFindDataRef("sim/aircraft/gear/acf_gear_retract");
    dr_flap_deploy  = XPLMFindDataRef("sim/cockpit2/controls/flap_system_deploy_ratio");
    dr_flap_handle  = XPLMFindDataRef("sim/cockpit2/controls/flap_handle_request_ratio");
    dr_speedbrake   = XPLMFindDataRef("sim/cockpit2/controls/speedbrake_ratio");
    dr_autopilot    = XPLMFindDataRef("sim/cockpit/autopilot/autopilot_mode");

    dr_visibility_sm = XPLMFindDataRef("sim/weather/aircraft/visibility_reported_sm");
    dr_cloud_base    = XPLMFindDataRef("sim/weather/aircraft/cloud_base_msl_m");
    dr_cloud_cover   = XPLMFindDataRef("sim/weather/aircraft/cloud_coverage_percent");
    dr_oat           = XPLMFindDataRef("sim/weather/aircraft/temperature_ambient_deg_c");
    dr_precip        = XPLMFindDataRef("sim/weather/aircraft/precipitation_on_aircraft_ratio");
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

// Maximum engines X-Plane models; the type array is sized to match.
static constexpr int MAX_ENGINES = 8;

static FlightLoggerLogic::AirframeMetrics read_airframe_metrics()
{
    FlightLoggerLogic::AirframeMetrics metrics;
    metrics.max_takeoff_mass_kg = dr_f(dr_acf_mass_max);
    metrics.engine_count        = dr_i(dr_acf_num_eng);

    if (dr_acf_eng_type)
    {
        int types[MAX_ENGINES] = {};
        XPLMGetDatavi(dr_acf_eng_type, types, 0, MAX_ENGINES);
        // Engine 0 decides: X-Plane has no mixed-propulsion airframe whose engines
        // would disagree in a way the landing profile cares about.
        metrics.engine_kind = FlightLoggerLogic::engine_kind_from_dataref(types[0]);
    }
    return metrics;
}

// The profile a landing is rated against. The user's own assignment wins outright; an
// ICAO the list names comes next; only an unlisted fixed-wing type is classified from
// the airframe, and only when the sim reports a usable mass — otherwise the historical
// medium_ga fallback stands.
static FlightLogger::ResolvedProfile resolve_landing_profile(const std::string &plane_icao, bool is_rotorcraft)
{
    FlightLogger::ResolvedProfile chosen;
    if (find_valid_override(plane_icao, chosen))
        return chosen;

    if (is_rotorcraft)
        return get_rating_profile(plane_icao, true);

    const std::string listed = find_listed_profile(plane_icao);
    if (!listed.empty())
        return bundled_profile(listed, FlightLogger::ProfileSource::IcaoList);

    const std::string classified = FlightLoggerLogic::classify_fixed_wing_profile(read_airframe_metrics());
    if (!classified.empty() && s_profiles.count(classified))
        return bundled_profile(classified, FlightLogger::ProfileSource::AirframeClass);
    return bundled_profile(get_profile_name(plane_icao), FlightLogger::ProfileSource::Fallback);
}

// The profile a stored flight was rated against, for regenerating its report. The
// thresholds travel with the flight since v8, so a report regenerated after the user
// changed their assignment still reproduces the rating the landing was given. Flights
// written before that carry only a name, and those before v7 not even that — they fall
// back through the profile table and then the ICAO lookup, as they always did.
static FlightLogger::ResolvedProfile stored_flight_profile(const FlightData &flight)
{
    if (flight.has_landing_thresholds)
    {
        FlightLogger::ResolvedProfile stored;
        stored.name       = flight.landing_profile.empty() ? FlightLogger::CUSTOM_PROFILE_NAME
                                                           : flight.landing_profile;
        stored.thresholds = flight.landing_thresholds;
        stored.is_custom  = stored.name == FlightLogger::CUSTOM_PROFILE_NAME;
        return stored;
    }
    if (!flight.landing_profile.empty())
        return bundled_profile(flight.landing_profile, FlightLogger::ProfileSource::IcaoList);
    return get_rating_profile(flight.aircraft_icao, flight.aircraft_category == "rotorcraft");
}

static const char *engine_kind_label(FlightLoggerLogic::EngineKind kind)
{
    switch (kind)
    {
    case FlightLoggerLogic::EngineKind::Turbine:
        return "turbine";
    case FlightLoggerLogic::EngineKind::Jet:
        return "jet";
    case FlightLoggerLogic::EngineKind::Piston:
        break;
    }
    return "piston";
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

// X-Plane sizes both gear arrays at ten legs regardless of the airframe, and the unused
// slots read zero — so the least-extended *present* leg is what "gear down" means here.
static float gear_deploy_ratio_min()
{
    if (!dr_gear_deploy || !dr_gear_type)
        return 0.f;

    constexpr int MAX_GEAR_LEGS = 10;
    float         deploy[MAX_GEAR_LEGS] = {};
    int           type[MAX_GEAR_LEGS]   = {};
    XPLMGetDatavf(dr_gear_deploy, deploy, 0, MAX_GEAR_LEGS);
    XPLMGetDatavi(dr_gear_type, type, 0, MAX_GEAR_LEGS);

    float least = 1.f;
    bool  found = false;
    for (int leg = 0; leg < MAX_GEAR_LEGS; ++leg)
    {
        if (type[leg] == 0)
            continue;
        found = true;
        least = std::min(least, deploy[leg]);
    }
    return found ? least : 0.f;
}

// The reported ceiling: the lowest broken-or-worse layer, in feet above the given field
// elevation. Scattered and few layers don't make a ceiling and are ignored.
static bool ceiling_above_field_ft(float field_elevation_m, float &ceiling_ft_out)
{
    if (!dr_cloud_base || !dr_cloud_cover)
        return false;

    constexpr int   MAX_CLOUD_LAYERS   = 3;
    constexpr float BROKEN_COVERAGE    = 0.6f; // X-Plane reports coverage as 0..1
    float           base_msl_m[MAX_CLOUD_LAYERS] = {};
    float           coverage[MAX_CLOUD_LAYERS]   = {};
    XPLMGetDatavf(dr_cloud_base, base_msl_m, 0, MAX_CLOUD_LAYERS);
    XPLMGetDatavf(dr_cloud_cover, coverage, 0, MAX_CLOUD_LAYERS);

    bool  found        = false;
    float lowest_ft    = 0.f;
    for (int layer = 0; layer < MAX_CLOUD_LAYERS; ++layer)
    {
        if (coverage[layer] < BROKEN_COVERAGE)
            continue;
        const float above_field_ft = (base_msl_m[layer] - field_elevation_m) * 3.28084f;
        if (above_field_ft <= 0.f)
            continue;
        if (!found || above_field_ft < lowest_ft)
            lowest_ft = above_field_ft;
        found = true;
    }
    ceiling_ft_out = found ? lowest_ft : 0.f;
    return found;
}

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

// The lookup module has no datarefs of its own; this is the seam that feeds it the
// aircraft's current position.
static std::string get_airport_id() { return AirportLookup::nearest_airport_id(dr_d(dr_lat), dr_d(dr_lon)); }
static std::string get_airport_id_now() { return AirportLookup::nearest_airport_id_now(dr_d(dr_lat), dr_d(dr_lon)); }

// ── Feature toggles (persisted via the settings file) ────────────────────────
static bool s_write_enabled       = true;
static bool s_html_report_enabled = true;

void FlightLogger::set_write_enabled(bool on) { s_write_enabled = on; }
bool FlightLogger::write_enabled() { return s_write_enabled; }
void FlightLogger::set_html_report_enabled(bool on) { s_html_report_enabled = on; }
bool FlightLogger::html_report_enabled() { return s_html_report_enabled; }

// ════════════════════════════════════════════════════════════════
// LANDING POPUP REPLAY
// ════════════════════════════════════════════════════════════════

bool FlightLogger::replay_last_landing_popup()
{
    if (LandingPopup::replay_remembered())
        return true;

    LandingData last;
    if (!FlightStore::load_last_landing(s_output_dir, last))
    {
        Overlay::show("No landing recorded yet", 5.f, 1.f, 0.8f, 0.2f);
        return false;
    }
    LandingPopup::show(last);
    return true;
}

// ════════════════════════════════════════════════════════════════
// LANDING DETECTION
// ════════════════════════════════════════════════════════════════

static LandingDetector s_landing;

static void calc_wind(float spd, float wind_dir, float hdg, float &hw_out, float &xw_out)
{
    float angle = (wind_dir - hdg) * static_cast<float>(M_PI) / 180.f;
    hw_out      = spd * std::cos(angle);
    xw_out      = spd * std::sin(angle);
}


// ════════════════════════════════════════════════════════════════
// STATE MACHINE
// ════════════════════════════════════════════════════════════════

static FlightSession                     s_session;
static FlightStateMachine::GroundContact s_ground_contact;
static FlightStateMachine::PauseTracker  s_pause_tracker;
static std::string                       s_last_gnd_apt;
static int                               s_prev_any_eng = -1; // -1 = unknown

// Guards the rating paths against a session whose profile was never resolved, which
// would otherwise silently rate against the wrong thresholds.
static FlightLogger::ResolvedProfile session_landing_profile(const FlightSession &session)
{
    if (!session.landing_profile.name.empty())
        return session.landing_profile;
    return get_rating_profile(session.aircraft_icao, session.is_rotorcraft);
}

static void session_reset() { s_session.reset(); }

// Block time is the accumulated active (unpaused) flight time; the pause total is
// what the wall clock ran beyond it.
static int block_time_seconds() { return static_cast<int>(std::lround(s_session.active_seconds)); }
static int block_time_minutes() { return static_cast<int>(s_session.active_seconds / 60.0); }

static int paused_seconds()
{
    return FlightLoggerLogic::paused_seconds(s_session.total_seconds, s_session.active_seconds);
}

static void finalize_flight();
static void end_session_on_placement();

// ── Per-frame data shared across state handlers ──────────────────────────────

// The one place the state machine and landing detection read the sim, so every step of a
// frame sees the same values and the logic behind it can run on synthetic frames in tests.
static FrameInput read_frame()
{
    FrameInput f;
    f.ground_speed_mps   = dr_f(dr_gs);
    f.agl_m              = dr_f(dr_agl);
    f.local_time_sec     = dr_f(dr_localtime);
    f.g_force            = dr_f(dr_gforce);
    f.on_any_gear        = dr_i(dr_onground) != 0;
    f.on_all_gear        = dr_i(dr_onground_all) != 0;
    f.paused             = dr_i(dr_paused);
    f.in_replay          = dr_i(dr_in_replay);
    f.ias_kts            = dr_f(dr_ias);
    f.vertical_speed_fpm = dr_f(dr_vertfpm);
    f.latitude           = dr_d(dr_lat);
    f.longitude          = dr_d(dr_lon);
    f.elevation_m        = dr_d(dr_elevation);
    f.roll_deg           = dr_f(dr_roll);
    f.yaw_rate_deg_s     = dr_f(dr_yaw_rate);
    f.pitch_deg          = dr_f(dr_Q);
    f.pitch_rate_deg_s   = dr_f(dr_Qrad);
    f.flap_handle_ratio  = dr_f(dr_flap_handle);
    f.sim_seconds        = SimClock::seconds();
    f.wall_time          = std::time(nullptr);
    return f;
}

// The frame values go with every transition, so an unexpected recording can be
// explained from Log.txt alone.
static void log_transition(const char *transition, const FrameInput &f)
{
    char msg[160];
    snprintf(msg, sizeof(msg), "[xp_pilot] State: %s (on_gnd=%d agl=%.1f m gs=%.1f m/s)\n", transition,
             f.on_any_gear ? 1 : 0, static_cast<double>(f.agl_m), static_cast<double>(f.ground_speed_mps));
    XPLMDebugString(msg);
}

// ── Airport caching ──────────────────────────────────────────────────────────

static void cache_airport_when_stationary(const FrameInput &f)
{
    if (!f.on_any_gear || f.ground_speed_mps >= FlightStateMachine::GROUND_SPEED_TAXI_STOP_MPS)
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
        Overlay::show(std::string(label) + (apt.empty() ? "?" : apt), 4.f, 0.2f, 1.f, 0.4f);
    }
}

// ── State handlers ───────────────────────────────────────────────────────────






// How the aircraft was configured when it arrived. Recorded as context rather than as a
// rating criterion — "Flaps 30, gear down, speedbrake armed" explains a landing that the
// numbers alone don't.
static void fill_configuration(LandingData &ld)
{
    ld.has_configuration = true;
    ld.gear_retractable  = dr_i(dr_gear_retract) != 0;
    ld.gear_deploy_ratio = gear_deploy_ratio_min();
    ld.flap_ratio        = dr_f(dr_flap_deploy);
    ld.speedbrake_ratio  = dr_f(dr_speedbrake);
    // Flight-director-only counts as hand-flown; the master must be engaged for an autoland.
    ld.autopilot_engaged = dr_i(dr_autopilot) >= 2;
}

// Weather at the airport, not at the aircraft: the cloud base is reported MSL, so it only
// becomes a ceiling once referenced to the elevation the aircraft just touched down at.
static void fill_weather(LandingData &ld, const FrameInput &f)
{
    const float field_elevation_m = static_cast<float>(f.elevation_m) - f.agl_m;

    ld.visibility_m        = dr_f(dr_visibility_sm) * 1609.344f;
    ld.has_ceiling         = ceiling_above_field_ft(field_elevation_m, ld.ceiling_ft_agl);
    ld.oat_c               = dr_f(dr_oat);
    ld.precipitation_ratio = dr_f(dr_precip);
    ld.meteo = FlightLoggerLogic::is_instrument_conditions(ld.visibility_m, ld.ceiling_ft_agl, ld.has_ceiling)
                   ? MeteoCondition::Imc
                   : MeteoCondition::Vmc;
}

// What the sim knows about a touchdown beyond the frame: heading, wind, configuration and
// weather. The kinematics come from LandingDetection.
static void describe_touchdown_conditions(LandingData &ld, const FrameInput &f)
{
    const float wind_spd = dr_f(dr_wind_spd);
    const float wind_dir = dr_f(dr_wind_dir);
    const float magpsi   = dr_f(dr_magpsi);

    float hw = 0.f, xw = 0.f;
    calc_wind(wind_spd, wind_dir, magpsi, hw, xw);
    WindCondition wcond = (wind_spd < 3.f)   ? WindCondition::Calm
                          : (wind_spd < 6.f) ? WindCondition::Light
                                             : WindCondition::Steady;

    ld.heading_true   = dr_f(dr_truepsi);
    ld.wind_speed_kts = static_cast<int>(std::lround(wind_spd));
    ld.wind_dir_mag   = static_cast<int>(std::lround(wind_dir));
    ld.wind_status    = wind_condition_to_string(wcond);
    ld.headwind_kts   = static_cast<int>(std::lround(hw));
    ld.crosswind_kts  = static_cast<int>(std::lround(xw));
    ld.crosswind_side = (xw >= 0.f) ? "R" : "L";

    fill_configuration(ld);
    fill_weather(ld, f);
}

static bool flight_in_progress() { return s_session.in_progress(); }

// Logs each pause so an unexpected block time can be traced from Log.txt without
// picking the flight JSON apart.
static void record_pause_release(const FrameInput &f)
{
    if (!FlightStateMachine::record_pause_release(s_pause_tracker, s_session, f))
        return;

    char msg[128];
    snprintf(msg, sizeof(msg), "[xp_pilot] Sim resumed after %d s (excluded from block time)\n",
             s_session.pauses.back().sec);
    XPLMDebugString(msg);
}

static void announce(const char *message, float seconds)
{
    if (s_write_enabled)
        Overlay::show(message, seconds);
}

static void react_to_transition(FlightStateMachine::Transition transition, const FrameInput &f)
{
    using FlightStateMachine::Transition;
    switch (transition)
    {
    case Transition::None:
        return;
    case Transition::ShutdownToIdle:
        XPLMDebugString("[xp_pilot] State: Shutdown -> Idle\n");
        return;
    case Transition::IdleToAirborneRotorcraft:
    case Transition::RollingToAirborne:
        announce("REC  Flight recording started", 5.f);
        break;
    case Transition::TouchAndGo:
        announce("REC  Touch-and-Go", 4.f);
        break;
    case Transition::AirborneToLanded:
    case Transition::AirborneToLandedRotorcraft:
        LandingPopup::post(s_session.landings.back());
        break;
    case Transition::IdleToRolling:
    case Transition::LandedToShutdown:
        break;
    }
    log_transition(FlightStateMachine::transition_label(transition), f);
    if (transition == Transition::LandedToShutdown)
        finalize_flight();
}

// Set while an aircraft change is waiting to be reported. X-Plane blocks for as long as
// a plugin sits in the load message, so the callback only raises this flag and the work
// happens on the next flight loop, once the aircraft is up.
static bool s_airframe_log_pending = false;
// Same deferral for a placement: ending the session saves a flight, which is no work
// for the load message either.
static bool s_placement_pending = false;

// Report the airframe facts and the profile they resolve to. The profile is otherwise
// only visible as one line in the HTML report, which leaves a surprising rating with no
// way back to the data behind it — and the mass is logged raw so its unit stays checkable.
static void log_airframe_now()
{
    const std::string icao          = dr_str(dr_acf_icao);
    const bool        is_rotorcraft = dr_i(dr_is_heli) != 0;
    const auto        metrics       = read_airframe_metrics();
    const auto        profile       = resolve_landing_profile(icao, is_rotorcraft);
    const auto       &thresholds    = profile.thresholds;

    char msg[320];
    snprintf(msg, sizeof(msg),
             "[xp_pilot] Aircraft '%s': m_max=%.0f engines=%d %s%s -> profile %s [%d/%d/%d/%d]\n",
             icao.empty() ? "(no ICAO)" : icao.c_str(), static_cast<double>(metrics.max_takeoff_mass_kg),
             metrics.engine_count, engine_kind_label(metrics.engine_kind), is_rotorcraft ? " rotorcraft" : "",
             profile.name.c_str(), thresholds[0], thresholds[1], thresholds[2], thresholds[3]);
    XPLMDebugString(msg);
}

// The sim behind the state machine: datarefs, landing profiles and the airport lookup.
class SimEnvironment final : public FlightStateMachine::Environment
{
  public:
    FlightStateMachine::AircraftType identify_aircraft() const override
    {
        FlightStateMachine::AircraftType aircraft;
        aircraft.icao = dr_str(dr_acf_icao);
        // X-Plane knows the airframe category itself, so an unlisted ICAO no longer ends up
        // in the fixed-wing path — where a helicopter lifting off from a hover would never
        // reach the rolling speed that starts a recording.
        aircraft.is_rotorcraft =
            get_profile_category(get_profile_name(aircraft.icao)) == "rotorcraft" || dr_i(dr_is_heli) != 0;
        return aircraft;
    }

    std::string aircraft_tail() const override { return dr_str(dr_acf_tail); }

    FlightLogger::ResolvedProfile landing_profile(const std::string &icao, bool rotorcraft) const override
    {
        return resolve_landing_profile(icao, rotorcraft);
    }

    std::string departure_airport() const override
    {
        return !s_last_gnd_apt.empty() ? s_last_gnd_apt : get_airport_id();
    }

    std::string nearest_airport() const override { return get_airport_id(); }

    bool shutdown_triggered(const std::string &icao) const override { return ::shutdown_triggered(icao); }

    std::array<int, 4> rating_thresholds(const FlightSession &session) const override
    {
        return session_landing_profile(session).thresholds;
    }

    void describe_conditions(LandingData &landing, const FrameInput &frame) const override
    {
        describe_touchdown_conditions(landing, frame);
    }

    std::string landing_airport() const override { return get_airport_id_now(); }

    void apply_runway_fix(LandingData &landing) const override { AirportLookup::apply_runway_fix(landing); }

    void preload_runways() const override { AirportLookup::request_runway_preload(get_airport_id()); }
};

static const SimEnvironment s_environment{};

static float triggers_cb(float elapsed_real_sec, float, int, void *)
{
    // Ahead of the enabled check: the airframe line is a diagnostic and stays useful
    // with both logger features switched off.
    if (s_airframe_log_pending)
    {
        s_airframe_log_pending = false;
        log_airframe_now();
    }
    if (s_placement_pending)
    {
        s_placement_pending = false;
        end_session_on_placement();
        // Captured at the previous position; the next departure is wherever the aircraft is now.
        s_last_gnd_apt.clear();
    }

    // Both logger features off → no state machine work this frame.
    // Reset any mid-flight state so re-enabling starts cleanly from Idle.
    const bool logger_active = s_write_enabled || LandingPopup::enabled();
    if (!logger_active)
    {
        if (s_session.state != FlightState::Idle)
            session_reset();
        return -1.f;
    }

    const FrameInput f = read_frame();
    FlightStateMachine::accumulate_flight_time(s_session, f, elapsed_real_sec);
    record_pause_release(f);
    cache_airport_when_stationary(f);
    FlightStateMachine::track_ground_contact(s_ground_contact, f, elapsed_real_sec);
    handle_engine_edge_detection(f.on_any_gear);

    // The track only feeds the saved flight, so a popup-only session does not sample it.
    if (s_write_enabled && s_session.state == FlightState::Airborne)
        FlightStateMachine::record_track_sample(s_session, f);
    react_to_transition(FlightStateMachine::step(s_session, s_landing, s_ground_contact, f, s_environment), f);
    return -1.f;
}

// ════════════════════════════════════════════════════════════════
// JSON SAVE + FINALIZE
// ════════════════════════════════════════════════════════════════

// Assemble the current session state into a FlightData. Used both for the finished
// flight and — with end_time set to "now" — for the live in-progress snapshot.
static FlightData build_flight_data(time_t end_time)
{
    FlightData fd;
    char       buf[16];
    struct tm *tm = gmtime(&s_session.start_time);
    strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
    fd.date = buf;
    strftime(buf, sizeof(buf), "%H:%M", tm);
    fd.start_utc = buf;
    tm           = gmtime(&end_time);
    strftime(buf, sizeof(buf), "%H:%M", tm);
    fd.end_utc           = buf;
    fd.departure_icao           = s_session.departure_icao;
    fd.departure_lat            = s_session.departure_lat;
    fd.departure_lon            = s_session.departure_lon;
    fd.arrival_icao             = s_session.arrival_icao;
    fd.aircraft_icao            = s_session.aircraft_icao;
    fd.aircraft_tail            = s_session.aircraft_tail;
    fd.aircraft_category        = s_session.is_rotorcraft ? "rotorcraft" : "fixed_wing";
    const auto session_profile  = session_landing_profile(s_session);
    fd.landing_profile          = session_profile.name;
    fd.landing_thresholds       = session_profile.thresholds;
    fd.has_landing_thresholds   = true;
    fd.start_time               = s_session.start_time;
    fd.end_time          = end_time;
    fd.block_time_min    = block_time_minutes();
    fd.block_time_sec    = block_time_seconds();
    fd.paused_sec        = paused_seconds();
    fd.max_altitude_ft          = s_session.max_altitude_ft;
    fd.max_speed_kts            = s_session.max_speed_kts;
    fd.track                    = s_session.track;
    fd.landings                 = s_session.landings;
    fd.pauses                   = s_session.pauses;
    return fd;
}

// The largest place within roughly 20 km — what an off-field site is called by. A
// recognisable town reads better than the nearest hamlet.
static std::string nearby_place(double lat, double lon)
{
    constexpr double SEARCH_RADIUS_DEG = 0.2;
    const auto       places = CityData::load_cities(s_config_dir + "cities.dat",
                                                    {lat - SEARCH_RADIUS_DEG, lat + SEARCH_RADIUS_DEG,
                                                     lon - SEARCH_RADIUS_DEG, lon + SEARCH_RADIUS_DEG},
                                                    1);
    return places.empty() ? std::string() : places.front().name;
}

static void name_nearby_places(FlightData &fd)
{
    if (fd.departure_lat != 0.0 || fd.departure_lon != 0.0)
        fd.departure_nearby_place = nearby_place(fd.departure_lat, fd.departure_lon);
    for (auto &landing : fd.landings)
        landing.nearby_place = nearby_place(landing.lat, landing.lon);
}

static void save_flight(FlightData fd)
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
    if (fd.arrival_icao.empty())
        fd.arrival_icao = s_last_gnd_apt;

    AirportLookup::resolve_ground_points(fd);
    name_nearby_places(fd);

    const std::string filename = FlightStore::save(fd, s_output_dir);
    if (filename.empty())
    {
        Overlay::show("! Flight save ERROR", 8.f, 1.f, 0.2f, 0.2f);
        session_reset();
        return;
    }
    fd.filename = filename;

    if (s_html_report_enabled)
    {
        HtmlReport::generate(fd, s_output_dir, filename, fd.landing_profile, fd.landing_thresholds);
        HtmlReport::generate_index(s_output_dir);
    }

    std::string dep = fd.departure_icao.empty() ? "?" : fd.departure_icao;
    std::string arr = fd.arrival_icao.empty() ? "?" : fd.arrival_icao;
    Overlay::show("Flight saved: " + dep + " -> " + arr, 8.f, 0.2f, 1.f, 0.4f);

    s_lb_needs_refresh = true;
    session_reset();
}

static void finalize_flight() { save_flight(build_flight_data(s_session.end_time)); }

static void save_up_to_last_landing()
{
    s_session.active_seconds = s_session.active_seconds_at_last_landing;
    s_session.total_seconds  = s_session.total_seconds_at_last_landing;
    FlightData fd            = build_flight_data(s_session.landings.back().time);
    FlightLoggerLogic::cut_after_last_landing(fd);
    save_flight(std::move(fd));
}

static void discard_session()
{
    session_reset();
    if (s_write_enabled)
        Overlay::show("Recording discarded: aircraft repositioned", 6.f, 1.f, 0.6f, 0.2f);
}

static void end_session_on_placement()
{
    using FlightStateMachine::PlacementOutcome;
    switch (FlightStateMachine::placement_outcome(s_session))
    {
    case PlacementOutcome::None:
        return;
    case PlacementOutcome::SaveFlight:
        XPLMDebugString("[xp_pilot] Aircraft repositioned after landing – saving flight\n");
        s_session.end_time = std::time(nullptr);
        finalize_flight();
        return;
    case PlacementOutcome::SaveUpToLastLanding:
        XPLMDebugString("[xp_pilot] Aircraft repositioned in flight – saving up to the last landing\n");
        save_up_to_last_landing();
        return;
    case PlacementOutcome::Discard:
        XPLMDebugString("[xp_pilot] Aircraft repositioned before any landing – recording discarded\n");
        discard_session();
        return;
    }
}

// ════════════════════════════════════════════════════════════════
// PUBLIC API
// ════════════════════════════════════════════════════════════════

FlightLogger::LiveFlight FlightLogger::live_flight()
{
    LiveFlight live;
    if (!flight_in_progress())
        return live;

    live.in_progress            = true;
    live.flight                 = build_flight_data(std::time(nullptr));
    live.latitude               = dr_d(dr_lat);
    live.longitude              = dr_d(dr_lon);
    live.altitude_ft            = static_cast<int>(dr_d(dr_elevation) * 3.28084);
    live.indicated_airspeed_kts = static_cast<int>(std::lround(dr_f(dr_ias)));
    live.vertical_speed_fpm     = static_cast<int>(dr_f(dr_vertfpm));
    live.agl_ft                 = dr_f(dr_agl) * 3.28084f;
    live.heading_true           = dr_f(dr_truepsi);
    return live;
}

FlightLogger::CurrentAircraft FlightLogger::current_aircraft()
{
    CurrentAircraft aircraft;
    aircraft.icao          = dr_str(dr_acf_icao);
    aircraft.tail          = dr_str(dr_acf_tail);
    aircraft.is_rotorcraft = dr_i(dr_is_heli) != 0;
    aircraft.profile       = resolve_landing_profile(aircraft.icao, aircraft.is_rotorcraft);
    return aircraft;
}

const std::string &FlightLogger::output_dir() { return s_output_dir; }
const std::string &FlightLogger::preferences_dir() { return s_preferences_dir; }
const std::string &FlightLogger::config_dir() { return s_config_dir; }
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
        const auto  profile = stored_flight_profile(fd);
        HtmlReport::generate(fd, s_output_dir, fname, profile.name, profile.thresholds);
        ++count;
    }
    HtmlReport::generate_index(s_output_dir);
    char msg[64];
    snprintf(msg, sizeof(msg), "[xp_pilot] Regenerated %d reports\n", count);
    XPLMDebugString(msg);
    Overlay::show(std::string("Regenerated ") + std::to_string(count) + " reports", 5.f, 0.2f, 1.f, 0.4f);
}

void FlightLogger::note_aircraft_changed() { s_airframe_log_pending = true; }

void FlightLogger::note_aircraft_placed()
{
    s_ground_contact.note_placed();
    s_placement_pending = true;
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

    // User data (flights, reports, index) lives under X-Plane's Output dir
    // so it survives plugin updates. XPLMGetSystemPath returns the X-Plane root with
    // a trailing separator.
    char systemPathRaw[2048] = {};
    XPLMGetSystemPath(systemPathRaw);
    std::filesystem::path outputPath = std::filesystem::path(systemPathRaw) / "Output" / "x_pilot_reports";
    s_output_dir                     = outputPath.generic_string() + "/";

    // The settings file goes to X-Plane's conventional preferences folder, where
    // other plugins keep theirs and where users look for them.
    std::filesystem::path preferencesPath = std::filesystem::path(systemPathRaw) / "Output" / "preferences";
    s_preferences_dir                     = preferencesPath.generic_string() + "/";
    AirportLookup::init((std::filesystem::path(systemPathRaw) / "Global Scenery" / "Global Airports" /
                         "Earth nav data" / "apt.dat")
                            .generic_string());
    XPLMDebugString(("[xp_pilot] config_dir: " + s_config_dir + "\n").c_str());
    XPLMDebugString(("[xp_pilot] output_dir: " + s_output_dir + "\n").c_str());
    XPLMDebugString(("[xp_pilot] preferences_dir: " + s_preferences_dir + "\n").c_str());

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
    ec.clear();
    std::filesystem::create_directories(preferencesPath, ec);
    if (ec)
        XPLMDebugString(("[xp_pilot] WARNING: cannot create preferences dir: " + ec.message() + "\n").c_str());

    FlightStore::migrate_user_data_to_output(configPath, outputPath);

    find_datarefs();
    load_profiles();
    XPLMRegisterFlightLoopCallback(triggers_cb, -1.f, nullptr);
    XPLMDebugString("[xp_pilot] FlightLogger initialized\n");
}

void FlightLogger::stop()
{
    XPLMUnregisterFlightLoopCallback(triggers_cb, nullptr);
    // The worker touches statics of this translation unit — it must not outlive unload.
    AirportLookup::stop();
}
