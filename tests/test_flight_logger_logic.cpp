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

#include "../src/geo_longitude.hpp"
#include "flight_logger_logic.hpp"

#include <catch2/catch_amalgamated.hpp>
#include <json.hpp>

#include <array>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

using FlightLoggerLogic::is_plausible_speed_sample;
using FlightLoggerLogic::MAX_IAS_STEP_PER_SAMPLE;
using FlightLoggerLogic::MAX_PLAUSIBLE_IAS_KTS;

TEST_CASE("first sample is accepted within the absolute bound", "[flight_logger][speed]")
{
    REQUIRE(is_plausible_speed_sample(0, 0, false) == true);
    REQUIRE(is_plausible_speed_sample(140, 0, false) == true);
    REQUIRE(is_plausible_speed_sample(MAX_PLAUSIBLE_IAS_KTS, 0, false) == true);
}

TEST_CASE("first sample beyond the absolute bound is rejected", "[flight_logger][speed]")
{
    REQUIRE(is_plausible_speed_sample(MAX_PLAUSIBLE_IAS_KTS + 1, 0, false) == false);
    REQUIRE(is_plausible_speed_sample(-1, 0, false) == false);
}

TEST_CASE("normal acceleration between two samples is accepted", "[flight_logger][speed]")
{
    REQUIRE(is_plausible_speed_sample(180, 150, true) == true);
    REQUIRE(is_plausible_speed_sample(120, 250, true) == true); // deceleration
    REQUIRE(is_plausible_speed_sample(250 + MAX_IAS_STEP_PER_SAMPLE, 250, true) == true);
}

TEST_CASE("a reposition spike between two samples is rejected", "[flight_logger][speed]")
{
    REQUIRE(is_plausible_speed_sample(250 + MAX_IAS_STEP_PER_SAMPLE + 1, 250, true) == false);
    REQUIRE(is_plausible_speed_sample(4000, 250, true) == false);
    REQUIRE(is_plausible_speed_sample(0, 250, true) == false); // instant drop to zero
}

TEST_CASE("bounds of an empty track are still divisible", "[flight_logger][track]")
{
    const auto bounds = FlightLoggerLogic::track_bounds({});
    REQUIRE(bounds.lat_max > bounds.lat_min);
    REQUIRE(bounds.lon_max > bounds.lon_min);
}

TEST_CASE("a single track point gets padded into a real box", "[flight_logger][track]")
{
    std::vector<TrackPoint> track(1);
    track[0].lat = 46.9;
    track[0].lon = 7.5;

    const auto bounds = FlightLoggerLogic::track_bounds(track);
    REQUIRE(bounds.lat_min == Catch::Approx(46.9 - FlightLoggerLogic::TRACK_BOUNDS_MIN_PADDING_DEG));
    REQUIRE(bounds.lat_max == Catch::Approx(46.9 + FlightLoggerLogic::TRACK_BOUNDS_MIN_PADDING_DEG));
    REQUIRE(bounds.lon_min == Catch::Approx(7.5 - FlightLoggerLogic::TRACK_BOUNDS_MIN_PADDING_DEG));
    REQUIRE(bounds.lon_max == Catch::Approx(7.5 + FlightLoggerLogic::TRACK_BOUNDS_MIN_PADDING_DEG));
}

TEST_CASE("bounds follow the track's own extent", "[flight_logger][track]")
{
    // The map frames the flight itself: a short circuit stays zoomed in, which keeps the
    // track legible even though nearby airspace boundaries then fall outside the frame.
    std::vector<TrackPoint> track(2);
    track[0].lat = 47.4;
    track[0].lon = 8.5;
    track[1].lat = 49.0;
    track[1].lon = 2.5;

    const auto bounds = FlightLoggerLogic::track_bounds(track);
    REQUIRE(bounds.lat_max - bounds.lat_min == Catch::Approx(1.6 + 2 * (1.6 * 0.05 + 0.001)));
}

TEST_CASE("bounds span the extremes of the track with padding", "[flight_logger][track]")
{
    std::vector<TrackPoint> track(3);
    track[0].lat = 46.0;
    track[0].lon = 7.0;
    track[1].lat = 47.0;
    track[1].lon = 6.0;
    track[2].lat = 46.5;
    track[2].lon = 8.0;

    const auto bounds = FlightLoggerLogic::track_bounds(track);
    REQUIRE(bounds.lat_min < 46.0);
    REQUIRE(bounds.lat_max > 47.0);
    REQUIRE(bounds.lon_min < 6.0);
    REQUIRE(bounds.lon_max > 8.0);
    // 1 degree of latitude spread, 5% padding on each side plus the minimum.
    REQUIRE(bounds.lat_max - bounds.lat_min == Catch::Approx(1.0 + 2 * (0.05 + 0.001)));
}

