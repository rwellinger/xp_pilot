#include "../src/map_overlay_cache.hpp"
#include "../src/airspace_data.hpp"
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
    MapOverlayCache::init("", ""); // as when X-Plane ships no airspace database
    REQUIRE(MapOverlayCache::for_bounds(46.0, 47.2, 5.8, 7.7).airspaces.empty());
    MapOverlayCache::stop();
}

TEST_CASE("the cache loads in the background and then serves from memory", "[airspace][cache]")
{
    MapOverlayCache::init(fixture_path(), "");

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
    MapOverlayCache::init(fixture_path(), "");
    MapOverlayCache::for_bounds(46.0, 47.2, 5.8, 7.7).airspaces;
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
                                                     [](const GeoOutline &o) { return o.is_lake; }));
    CHECK(lakes == 1);
    // A lake is filled and a coastline is not, so mixing the two would paint land blue.
    CHECK(std::count_if(outlines.begin(), outlines.end(), [](const GeoOutline &o) { return !o.is_lake; }) == 1);
}

TEST_CASE("load_outlines reads lon/lat in file order into lat/lon fields", "[coastline]")
{
    const auto outlines = CoastlineData::load_outlines(coastline_fixture(), SWISS_OUTLINE);
    const auto coast    = std::find_if(outlines.begin(), outlines.end(), [](const GeoOutline &o) { return !o.is_lake; });
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
    CHECK(std::any_of(outlines.begin(), outlines.end(), [](const GeoOutline &o) { return o.is_lake; }));
}
