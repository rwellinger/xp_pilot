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
#include "flight_summary.hpp"
#include <catch2/catch_amalgamated.hpp>

#include <filesystem>
#include <string>
#include <vector>

using FlightSummary::Filter;
using FlightSummary::LoggedFlight;
using FlightSummary::Period;

namespace
{

LandingData landing_at(const std::string &icao, float fpm, float g_force, const std::string &rating)
{
    LandingData ld;
    ld.airport_icao       = icao;
    ld.airport_distance_m = 0.f;
    ld.fpm                = fpm;
    ld.g_force            = g_force;
    ld.rating             = rating;
    return ld;
}

LoggedFlight flight(const std::string &date, const std::string &from, const std::string &to,
                    const std::string &aircraft, int block_time_sec, double distance_nm = 0)
{
    LoggedFlight logged;
    logged.flight.date                 = date;
    logged.flight.start_utc            = "10:00";
    logged.flight.filename             = date + "_" + from + "_" + to + "_" + aircraft + ".json";
    logged.flight.departure_icao       = from;
    logged.flight.departure_distance_m = 0.f;
    logged.flight.arrival_icao         = to;
    logged.flight.aircraft_icao        = aircraft;
    logged.flight.block_time_sec       = block_time_sec;
    logged.distance_nm                 = distance_nm;
    return logged;
}

LoggedFlight landed(LoggedFlight logged, float fpm, float g_force, const std::string &rating)
{
    logged.flight.landings.push_back(landing_at(logged.flight.arrival_icao, fpm, g_force, rating));
    return logged;
}

int rating_count(const FlightSummary::Summary &summary, const std::string &rating)
{
    for (const auto &entry : summary.ratings)
        if (entry.name == rating)
            return entry.count;
    return -1;
}

const std::vector<LoggedFlight> &sample_log()
{
    static const std::vector<LoggedFlight> log = {
        landed(flight("2026-10-30", "LSZB", "LSGG", "DA42", 3600, 80), -150.f, 1.3f, "GREAT LANDING!"),
        landed(flight("2026-11-02", "LSGG", "LSZB", "DA42", 3000, 75), -420.f, 1.8f, "HARD LANDING!"),
        landed(flight("2026-11-05", "LSZB", "LSZB", "C172", 1800, 30), -90.f, 1.1f, "BUTTER!"),
        flight("2026-11-07", "LSZB", "LSZG", "C172", 1200, 25), // no landing recorded
        landed(flight("2025-06-01", "EDDF", "LSZB", "A320", 4200, 200), -250.f, 1.4f, "ACCEPTABLE"),
    };
    return log;
}

} // namespace

TEST_CASE("Period matches flight dates by month, year and all time", "[flight_summary]")
{
    const Period november = Period::month_of(2026, 11);
    CHECK(november.contains("2026-11-01"));
    CHECK(november.contains("2026-11-30"));
    CHECK_FALSE(november.contains("2026-10-31"));
    CHECK_FALSE(november.contains("2025-11-15"));

    const Period year = Period::year_of(2026);
    CHECK(year.contains("2026-01-01"));
    CHECK(year.contains("2026-12-31"));
    CHECK_FALSE(year.contains("2025-12-31"));

    // Very old or hand-edited logs carry "?" — they still count towards all time.
    CHECK_FALSE(november.contains("?"));
    CHECK_FALSE(year.contains(""));
    CHECK(Period::all_time().contains("?"));
}

TEST_CASE("Period labels and keys", "[flight_summary]")
{
    CHECK(Period::month_of(2026, 11).label() == "November 2026");
    CHECK(Period::month_of(2026, 3).key() == "2026-03");
    CHECK(Period::year_of(2026).label() == "2026");
    CHECK(Period::year_of(2026).key() == "2026");
    CHECK(Period::all_time().label() == "All time");
    CHECK(Period::all_time().key() == "all");
}

TEST_CASE("current_month uses the UTC calendar month", "[flight_summary]")
{
    // 2026-11-30 23:30 UTC — still November in UTC, wherever the user sits.
    CHECK(FlightSummary::current_month(1796081400) == Period::month_of(2026, 11));
}

TEST_CASE("available periods and aircraft list only what the log holds", "[flight_summary]")
{
    const auto months = FlightSummary::available_months(sample_log());
    REQUIRE(months.size() == 3);
    CHECK(months[0] == Period::month_of(2026, 11));
    CHECK(months[1] == Period::month_of(2026, 10));
    CHECK(months[2] == Period::month_of(2025, 6));

    const auto years = FlightSummary::available_years(sample_log());
    REQUIRE(years.size() == 2);
    CHECK(years[0] == Period::year_of(2026));
    CHECK(years[1] == Period::year_of(2025));

    CHECK(FlightSummary::available_aircraft(sample_log(), Period::month_of(2026, 11)) ==
          std::vector<std::string>{"C172", "DA42"});
    CHECK(FlightSummary::available_aircraft(sample_log(), Period::all_time()) ==
          std::vector<std::string>{"A320", "C172", "DA42"});
}