TEST_CASE("projection centres the map and keeps north above south", "[flight_logger][track]")
{
    const FlightLoggerLogic::GeoBounds bounds{46.0, 47.0, 7.0, 8.0};
    const auto     viewport = FlightLoggerLogic::make_viewport(bounds, 200.f, 100.f);
    float          x = 0, y = 0;

    // The centre of the bounds lands in the centre of the box, whatever the letterboxing.
    FlightLoggerLogic::project_to_pixel(viewport, 46.5, 7.5, x, y);
    REQUIRE(x == Catch::Approx(100.f).margin(0.5f));
    REQUIRE(y == Catch::Approx(50.f).margin(0.5f));

    float north_y = 0, south_y = 0, unused = 0;
    FlightLoggerLogic::project_to_pixel(viewport, 47.0, 7.5, unused, north_y);
    FlightLoggerLogic::project_to_pixel(viewport, 46.0, 7.5, unused, south_y);
    REQUIRE(north_y < south_y); // latitude grows upwards, pixel y downwards

    float west_x = 0, east_x = 0;
    FlightLoggerLogic::project_to_pixel(viewport, 46.5, 7.0, west_x, unused);
    FlightLoggerLogic::project_to_pixel(viewport, 46.5, 8.0, east_x, unused);
    REQUIRE(west_x < east_x);
}

TEST_CASE("km_per_pixel converts pixel distance back to ground distance", "[flight_logger][track]")
{
    // At 46 N one degree of longitude is 111.32 * cos(46) = 77.3 km on the ground.
    const FlightLoggerLogic::GeoBounds bounds{45.5, 46.5, 7.0, 8.0};
    const auto   viewport  = FlightLoggerLogic::make_viewport(bounds, 400.f, 400.f);
    const double km_per_px = FlightLoggerLogic::km_per_pixel(bounds, viewport);

    float west_x = 0, east_x = 0, unused = 0;
    FlightLoggerLogic::project_to_pixel(viewport, 46.0, 7.0, west_x, unused);
    FlightLoggerLogic::project_to_pixel(viewport, 46.0, 8.0, east_x, unused);

    const double measured_km = (east_x - west_x) * km_per_px;
    REQUIRE(measured_km == Catch::Approx(77.3).margin(1.0));
}

TEST_CASE("km_per_pixel falls as the map grows", "[flight_logger][track]")
{
    const FlightLoggerLogic::GeoBounds bounds{45.5, 46.5, 7.0, 8.0};

    const double small = FlightLoggerLogic::km_per_pixel(bounds, FlightLoggerLogic::make_viewport(bounds, 400.f, 400.f));
    const double large = FlightLoggerLogic::km_per_pixel(bounds, FlightLoggerLogic::make_viewport(bounds, 800.f, 800.f));

    REQUIRE(small > 0.0);
    REQUIRE(large == Catch::Approx(small / 2.0).epsilon(0.01));
}

// The regression this guards: a linear lat/lon stretch into a fixed-aspect box squashed
// north-south flights, the more so the further from the equator they happened.
TEST_CASE("projection preserves the aspect ratio and letterboxes the remainder", "[flight_logger][track]")
{
    // One degree of latitude covers more Mercator span than one of longitude at 46 N,
    // so a square-in-degrees box is taller than it is wide once projected.
    const FlightLoggerLogic::GeoBounds bounds{46.0, 47.0, 7.0, 8.0};
    const auto viewport = FlightLoggerLogic::make_viewport(bounds, 400.f, 400.f);

    float left_x = 0, right_x = 0, top_y = 0, bottom_y = 0, unused = 0;
    FlightLoggerLogic::project_to_pixel(viewport, 46.5, 7.0, left_x, unused);
    FlightLoggerLogic::project_to_pixel(viewport, 46.5, 8.0, right_x, unused);
    FlightLoggerLogic::project_to_pixel(viewport, 47.0, 7.5, unused, top_y);
    FlightLoggerLogic::project_to_pixel(viewport, 46.0, 7.5, unused, bottom_y);

    const float drawn_w = right_x - left_x;
    const float drawn_h = bottom_y - top_y;
    REQUIRE(drawn_h > drawn_w);                          // no squashing into the square box
    REQUIRE(drawn_h == Catch::Approx(400.f).margin(1.f)); // the taller axis fills the box
    REQUIRE(left_x > 0.f);                               // the narrower one is letterboxed
    REQUIRE(left_x == Catch::Approx(400.f - right_x).margin(0.5f)); // ...symmetrically

    // Same scale on both axes is what "undistorted" means.
    REQUIRE(viewport.scale > 0.0);
}

