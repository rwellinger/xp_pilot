#include "../src/flight_store.hpp"
#include "../src/html_report.hpp"
#include <catch_amalgamated.hpp>
#include <filesystem>
#include <fstream>
#include <json.hpp>

using json = nlohmann::json;

namespace
{

// FlightStore::save() writes into <dir>/flights/, so each test gets its own tree.
struct TempOutputDir
{
    std::filesystem::path root;

    explicit TempOutputDir(const std::string &name)
        : root(std::filesystem::temp_directory_path() / ("xp_pilot_store_" + name))
    {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "flights");
    }
    ~TempOutputDir() { std::filesystem::remove_all(root); }

    std::string path() const { return root.generic_string() + "/"; }
};

FlightData sample_flight()
{
    FlightData fd;
    fd.date              = "2026-04-01";
    fd.start_utc         = "08:15";
    fd.end_utc           = "09:42";
    fd.departure_icao    = "LSZB";
    fd.arrival_icao      = "LSGG";
    fd.aircraft_icao     = "DA42";
    fd.aircraft_tail     = "HB-XYZ";
    fd.aircraft_category = "fixed_wing";
    fd.landing_profile   = "medium_ga";
    fd.landing_thresholds = {-125, -250, -350, -600};
    fd.has_landing_thresholds = true;
    fd.start_time        = 1774944900;
    fd.end_time          = 1774950120;
    fd.block_time_min    = 87;
    fd.block_time_sec    = 5220;
    fd.paused_sec        = 60;
    fd.max_altitude_ft   = 9500;
    fd.max_speed_kts     = 165;

    TrackPoint tp;
    tp.t = 1774944900, tp.lat = 46.914, tp.lon = 7.497, tp.alt_ft = 1720, tp.spd_kts = 0, tp.vs_fpm = 0;
    fd.track.push_back(tp);
    tp.t = 1774945200, tp.lat = 46.800, tp.lon = 7.100, tp.alt_ft = 9500, tp.spd_kts = 165, tp.vs_fpm = 700;
    fd.track.push_back(tp);

    PauseEvent pause;
    pause.t = 1774946000, pause.sec = 60, pause.lat = 46.5, pause.lon = 6.9;
    fd.pauses.push_back(pause);

    LandingData ld;
    ld.fpm               = -142.5f;
    ld.g_force           = 1.23f;
    ld.pitch_deg         = 4.5f;
    ld.pitch_rate        = 0.8f;
    ld.agl_ft            = 2.f;
    ld.gate_ias_kts      = 78.f;
    ld.gate_fpm          = -520.f;
    ld.float_time        = 3.4f;
    ld.ias_kts           = 71.f;
    ld.ground_speed_kts  = 68.f;
    ld.bank_deg          = 2.5f;
    ld.yaw_rate_deg_s    = 1.5f;
    ld.lat               = 46.238;
    ld.lon               = 6.109;
    ld.heading_true      = 231.f;
    ld.time              = 1774950000;
    ld.wind_speed_kts    = 8;
    ld.wind_dir_mag      = 240;
    ld.headwind_kts      = 7;
    ld.crosswind_kts     = 3;
    ld.bounce_count      = 1;
    ld.is_rotorcraft     = false;
    ld.flare             = "GOOD";
    ld.rating            = "GREAT LANDING!";
    ld.wind_status       = "OK";
    ld.crosswind_side    = "left";
    ld.airport_icao      = "LSGG";
    ld.runway_ident      = "22";
    ld.runway_offset_m   = -1.5f;
    ld.runway_distance_m = 310.f;
    ld.runway_length_m   = 3900.f;

    ld.has_configuration    = true;
    ld.gear_retractable     = true;
    ld.gear_deploy_ratio    = 1.f;
    ld.flap_ratio           = 1.f;
    ld.gate_flap_ratio      = 1.f;
    ld.speedbrake_ratio     = -0.5f;
    ld.autopilot_engaged    = false;
    ld.meteo                = MeteoCondition::Imc;
    ld.visibility_m         = 3200.f;
    ld.ceiling_ft_agl       = 700.f;
    ld.has_ceiling          = true;
    ld.oat_c                = 8.f;
    ld.precipitation_ratio  = 0.3f;
    fd.landings.push_back(ld);

    return fd;
}

json written_json(const TempOutputDir &dir, const std::string &filename)
{
    std::ifstream f(dir.root / "flights" / filename);
    REQUIRE(f.is_open());
    json j;
    f >> j;
    return j;
}

} // namespace

