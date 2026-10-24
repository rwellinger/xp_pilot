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
#include "runway_data.hpp"
#include "runway_geometry.hpp"
#include <XPLM/XPLMNavigation.h>
#include <XPLM/XPLMUtilities.h>
#include <ctime> // std::time, time_t
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace
{

std::string s_apt_dat_path; // X-Plane's global airport database
bool        s_analysis_enabled = true;

std::mutex          s_cache_mutex;
std::string         s_cache_icao; // guarded by s_cache_mutex
std::vector<Runway> s_cache;      // guarded by s_cache_mutex
std::thread         s_loader;
std::string         s_loader_icao; // flight-loop thread only

std::string s_nearest_id; // last nearest-airport answer, flight-loop thread only
time_t      s_nearest_checked_at = 0;

std::vector<Runway> cached_runways_for(const std::string &icao)
{
    std::lock_guard<std::mutex> lock(s_cache_mutex);
    return (icao == s_cache_icao) ? s_cache : std::vector<Runway>();
}

void place_on_runway(const std::vector<Runway> &runways, LandingData &landing)
{
    const RunwayFix fix     = RunwayGeometry::locate_touchdown(runways, landing.lat, landing.lon, landing.heading_true);
    landing.runway_ident    = fix.runway_ident;
    landing.runway_offset_m = fix.centerline_offset_m;
    landing.runway_distance_m = fix.distance_from_thr_m;
    landing.runway_length_m   = fix.runway_length_m;
}

// A rotorcraft set-down has no runway to be measured against, and an already
// resolved landing is left alone.
bool needs_runway_fix(const LandingData &landing)
{
    return !landing.is_rotorcraft && landing.runway_ident.empty() && !landing.airport_icao.empty();
}

std::set<std::string> airports_of(const FlightData &flight)
{
    std::set<std::string> icaos;
    if (!flight.departure_icao.empty())
        icaos.insert(flight.departure_icao);
    for (const auto &landing : flight.landings)
    {
        if (!landing.airport_icao.empty())
            icaos.insert(landing.airport_icao);
    }
    return icaos;
}

// -1 when the airport is missing from the global apt.dat — a custom-scenery field the
// scan can't see. Unknown counts as on the airport, which is what the ICAO claims.
float distance_to_airport(const std::map<std::string, AirportSurfaces> &surfaces_by_airport,
                          const std::string &icao, double lat, double lon)
{
    auto it = surfaces_by_airport.find(icao);
    if (it == surfaces_by_airport.end())
        return -1.f;
    return RunwayGeometry::distance_to_nearest_surface_m(it->second, lat, lon);
}

void join_loader()
{
    if (s_loader.joinable())
        s_loader.join();
    s_loader_icao.clear();
}

} // namespace

void AirportLookup::init(const std::string &apt_dat_path) { s_apt_dat_path = apt_dat_path; }

void AirportLookup::stop() { join_loader(); }

void AirportLookup::set_analysis_enabled(bool on) { s_analysis_enabled = on; }
bool AirportLookup::analysis_enabled() { return s_analysis_enabled; }

std::string AirportLookup::nearest_airport_id(double latitude, double longitude)
{
    // XPLMFindNavAid is expensive — cache result with 5-second TTL
    if (std::time(nullptr) - s_nearest_checked_at < 5)
        return s_nearest_id;
    return nearest_airport_id_now(latitude, longitude);
}

std::string AirportLookup::nearest_airport_id_now(double latitude, double longitude)
{
    s_nearest_checked_at = std::time(nullptr);

    float      lat = static_cast<float>(latitude);
    float      lon = static_cast<float>(longitude);
    XPLMNavRef ref = XPLMFindNavAid(nullptr, nullptr, &lat, &lon, nullptr, xplm_Nav_Airport);
    if (ref == XPLM_NAV_NOT_FOUND)
    {
        s_nearest_id.clear();
        return s_nearest_id;
    }
    char outID[32] = {};
    XPLMGetNavAidInfo(ref, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, outID, nullptr, nullptr);
    s_nearest_id = outID;
    return s_nearest_id;
}

void AirportLookup::request_runway_preload(const std::string &icao)
{
    if (!s_analysis_enabled || icao.empty() || icao == s_loader_icao)
        return;
    if (s_loader.joinable())
        return; // a load is still in flight; the next approach will pick this one up

    // The worker reads s_loader_icao and s_apt_dat_path rather than capturing them:
    // constructing captures could throw, and an exception escaping a thread entry
    // point terminates X-Plane. Both are written before the thread starts and only
    // rewritten after it has been joined.
    s_loader_icao = icao;
    s_loader      = std::thread(
        []() noexcept
        {
            try
            {
                const std::string &wanted = s_loader_icao;
                auto               found  = RunwayData::load_runways(s_apt_dat_path, {wanted});
                auto               it     = found.find(wanted);

                std::lock_guard<std::mutex> lock(s_cache_mutex);
                s_cache_icao = wanted;
                s_cache      = (it != found.end()) ? it->second : std::vector<Runway>();
            }
            catch (...)
            {
                XPLMDebugString("[xp_pilot] Runway preload failed\n");
            }
        });
}

void AirportLookup::apply_runway_fix(LandingData &landing)
{
    if (!s_analysis_enabled || landing.is_rotorcraft || landing.airport_icao.empty())
        return;

    const std::vector<Runway> runways = cached_runways_for(landing.airport_icao);
    if (runways.empty())
        return;

    place_on_runway(runways, landing);
}

void AirportLookup::resolve_ground_points(FlightData &flight)
{
    join_loader();
    const std::set<std::string> icaos = airports_of(flight);
    if (icaos.empty())
        return;

    const auto surfaces_by_airport = RunwayData::load_surfaces(s_apt_dat_path, icaos);
    if (surfaces_by_airport.empty())
    {
        XPLMDebugString("[xp_pilot] Airport lookup: no airport data found\n");
        return;
    }

    if (flight.departure_lat != 0.0 || flight.departure_lon != 0.0)
        flight.departure_distance_m =
            distance_to_airport(surfaces_by_airport, flight.departure_icao, flight.departure_lat, flight.departure_lon);

    for (auto &landing : flight.landings)
    {
        landing.airport_distance_m =
            distance_to_airport(surfaces_by_airport, landing.airport_icao, landing.lat, landing.lon);

        auto it = surfaces_by_airport.find(landing.airport_icao);
        if (s_analysis_enabled && needs_runway_fix(landing) && it != surfaces_by_airport.end())
            place_on_runway(it->second.runways, landing);
    }
}