// ── Pause total ───────────────────────────────────────────────────────────────
// The regression these guard: feeding the total from a whole-second wall clock while
// the active time accumulates continuously made the difference oscillate between 0
// and 1, so a flight that was never paused reported a phantom pause of one second.

TEST_CASE("a flight that was never paused reports no pause", "[flight_logger][pause]")
{
    REQUIRE(FlightLoggerLogic::paused_seconds(0.0, 0.0) == 0);
    REQUIRE(FlightLoggerLogic::paused_seconds(2843.5, 2843.5) == 0);
}

TEST_CASE("floating point noise is not reported as a pause", "[flight_logger][pause]")
{
    REQUIRE(FlightLoggerLogic::paused_seconds(100.0000001, 100.0) == 0);
    REQUIRE(FlightLoggerLogic::paused_seconds(100.49, 100.0) == 0);
}

TEST_CASE("a real pause is reported to the nearest second", "[flight_logger][pause]")
{
    REQUIRE(FlightLoggerLogic::paused_seconds(130.4, 100.2) == 30);
    REQUIRE(FlightLoggerLogic::paused_seconds(1300.0, 100.0) == 1200);
    REQUIRE(FlightLoggerLogic::paused_seconds(100.6, 100.0) == 1);
}

TEST_CASE("active time exceeding total time cannot go negative", "[flight_logger][pause]")
{
    REQUIRE(FlightLoggerLogic::paused_seconds(100.0, 140.0) == 0);
}

// ── Date line ─────────────────────────────────────────────────────────────────
// The regression these guard: longitude wraps at ±180, and a Pacific crossing steps
// from 179.99 to -179.99. Treated as plain numbers that spans the globe — Auckland to
// Los Angeles rendered as a world map with the track jumping between the two edges.
// Long-haul users fly these routes routinely.

TEST_CASE("unwrapped_near expresses longitude next to a reference", "[flight_logger][dateline]")
{
    CHECK(GeoLongitude::unwrapped_near(-179.0, 179.0) == Catch::Approx(181.0));
    CHECK(GeoLongitude::unwrapped_near(179.0, -179.0) == Catch::Approx(-181.0));
    CHECK(GeoLongitude::unwrapped_near(8.5, 7.0) == Catch::Approx(8.5)); // untouched nearby
    CHECK(GeoLongitude::unwrapped_near(-120.0, 210.0) == Catch::Approx(240.0));
}

TEST_CASE("track_bounds keeps a date-line crossing compact", "[flight_logger][dateline]")
{
    std::vector<TrackPoint> track(4); // Auckland heading east past the date line
    track[0].lat = -37.0; track[0].lon = 174.8;
    track[1].lat = -20.0; track[1].lon = 179.9;
    track[2].lat = 0.0;   track[2].lon = -179.9;
    track[3].lat = 21.3;  track[3].lon = -157.9; // Honolulu

    const auto bounds = FlightLoggerLogic::track_bounds(track);

    // Unwrapped the span is about 27 degrees; wrapped it would be the full 360.
    REQUIRE(bounds.lon_max - bounds.lon_min < 40.0);
    REQUIRE(bounds.lon_min > 170.0); // stays east of the date line rather than at -180
}

TEST_CASE("projection places both sides of the date line side by side", "[flight_logger][dateline]")
{
    std::vector<TrackPoint> track(2);
    track[0].lat = 0.0; track[0].lon = 179.0;
    track[1].lat = 0.0; track[1].lon = -179.0; // two degrees further east

    const auto bounds   = FlightLoggerLogic::track_bounds(track);
    const auto viewport = FlightLoggerLogic::make_viewport(bounds, 1000.f, 500.f);

    float west_x = 0, east_x = 0, unused = 0;
    FlightLoggerLogic::project_to_pixel(viewport, 0.0, 179.0, west_x, unused);
    FlightLoggerLogic::project_to_pixel(viewport, 0.0, -179.0, east_x, unused);

    REQUIRE(east_x > west_x);                     // continues east, does not jump back
    REQUIRE(east_x - west_x < 1000.f);            // and not across the whole map
}

