#include "../src/airspace_cache.hpp"
#include "../src/airspace_data.hpp"
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

TEST_CASE("the cache stays empty and harmless without a path", "[airspace][cache]")
{
    AirspaceCache::init(""); // as when X-Plane ships no airspace database
    REQUIRE(AirspaceCache::for_bounds(SWITZERLAND).empty());
    AirspaceCache::stop();
}

TEST_CASE("the cache loads in the background and then serves from memory", "[airspace][cache]")
{
    AirspaceCache::init(fixture_path());

    // The first call only kicks off the load — the map draws without airspaces until
    // it lands, rather than stalling the frame.
    REQUIRE(AirspaceCache::for_bounds(SWITZERLAND).empty());

    std::vector<Airspace> loaded;
    for (int attempt = 0; attempt < 200 && loaded.empty(); ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        loaded = AirspaceCache::for_bounds(SWITZERLAND);
    }
    REQUIRE_FALSE(loaded.empty());
    CHECK(find_named(loaded, "BERN CTR") != nullptr);

    // A repeat for the same bounds is served from the cache, not reloaded.
    CHECK(AirspaceCache::for_bounds(SWITZERLAND).size() == loaded.size());

    AirspaceCache::stop();
}

TEST_CASE("stop() joins a load that is still running", "[airspace][cache]")
{
    // Leaving the worker running past plugin shutdown would terminate X-Plane.
    AirspaceCache::init(fixture_path());
    AirspaceCache::for_bounds(SWITZERLAND);
    AirspaceCache::stop(); // must not hang, crash or leave the thread behind
    SUCCEED("stop() returned cleanly with a load in flight");
}