// The flight JSON is compatibility surface: over 3000 users have logs in it, and a
// renamed or dropped key silently breaks their existing logbook.
TEST_CASE("FlightStore::save writes the v8 field set", "[flight_store][format]")
{
    TempOutputDir dir("v8_fields");
    const auto    filename = FlightStore::save(sample_flight(), dir.path());
    REQUIRE(filename == "2026-04-01_LSZB_LSGG_DA42.json");

    const json j = written_json(dir, filename);

    CHECK(j["version"] == 8);
    CHECK(j["date"] == "2026-04-01");
    CHECK(j["start_utc"] == "08:15");
    CHECK(j["end_utc"] == "09:42");
    CHECK(j["departure_icao"] == "LSZB");
    CHECK(j["arrival_icao"] == "LSGG");
    CHECK(j["aircraft_icao"] == "DA42");
    CHECK(j["aircraft_tail"] == "HB-XYZ");
    CHECK(j["aircraft_category"] == "fixed_wing");
    CHECK(j["landing_profile"] == "medium_ga");
    CHECK(j["landing_thresholds"] == json::array({-125, -250, -350, -600}));
    CHECK(j["start_time"] == 1774944900);
    CHECK(j["end_time"] == 1774950120);
    CHECK(j["block_time_min"] == 87);
    CHECK(j["block_time_sec"] == 5220);
    CHECK(j["paused_sec"] == 60);
    CHECK(j["max_altitude_ft"] == 9500);
    CHECK(j["max_speed_kts"] == 165);
    CHECK(j["fuel_used_kg"] == 0);

    REQUIRE(j["track"].size() == 2);
    for (const char *key : {"t", "lat", "lon", "alt", "spd", "vs"})
        CHECK(j["track"][0].contains(key));

    REQUIRE(j["pauses"].size() == 1);
    for (const char *key : {"t", "sec", "lat", "lon"})
        CHECK(j["pauses"][0].contains(key));

    REQUIRE(j["landings"].size() == 1);
    for (const char *key : {"fpm",           "g_force",        "pitch_deg",         "pitch_rate",
                            "agl_ft",        "gate_ias_kts",   "gate_fpm",          "float_time",
                            "ias_kts",       "ground_speed_kts", "lat",             "lon",
                            "heading_true",  "airport_icao",   "runway_ident",      "runway_offset_m",
                            "runway_distance_m", "runway_length_m", "time",         "wind_speed_kts",
                            "wind_dir_mag",  "wind_status",    "headwind_kts",      "crosswind_kts",
                            "crosswind_side", "bounce_count",  "is_rotorcraft",     "flare",
                            "rating",        "bank_deg",       "yaw_rate_deg_s",    "has_configuration",
                            "gear_retractable", "gear_deploy_ratio", "flap_ratio",  "gate_flap_ratio",
                            "speedbrake_ratio", "autopilot_engaged", "meteo",       "visibility_m",
                            "ceiling_ft_agl", "has_ceiling",   "oat_c",             "precipitation_ratio"})
        CHECK(j["landings"][0].contains(key));
}

// The reader is the only consumer that matters, so the pairing is what has to hold.
TEST_CASE("FlightStore::save round-trips through parse_flight_json", "[flight_store][format]")
{
    TempOutputDir    dir("round_trip");
    const FlightData original = sample_flight();
    const auto       filename = FlightStore::save(original, dir.path());
    REQUIRE_FALSE(filename.empty());

    std::ifstream     f(dir.root / "flights" / filename);
    const std::string content((std::istreambuf_iterator<char>(f)), {});
    const FlightData  parsed = parse_flight_json(content, filename);

    CHECK(parsed.date == original.date);
    CHECK(parsed.start_utc == original.start_utc);
    CHECK(parsed.end_utc == original.end_utc);
    CHECK(parsed.departure_icao == original.departure_icao);
    CHECK(parsed.arrival_icao == original.arrival_icao);
    CHECK(parsed.aircraft_icao == original.aircraft_icao);
    CHECK(parsed.aircraft_tail == original.aircraft_tail);
    CHECK(parsed.aircraft_category == original.aircraft_category);
    CHECK(parsed.landing_profile == original.landing_profile);
    CHECK(parsed.has_landing_thresholds);
    CHECK(parsed.landing_thresholds == original.landing_thresholds);
    CHECK(parsed.start_time == original.start_time);
    CHECK(parsed.end_time == original.end_time);
    CHECK(parsed.block_time_min == original.block_time_min);
    CHECK(parsed.block_time_sec == original.block_time_sec);
    CHECK(parsed.paused_sec == original.paused_sec);
    CHECK(parsed.max_altitude_ft == original.max_altitude_ft);
    CHECK(parsed.max_speed_kts == original.max_speed_kts);

    REQUIRE(parsed.track.size() == original.track.size());
    CHECK(parsed.track[1].alt_ft == original.track[1].alt_ft);
    CHECK(parsed.track[1].lat == Catch::Approx(original.track[1].lat));

    REQUIRE(parsed.pauses.size() == original.pauses.size());
    CHECK(parsed.pauses[0].sec == original.pauses[0].sec);

    REQUIRE(parsed.landings.size() == original.landings.size());
    const LandingData &a = parsed.landings[0];
    const LandingData &b = original.landings[0];
    CHECK(a.rating == b.rating);
    CHECK(a.flare == b.flare);
    CHECK(a.airport_icao == b.airport_icao);
    CHECK(a.runway_ident == b.runway_ident);
    CHECK(a.crosswind_side == b.crosswind_side);
    CHECK(a.wind_status == b.wind_status);
    CHECK(a.bounce_count == b.bounce_count);
    CHECK(a.is_rotorcraft == b.is_rotorcraft);
    CHECK(a.time == b.time);
    CHECK(a.fpm == Catch::Approx(b.fpm));
    CHECK(a.g_force == Catch::Approx(b.g_force));
    CHECK(a.runway_distance_m == Catch::Approx(b.runway_distance_m));
    CHECK(a.bank_deg == Catch::Approx(b.bank_deg));
    CHECK(a.yaw_rate_deg_s == Catch::Approx(b.yaw_rate_deg_s));

    CHECK(a.has_configuration == b.has_configuration);
    CHECK(a.gear_retractable == b.gear_retractable);
    CHECK(a.gear_deploy_ratio == Catch::Approx(b.gear_deploy_ratio));
    CHECK(a.flap_ratio == Catch::Approx(b.flap_ratio));
    CHECK(a.gate_flap_ratio == Catch::Approx(b.gate_flap_ratio));
    CHECK(a.speedbrake_ratio == Catch::Approx(b.speedbrake_ratio));
    CHECK(a.autopilot_engaged == b.autopilot_engaged);
    CHECK(a.meteo == b.meteo);
    CHECK(a.visibility_m == Catch::Approx(b.visibility_m));
    CHECK(a.ceiling_ft_agl == Catch::Approx(b.ceiling_ft_agl));
    CHECK(a.has_ceiling == b.has_ceiling);
    CHECK(a.oat_c == Catch::Approx(b.oat_c));
    CHECK(a.precipitation_ratio == Catch::Approx(b.precipitation_ratio));
}