// ── Aircraft profile mapping ─────────────────────────────────────────────────
// The regression these guard: a helicopter whose ICAO is missing from
// flight_logger_profiles.json resolves to a fixed-wing profile. It then takes the
// fixed-wing state path, which only starts recording above 30 kts ground speed — a
// helicopter lifting off from a hover never gets there, so its flight is silently
// never written. The AW139 (A139) and the Guimbal Cabri G2 (G2CA) hit exactly that.

namespace
{

struct ProfileMapping
{
    std::vector<std::pair<std::string, std::string>> aircraft;   // match string -> profile name
    std::map<std::string, std::string>               categories; // profile name -> category
};

ProfileMapping load_profile_mapping()
{
    std::ifstream file(std::string(XP_PILOT_SOURCE_DIR) + "/data/flight_logger_profiles.json");
    REQUIRE(file.is_open());
    nlohmann::json j;
    file >> j;

    ProfileMapping mapping;
    for (auto &[name, value] : j["profiles"].items())
        mapping.categories[name] = value.is_object() ? value.value("category", "fixed_wing") : "fixed_wing";
    for (auto &entry : j["aircraft"])
        mapping.aircraft.emplace_back(entry.value("match", ""), entry.value("profile", "medium_ga"));
    return mapping;
}

// Mirrors get_profile_name() in flight_logger.cpp: first entry in file order whose
// match string is contained in the ICAO wins, medium_ga otherwise.
std::string category_of(const ProfileMapping &mapping, const std::string &icao)
{
    for (const auto &[match, profile] : mapping.aircraft)
        if (FlightLoggerLogic::icao_matches_profile(icao, match))
            return mapping.categories.at(profile);
    return mapping.categories.count("medium_ga") ? mapping.categories.at("medium_ga") : "fixed_wing";
}

} // namespace

TEST_CASE("every helicopter in the profile list resolves to a rotorcraft profile", "[flight_logger][profiles]")
{
    const ProfileMapping mapping = load_profile_mapping();

    // An earlier fixed-wing entry containing the same substring would shadow a
    // helicopter, and the shadowed type would silently stop being recorded.
    for (const auto &[match, profile] : mapping.aircraft)
        if (mapping.categories.at(profile) == "rotorcraft")
            CHECK(category_of(mapping, match) == "rotorcraft");
}

TEST_CASE("helicopters flown in the sim are covered by the profile list", "[flight_logger][profiles]")
{
    const ProfileMapping mapping = load_profile_mapping();

    for (const char *icao : {"R22", "R44", "R66", "G2CA", "EC30", "H130", "A109", "A139", "S76", "H145", "B407"})
        CHECK(category_of(mapping, icao) == "rotorcraft");
}

TEST_CASE("fixed-wing types are not caught by a helicopter match string", "[flight_logger][profiles]")
{
    const ProfileMapping mapping = load_profile_mapping();

    for (const char *icao : {"C172", "PA28", "SR22", "TBM9", "B738", "A320", "A333", "B77W", "E170"})
        CHECK(category_of(mapping, icao) == "fixed_wing");
}

namespace
{

// Mirrors get_profile_name(): the profile itself, not just its category.
std::string profile_of(const ProfileMapping &mapping, const std::string &icao)
{
    for (const auto &[match, profile] : mapping.aircraft)
        if (FlightLoggerLogic::icao_matches_profile(icao, match))
            return profile;
    return "medium_ga";
}

} // namespace

// The regression this guards: a match string that no aircraft in the sim actually
// reports. "ASK2" and "EVOL" were written from the model name rather than the ICAO the
// airframe sends, so both entries sat in the list without ever matching and their
// aircraft silently fell through to medium_ga.
TEST_CASE("default-fleet ICAO codes reach their intended profile", "[flight_logger][profiles]")
{
    const ProfileMapping mapping = load_profile_mapping();

    CHECK(profile_of(mapping, "AS21") == "ultra_light"); // Laminar Schleicher ASK 21
    CHECK(profile_of(mapping, "EVOT") == "turboprop");   // Laminar Lancair Evolution, PT6
    CHECK(profile_of(mapping, "PA18") == "light_ga");
    CHECK(profile_of(mapping, "BE9L") == "turboprop");
    CHECK(profile_of(mapping, "SF50") == "vlj");

    // The longer add-on codes the original entries were aimed at keep working.
    CHECK(profile_of(mapping, "ASK21") == "ultra_light");
    CHECK(profile_of(mapping, "EVOL") == "medium_ga");
}

