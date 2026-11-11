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

#include "summary_report.hpp"
#include "html_common.hpp"
#include <catch2/catch_amalgamated.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using FlightSummary::Filter;
using FlightSummary::Period;

namespace
{

constexpr time_t GENERATED_AT = 1794787200; // 2026-11-16 00:00 UTC

FlightSummary::Summary one_flight_summary(const std::string &tail)
{
    FlightSummary::LoggedFlight logged;
    logged.flight.date                 = "2026-11-02";
    logged.flight.start_utc            = "10:00";
    logged.flight.departure_icao       = "LSZB";
    logged.flight.departure_distance_m = 0.f;
    logged.flight.arrival_icao         = "LSGG";
    logged.flight.aircraft_icao        = "DA42";
    logged.flight.aircraft_tail        = tail;
    logged.flight.block_time_sec       = 3600;

    LandingData ld;
    ld.airport_icao       = "LSGG";
    ld.airport_distance_m = 0.f;
    ld.fpm                = -150.f;
    ld.rating             = "GREAT LANDING!";
    logged.flight.landings.push_back(ld);

    return FlightSummary::summarize({logged}, {Period::all_time(), ""});
}

bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("summary report file names name the period and aircraft", "[summary_report]")
{
    CHECK(SummaryReport::filename({Period::month_of(2026, 11), ""}) == "summary_2026-11_all.html");
    CHECK(SummaryReport::filename({Period::all_time(), "DA42"}) == "summary_all_DA42.html");
    CHECK(SummaryReport::filename({Period::year_of(2026), "../x y"}) == "summary_2026____x_y.html");
}

TEST_CASE("summary report carries the simulation-only notice and print layout", "[summary_report]")
{
    const std::string html =
        SummaryReport::html(one_flight_summary("HB-XYZ"), {Period::month_of(2026, 11), ""}, GENERATED_AT);

    CHECK(contains(html, "class=\"sim-only\""));
    CHECK(contains(html, "class=\"print-footer\""));
    CHECK(contains(html, HtmlCommon::SIM_ONLY_NOTICE));
    CHECK(contains(html, "@media print"));
    CHECK(contains(html, "size:A4"));
    CHECK(contains(html, "window.print()"));
    CHECK(contains(html, "November 2026"));
    CHECK(contains(html, "All aircraft types"));
    CHECK(contains(html, "2026-11-16 00:00 UTC"));
    CHECK(contains(html, "GREAT LANDING!"));
}

TEST_CASE("summary report escapes logged text", "[summary_report]")
{
    const std::string html =
        SummaryReport::html(one_flight_summary("<script>"), {Period::all_time(), ""}, GENERATED_AT);

    CHECK_FALSE(contains(html, "<script>"));
    CHECK(contains(html, "&lt;script&gt;"));
}

TEST_CASE("summary report without flights says so", "[summary_report]")
{
    const std::string html = SummaryReport::html({}, {Period::month_of(2024, 1), ""}, GENERATED_AT);

    CHECK(contains(html, "No flights in this period."));
    CHECK(contains(html, HtmlCommon::SIM_ONLY_NOTICE));
}

TEST_CASE("summary report is written below summaries/", "[summary_report]")
{
    namespace fs        = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "xp_pilot_summary_report";
    fs::remove_all(root);
    fs::create_directories(root);

    const std::string path =
        SummaryReport::generate(one_flight_summary("HB-XYZ"), {Period::all_time(), "DA42"}, root.generic_string() + "/");

    CHECK(path == (root / "summaries" / "summary_all_DA42.html").generic_string());
    CHECK(fs::exists(path));
    fs::remove_all(root);
}
