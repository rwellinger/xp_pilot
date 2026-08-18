#include "../src/map_overlay_cache.hpp"
#include "../src/airspace_data.hpp"
#include "../src/city_data.hpp"
#include "../src/coastline_data.hpp"
#include <catch_amalgamated.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

namespace
{
std::string fixture_path() { return std::string(XP_PILOT_TEST_FIXTURES_DIR) + "/mini_airspace.txt"; }

// Generous box around Bern and Geneva.
constexpr AirspaceBounds SWITZERLAND{46.0, 47.2, 5.8, 7.7};

const Airspace *find_named(const std::vector<Airspace> &airspaces, const std::string &name)
{
    const auto it = std::find_if(airspaces.begin(), airspaces.end(), [&](const Airspace &a) { return a.name == name; });
    return it == airspaces.end() ? nullptr : &*it;
}
} // namespace

TEST_CASE("load_airspaces keeps only what overlaps the bounds", "[airspace]")
{
    const auto airspaces = AirspaceData::load_airspaces(fixture_path(), SWITZERLAND);

    REQUIRE(find_named(airspaces, "BERN CTR") != nullptr);
    REQUIRE(find_named(airspaces, "GENEVA TMA") != nullptr);
    // Filtering matters: the world file holds ~24,000 airspaces and drawing the lot
    // would swamp both the map and the frame budget.
    REQUIRE(find_named(airspaces, "FAR AWAY RESTRICTED") == nullptr);
    REQUIRE(find_named(airspaces, "SOUTHERN HEMISPHERE") == nullptr);
}

// The regression this guards: filtering on outline points alone drops any airspace that
// surrounds the view. A CTR around the departure airport has all four corners outside a
// short flight's bounds — EDNY showed no airspaces at all because of this, while nearby
// LSZG happened to sit close enough to airspace edges to show some.
TEST_CASE("load_airspaces keeps an airspace that surrounds the view", "[airspace][regression]")
{
    // A patch well inside SURROUNDING CTR, with none of its corners nearby.
    const AirspaceBounds inside_ctr{46.9, 46.95, 7.4, 7.45};
    const auto           airspaces = AirspaceData::load_airspaces(fixture_path(), inside_ctr);

    REQUIRE(find_named(airspaces, "SURROUNDING CTR") != nullptr);
}

TEST_CASE("load_airspaces still rejects airspaces that only share a bounding box", "[airspace]")
{
    // Naively testing bounding-box overlap pulls in far-away airspaces whose outline
    // spans a wide longitude range; the view centre must actually fall inside.
    const AirspaceBounds pacific{-11.0, -9.0, -121.0, -119.0};
    const auto           airspaces = AirspaceData::load_airspaces(fixture_path(), pacific);

    REQUIRE(find_named(airspaces, "SURROUNDING CTR") == nullptr);
    REQUIRE(find_named(airspaces, "BERN CTR") == nullptr);
    CHECK(find_named(airspaces, "FAR AWAY RESTRICTED") != nullptr); // this one is genuinely there
}

TEST_CASE("load_airspaces reads class, name and altitude limits verbatim", "[airspace]")
{
    const auto  airspaces = AirspaceData::load_airspaces(fixture_path(), SWITZERLAND);
    const auto *bern      = find_named(airspaces, "BERN CTR");
    REQUIRE(bern != nullptr);

    CHECK(bern->airspace_class == "C");
    CHECK(bern->lower_limit == "GND");
    CHECK(bern->upper_limit == "5500 MSL");
    CHECK(bern->outline.size() == 3);

    const auto *geneva = find_named(airspaces, "GENEVA TMA");
    REQUIRE(geneva != nullptr);
    CHECK(geneva->airspace_class == "D");
    CHECK(geneva->upper_limit == "FL195");
}

TEST_CASE("load_airspaces converts DMS coordinates with the right sign", "[airspace]")
{
    const auto  airspaces = AirspaceData::load_airspaces(fixture_path(), SWITZERLAND);
    const auto *bern      = find_named(airspaces, "BERN CTR");
    REQUIRE(bern != nullptr);

    // "DP 46:54:00 N 007:29:00 E" -> 46 + 54/60, 7 + 29/60
    CHECK(bern->outline[0].lat == Catch::Approx(46.9));
    CHECK(bern->outline[0].lon == Catch::Approx(7.4833).margin(0.001));
}

TEST_CASE("load_airspaces drops outlines too small to be a polygon", "[airspace]")
{
    const auto airspaces = AirspaceData::load_airspaces(fixture_path(), SWITZERLAND);
    // Two points cannot enclose anything; drawing it would be a stray line on the map.
    REQUIRE(find_named(airspaces, "DEGENERATE TWO POINTS") == nullptr);
}