// ── Rotorcraft landing rating ────────────────────────────────────────────────
// The regression these guard: the rating used to come from descent rate alone. A
// helicopter set down from the hover has a descent rate near zero, so every landing —
// including one that slid sideways onto the skids — came out as "BUTTER!".

namespace
{

// Thresholds of the turbine_helicopter profile in data/flight_logger_profiles.json.
constexpr std::array<int, 4> TURBINE_HELI_FPM{-75, -150, -300, -500};

// A textbook set-down: no descent to speak of, no drift, level, no yaw.
FlightLoggerLogic::RotorcraftTouchdown clean_touchdown()
{
    FlightLoggerLogic::RotorcraftTouchdown touchdown;
    touchdown.descent_fpm    = -1.f;
    touchdown.drift_kts      = 0.f;
    touchdown.bank_deg       = 0.5f;
    touchdown.yaw_rate_deg_s = 0.5f;
    touchdown.g_force        = 1.01f;
    return touchdown;
}

int rate(const FlightLoggerLogic::RotorcraftTouchdown &touchdown)
{
    return FlightLoggerLogic::rotorcraft_rating_index(touchdown, TURBINE_HELI_FPM);
}

} // namespace

TEST_CASE("a clean set-down is still rated BUTTER", "[flight_logger][rating]")
{
    REQUIRE(rate(clean_touchdown()) == 0);
    REQUIRE(std::string(FlightLoggerLogic::rating_label(0)) == "BUTTER!");
}

TEST_CASE("drift and bank sink the rating despite a gentle descent", "[flight_logger][rating]")
{
    // The landing from the bug report: -1 fpm and 1.01 G, but visibly sliding and banked.
    auto touchdown           = clean_touchdown();
    touchdown.drift_kts      = 6.f;
    touchdown.bank_deg       = 9.f;

    CHECK(rate(touchdown) > 0);
    CHECK(std::string(FlightLoggerLogic::rating_label(rate(touchdown))) == "HARD LANDING!");
}

TEST_CASE("each criterion on its own can drive the rating", "[flight_logger][rating]")
{
    auto drifting        = clean_touchdown();
    drifting.drift_kts   = 12.f;
    CHECK(rate(drifting) == 4);

    auto banked      = clean_touchdown();
    banked.bank_deg  = 11.f;
    CHECK(rate(banked) == 4);

    auto yawing            = clean_touchdown();
    yawing.yaw_rate_deg_s  = 16.f;
    CHECK(rate(yawing) == 4);

    auto slammed      = clean_touchdown();
    slammed.g_force   = 1.9f;
    CHECK(rate(slammed) == 4);

    auto dropped          = clean_touchdown();
    dropped.descent_fpm   = -600.f;
    CHECK(rate(dropped) == 4);
}

TEST_CASE("the worst criterion decides, a good one cannot rescue it", "[flight_logger][rating]")
{
    auto touchdown      = clean_touchdown();
    touchdown.bank_deg  = 5.f; // ACCEPTABLE on its own

    CHECK(rate(touchdown) == 2);

    touchdown.yaw_rate_deg_s = 16.f; // WASTED on its own
    CHECK(rate(touchdown) == 4);     // and it wins over the otherwise clean numbers
}

TEST_CASE("a deliberate run-on landing is not judged as drift", "[flight_logger][rating]")
{
    // Rolling on at 20 kts is a technique, not a mistake — grading it against the drift
    // limits would fail every run-on landing.
    auto touchdown       = clean_touchdown();
    touchdown.drift_kts  = 20.f;
    CHECK(rate(touchdown) == 0);

    // Just below the run-on threshold it counts as drift again.
    touchdown.drift_kts = FlightLoggerLogic::ROTORCRAFT_RUN_ON_KTS - 1.f;
    CHECK(rate(touchdown) == 4);
}

TEST_CASE("negative bank and yaw are graded by magnitude", "[flight_logger][rating]")
{
    auto left_bank      = clean_touchdown();
    left_bank.bank_deg  = -9.f;
    auto right_bank     = clean_touchdown();
    right_bank.bank_deg = 9.f;

    CHECK(rate(left_bank) == rate(right_bank));
}

