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

#pragma once

#include "html_report.hpp"
#include "runway_geometry.hpp"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

// The route a flight took, derived from its landings when it is displayed. Nothing
// here is stored: the logger records raw distances, and whether a landing was off-field
// is decided only now — so a wrong threshold is fixed by regenerating, not by losing data.
namespace FlightLegs
{

// Beyond this from every runway and helipad of the nearest airport, a landing is
// off-field: a rescue site on a road, a mountain hut, a field next to a lake.
inline constexpr float OFF_FIELD_DISTANCE_M = 500.f;

// Shorter than this on the ground is a touch-and-go rather than a stop.
inline constexpr int TOUCH_AND_GO_MAX_GROUND_SEC = 15;

// Unknown distances (-1, flights before v9) count as on the airport the ICAO names.
inline bool is_off_field(float airport_distance_m) { return airport_distance_m > OFF_FIELD_DISTANCE_M; }

struct Place
{
    std::string name;  // "LSZB" or "LS1" (landing site)
    std::string label; // "LSZB" or "LS1 (near Thun)"
    double      lat = 0, lon = 0;
    bool        off_field = false;
};

struct Leg
{
    Place  from, to;
    size_t landing_index = 0;
    int    airborne_sec  = 0; // 0 when the lift-off time is unknown
    int    ground_sec    = 0; // time on the ground at `to`; 0 after the final landing
    bool   touch_and_go  = false;
};

struct Route
{
    std::vector<Place> stops; // departure, every place visited in order, arrival
    std::vector<Leg>   legs;  // one per landing

    size_t intermediate_stops() const { return stops.size() > 2 ? stops.size() - 2 : 0; }
};

namespace detail
{

inline bool same_place(const Place &a, const Place &b)
{
    if (a.off_field != b.off_field)
        return false;
    if (!a.off_field)
        return a.name == b.name;
    return RunwayGeometry::distance_m(a.lat, a.lon, b.lat, b.lon) <= OFF_FIELD_DISTANCE_M;
}

inline std::string coordinates_text(double lat, double lon)
{
    char text[48];
    snprintf(text, sizeof(text), "%.3f%c %.3f%c", std::fabs(lat), lat >= 0 ? 'N' : 'S', std::fabs(lon),
             lon >= 0 ? 'E' : 'W');
    return text;
}

// Numbers off-field sites in the order they were first reached; returning to a site
// keeps its number, so "LS1 -> LS2 -> LS1" reads as a shuttle.
class SiteNamer
{
  public:
    Place name(Place place, const std::string &nearby_place)
    {
        for (const Place &known : sites_)
        {
            if (same_place(known, place))
                return known;
        }
        place.name  = "LS" + std::to_string(sites_.size() + 1);
        place.label = place.name + " (" + (nearby_place.empty() ? coordinates_text(place.lat, place.lon)
                                                                  : "near " + nearby_place) + ")";
        sites_.push_back(place);
        return place;
    }

  private:
    std::vector<Place> sites_;
};

// Logs from before per-landing airports were recorded leave the code empty.
inline bool is_unknown(const Place &place) { return !place.off_field && place.name == "?"; }

inline Place resolve_place(SiteNamer &namer, const std::string &icao, float distance_m, double lat, double lon,
                           const std::string &nearby_place)
{
    Place place;
    place.lat       = lat;
    place.lon       = lon;
    place.off_field = is_off_field(distance_m);
    if (place.off_field)
        return namer.name(place, nearby_place);

    place.name  = icao.empty() ? "?" : icao;
    place.label = place.name;
    return place;
}

// Flights before v9 recorded no lift-off point; the first track sample is close enough
// for the map, and their unknown distance keeps them on the departure airport anyway.
inline Place departure_place(SiteNamer &namer, const FlightData &flight)
{
    double lat = flight.departure_lat, lon = flight.departure_lon;
    if (lat == 0.0 && lon == 0.0 && !flight.track.empty())
    {
        lat = flight.track.front().lat;
        lon = flight.track.front().lon;
    }
    return resolve_place(namer, flight.departure_icao, flight.departure_distance_m, lat, lon,
                         flight.departure_nearby_place);
}

// Wall-clock span minus the sim pauses that fell inside it.
inline int active_seconds_between(const FlightData &flight, time_t from, time_t to)
{
    if (from <= 0 || to <= from)
        return 0;
    int seconds = static_cast<int>(to - from);
    for (const PauseEvent &pause : flight.pauses)
    {
        if (pause.t >= from && pause.t < to)
            seconds -= pause.sec;
    }
    return seconds > 0 ? seconds : 0;
}

inline Leg make_leg(const FlightData &flight, size_t index, const Place &from, const Place &to)
{
    const LandingData &landing = flight.landings[index];
    const time_t lifted_off = index == 0 ? flight.start_time : flight.landings[index - 1].liftoff_time;

    Leg leg;
    leg.from          = from;
    leg.to            = to;
    leg.landing_index = index;
    leg.airborne_sec  = active_seconds_between(flight, lifted_off, landing.time);
    leg.ground_sec    = active_seconds_between(flight, landing.time, landing.liftoff_time);
    leg.touch_and_go  = landing.liftoff_time > 0 && leg.ground_sec < TOUCH_AND_GO_MAX_GROUND_SEC;
    return leg;
}

} // namespace detail

// Consecutive landings at the same place collapse into one stop, so a circuit of
// touch-and-goes stays "LSZB -> LSZB" while a rescue mission reads
// "LSZB -> LS1 (near Thun) -> LS2 (near Spiez) -> LSZB".
inline Route derive_route(const FlightData &flight)
{
    detail::SiteNamer namer;
    Route             route;
    Place             previous = detail::departure_place(namer, flight);
    route.stops.push_back(previous);

    for (size_t i = 0; i < flight.landings.size(); ++i)
    {
        const LandingData &landing = flight.landings[i];
        const Place        here    = detail::resolve_place(namer, landing.airport_icao, landing.airport_distance_m,
                                                           landing.lat, landing.lon, landing.nearby_place);
        route.legs.push_back(detail::make_leg(flight, i, previous, here));
        if (!detail::is_unknown(here) && !detail::same_place(route.stops.back(), here))
            route.stops.push_back(here);
        previous = here;
    }

    // A round trip still names both ends; without a usable landing, the arrival the
    // logger recorded for the whole flight stands in.
    if (route.stops.size() == 1)
    {
        Place arrival = previous;
        if (flight.landings.empty() || detail::is_unknown(previous))
            arrival.name = arrival.label = flight.arrival_icao.empty() ? "?" : flight.arrival_icao;
        route.stops.push_back(arrival);
    }
    return route;
}

inline std::string route_text(const Route &route, const char *separator, bool full_labels)
{
    std::string text;
    for (const Place &stop : route.stops)
    {
        if (!text.empty())
            text += separator;
        text += full_labels ? stop.label : stop.name;
    }
    return text;
}

} // namespace FlightLegs
