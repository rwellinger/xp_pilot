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
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// Totals over a period of the flight log, for the reports screen and the summary report.
// Pure logic: the caller loads the flights, nothing here touches disk.
namespace FlightSummary
{

// A logged flight as the summaries need it: the record without its track samples,
// which would cost far too much memory across a whole log, plus the distance flown
// along that track.
struct LoggedFlight
{
    FlightData flight;
    double     distance_nm = 0;
};

double track_distance_nm(const std::vector<TrackPoint> &track);

struct Period
{
    enum class Kind : std::uint8_t
    {
        Month,
        Year,
        All
    };

    Kind kind  = Kind::All;
    int  year  = 0;
    int  month = 0; // 1..12, Kind::Month only

    static Period month_of(int year, int month);
    static Period year_of(int year);
    static Period all_time();

    // `date` as flights store it, "YYYY-MM-DD" in UTC. A flight without a usable date
    // still counts towards all time.
    bool        contains(const std::string &date) const;
    std::string label() const; // "November 2026", "2026", "All time"
    std::string key() const;   // "2026-11", "2026", "all" — safe for a file name

    bool operator==(const Period &other) const;
    bool operator!=(const Period &other) const { return !(*this == other); }
};

// The UTC month `now` falls in — the same clock the flight dates are written with.
Period current_month(time_t now);

struct Filter
{
    Period      period;
    std::string aircraft_icao; // empty = all types
};

// The choices offered for the filter, each only listing what the log actually holds.
std::vector<Period>      available_months(const std::vector<LoggedFlight> &flights); // newest first
std::vector<Period>      available_years(const std::vector<LoggedFlight> &flights);  // newest first
std::vector<std::string> available_aircraft(const std::vector<LoggedFlight> &flights,
                                            const Period &period); // alphabetical

inline constexpr size_t TOP_LIST_SIZE = 5;

struct Ranked
{
    std::string name;
    int         count = 0;
};

struct AircraftTotals
{
    std::string icao;
    int         flights        = 0;
    int         block_time_sec = 0;
    int         landings       = 0;
};

struct Summary
{
    int    flights        = 0;
    int    block_time_sec = 0;
    int    landings       = 0;
    double distance_nm    = 0;
    int    airports       = 0; // distinct airports departed from or landed at

    // Landing quality, only meaningful when `landings` is not zero. The extremes keep
    // their sign, so they read exactly like the flight report shows them.
    std::vector<Ranked> ratings; // every rating label, best first, zero counts included
    float               average_fpm = 0;
    float               softest_fpm = 0; // closest to zero
    float               hardest_fpm = 0;
    float               max_g       = 0;

    std::vector<AircraftTotals> aircraft;     // most flights first
    std::vector<Ranked>         top_airports; // at most TOP_LIST_SIZE, most visits first
    std::vector<Ranked>         top_routes;   // at most TOP_LIST_SIZE, "LSZB-LSGG"
    std::vector<FlightData>     flight_list;  // newest first
};

Summary summarize(const std::vector<LoggedFlight> &flights, const Filter &filter);

} // namespace FlightSummary