// ── Fixed-wing rating ────────────────────────────────────────────────────────

namespace
{

// Thresholds of the light_ga profile in data/flight_logger_profiles.json.
constexpr std::array<int, 4> LIGHT_GA_FPM{-100, -200, -300, -500};

// A greaser in calm air: gentle rate, barely any vertical acceleration.
FlightLoggerLogic::FixedWingTouchdown greaser()
{
    FlightLoggerLogic::FixedWingTouchdown touchdown;
    touchdown.descent_fpm   = -60.f;
    touchdown.g_force       = 1.05f;
    touchdown.crosswind_kts = 0.f;
    touchdown.wind          = WindCondition::Calm;
    return touchdown;
}

int rate_fixed_wing(const FlightLoggerLogic::FixedWingTouchdown &touchdown)
{
    return FlightLoggerLogic::fixed_wing_rating_index(touchdown, LIGHT_GA_FPM);
}

} // namespace

TEST_CASE("a gentle touchdown in calm air is BUTTER", "[flight_logger][rating]")
{
    REQUIRE(rate_fixed_wing(greaser()) == 0);
}

TEST_CASE("vertical acceleration can sink a rating the descent rate would pass",
          "[flight_logger][rating]")
{
    // Dropping the last foot flat: the rate still reads gently, the arrival does not.
    auto touchdown     = greaser();
    touchdown.g_force  = 2.2f;

    CHECK(FlightLoggerLogic::grade_descent(touchdown.descent_fpm, LIGHT_GA_FPM) == 0);
    CHECK(rate_fixed_wing(touchdown) == 3);
}

TEST_CASE("a good g-force cannot rescue a hard descent rate", "[flight_logger][rating]")
{
    auto touchdown          = greaser();
    touchdown.descent_fpm   = -600.f;

    CHECK(rate_fixed_wing(touchdown) == 4);
}

TEST_CASE("a crosswind buys allowance on the descent rate", "[flight_logger][rating]")
{
    auto calm            = greaser();
    calm.descent_fpm     = -210.f; // ACCEPTABLE with no allowance

    CHECK(rate_fixed_wing(calm) == 2);

    auto gusty            = calm;
    gusty.crosswind_kts   = 30.f;
    gusty.wind            = WindCondition::Steady;

    CHECK(rate_fixed_wing(gusty) == 1);
}

TEST_CASE("climbing on contact is never a good landing", "[flight_logger][rating]")
{
    auto touchdown        = greaser();
    touchdown.descent_fpm = 20.f;

    CHECK(rate_fixed_wing(touchdown) == 4);
}

// ── Meteorological conditions ────────────────────────────────────────────────

TEST_CASE("instrument conditions follow visibility or ceiling", "[flight_logger][weather]")
{
    using FlightLoggerLogic::is_instrument_conditions;

    CHECK_FALSE(is_instrument_conditions(10000.f, 4000.f, true));
    CHECK_FALSE(is_instrument_conditions(10000.f, 0.f, false));

    CHECK(is_instrument_conditions(3000.f, 4000.f, true));
    CHECK(is_instrument_conditions(10000.f, 600.f, true));

    // A low layer that isn't broken or overcast is no ceiling, so it doesn't make it IMC.
    CHECK_FALSE(is_instrument_conditions(10000.f, 600.f, false));
}

// ── Airframe classification for unlisted aircraft ────────────────────────────

namespace
{
using FlightLoggerLogic::AirframeMetrics;
using FlightLoggerLogic::EngineKind;

AirframeMetrics airframe(float mass_kg, int engines, EngineKind kind)
{
    AirframeMetrics metrics;
    metrics.max_takeoff_mass_kg = mass_kg;
    metrics.engine_count        = engines;
    metrics.engine_kind         = kind;
    return metrics;
}
} // namespace

