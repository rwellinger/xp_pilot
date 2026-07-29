/*
 * xp_pilot - Flight Logger and Auto QNH plugin for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
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

#include "html_report.hpp"

#include <catch2/catch_amalgamated.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace
{

std::string slurp(const fs::path &p)
{
    std::ifstream     f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string read_fixture(const std::string &name)
{
    fs::path p = fs::path(XP_PILOT_TEST_FIXTURES_DIR) / name;
    return slurp(p);
}

// Build an isolated data_dir/{flights,reports}/ tree under the OS temp dir so
// tests do not share state.
fs::path make_tmp_data_dir()
{
    static std::mt19937_64           rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;

    fs::path root = fs::temp_directory_path() / ("xp_pilot_test_" + std::to_string(dist(rng)));
    fs::create_directories(root / "flights");
    fs::create_directories(root / "reports");
    return root;
}

} // namespace

// ── Wind condition string converters ─────────────────────────────────────────

TEST_CASE("wind_condition_from_string: known tags", "[wind]")
{
    REQUIRE(wind_condition_from_string("CALM") == WindCondition::Calm);
    REQUIRE(wind_condition_from_string("LIGHT") == WindCondition::Light);
    REQUIRE(wind_condition_from_string("STEADY") == WindCondition::Steady);
}

TEST_CASE("wind_condition_from_string: unknown defaults to Steady", "[wind]")
{
    REQUIRE(wind_condition_from_string("") == WindCondition::Steady);
    REQUIRE(wind_condition_from_string("BREEZY") == WindCondition::Steady);
    REQUIRE(wind_condition_from_string("calm") == WindCondition::Steady); // case sensitive
}

TEST_CASE("wind_condition_to_string: round-trips for known tags", "[wind]")
{
    REQUIRE(std::string(wind_condition_to_string(WindCondition::Calm)) == "CALM");
    REQUIRE(std::string(wind_condition_to_string(WindCondition::Light)) == "LIGHT");
    REQUIRE(std::string(wind_condition_to_string(WindCondition::Steady)) == "STEADY");
}

// ── parse_flight_json ────────────────────────────────────────────────────────

TEST_CASE("parse_flight_json: populates all top-level fields", "[parse]")
{
    std::string content = read_fixture("sample_flight.json");
    FlightData  fd      = parse_flight_json(content, "2026-04-29_LSZB_LSGG_DA42.json");

    REQUIRE(fd.filename == "2026-04-29_LSZB_LSGG_DA42.json");
    REQUIRE(fd.date == "2026-04-29");
    REQUIRE(fd.start_utc == "08:30");
    REQUIRE(fd.end_utc == "09:45");
    REQUIRE(fd.departure_icao == "LSZB");
    REQUIRE(fd.arrival_icao == "LSGG");
    REQUIRE(fd.aircraft_icao == "DA42");
    REQUIRE(fd.aircraft_tail == "HB-LXY");
    REQUIRE(fd.block_time_min == 75);
    REQUIRE(fd.max_altitude_ft == 9500);
    REQUIRE(fd.max_speed_kts == 165);
}

TEST_CASE("parse_flight_json: reads the pause total of a v2 flight", "[parse]")
{
    std::string content = read_fixture("sample_flight_paused.json");
    FlightData  fd      = parse_flight_json(content, "2026-04-29_LSZB_LSGG_DA42.json");

    REQUIRE(fd.block_time_min == 55);
    REQUIRE(fd.block_time_sec == 3312);
    REQUIRE(fd.paused_sec == 1200);
}

TEST_CASE("parse_flight_json: pre-v2 flights without paused_sec default to no pause", "[parse]")
{
    FlightData fd = parse_flight_json(read_fixture("sample_flight.json"), "x.json");
    REQUIRE(fd.paused_sec == 0);
    // No second-resolution field: the minute value carries the block time.
    REQUIRE(fd.block_time_sec == 75 * 60);
}

TEST_CASE("parse_flight_json: parses every track point", "[parse]")
{
    std::string content = read_fixture("sample_flight.json");
    FlightData  fd      = parse_flight_json(content, "x.json");

    REQUIRE(fd.track.size() == 4);
    REQUIRE(fd.track.front().lat == Catch::Approx(46.9141));
    REQUIRE(fd.track.front().lon == Catch::Approx(7.4970));
    REQUIRE(fd.track.front().alt_ft == 1700);
    REQUIRE(fd.track.back().vs_fpm == -700);
}

TEST_CASE("parse_flight_json: parses landings array", "[parse]")
{
    std::string content = read_fixture("sample_flight.json");
    FlightData  fd      = parse_flight_json(content, "x.json");

    REQUIRE(fd.landings.size() == 1);
    const auto &ld = fd.landings.front();
    REQUIRE(ld.fpm == Catch::Approx(-180.0f));
    REQUIRE(ld.g_force == Catch::Approx(1.25f));
    REQUIRE(ld.pitch_deg == Catch::Approx(4.5f));
    REQUIRE(ld.agl_ft == Catch::Approx(51.0f));
    REQUIRE(ld.headwind_kts == 6);
    REQUIRE(ld.crosswind_kts == -4);
    REQUIRE(ld.rating == "GREAT LANDING!");
    REQUIRE(ld.wind_status == "STEADY");
    REQUIRE(ld.crosswind_side == "LEFT");
}

TEST_CASE("parse_flight_json: malformed JSON returns empty FlightData without throwing", "[parse]")
{
    FlightData fd = parse_flight_json("{this is not json", "broken.json");
    REQUIRE(fd.filename == "broken.json");
    REQUIRE(fd.track.empty());
    REQUIRE(fd.landings.empty());
}

TEST_CASE("parse_flight_json: missing fields take documented defaults", "[parse]")
{
    FlightData fd = parse_flight_json("{}", "empty.json");
    REQUIRE(fd.date == "?");          // documented placeholder
    REQUIRE(fd.start_utc.empty());
    REQUIRE(fd.block_time_min == 0);
    REQUIRE(fd.paused_sec == 0);
    REQUIRE(fd.block_time_sec == 0);
    REQUIRE(fd.max_altitude_ft == 0);
    REQUIRE(fd.track.empty());
    REQUIRE(fd.landings.empty());
}

// ── resolve_pauses ───────────────────────────────────────────────────────────

TEST_CASE("resolve_pauses: returns the recorded pauses unchanged", "[pauses]")
{
    FlightData fd;
    fd.paused_sec = 50;
    fd.pauses.push_back(PauseEvent{1745916700, 50, 46.9, 7.5});

    auto pauses = resolve_pauses(fd);
    REQUIRE(pauses.size() == 1);
    REQUIRE(pauses.front().sec == 50);
    REQUIRE(pauses.front().lat == Catch::Approx(46.9));
}

TEST_CASE("resolve_pauses: reconstructs pauses from track gaps when none were recorded", "[pauses]")
{
    FlightData fd;
    fd.paused_sec = 50;
    // 10 s sampling; the third gap is 60 s wall clock, so 50 s of it was a pause.
    for (int i = 0; i < 4; ++i)
        fd.track.push_back(TrackPoint{1745916600 + i * 10, 46.9 + i * 0.01, 7.5, 0, 0, 0});
    fd.track.push_back(TrackPoint{1745916600 + 30 + 60, 47.2, 7.5, 0, 0, 0});

    auto pauses = resolve_pauses(fd);
    REQUIRE(pauses.size() == 1);
    REQUIRE(pauses.front().sec == 50);
    REQUIRE(pauses.front().t == 1745916630);
    REQUIRE(pauses.front().lat == Catch::Approx(46.93));
}

TEST_CASE("resolve_pauses: a flight without a pause yields nothing", "[pauses]")
{
    FlightData fd = parse_flight_json(read_fixture("sample_flight.json"), "x.json");
    REQUIRE(resolve_pauses(fd).empty());
}

TEST_CASE("parse_flight_json: reads a recorded pauses array", "[parse][pauses]")
{
    FlightData fd = parse_flight_json(
        R"({"paused_sec":50,"pauses":[{"t":1745916630,"sec":50,"lat":46.93,"lon":7.5}]})", "x.json");

    REQUIRE(fd.pauses.size() == 1);
    REQUIRE(fd.pauses.front().sec == 50);
    REQUIRE(fd.pauses.front().lon == Catch::Approx(7.5));
}

// ── HtmlReport::generate ─────────────────────────────────────────────────────

TEST_CASE("HtmlReport::generate: writes a report file and returns its basename", "[report]")
{
    fs::path    root  = make_tmp_data_dir();
    std::string ddir  = root.string() + "/";
    std::string jname = "2026-04-29_LSZB_LSGG_DA42.json";

    FlightData fd = parse_flight_json(read_fixture("sample_flight.json"), jname);

    std::array<int, 4> thresholds{-100, -250, -350, -600};
    std::string        rname = HtmlReport::generate(fd, ddir, jname, "medium_ga", thresholds);

    REQUIRE(rname == "2026-04-29_LSZB_LSGG_DA42.html");
    fs::path out = root / "reports" / rname;
    REQUIRE(fs::exists(out));

    std::string html = slurp(out);
    REQUIRE(html.find("<!DOCTYPE html>") != std::string::npos);
    REQUIRE(html.find("LSZB") != std::string::npos);
    REQUIRE(html.find("LSGG") != std::string::npos);
    REQUIRE(html.find("DA42") != std::string::npos);
    REQUIRE(html.find("GREAT LANDING!") != std::string::npos);
    // Threshold legend is rendered from the supplied profile thresholds.
    REQUIRE(html.find("Butter &gt;-100") != std::string::npos);

    fs::remove_all(root);
}

TEST_CASE("HtmlReport::generate: a paused flight shows total, pause and net block time", "[report]")
{
    fs::path    root  = make_tmp_data_dir();
    std::string ddir  = root.string() + "/";
    std::string jname = "2026-04-29_LSZB_LSGG_DA42.json";

    std::array<int, 4> thresholds{-100, -250, -350, -600};

    FlightData  paused = parse_flight_json(read_fixture("sample_flight_paused.json"), jname);
    std::string html   = slurp(root / "reports" / HtmlReport::generate(paused, ddir, jname, "medium_ga", thresholds));

    // 3312 s block time + 1200 s pause = 4512 s gross, and the three tiles add up.
    REQUIRE(html.find(">1h 15m</div><div class=\"lbl\">Total<") != std::string::npos);
    REQUIRE(html.find(">20m 00s</div><div class=\"lbl\">Paused<") != std::string::npos);
    REQUIRE(html.find(">55m 12s</div><div class=\"lbl\">Block Time<") != std::string::npos);

    // The pause is marked on the route and explained by a legend below the map.
    REQUIRE(html.find("pauses=[[46.950000,7.300000,\"20m 00s\"]]") != std::string::npos);
    REQUIRE(html.find("<p class=\"legend\">") != std::string::npos);

    // A flight without a pause keeps the single Block Time tile it always had.
    FlightData  unpaused = parse_flight_json(read_fixture("sample_flight.json"), jname);
    std::string plain = slurp(root / "reports" / HtmlReport::generate(unpaused, ddir, jname, "medium_ga", thresholds));

    REQUIRE(plain.find("Paused") == std::string::npos);
    REQUIRE(plain.find("Total") == std::string::npos);
    REQUIRE(plain.find("<p class=\"legend\">") == std::string::npos);
    REQUIRE(plain.find("pauses=[]") != std::string::npos);
    REQUIRE(plain.find(">1h 15m</div><div class=\"lbl\">Block Time<") != std::string::npos);

    fs::remove_all(root);
}

// Regression for issue #3: a 50-second pause was measured but swallowed by a display
// threshold, so the report looked exactly like a flight that was never paused.
TEST_CASE("HtmlReport::generate: a pause under a minute is still reported", "[report]")
{
    fs::path    root  = make_tmp_data_dir();
    std::string ddir  = root.string() + "/";
    std::string jname = "2026-07-29_EDNY_EDNY_EFOX.json";

    FlightData fd     = parse_flight_json(read_fixture("sample_flight.json"), jname);
    fd.block_time_sec = 492;
    fd.paused_sec     = 50;

    std::array<int, 4> thresholds{-100, -250, -350, -600};
    std::string html = slurp(root / "reports" / HtmlReport::generate(fd, ddir, jname, "medium_ga", thresholds));

    REQUIRE(html.find(">9m 02s</div><div class=\"lbl\">Total<") != std::string::npos);
    REQUIRE(html.find(">50s</div><div class=\"lbl\">Paused<") != std::string::npos);
    REQUIRE(html.find(">8m 12s</div><div class=\"lbl\">Block Time<") != std::string::npos);

    fs::remove_all(root);
}

TEST_CASE("HtmlReport::generate: returns empty when reports dir is missing", "[report]")
{
    fs::path    root  = fs::temp_directory_path() / "xp_pilot_no_reports_dir_should_not_exist_xyz";
    fs::remove_all(root);
    fs::create_directories(root); // intentionally no reports/ subdir

    FlightData         fd;
    fd.departure_icao = "AAAA";
    fd.arrival_icao   = "BBBB";

    std::array<int, 4> thresholds{-100, -250, -350, -600};
    std::string        rname = HtmlReport::generate(fd, root.string() + "/", "x.json", "p", thresholds);
    REQUIRE(rname.empty());

    fs::remove_all(root);
}

TEST_CASE("HtmlReport::generate: empty track and no landings still produces valid HTML", "[report]")
{
    fs::path           root = make_tmp_data_dir();
    FlightData         fd;
    fd.departure_icao = "AAAA";
    fd.arrival_icao   = "BBBB";
    fd.aircraft_icao  = "C172";
    fd.date           = "2026-01-01";

    std::array<int, 4> thresholds{-100, -250, -350, -600};
    std::string        rname = HtmlReport::generate(fd, root.string() + "/", "empty.json", "small_ga", thresholds);

    REQUIRE(rname == "empty.html");
    std::string html = slurp(root / "reports" / rname);
    REQUIRE(html.find("No landing recorded") != std::string::npos);

    fs::remove_all(root);
}

TEST_CASE("HtmlReport::generate: HTML-escapes hostile characters in identifiers", "[report]")
{
    fs::path           root = make_tmp_data_dir();
    FlightData         fd;
    fd.departure_icao = "<script>alert('x')</script>";
    fd.arrival_icao   = "B&B";

    std::array<int, 4> thresholds{-100, -250, -350, -600};
    std::string        rname = HtmlReport::generate(fd, root.string() + "/", "esc.json", "p", thresholds);
    std::string        html  = slurp(root / "reports" / rname);

    REQUIRE(html.find("<script>alert") == std::string::npos); // raw tag must not survive
    REQUIRE(html.find("&lt;script&gt;") != std::string::npos);
    REQUIRE(html.find("B&amp;B") != std::string::npos);

    fs::remove_all(root);
}

// ── HtmlReport::generate_index ───────────────────────────────────────────────

TEST_CASE("generate_index: lists every flight JSON in the directory", "[report][index]")
{
    fs::path    root = make_tmp_data_dir();
    std::string ddir = root.string() + "/";

    std::ofstream(root / "flights" / "2026-03-01_LSZB_LSGG_DA42.json") << read_fixture("sample_flight.json");
    std::ofstream(root / "flights" / "2026-04-29_LSZB_LSGG_DA42.json") << read_fixture("sample_flight.json");

    HtmlReport::generate_index(ddir);

    fs::path index = root / "index.html";
    REQUIRE(fs::exists(index));

    std::string idx = slurp(index);
    REQUIRE(idx.find("2 flights") != std::string::npos);
    REQUIRE(idx.find("LSZB") != std::string::npos);
    REQUIRE(idx.find("LSGG") != std::string::npos);
    // Newest first — both share the same departure ICAO so we check date order.
    REQUIRE(idx.find("2026-04-29") < idx.find("2026-03-01"));

    fs::remove_all(root);
}

TEST_CASE("generate_index: silently handles missing flights directory", "[report][index]")
{
    fs::path root = fs::temp_directory_path() / "xp_pilot_index_no_flights_dir_xyz";
    fs::remove_all(root);
    fs::create_directories(root);

    REQUIRE_NOTHROW(HtmlReport::generate_index(root.string() + "/"));
    REQUIRE_FALSE(fs::exists(root / "index.html"));

    fs::remove_all(root);
}

TEST_CASE("generate_index: ignores non-JSON files in flights/", "[report][index]")
{
    fs::path    root = make_tmp_data_dir();
    std::string ddir = root.string() + "/";

    std::ofstream(root / "flights" / "2026-04-29_LSZB_LSGG_DA42.json") << read_fixture("sample_flight.json");
    std::ofstream(root / "flights" / "README.md") << "not a flight";

    HtmlReport::generate_index(ddir);
    std::string idx = slurp(root / "index.html");
    REQUIRE(idx.find("1 flights") != std::string::npos);
    REQUIRE(idx.find("README") == std::string::npos);

    fs::remove_all(root);
}
