#include "../src/runway_data.hpp"
#include "../src/runway_geometry.hpp"
#include <catch_amalgamated.hpp>

#include <string>

namespace
{
std::string fixture_path()
{
    return std::string(XP_PILOT_TEST_FIXTURES_DIR) + "/mini_apt.dat";
}

std::vector<Runway> lszb_runways()
{
    return RunwayData::load_runways(fixture_path(), {"LSZB"}).at("LSZB");
}

const RunwayEnd &end_named(const std::vector<Runway> &runways, const std::string &designator)
{
    for (const Runway &r : runways)
        for (int i = 0; i < 2; ++i)
            if (r.ends[i].designator == designator)
                return r.ends[i];
    FAIL("runway end " << designator << " not found");
    return runways.at(0).ends[0];
}

// Touchdown coordinates derived independently with spherical direct/inverse formulas,
// measured from the displaced threshold of LSZB RWY 14 (course 140.21 deg true).
constexpr double TD_ON_CENTERLINE_LAT = 46.9151789;
constexpr double TD_ON_CENTERLINE_LON = 7.4958682;
constexpr double TD_5M_RIGHT_LAT      = 46.9151501;
constexpr double TD_5M_RIGHT_LON      = 7.4958176;
constexpr double RWY14_COURSE_DEG     = 140.21;
constexpr double RWY14_USABLE_M       = 1527.0;
} // namespace

TEST_CASE("load_runways: parses land runways of the requested airport")
{
    const std::vector<Runway> runways = lszb_runways();

    REQUIRE(runways.size() == 2);
    CHECK(runways[0].width_m == Catch::Approx(30.0));
    CHECK(runways[0].ends[0].designator == "16");
    CHECK(runways[0].ends[1].designator == "34");
    CHECK(runways[1].ends[0].designator == "14");
    CHECK(runways[1].ends[1].designator == "32");
}

TEST_CASE("load_runways: keeps the displaced threshold")
{
    const std::vector<Runway> runways = lszb_runways();

    CHECK(end_named(runways, "14").displaced_threshold_m == Catch::Approx(200.0));
    CHECK(end_named(runways, "32").displaced_threshold_m == Catch::Approx(0.0));
    CHECK(end_named(runways, "16").displaced_threshold_m == Catch::Approx(0.0));
}

TEST_CASE("load_runways: airports stay separate and helipads are ignored")
{
    const auto found = RunwayData::load_runways(fixture_path(), {"LSZB", "LSGG"});

    REQUIRE(found.size() == 2);
    CHECK(found.at("LSZB").size() == 2); // the 102 helipad row is not a runway
    CHECK(found.at("LSGG").size() == 1);
    CHECK(found.at("LSGG")[0].ends[0].designator == "04");
}

TEST_CASE("load_runways: unknown airport and unreadable file yield nothing")
{
    CHECK(RunwayData::load_runways(fixture_path(), {"KJFK"}).empty());
    CHECK(RunwayData::load_runways(fixture_path(), {}).empty());
    CHECK(RunwayData::load_runways("/nonexistent/apt.dat", {"LSZB"}).empty());
}

TEST_CASE("locate_touchdown: measures distance from the displaced threshold")
{
    const RunwayFix fix = RunwayGeometry::locate_touchdown(lszb_runways(), TD_ON_CENTERLINE_LAT,
                                                           TD_ON_CENTERLINE_LON, RWY14_COURSE_DEG);

    CHECK(fix.runway_ident == "14");
    CHECK(fix.distance_from_thr_m == Catch::Approx(400.0).margin(2.0));
    CHECK(fix.centerline_offset_m == Catch::Approx(0.0).margin(1.0));
    CHECK(fix.runway_length_m == Catch::Approx(RWY14_USABLE_M).margin(5.0));
}

TEST_CASE("locate_touchdown: centerline offset is signed, positive to the right")
{
    const RunwayFix right =
        RunwayGeometry::locate_touchdown(lszb_runways(), TD_5M_RIGHT_LAT, TD_5M_RIGHT_LON, RWY14_COURSE_DEG);

    CHECK(right.runway_ident == "14");
    CHECK(right.centerline_offset_m == Catch::Approx(5.0).margin(0.5));

    // The same point approached from the opposite end lies to the left instead.
    const RunwayFix reverse = RunwayGeometry::locate_touchdown(lszb_runways(), TD_5M_RIGHT_LAT, TD_5M_RIGHT_LON,
                                                              RWY14_COURSE_DEG - 180.0);
    CHECK(reverse.runway_ident == "32");
    CHECK(reverse.centerline_offset_m == Catch::Approx(-5.0).margin(0.5));
}

TEST_CASE("locate_touchdown: heading selects the landing direction")
{
    const std::vector<Runway> runways = lszb_runways();

    CHECK(RunwayGeometry::locate_touchdown(runways, TD_ON_CENTERLINE_LAT, TD_ON_CENTERLINE_LON, 140.0).runway_ident ==
          "14");
    CHECK(RunwayGeometry::locate_touchdown(runways, TD_ON_CENTERLINE_LAT, TD_ON_CENTERLINE_LON, 320.0).runway_ident ==
          "32");
}

TEST_CASE("locate_touchdown: rejects positions and headings off the runway")
{
    const std::vector<Runway> runways = lszb_runways();

    // Crossing the runway at 90 degrees is not a landing on it.
    CHECK(RunwayGeometry::locate_touchdown(runways, TD_ON_CENTERLINE_LAT, TD_ON_CENTERLINE_LON, 50.0)
              .runway_ident.empty());
    // A field two kilometres away belongs to no runway.
    CHECK(RunwayGeometry::locate_touchdown(runways, 46.9400000, 7.4908128, RWY14_COURSE_DEG).runway_ident.empty());
    // Long past the far end.
    CHECK(RunwayGeometry::locate_touchdown(runways, 46.9040000, 7.5100000, RWY14_COURSE_DEG).runway_ident.empty());
    CHECK(RunwayGeometry::locate_touchdown({}, TD_ON_CENTERLINE_LAT, TD_ON_CENTERLINE_LON, RWY14_COURSE_DEG)
              .runway_ident.empty());
}