TEST_CASE("load_airspaces survives a missing file", "[airspace]")
{
    // X-Plane installs vary; a missing database must leave the map usable, not crash it.
    const auto airspaces = AirspaceData::load_airspaces("/nonexistent/airspace.txt", SWITZERLAND);
    REQUIRE(airspaces.empty());
}

// ── Cache ─────────────────────────────────────────────────────────────────────
// The cache exists because scanning the 17 MB world file costs ~110 ms — far too much
// for the frame that draws the map. These guard the threading contract around that.

TEST_CASE("the overlay cache stays empty and harmless without paths", "[airspace][cache]")
{
    MapOverlayCache::init("", "", ""); // as when X-Plane ships no airspace database
    REQUIRE(MapOverlayCache::for_bounds(46.0, 47.2, 5.8, 7.7).airspaces.empty());
    MapOverlayCache::stop();
}

TEST_CASE("the cache loads in the background and then serves from memory", "[airspace][cache]")
{
    MapOverlayCache::init(fixture_path(), "", "");

    // The first call only kicks off the load — the map draws without airspaces until
    // it lands, rather than stalling the frame.
    REQUIRE(MapOverlayCache::for_bounds(46.0, 47.2, 5.8, 7.7).airspaces.empty());

    std::vector<Airspace> loaded;
    for (int attempt = 0; attempt < 200 && loaded.empty(); ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        loaded = MapOverlayCache::for_bounds(46.0, 47.2, 5.8, 7.7).airspaces;
    }
    REQUIRE_FALSE(loaded.empty());
    CHECK(find_named(loaded, "BERN CTR") != nullptr);

    // A repeat for the same bounds is served from the cache, not reloaded.
    CHECK(MapOverlayCache::for_bounds(46.0, 47.2, 5.8, 7.7).airspaces.size() == loaded.size());

    MapOverlayCache::stop();
}

TEST_CASE("stop() joins a load that is still running", "[airspace][cache]")
{
    // Leaving the worker running past plugin shutdown would terminate X-Plane.
    MapOverlayCache::init(fixture_path(), "", "");
    (void)MapOverlayCache::for_bounds(46.0, 47.2, 5.8, 7.7);
    MapOverlayCache::stop(); // must not hang, crash or leave the thread behind
    SUCCEED("stop() returned cleanly with a load in flight");
}

// ── Coastlines and lakes ──────────────────────────────────────────────────────

namespace
{
std::string coastline_fixture() { return std::string(XP_PILOT_TEST_FIXTURES_DIR) + "/mini_coastlines.dat"; }
constexpr OutlineBounds SWISS_OUTLINE{46.0, 47.2, 5.8, 7.7};
} // namespace

TEST_CASE("load_outlines keeps what overlaps and separates lakes from coastlines", "[coastline]")
{
    const auto outlines = CoastlineData::load_outlines(coastline_fixture(), SWISS_OUTLINE);

    REQUIRE(outlines.size() == 2); // one coastline, one lake; the far ones are dropped
    const int lakes = static_cast<int>(std::count_if(outlines.begin(), outlines.end(),
                                                     [](const GeoOutline &o) { return o.kind == OutlineKind::Lake; }));
    CHECK(lakes == 1);
    // A lake is filled and a coastline is not, so mixing the two would paint land blue.
    CHECK(std::count_if(outlines.begin(), outlines.end(), [](const GeoOutline &o) { return o.kind == OutlineKind::Coastline; }) == 1);
}

TEST_CASE("load_outlines returns the largest parts first", "[coastline]")
{
    // The draw budget is finite; when it runs out on a wide view, the big coastlines
    // must already be drawn rather than losing out to an anonymous islet.
    const auto outlines = CoastlineData::load_outlines(coastline_fixture(), {-90.0, 90.0, -180.0, 180.0});

    REQUIRE(outlines.size() >= 2);
    for (size_t i = 1; i < outlines.size(); ++i)
        REQUIRE(outlines[i - 1].points.size() >= outlines[i].points.size());
}

TEST_CASE("load_outlines reads lon/lat in file order into lat/lon fields", "[coastline]")
{
    const auto outlines = CoastlineData::load_outlines(coastline_fixture(), SWISS_OUTLINE);
    const auto coast    = std::find_if(outlines.begin(), outlines.end(), [](const GeoOutline &o) { return o.kind == OutlineKind::Coastline; });
    REQUIRE(coast != outlines.end());

    // File line is "7.0000 46.5000" — longitude first, as GeoJSON has it.
    CHECK(coast->points[0].lon == Catch::Approx(7.0));
    CHECK(coast->points[0].lat == Catch::Approx(46.5));
}

TEST_CASE("load_outlines survives a missing file", "[coastline]")
{
    REQUIRE(CoastlineData::load_outlines("/nonexistent/coastlines.dat", SWISS_OUTLINE).empty());
}

