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

#include "flight_summary.hpp"
#include "flight_legs.hpp"
#include "flight_logger_logic.hpp"
#include "runway_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio> // snprintf
#include <ctime>
#include <map>
#include <set>
#include <utility>

using FlightSummary::LoggedFlight;
using FlightSummary::Period;
using FlightSummary::Ranked;

namespace
{

constexpr double METERS_PER_NM = 1852.0;
constexpr int    RATING_COUNT  = 5;

struct YearMonth
{
    int year  = 0;
    int month = 0;
};

// -1 unless every character in the range is a digit.
int digits_value(const std::string &text, size_t from, size_t count)
{
    int value = 0;
    for (size_t i = from; i < from + count; ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(text[i])))
            return -1;
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

// Flights store "YYYY-MM-DD"; hand-edited or very old logs may hold "?" instead.
bool parse_year_month(const std::string &date, YearMonth &out)
{
    if (date.size() < 7 || date[4] != '-')
        return false;
    const int year  = digits_value(date, 0, 4);
    const int month = digits_value(date, 5, 2);
    if (year < 0 || month < 1 || month > 12)
        return false;
    out = {year, month};
    return true;
}

const char *month_name(int month)
{
    static const char *names[] = {"January", "February", "March",     "April",   "May",      "June",
                                  "July",    "August",   "September", "October", "November", "December"};
    return names[std::clamp(month, 1, 12) - 1];
}

std::string aircraft_of(const FlightData &flight) { return flight.aircraft_icao.empty() ? "?" : flight.aircraft_icao; }

bool matches(const FlightData &flight, const FlightSummary::Filter &filter)
{
    if (!filter.period.contains(flight.date))
        return false;
    return filter.aircraft_icao.empty() || aircraft_of(flight) == filter.aircraft_icao;
}

bool is_airport(const FlightLegs::Place &place) { return !place.off_field && place.name != "?"; }

// Off-field sites are numbered per flight, so "LS1" names a different place in every
// flight and must not be grouped across them.
std::string route_end_name(const FlightLegs::Place &place) { return place.off_field ? "off-field" : place.name; }

std::string route_key(const FlightLegs::Route &route)
{
    return route_end_name(route.stops.front()) + "-" + route_end_name(route.stops.back());
}

std::vector<Ranked> top_ranked(const std::map<std::string, int> &counts)
{
    std::vector<Ranked> ranked;
    ranked.reserve(counts.size());
    for (const auto &[name, count] : counts)
        ranked.push_back({name, count});
    // Ties stay alphabetical: std::map already handed them over in that order.
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) { return a.count > b.count; });
    if (ranked.size() > FlightSummary::TOP_LIST_SIZE)
        ranked.resize(FlightSummary::TOP_LIST_SIZE);
    return ranked;
}

std::vector<Ranked> empty_rating_counts()
{
    std::vector<Ranked> ratings;
    ratings.reserve(RATING_COUNT);
    for (int i = 0; i < RATING_COUNT; ++i)
        ratings.push_back({FlightLoggerLogic::rating_label(i), 0});
    return ratings;
}

void count_rating(std::vector<Ranked> &ratings, const std::string &rating)
{
    for (Ranked &entry : ratings)
    {
        if (entry.name == rating)
        {
            ++entry.count;
            return;
        }
    }
}

// Collects everything that has to be grouped or compared across flights before it can
// be written into the summary.
class Accumulator
{
  public:
    Accumulator() { summary_.ratings = empty_rating_counts(); }

    void add(const LoggedFlight &logged)
    {
        const FlightData &flight = logged.flight;
        summary_.flights += 1;
        summary_.block_time_sec += flight.block_time_sec;
        summary_.distance_nm += logged.distance_nm;
        summary_.flight_list.push_back(flight);

        add_aircraft(flight);
        add_places(flight);
        for (const LandingData &landing : flight.landings)
            add_landing(landing);
    }

    FlightSummary::Summary finish()
    {
        summary_.airports     = static_cast<int>(airports_.size());
        summary_.top_airports = top_ranked(airport_visits_);
        summary_.top_routes   = top_ranked(route_counts_);
        if (summary_.landings > 0)
            summary_.average_fpm = static_cast<float>(fpm_sum_ / summary_.landings);

        for (auto &[icao, totals] : aircraft_)
            summary_.aircraft.push_back(totals);
        std::stable_sort(summary_.aircraft.begin(), summary_.aircraft.end(),
                         [](const FlightSummary::AircraftTotals &a, const FlightSummary::AircraftTotals &b)
                         { return a.flights > b.flights; });

        std::sort(summary_.flight_list.begin(), summary_.flight_list.end(), flown_later);
        return summary_;
    }

  private:
    void add_aircraft(const FlightData &flight)
    {
        FlightSummary::AircraftTotals &totals = aircraft_[aircraft_of(flight)];
        totals.icao = aircraft_of(flight);
        totals.flights += 1;
        totals.block_time_sec += flight.block_time_sec;
        totals.landings += static_cast<int>(flight.landings.size());
    }

