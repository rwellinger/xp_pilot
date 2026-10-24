#include "../src/flight_legs.hpp"
#include <catch_amalgamated.hpp>

namespace
{

// Bern-Belp RWY 14 centerline, and two sites well away from the field.
constexpr double LSZB_LAT   = 46.9151789, LSZB_LON = 7.4958682;
constexpr double SITE_A_LAT = 46.7580, SITE_A_LON = 7.6280; // near Thun
constexpr double SITE_B_LAT = 46.6860, SITE_B_LON = 7.6900; // near Spiez

constexpr time_t T0 = 1774944900;

LandingData landing_at(const std::string &icao, float distance_m, double lat, double lon, time_t touchdown,
                       time_t liftoff, const std::string &nearby = "")
{
    LandingData ld;
    ld.airport_icao       = icao;
    ld.airport_distance_m = distance_m;
    ld.lat                = lat;
    ld.lon                = lon;
    ld.time               = touchdown;
    ld.liftoff_time       = liftoff;
    ld.nearby_place       = nearby;
    return ld;
}

FlightData departing_lszb()
{
    FlightData fd;
    fd.departure_icao       = "LSZB";
    fd.departure_lat        = LSZB_LAT;
    fd.departure_lon        = LSZB_LON;
    fd.departure_distance_m = 0.f;
    fd.arrival_icao         = "LSZB";
    fd.start_time           = T0;
    return fd;
}

} // namespace

TEST_CASE("derive_route: a circuit of touch-and-goes stays one airport", "[legs]")
{
    FlightData fd = departing_lszb();
    fd.landings.push_back(landing_at("LSZB", 3.f, LSZB_LAT, LSZB_LON, T0 + 600, T0 + 608));
    fd.landings.push_back(landing_at("LSZB", 2.f, LSZB_LAT, LSZB_LON, T0 + 1200, 0));

    const auto route = FlightLegs::derive_route(fd);

    CHECK(FlightLegs::route_text(route, " > ", true) == "LSZB > LSZB");
    CHECK(route.intermediate_stops() == 0);
    REQUIRE(route.legs.size() == 2);
    CHECK(route.legs[0].touch_and_go);
    CHECK(route.legs[0].ground_sec == 8);
    CHECK_FALSE(route.legs[1].touch_and_go);
    CHECK(route.legs[1].airborne_sec == 592);
}

TEST_CASE("derive_route: a rescue mission lists every site in order", "[legs]")
{
    FlightData fd = departing_lszb();
    fd.landings.push_back(landing_at("LSZB", 14000.f, SITE_A_LAT, SITE_A_LON, T0 + 420, T0 + 1020, "Thun"));
    fd.landings.push_back(landing_at("LSZB", 22000.f, SITE_B_LAT, SITE_B_LON, T0 + 1320, T0 + 1500, "Spiez"));
    fd.landings.push_back(landing_at("LSZB", 40.f, LSZB_LAT, LSZB_LON, T0 + 2100, 0));

    const auto route = FlightLegs::derive_route(fd);

    CHECK(FlightLegs::route_text(route, " > ", true) == "LSZB > LS1 (near Thun) > LS2 (near Spiez) > LSZB");
    CHECK(FlightLegs::route_text(route, "-", false) == "LSZB-LS1-LS2-LSZB");
    CHECK(route.intermediate_stops() == 2);
    REQUIRE(route.legs.size() == 3);
    CHECK(route.legs[0].airborne_sec == 420);
    CHECK(route.legs[0].ground_sec == 600);
    CHECK_FALSE(route.legs[0].touch_and_go);
    CHECK(route.legs[1].from.name == "LS1");
    CHECK(route.legs[1].to.name == "LS2");
    CHECK(route.legs[2].ground_sec == 0);
}

TEST_CASE("derive_route: returning to a site keeps its number", "[legs]")
{
    FlightData fd = departing_lszb();
    fd.landings.push_back(landing_at("LSZB", 14000.f, SITE_A_LAT, SITE_A_LON, T0 + 420, T0 + 600));
    fd.landings.push_back(landing_at("LSZB", 22000.f, SITE_B_LAT, SITE_B_LON, T0 + 900, T0 + 1000));
    fd.landings.push_back(landing_at("LSZB", 14000.f, SITE_A_LAT + 0.001, SITE_A_LON, T0 + 1300, 0));

    const auto route = FlightLegs::derive_route(fd);

    CHECK(FlightLegs::route_text(route, "-", false) == "LSZB-LS1-LS2-LS1");
}

TEST_CASE("derive_route: a site without a known place is named by its coordinates", "[legs]")
{
    FlightData fd = departing_lszb();
    fd.landings.push_back(landing_at("LSZB", 14000.f, SITE_A_LAT, SITE_A_LON, T0 + 420, 0));

    const auto route = FlightLegs::derive_route(fd);

    CHECK(route.stops.back().label == "LS1 (46.758N 7.628E)");
}

TEST_CASE("derive_route: an off-field lift-off is a site too", "[legs]")
{
    FlightData fd           = departing_lszb();
    fd.departure_lat        = SITE_A_LAT;
    fd.departure_lon        = SITE_A_LON;
    fd.departure_distance_m = 14000.f;
    fd.departure_nearby_place = "Thun";
    fd.landings.push_back(landing_at("LSZB", 10.f, LSZB_LAT, LSZB_LON, T0 + 420, 0));

    const auto route = FlightLegs::derive_route(fd);

    CHECK(FlightLegs::route_text(route, " > ", true) == "LS1 (near Thun) > LSZB");
}

TEST_CASE("derive_route: flights before v9 read as before", "[legs]")
{
    FlightData fd;
    fd.departure_icao = "LSZB";
    fd.arrival_icao   = "LSGG";
    fd.start_time     = T0;
    // A pre-v9 helicopter set-down 14 km out still carries the nearest airport's code.
    fd.landings.push_back(landing_at("LSZB", -1.f, SITE_A_LAT, SITE_A_LON, T0 + 420, 0));
    fd.landings.push_back(landing_at("LSGG", -1.f, 46.238, 6.109, T0 + 3600, 0));

    const auto route = FlightLegs::derive_route(fd);

    CHECK(FlightLegs::route_text(route, "-", false) == "LSZB-LSGG");
    REQUIRE(route.legs.size() == 2);
    CHECK(route.legs[1].airborne_sec == 0); // lift-off time unknown
    CHECK_FALSE(route.legs[0].touch_and_go);
}

TEST_CASE("derive_route: a flight without landings shows its recorded ends", "[legs]")
{
    FlightData fd   = departing_lszb();
    fd.arrival_icao = "LSGG";

    CHECK(FlightLegs::route_text(FlightLegs::derive_route(fd), "-", false) == "LSZB-LSGG");
}

TEST_CASE("derive_route: sim pauses are not flight time", "[legs]")
{
    FlightData fd = departing_lszb();
    PauseEvent pause;
    pause.t   = T0 + 100;
    pause.sec = 300;
    fd.pauses.push_back(pause);
    fd.landings.push_back(landing_at("LSZB", 14000.f, SITE_A_LAT, SITE_A_LON, T0 + 720, 0));

    CHECK(FlightLegs::derive_route(fd).legs[0].airborne_sec == 420);
}

TEST_CASE("is_off_field: the threshold and the unknown case", "[legs]")
{
    CHECK_FALSE(FlightLegs::is_off_field(-1.f));
    CHECK_FALSE(FlightLegs::is_off_field(0.f));
    CHECK_FALSE(FlightLegs::is_off_field(500.f));
    CHECK(FlightLegs::is_off_field(501.f));
}