TEST_CASE("the shipped coastline data parses and covers Lake Geneva", "[coastline][data]")
{
    // Guards the generated file itself: a broken tools/build_coastlines.py run would
    // otherwise only show up as an empty map.
    const auto outlines = CoastlineData::load_outlines(std::string(XP_PILOT_SOURCE_DIR) + "/data/coastlines.dat",
                                                       {46.2, 46.6, 6.1, 6.9});
    REQUIRE_FALSE(outlines.empty());
    CHECK(std::any_of(outlines.begin(), outlines.end(), [](const GeoOutline &o) { return o.kind == OutlineKind::Lake; }));
}

// ── Populated places ──────────────────────────────────────────────────────────

namespace
{
std::string city_fixture() { return std::string(XP_PILOT_TEST_FIXTURES_DIR) + "/mini_cities.dat"; }
} // namespace

TEST_CASE("load_cities returns the largest places inside the view", "[cities]")
{
    // A local Swiss view: Berlin and Paris are outside it.
    const auto cities = CityData::load_cities(city_fixture(), {46.5, 47.8, 6.8, 8.8}, 10);

    REQUIRE(cities.size() == 4);
    CHECK(cities[0].name == "Zürich"); // file is population-ordered, so is the result
    CHECK(cities[0].population == 800000);
    CHECK(cities.back().name == "Grenchen");
}

TEST_CASE("load_cities honours the limit, keeping the biggest", "[cities]")
{
    // The limit is what keeps labels readable: a wide view must not draw every village.
    const auto cities = CityData::load_cities(city_fixture(), {46.5, 47.8, 6.8, 8.8}, 2);

    REQUIRE(cities.size() == 2);
    CHECK(cities[0].name == "Zürich");
    CHECK(cities[1].name == "Basel");
}

TEST_CASE("load_cities parses coordinates and non-ASCII names", "[cities]")
{
    const auto cities = CityData::load_cities(city_fixture(), {47.3, 47.5, 8.4, 8.7}, 5);
    REQUIRE(cities.size() == 1);
    CHECK(cities[0].name == "Zürich"); // the name field may contain anything but a newline
    CHECK(cities[0].lat == Catch::Approx(47.3769));
    CHECK(cities[0].lon == Catch::Approx(8.5417));
}

TEST_CASE("load_cities skips malformed lines instead of placing ghost cities", "[cities]")
{
    // Unchecked atoi/atof would turn every broken line into a city at (0, 0) — a dot in
    // the Gulf of Guinea on any map that happens to include it.
    const auto gulf_of_guinea = CityData::load_cities(city_fixture(), {-1.0, 1.0, -1.0, 1.0}, 10);
    CHECK(gulf_of_guinea.empty());

    // The good lines around them still load.
    const auto swiss = CityData::load_cities(city_fixture(), {46.5, 47.8, 6.8, 8.8}, 10);
    CHECK(swiss.size() == 4);
}

TEST_CASE("load_cities survives a missing file and a zero limit", "[cities]")
{
    CHECK(CityData::load_cities("/nonexistent/cities.dat", {46.0, 48.0, 6.0, 9.0}, 10).empty());
    CHECK(CityData::load_cities(city_fixture(), {46.0, 48.0, 6.0, 9.0}, 0).empty());
}

TEST_CASE("the shipped city data parses and covers the Zurich-Paris corridor", "[cities][data]")
{
    const auto cities =
        CityData::load_cities(std::string(XP_PILOT_SOURCE_DIR) + "/data/cities.dat", {47.4, 49.1, 2.4, 8.6}, 14);
    REQUIRE(cities.size() >= 5);
    // Population order is what makes the limit meaningful.
    CHECK(cities.front().population >= cities.back().population);
}

// The regression this guards: a view crossing the date line has bounds running past
// ±180 (say 171 to 245). Comparing raw longitudes then drops everything on the American
// side — the Auckland-Los Angeles map would show no US coastline at all.
TEST_CASE("load_cities finds places on both sides of the date line", "[cities][dateline]")
{
    const std::string shipped = std::string(XP_PILOT_SOURCE_DIR) + "/data/cities.dat";
    // Bounds as track_bounds produces them for Auckland -> Los Angeles.
    const auto cities = CityData::load_cities(shipped, {-40.6, 37.5, 171.4, 244.9}, 20);

    REQUIRE_FALSE(cities.empty());
    const auto has = [&](const std::string &name) {
        return std::any_of(cities.begin(), cities.end(), [&](const City &c) { return c.name == name; });
    };
    CHECK(has("Auckland"));     // east of the date line, longitude ~175
    CHECK(has("Los Angeles"));  // west of it, longitude ~-118 -> unwrapped to ~242
}