// Empty codes have been stored as placeholders since v1; they also form the filename.
TEST_CASE("FlightStore::save substitutes placeholders for missing codes", "[flight_store][format]")
{
    TempOutputDir dir("placeholders");
    FlightData    fd = sample_flight();
    fd.departure_icao.clear();
    fd.arrival_icao.clear();
    fd.aircraft_icao.clear();

    const auto filename = FlightStore::save(fd, dir.path());
    REQUIRE(filename == "2026-04-01_ZZZZ_ZZZZ_UNKN.json");

    const json j = written_json(dir, filename);
    CHECK(j["departure_icao"] == "ZZZZ");
    CHECK(j["arrival_icao"] == "ZZZZ");
    CHECK(j["aircraft_icao"] == "UNKN");
}

// A second flight on the same route and day must not overwrite the first.
TEST_CASE("FlightStore::save keeps a same-name flight from overwriting", "[flight_store]")
{
    TempOutputDir    dir("collision");
    const FlightData fd = sample_flight();

    const auto first  = FlightStore::save(fd, dir.path());
    const auto second = FlightStore::save(fd, dir.path());

    CHECK(first == "2026-04-01_LSZB_LSGG_DA42.json");
    CHECK(second == "2026-04-01_LSZB_LSGG_DA42_1774944900.json");
    CHECK(first != second);
}

TEST_CASE("FlightStore::save reports failure when the flights dir is missing", "[flight_store]")
{
    const std::string nowhere =
        (std::filesystem::temp_directory_path() / "xp_pilot_store_absent_dir").generic_string() + "/";
    std::filesystem::remove_all(nowhere);

    CHECK(FlightStore::save(sample_flight(), nowhere).empty());
}

TEST_CASE("FlightStore::load_last_landing picks the newest flight that has one", "[flight_store]")
{
    TempOutputDir dir("last_landing");

    FlightData older       = sample_flight();
    older.date             = "2026-03-01";
    older.landings[0].rating = "ACCEPTABLE";
    REQUIRE_FALSE(FlightStore::save(older, dir.path()).empty());

    // Newer, but with no landing — must be skipped rather than ending the search.
    FlightData landless = sample_flight();
    landless.date       = "2026-05-01";
    landless.landings.clear();
    REQUIRE_FALSE(FlightStore::save(landless, dir.path()).empty());

    FlightData newer         = sample_flight();
    newer.date               = "2026-04-01";
    newer.landings[0].rating = "BUTTER!";
    REQUIRE_FALSE(FlightStore::save(newer, dir.path()).empty());

    LandingData found;
    REQUIRE(FlightStore::load_last_landing(dir.path(), found));
    CHECK(found.rating == "BUTTER!");
}

TEST_CASE("FlightStore::load_last_landing reports nothing when there are no flights", "[flight_store]")
{
    TempOutputDir dir("no_flights");
    LandingData   found;
    CHECK_FALSE(FlightStore::load_last_landing(dir.path(), found));
}