// Values taken from X-Plane's DataRefs.txt entry for acf_en_type.
TEST_CASE("engine_kind_from_dataref maps X-Plane engine types")
{
    CHECK(FlightLoggerLogic::engine_kind_from_dataref(0) == EngineKind::Piston);  // recip carburetted
    CHECK(FlightLoggerLogic::engine_kind_from_dataref(1) == EngineKind::Piston);  // recip injected
    CHECK(FlightLoggerLogic::engine_kind_from_dataref(3) == EngineKind::Piston);  // electric
    CHECK(FlightLoggerLogic::engine_kind_from_dataref(9) == EngineKind::Turbine); // free turboprop
    CHECK(FlightLoggerLogic::engine_kind_from_dataref(10) == EngineKind::Turbine); // fixed turboprop
    CHECK(FlightLoggerLogic::engine_kind_from_dataref(5) == EngineKind::Jet);     // single spool
    CHECK(FlightLoggerLogic::engine_kind_from_dataref(7) == EngineKind::Jet);     // multi spool
    CHECK(FlightLoggerLogic::engine_kind_from_dataref(6) == EngineKind::Piston);  // rocket, no profile of its own
    // Unused slots must not be read as a category by accident.
    CHECK(FlightLoggerLogic::engine_kind_from_dataref(2) == EngineKind::Piston);
    CHECK(FlightLoggerLogic::engine_kind_from_dataref(8) == EngineKind::Piston);
}

TEST_CASE("classify_fixed_wing_profile sorts piston singles by mass")
{
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(450, 1, EngineKind::Piston)) == "ultra_light");
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(1111, 1, EngineKind::Piston)) == "light_ga");
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(1633, 1, EngineKind::Piston)) == "medium_ga");
}

TEST_CASE("classify_fixed_wing_profile treats mass boundaries as inclusive")
{
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(600, 1, EngineKind::Piston)) == "ultra_light");
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(601, 1, EngineKind::Piston)) == "light_ga");
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(1500, 1, EngineKind::Piston)) == "light_ga");
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(1501, 1, EngineKind::Piston)) == "medium_ga");
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(20000, 2, EngineKind::Jet)) == "vlj");
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(20001, 2, EngineKind::Jet)) == "heavy_jet");
}

TEST_CASE("classify_fixed_wing_profile keeps piston twins out of the light profiles")
{
    // A light twin still flies a twin approach, so mass alone must not demote it.
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(1200, 2, EngineKind::Piston)) == "medium_ga");
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(500, 2, EngineKind::Piston)) == "medium_ga");
}

TEST_CASE("classify_fixed_wing_profile rates turbine props as turboprop regardless of mass")
{
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(2200, 1, EngineKind::Turbine)) == "turboprop");
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(22800, 2, EngineKind::Turbine)) == "turboprop");
}

TEST_CASE("classify_fixed_wing_profile separates business jets from airliners")
{
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(3921, 1, EngineKind::Jet)) == "vlj");         // C510
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(16011, 2, EngineKind::Jet)) == "vlj");        // C750
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(38600, 2, EngineKind::Jet)) == "heavy_jet");  // E170
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(79000, 2, EngineKind::Jet)) == "heavy_jet");  // A20N
}

TEST_CASE("classify_fixed_wing_profile declines to guess without a usable mass")
{
    // An empty result leaves the caller's medium_ga fallback in charge.
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(0, 1, EngineKind::Piston)).empty());
    CHECK(FlightLoggerLogic::classify_fixed_wing_profile(airframe(-1, 2, EngineKind::Jet)).empty());
}

// ── User profile overrides ───────────────────────────────────────────────────
// The regression these guard: a user correcting a misjudged aircraft in the settings file
// silently has no effect, or a short override code swallows unrelated types the way a
// substring match string would.

using FlightLoggerLogic::ProfileOverride;

namespace
{
ProfileOverride named(const std::string &profile_name) { return {profile_name, {}, false}; }
ProfileOverride custom(const std::array<int, 4> &thresholds) { return {"", thresholds, true}; }
} // namespace

TEST_CASE("find_profile_override matches the ICAO code exactly")
{
    const std::map<std::string, ProfileOverride> overrides{{"B77W", named("heavy_jet")},
                                                           {"C208", named("turboprop")}};

    REQUIRE(FlightLoggerLogic::find_profile_override(overrides, "B77W") != nullptr);
    CHECK(FlightLoggerLogic::find_profile_override(overrides, "B77W")->profile_name == "heavy_jet");
    CHECK(FlightLoggerLogic::find_profile_override(overrides, "C208")->profile_name == "turboprop");
}

TEST_CASE("find_profile_override does not match on a substring")
{
    // "B77" must not catch B77W, and B77W must not be caught by an unrelated longer code.
    const std::map<std::string, ProfileOverride> overrides{{"B77", named("heavy_jet")}};

    CHECK(FlightLoggerLogic::find_profile_override(overrides, "B77W") == nullptr);
    CHECK(FlightLoggerLogic::find_profile_override(overrides, "B77")->profile_name == "heavy_jet");
}