TEST_CASE("summarize totals a month across all aircraft", "[flight_summary]")
{
    const auto summary = FlightSummary::summarize(sample_log(), {Period::month_of(2026, 11), ""});

    CHECK(summary.flights == 3);
    CHECK(summary.block_time_sec == 3000 + 1800 + 1200);
    CHECK(summary.landings == 2);
    CHECK(summary.distance_nm == Catch::Approx(130.0));
    CHECK(summary.airports == 3); // LSGG, LSZB, LSZG

    CHECK(summary.average_fpm == Catch::Approx(-255.f));
    CHECK(summary.softest_fpm == Catch::Approx(-90.f));
    CHECK(summary.hardest_fpm == Catch::Approx(-420.f));
    CHECK(summary.max_g == Catch::Approx(1.8f));
    CHECK(rating_count(summary, "BUTTER!") == 1);
    CHECK(rating_count(summary, "HARD LANDING!") == 1);
    CHECK(rating_count(summary, "WASTED!") == 0);
    CHECK(summary.ratings.size() == 5);

    REQUIRE(summary.flight_list.size() == 3);
    CHECK(summary.flight_list.front().date == "2026-11-07");
    CHECK(summary.flight_list.back().date == "2026-11-02");
}

TEST_CASE("summarize narrows to one aircraft type", "[flight_summary]")
{
    const auto summary = FlightSummary::summarize(sample_log(), {Period::all_time(), "DA42"});

    CHECK(summary.flights == 2);
    CHECK(summary.block_time_sec == 6600);
    REQUIRE(summary.aircraft.size() == 1);
    CHECK(summary.aircraft[0].icao == "DA42");
    CHECK(summary.aircraft[0].landings == 2);
}

TEST_CASE("summarize ranks aircraft, airports and routes", "[flight_summary]")
{
    const auto summary = FlightSummary::summarize(sample_log(), {Period::all_time(), ""});

    REQUIRE(summary.aircraft.size() == 3);
    // Two flights each for C172 and DA42; the tie keeps alphabetical order.
    CHECK(summary.aircraft[0].icao == "C172");
    CHECK(summary.aircraft[1].icao == "DA42");
    CHECK(summary.aircraft[2].icao == "A320");

    REQUIRE_FALSE(summary.top_airports.empty());
    CHECK(summary.top_airports[0].name == "LSZB"); // every flight touches it
    CHECK(summary.top_airports[0].count == 5);

    // A circuit lands at its departure airport, which still counts as one visit.
    REQUIRE(summary.top_routes.size() == 5);
    for (const auto &route : summary.top_routes)
        CHECK(route.count == 1);
}

TEST_CASE("summarize of an empty selection is all zeros", "[flight_summary]")
{
    const auto summary = FlightSummary::summarize(sample_log(), {Period::month_of(2024, 1), ""});

    CHECK(summary.flights == 0);
    CHECK(summary.landings == 0);
    CHECK(summary.flight_list.empty());
    CHECK(summary.average_fpm == 0.f);
    CHECK(rating_count(summary, "BUTTER!") == 0);
}

TEST_CASE("off-field landings are neither airports nor grouped as routes", "[flight_summary]")
{
    LoggedFlight rescue = flight("2026-11-10", "LSZB", "LSZB", "EC35", 1500);
    LandingData  site   = landing_at("LSZB", -60.f, 1.0f, "BUTTER!");
    site.airport_distance_m = 5000.f; // far from any runway: off-field
    site.lat                = 46.7;
    site.lon                = 7.6;
    rescue.flight.landings.push_back(site);

    const auto summary = FlightSummary::summarize({rescue}, {Period::all_time(), ""});
    CHECK(summary.airports == 1); // LSZB only, not the landing site
    REQUIRE(summary.top_routes.size() == 1);
    CHECK(summary.top_routes[0].name == "LSZB-off-field");
}

TEST_CASE("track_distance_nm sums the flown segments", "[flight_summary]")
{
    std::vector<TrackPoint> track(3);
    track[0].lat = 47.0, track[0].lon = 8.0;
    track[1].lat = 47.1, track[1].lon = 8.0; // ~6 nm north
    track[2].lat = 47.2, track[2].lon = 8.0; // and another ~6 nm

    CHECK(FlightSummary::track_distance_nm(track) == Catch::Approx(12.0).margin(0.1));
    CHECK(FlightSummary::track_distance_nm({}) == 0.0);
}

TEST_CASE("read_logged_flights includes the archive and drops the track", "[flight_summary][flight_store]")
{
    namespace fs        = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "xp_pilot_summary_store";
    fs::remove_all(root);
    fs::create_directories(root / "flights" / "archived");
    const std::string output_dir = root.generic_string() + "/";

    FlightData active = flight("2026-11-02", "LSZB", "LSGG", "DA42", 3000).flight;
    active.track.resize(2);
    active.track[0].lat = 47.0, active.track[0].lon = 8.0;
    active.track[1].lat = 47.1, active.track[1].lon = 8.0;
    const std::string active_file = FlightStore::save(active, output_dir);

    FlightData archived = flight("2025-06-01", "EDDF", "LSZB", "A320", 4200).flight;
    const std::string archived_file = FlightStore::save(archived, output_dir);
    fs::rename(root / "flights" / archived_file, root / "flights" / "archived" / archived_file);

    const auto logged = FlightStore::read_logged_flights(output_dir);
    fs::remove_all(root);

    REQUIRE(logged.size() == 2);
    for (const auto &entry : logged)
    {
        CHECK(entry.flight.track.empty());
        if (entry.flight.filename == active_file)
            CHECK(entry.distance_nm == Catch::Approx(6.0).margin(0.1));
        else
            CHECK(entry.flight.filename == archived_file);
    }
}
