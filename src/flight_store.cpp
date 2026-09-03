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

#include "flight_store.hpp"
#include <algorithm>
#include <cstdio> // snprintf
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace
{

// Empty codes are written as placeholders rather than blanks. Logs going back to
// version 1 carry these, and the filename is built from them too.
std::string or_placeholder(const std::string &code, const char *fallback)
{
    return code.empty() ? fallback : code;
}

json track_to_json(const std::vector<TrackPoint> &track)
{
    json points = json::array();
    for (const auto &tp : track)
    {
        points.push_back({{"t", tp.t},
                          {"lat", tp.lat},
                          {"lon", tp.lon},
                          {"alt", tp.alt_ft},
                          {"spd", tp.spd_kts},
                          {"vs", tp.vs_fpm}});
    }
    return points;
}

json pauses_to_json(const std::vector<PauseEvent> &pauses)
{
    json entries = json::array();
    for (const auto &p : pauses)
        entries.push_back({{"t", (long long)p.t}, {"sec", p.sec}, {"lat", p.lat}, {"lon", p.lon}});
    return entries;
}

json landings_to_json(const std::vector<LandingData> &landings)
{
    json entries = json::array();
    for (const auto &ld : landings)
    {
        entries.push_back({{"fpm", ld.fpm},
                           {"g_force", ld.g_force},
                           {"pitch_deg", ld.pitch_deg},
                           {"pitch_rate", ld.pitch_rate},
                           {"agl_ft", ld.agl_ft},
                           {"gate_ias_kts", ld.gate_ias_kts},
                           {"gate_fpm", ld.gate_fpm},
                           {"float_time", ld.float_time},
                           {"ias_kts", ld.ias_kts},
                           {"ground_speed_kts", ld.ground_speed_kts},
                           {"bank_deg", ld.bank_deg},
                           {"yaw_rate_deg_s", ld.yaw_rate_deg_s},
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
                           {"has_configuration", ld.has_configuration},
                           {"gear_retractable", ld.gear_retractable},
                           {"gear_deploy_ratio", ld.gear_deploy_ratio},
                           {"flap_ratio", ld.flap_ratio},
                           {"gate_flap_ratio", ld.gate_flap_ratio},
                           {"speedbrake_ratio", ld.speedbrake_ratio},
                           {"autopilot_engaged", ld.autopilot_engaged},
                           {"meteo", meteo_condition_to_string(ld.meteo)},
                           {"visibility_m", ld.visibility_m},
                           {"ceiling_ft_agl", ld.ceiling_ft_agl},
                           {"has_ceiling", ld.has_ceiling},
                           {"oat_c", ld.oat_c},
                           {"precipitation_ratio", ld.precipitation_ratio},
                           {"flare", ld.flare},
                           {"rating", ld.rating}});
    }
    return entries;
}

} // namespace

std::string FlightStore::save(const FlightData &flight, const std::string &output_dir)
{
    const std::string departure = or_placeholder(flight.departure_icao, "ZZZZ");
    const std::string arrival   = or_placeholder(flight.arrival_icao, "ZZZZ");
    const std::string aircraft  = or_placeholder(flight.aircraft_icao, "UNKN");

    char base[256];
    snprintf(base, sizeof(base), "%s_%s_%s_%s", flight.date.c_str(), departure.c_str(), arrival.c_str(),
             aircraft.c_str());

    const std::string flights_dir = output_dir + "flights/";
    std::string       path        = flights_dir + base + ".json";
    // Avoid overwrite
    if (std::ifstream(path).good())
        path = flights_dir + base + "_" + std::to_string(flight.start_time) + ".json";

    json obj;
    obj["version"]           = 8;
    obj["date"]              = flight.date;
    obj["start_utc"]         = flight.start_utc;
    obj["end_utc"]           = flight.end_utc;
    obj["departure_icao"]    = departure;
    obj["arrival_icao"]      = arrival;
    obj["aircraft_icao"]     = aircraft;
    obj["aircraft_tail"]     = flight.aircraft_tail;
    obj["aircraft_category"] = flight.aircraft_category;
    obj["landing_profile"]   = flight.landing_profile;
    // The thresholds travel with the flight so a regenerated report cannot disagree with
    // the logbook about the same landing, and so custom thresholds — which have no
    // profile name to look up — survive at all.
    obj["landing_thresholds"] = flight.landing_thresholds;
    obj["start_time"]        = (long long)flight.start_time;
    obj["end_time"]          = (long long)flight.end_time;
    obj["block_time_min"]    = flight.block_time_min;
    obj["block_time_sec"]    = flight.block_time_sec;
    obj["paused_sec"]        = flight.paused_sec;
    obj["max_altitude_ft"]   = flight.max_altitude_ft;
    obj["max_speed_kts"]     = flight.max_speed_kts;
    obj["fuel_used_kg"]      = 0;
    obj["track"]             = track_to_json(flight.track);
    obj["pauses"]            = pauses_to_json(flight.pauses);
    obj["landings"]          = landings_to_json(flight.landings);

    std::ofstream f(path);
    if (!f.is_open())
        return "";
    f << obj.dump();

    return path.substr(path.rfind('/') + 1);
}

bool FlightStore::load_last_landing(const std::string &output_dir, LandingData &out)
{
    // Filenames start with the date, so descending name order visits the most recent
    // flights first.
    const std::string        flights_dir = output_dir + "flights/";
    std::vector<std::string> filenames;

    std::error_code ec;
    auto            entries = std::filesystem::directory_iterator(flights_dir, ec);
    if (ec)
        return false;
    for (const auto &entry : entries)
    {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && name.size() > 5 && name.substr(name.size() - 5) == ".json")
            filenames.push_back(name);
    }
    std::sort(filenames.rbegin(), filenames.rend());

    for (const auto &name : filenames)
    {
        std::ifstream f(flights_dir + name);
        if (!f.is_open())
            continue;
        const std::string content((std::istreambuf_iterator<char>(f)), {});
        const FlightData  flight = parse_flight_json(content, name);
        if (!flight.landings.empty())
        {
            out = flight.landings.back();
            return true;
        }
    }
    return false;
}