TEST_CASE("find_profile_override is case sensitive and ignores unrelated aircraft")
{
    const std::map<std::string, ProfileOverride> overrides{{"SF50", named("turboprop")}};

    CHECK(FlightLoggerLogic::find_profile_override(overrides, "C172") == nullptr);
    CHECK(FlightLoggerLogic::find_profile_override(overrides, "sf50") == nullptr);
}

TEST_CASE("find_profile_override handles an aircraft that reports no ICAO")
{
    // The Aerolite 103 reports an empty code; an override keyed on "" must not apply to it.
    const std::map<std::string, ProfileOverride> overrides{{"", named("heavy_jet")}, {"C172", named("medium_ga")}};

    CHECK(FlightLoggerLogic::find_profile_override(overrides, "") == nullptr);
}

TEST_CASE("find_profile_override on an empty table leaves the normal lookup in charge")
{
    CHECK(FlightLoggerLogic::find_profile_override({}, "C172") == nullptr);
}

TEST_CASE("find_profile_override returns custom thresholds as the user entered them")
{
    const std::map<std::string, ProfileOverride> overrides{{"C208", custom({-150, -275, -400, -650})}};

    const auto *found = FlightLoggerLogic::find_profile_override(overrides, "C208");
    REQUIRE(found != nullptr);
    CHECK(found->is_custom);
    CHECK(found->profile_name.empty());
    CHECK(found->thresholds == std::array<int, 4>{-150, -275, -400, -650});
}

// ── Custom threshold validation ──────────────────────────────────────────────
// Hand-entered values reach the rating directly, so they are checked at the edge. A set
// that passes here decides what counts as a BUTTER landing for that aircraft.

TEST_CASE("Thresholds descending from gentlest to harshest are accepted")
{
    CHECK(FlightLoggerLogic::are_valid_thresholds({-100, -200, -300, -500}));
    CHECK(FlightLoggerLogic::are_valid_thresholds({-150, -275, -400, -650}));
}

TEST_CASE("Ascending or repeated thresholds are rejected")
{
    // Reversed entirely, one step out of order, and two steps that would grade the same.
    CHECK_FALSE(FlightLoggerLogic::are_valid_thresholds({-500, -300, -200, -100}));
    CHECK_FALSE(FlightLoggerLogic::are_valid_thresholds({-100, -300, -200, -500}));
    CHECK_FALSE(FlightLoggerLogic::are_valid_thresholds({-100, -200, -200, -500}));
}

TEST_CASE("Positive thresholds are rejected")
{
    // A descent rate is negative; a positive value would grade a climb as a landing.
    CHECK_FALSE(FlightLoggerLogic::are_valid_thresholds({100, 200, 300, 500}));
    CHECK_FALSE(FlightLoggerLogic::are_valid_thresholds({0, -200, -300, -500}));
}

TEST_CASE("Thresholds outside the plausible range are rejected")
{
    CHECK_FALSE(FlightLoggerLogic::are_valid_thresholds({-5, -200, -300, -500}));
    CHECK_FALSE(FlightLoggerLogic::are_valid_thresholds({-100, -200, -300, -4000}));
    CHECK(FlightLoggerLogic::are_valid_thresholds({FlightLoggerLogic::MAX_THRESHOLD_FPM, -200, -300,
                                                   FlightLoggerLogic::MIN_THRESHOLD_FPM}));
}

TEST_CASE("Custom thresholds decide the rating exactly as a bundled profile does")
{
    // The point of the feature: a pilot who finds light_ga too strict sets their own
    // values and the same touchdown earns a different grade.
    const std::array<int, 4> strict{-80, -150, -250, -400};
    const std::array<int, 4> lenient{-200, -350, -500, -800};

    FlightLoggerLogic::FixedWingTouchdown touchdown;
    touchdown.descent_fpm = -180.f;
    touchdown.g_force     = 1.2f;
    touchdown.wind        = WindCondition::Calm;

    CHECK(FlightLoggerLogic::rating_label(FlightLoggerLogic::fixed_wing_rating_index(touchdown, strict)) ==
          std::string("ACCEPTABLE"));
    CHECK(FlightLoggerLogic::rating_label(FlightLoggerLogic::fixed_wing_rating_index(touchdown, lenient)) ==
          std::string("BUTTER!"));
}