    void add_places(const FlightData &flight)
    {
        const FlightLegs::Route route = FlightLegs::derive_route(flight);
        route_counts_[route_key(route)] += 1;

        // A round trip visits its airport once, however often it lands there.
        std::set<std::string> visited;
        for (const FlightLegs::Place &stop : route.stops)
            if (is_airport(stop))
                visited.insert(stop.name);
        for (const std::string &airport : visited)
        {
            airports_.insert(airport);
            airport_visits_[airport] += 1;
        }
    }

    void add_landing(const LandingData &landing)
    {
        const bool first = summary_.landings == 0;
        summary_.landings += 1;
        fpm_sum_ += landing.fpm;
        count_rating(summary_.ratings, landing.rating);

        if (first || std::fabs(landing.fpm) < std::fabs(summary_.softest_fpm))
            summary_.softest_fpm = landing.fpm;
        if (first || std::fabs(landing.fpm) > std::fabs(summary_.hardest_fpm))
            summary_.hardest_fpm = landing.fpm;
        summary_.max_g = std::max(summary_.max_g, landing.g_force);
    }

    FlightSummary::Summary                               summary_;
    double                                               fpm_sum_ = 0;
    std::set<std::string>                                airports_;
    std::map<std::string, int>                           airport_visits_;
    std::map<std::string, int>                           route_counts_;
    std::map<std::string, FlightSummary::AircraftTotals> aircraft_;
};

} // namespace

double FlightSummary::track_distance_nm(const std::vector<TrackPoint> &track)
{
    double meters = 0;
    for (size_t i = 1; i < track.size(); ++i)
        meters += RunwayGeometry::distance_m(track[i - 1].lat, track[i - 1].lon, track[i].lat, track[i].lon);
    return meters / METERS_PER_NM;
}

Period Period::month_of(int year, int month) { return {Kind::Month, year, month}; }

Period Period::year_of(int year) { return {Kind::Year, year, 0}; }

Period Period::all_time() { return {Kind::All, 0, 0}; }

bool Period::contains(const std::string &date) const
{
    if (kind == Kind::All)
        return true;
    YearMonth flown;
    if (!parse_year_month(date, flown) || flown.year != year)
        return false;
    return kind == Kind::Year || flown.month == month;
}

std::string Period::label() const
{
    switch (kind)
    {
    case Kind::Month:
        return std::string(month_name(month)) + " " + std::to_string(year);
    case Kind::Year:
        return std::to_string(year);
    case Kind::All:
        break;
    }
    return "All time";
}

std::string Period::key() const
{
    char text[16];
    switch (kind)
    {
    case Kind::Month:
        std::snprintf(text, sizeof(text), "%04d-%02d", year, month);
        return text;
    case Kind::Year:
        return std::to_string(year);
    case Kind::All:
        break;
    }
    return "all";
}

bool Period::operator==(const Period &other) const
{
    return kind == other.kind && year == other.year && month == other.month;
}

Period FlightSummary::current_month(time_t now)
{
    const struct tm *utc = std::gmtime(&now);
    if (!utc)
        return Period::all_time();
    return Period::month_of(utc->tm_year + 1900, utc->tm_mon + 1);
}

std::vector<Period> FlightSummary::available_months(const std::vector<LoggedFlight> &flights)
{
    std::set<std::pair<int, int>> months;
    for (const LoggedFlight &logged : flights)
    {
        YearMonth flown;
        if (parse_year_month(logged.flight.date, flown))
            months.insert({flown.year, flown.month});
    }

    std::vector<Period> periods;
    for (auto it = months.rbegin(); it != months.rend(); ++it)
        periods.push_back(Period::month_of(it->first, it->second));
    return periods;
}

std::vector<Period> FlightSummary::available_years(const std::vector<LoggedFlight> &flights)
{
    std::set<int> years;
    for (const LoggedFlight &logged : flights)
    {
        YearMonth flown;
        if (parse_year_month(logged.flight.date, flown))
            years.insert(flown.year);
    }

    std::vector<Period> periods;
    for (auto it = years.rbegin(); it != years.rend(); ++it)
        periods.push_back(Period::year_of(*it));
    return periods;
}

std::vector<std::string> FlightSummary::available_aircraft(const std::vector<LoggedFlight> &flights,
                                                           const Period &period)
{
    std::set<std::string> aircraft;
    for (const LoggedFlight &logged : flights)
        if (period.contains(logged.flight.date))
            aircraft.insert(aircraft_of(logged.flight));
    return {aircraft.begin(), aircraft.end()};
}

FlightSummary::Summary FlightSummary::summarize(const std::vector<LoggedFlight> &flights, const Filter &filter)
{
    Accumulator accumulator;
    for (const LoggedFlight &logged : flights)
        if (matches(logged.flight, filter))
            accumulator.add(logged);
    return accumulator.finish();
}
