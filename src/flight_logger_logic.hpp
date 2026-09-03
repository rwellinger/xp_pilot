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

#pragma once

#include "geo_longitude.hpp"
#include "html_report.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

// Pure sampling and track-geometry logic for the flight logger — no XPLM dependency,
// exposed so unit tests can exercise it without standing up the plugin.

namespace FlightLoggerLogic
{

inline constexpr int MAX_PLAUSIBLE_IAS_KTS  = 1000; // beyond any airliner/fighter in the sim
inline constexpr int MAX_IAS_STEP_PER_SAMPLE = 150; // kts a 10 s sample interval can plausibly gain

// Reject speed samples that a real airframe cannot produce between two 10 s samples.
// A reposition, an aircraft reload or the first frame after leaving a paused/replayed
// state can hand out a single garbage IAS reading, and the running maximum would keep
// it for the rest of the flight.
//
// has_previous is false for the first sample of a flight, where only the absolute
// bound can be checked.
inline bool is_plausible_speed_sample(int speed_kts, int previous_speed_kts, bool has_previous)
{
    if (speed_kts < 0 || speed_kts > MAX_PLAUSIBLE_IAS_KTS)
        return false;
    if (!has_previous)
        return true;
    return std::abs(speed_kts - previous_speed_kts) <= MAX_IAS_STEP_PER_SAMPLE;
}

// Aircraft-to-profile matching as flight_logger_profiles.json defines it: a match string
// counts when it appears anywhere in the aircraft's ICAO code, so "R22" also catches a
// model reporting "R22B". Entries are scanned in file order and the first hit wins,
// which is why the list puts the more specific codes first.
inline bool icao_matches_profile(const std::string &aircraft_icao, const std::string &match_string)
{
    return !match_string.empty() && aircraft_icao.find(match_string) != std::string::npos;
}

// ── User profile overrides ───────────────────────────────────────────────────

// The profile the user picked for this aircraft in the settings file, or an empty string
// when they picked none.
//
// Matching is exact, unlike the bundled list's substring rule. An override is typed by
// hand against the code the log prints on aircraft load, and an exact match cannot
// silently shadow another type the way a too-short match string can — "ASK2" sat in the
// list for a long time matching nothing, and a "B73" override would otherwise catch
// every 737 variant at once.
inline std::string find_profile_override(const std::map<std::string, std::string> &overrides,
                                         const std::string                        &aircraft_icao)
{
    if (aircraft_icao.empty())
        return "";
    const auto entry = overrides.find(aircraft_icao);
    return entry == overrides.end() ? "" : entry->second;
}

// ── Airframe classification ──────────────────────────────────────────────────

// An aircraft the ICAO list does not name used to be rated against medium_ga, which
// calls a normal -400 fpm airliner touchdown a hard landing and lets an ultralight get
// away with far too much. X-Plane knows the airframe itself, so the profile can be
// estimated instead of defaulted.

// What sim/aircraft/prop/acf_en_type reports for one engine. Only the distinctions
// that change the landing profile are named; the rest is decided by mass.
enum class EngineKind
{
    Piston,  // carburetted, injected and electric — same approach speeds
    Turbine, // free or fixed turbine driving a propeller
    Jet,     // low or high bypass
};

// Values as X-Plane's own DataRefs.txt documents them for acf_en_type. Anything else —
// a rocket, or a type a later version adds — falls through to Piston, which only ever
// applies to an aircraft the ICAO list does not name anyway.
inline EngineKind engine_kind_from_dataref(int acf_en_type)
{
    switch (acf_en_type)
    {
    case 9:  // free turboprop
    case 10: // fixed turboprop
        return EngineKind::Turbine;
    case 5: // single spool jet
    case 7: // multi spool jet
        return EngineKind::Jet;
    default: // 0 recip carb, 1 recip injected, 3 electric, 6 rocket
        return EngineKind::Piston;
    }
}

struct AirframeMetrics
{
    // Kilograms. X-Plane's DataRefs.txt documents no unit for acf_m_max and flags the
    // neighbouring acf_m_fuel_tot as pounds, and the acf file stores pounds throughout —
    // but the dataref converts. Confirmed in X-Plane 12: the C172 whose acf says 2558
    // reports 1160. Do not "fix" these thresholds to pounds.
    float      max_takeoff_mass_kg = 0;
    int        engine_count        = 0;
    EngineKind engine_kind         = EngineKind::Piston;
};

// 600 kg is the international light-sport limit; 1500 kg separates the C172/PA28 class
// from the SR22/DA62 class.
inline constexpr float ULTRA_LIGHT_MAX_MASS_KG = 600.f;
inline constexpr float LIGHT_GA_MAX_MASS_KG    = 1500.f;

// Business jets and airliners part company around 20 t: the Citation X and Challenger
// stay below it and fly a bizjet approach, the E170 and A320 sit above it. The ICAO
// list makes the same cut, keeping C750 and CL60 on vlj and E170 on heavy_jet.
inline constexpr float VLJ_MAX_MASS_KG = 20000.f;

// Pick the landing profile for an unlisted aircraft. Returns an empty string when the
// sim reports no usable mass, which leaves the caller's own fallback in charge rather
// than guessing from nothing.
inline std::string classify_fixed_wing_profile(const AirframeMetrics &metrics)
{
    if (metrics.max_takeoff_mass_kg <= 0.f)
        return "";

    if (metrics.engine_kind == EngineKind::Jet)
        return metrics.max_takeoff_mass_kg <= VLJ_MAX_MASS_KG ? "vlj" : "heavy_jet";

    if (metrics.engine_kind == EngineKind::Turbine)
        return "turboprop";

    // A second piston engine puts an aircraft in the twin class whatever it weighs —
    // the two light profiles describe single-engine handling.
    if (metrics.engine_count > 1)
        return "medium_ga";

    if (metrics.max_takeoff_mass_kg <= ULTRA_LIGHT_MAX_MASS_KG)
        return "ultra_light";
    if (metrics.max_takeoff_mass_kg <= LIGHT_GA_MAX_MASS_KG)
        return "light_ga";
    return "medium_ga";
}

// ── Landing rating ───────────────────────────────────────────────────────────

// Worst to best in the order the ratings are handed out; the index into this table is
// what the criteria below produce.
inline const char *rating_label(int index)
{
    static const char *labels[] = {"BUTTER!", "GREAT LANDING!", "ACCEPTABLE", "HARD LANDING!", "WASTED!"};
    return labels[std::clamp(index, 0, 4)];
}

// One touchdown as a helicopter is judged: descent rate alone says almost nothing about
// a set-down from the hover, where it is always near zero. What ruins a rotorcraft
// landing is sliding, touching down banked, or letting the tail swing round.
struct RotorcraftTouchdown
{
    float descent_fpm    = 0; // negative
    float drift_kts      = 0; // ground speed at the moment of contact
    float bank_deg       = 0; // absolute roll angle
    float yaw_rate_deg_s = 0; // absolute yaw rate
    float g_force        = 0;
};

// Sliding sideways onto the skids is what rolls a helicopter over, so drift is graded
// tightly. Above this speed the forward movement is a deliberate run-on landing rather
// than drift, and grading it would fail every intentional one.
inline constexpr float ROTORCRAFT_RUN_ON_KTS = 15.f;

// Each row is the upper bound for BUTTER, GREAT, ACCEPTABLE and HARD; anything beyond
// the last entry is WASTED.
inline constexpr float ROTORCRAFT_DRIFT_LIMITS_KTS[4]    = {1.f, 3.f, 6.f, 10.f};
inline constexpr float ROTORCRAFT_BANK_LIMITS_DEG[4]     = {2.f, 4.f, 7.f, 10.f};
inline constexpr float ROTORCRAFT_YAW_LIMITS_DEG_S[4]    = {2.f, 5.f, 10.f, 15.f};
inline constexpr float ROTORCRAFT_G_FORCE_LIMITS[4]      = {1.15f, 1.3f, 1.5f, 1.8f};

inline int grade_against(float value, const float (&limits)[4])
{
    for (int index = 0; index < 4; ++index)
        if (value <= limits[index])
            return index;
    return 4;
}

// Descent rate keeps the per-aircraft thresholds from flight_logger_profiles.json, which
// are negative fpm values ordered from gentlest to harshest.
inline int grade_descent(float descent_fpm, const std::array<int, 4> &fpm_thresholds)
{
    for (int index = 0; index < 4; ++index)
        if (descent_fpm >= static_cast<float>(fpm_thresholds[index]))
            return index;
    return 4;
}

// The worst single criterion decides: a gentle descent must not paper over a landing
// that slid or came down banked. Crosswind deliberately plays no part — it is measured
// against the nose heading, which for a helicopter says nothing about the touchdown.
inline int rotorcraft_rating_index(const RotorcraftTouchdown &touchdown, const std::array<int, 4> &fpm_thresholds)
{
    int worst = grade_descent(touchdown.descent_fpm, fpm_thresholds);
    worst     = std::max(worst, grade_against(std::abs(touchdown.bank_deg), ROTORCRAFT_BANK_LIMITS_DEG));
    worst     = std::max(worst, grade_against(std::abs(touchdown.yaw_rate_deg_s), ROTORCRAFT_YAW_LIMITS_DEG_S));
    worst     = std::max(worst, grade_against(touchdown.g_force, ROTORCRAFT_G_FORCE_LIMITS));
    if (touchdown.drift_kts < ROTORCRAFT_RUN_ON_KTS)
        worst = std::max(worst, grade_against(touchdown.drift_kts, ROTORCRAFT_DRIFT_LIMITS_KTS));
    return worst;
}

// ── Fixed-wing landing rating ────────────────────────────────────────────────

// One touchdown as a fixed-wing aircraft is judged. Descent rate is the primary
// criterion, but a gentle-looking rate can still hide a jarring arrival: an aircraft
// that drops the last foot flat records a modest fpm and a hefty vertical acceleration.
struct FixedWingTouchdown
{
    float         descent_fpm   = 0; // negative
    float         g_force       = 0; // smoothed vertical acceleration around contact
    float         crosswind_kts = 0;
    WindCondition wind          = WindCondition::Steady;
};

// Upper bounds for BUTTER, GREAT, ACCEPTABLE and HARD; beyond the last entry is WASTED.
// Anchored on the flight-data-monitoring convention of 2.1 G for a hard landing and
// 2.6 G for a severe one, with the two gentler steps interpolated below.
inline constexpr float FIXED_WING_G_FORCE_LIMITS[4] = {1.4f, 1.7f, 2.1f, 2.6f};

// A crosswind makes the same descent rate a better piece of flying, so the rate is
// relaxed by up to 40% in a full 30-knot crosswind. Calm air earns no allowance at all.
inline float crosswind_allowance(float crosswind_kts, WindCondition wind)
{
    float scale = 0.f;
    switch (wind)
    {
    case WindCondition::Calm:
        scale = 0.f;
        break;
    case WindCondition::Light:
        scale = 0.5f;
        break;
    case WindCondition::Steady:
        scale = 1.f;
        break;
    }
    const float capped = std::min(std::abs(crosswind_kts), 30.f);
    return 1.f + (capped / 30.f) * 0.4f * scale;
}

// The worst of descent rate and vertical acceleration decides, mirroring how the
// rotorcraft rating treats its criteria.
inline int fixed_wing_rating_index(const FixedWingTouchdown &touchdown, const std::array<int, 4> &fpm_thresholds)
{
    const float effective_fpm = touchdown.descent_fpm / crosswind_allowance(touchdown.crosswind_kts, touchdown.wind);
    // Climbing on contact is not a landing anyone got right.
    if (effective_fpm > 0.f)
        return 4;
    int worst = grade_descent(effective_fpm, fpm_thresholds);
    return std::max(worst, grade_against(touchdown.g_force, FIXED_WING_G_FORCE_LIMITS));
}

// ── Meteorological conditions ────────────────────────────────────────────────

// Below these the approach was flown in instrument conditions. The boundary follows the
// usual VMC minima for controlled airspace rather than any single national rule — it is
// logbook context, not a legal determination.
inline constexpr float VMC_MIN_VISIBILITY_M   = 5000.f;
inline constexpr float VMC_MIN_CEILING_FT_AGL = 1500.f;

inline bool is_instrument_conditions(float visibility_m, float ceiling_ft_agl, bool has_ceiling)
{
    if (visibility_m < VMC_MIN_VISIBILITY_M)
        return true;
    return has_ceiling && ceiling_ft_agl < VMC_MIN_CEILING_FT_AGL;
}

// Geographic extent of a track, already padded for display. Degenerate tracks (empty,
// or every sample at the same spot) still yield a non-zero span, so callers can divide
// by it without guarding.
struct GeoBounds
{
    double lat_min = 0, lat_max = 0, lon_min = 0, lon_max = 0;
};

// Pause total: whatever the flight clock ran beyond the active (unpaused) time.
// Both counters are fed from the same per-frame delta, so their difference carries no
// quantisation noise and is exactly zero for a flight that was never paused. Feeding
// one of them from a whole-second wall clock instead would make this oscillate
// between 0 and 1 every second.
inline int paused_seconds(double total_seconds, double active_seconds)
{
    const double paused = total_seconds - active_seconds;
    return paused > 0.0 ? static_cast<int>(std::lround(paused)) : 0;
}

inline constexpr double TRACK_BOUNDS_PADDING_FRACTION = 0.05;
inline constexpr double TRACK_BOUNDS_MIN_PADDING_DEG  = 0.001;


inline GeoBounds track_bounds(const std::vector<TrackPoint> &track)
{
    GeoBounds bounds;
    if (!track.empty())
    {
        bounds.lat_min = bounds.lat_max = track.front().lat;
        bounds.lon_min = bounds.lon_max = track.front().lon;
        // Longitudes are unwrapped against the first point, so a track crossing the date
        // line keeps growing east instead of snapping back and spanning the globe.
        const double reference = track.front().lon;
        for (const auto &point : track)
        {
            const double lon = GeoLongitude::unwrapped_near(point.lon, reference);
            bounds.lat_min   = std::min(bounds.lat_min, point.lat);
            bounds.lat_max   = std::max(bounds.lat_max, point.lat);
            bounds.lon_min   = std::min(bounds.lon_min, lon);
            bounds.lon_max   = std::max(bounds.lon_max, lon);
        }
    }

    const double lat_padding =
        (bounds.lat_max - bounds.lat_min) * TRACK_BOUNDS_PADDING_FRACTION + TRACK_BOUNDS_MIN_PADDING_DEG;
    const double lon_padding =
        (bounds.lon_max - bounds.lon_min) * TRACK_BOUNDS_PADDING_FRACTION + TRACK_BOUNDS_MIN_PADDING_DEG;
    bounds.lat_min -= lat_padding;
    bounds.lat_max += lat_padding;
    bounds.lon_min -= lon_padding;
    bounds.lon_max += lon_padding;
    return bounds;
}

// ── Map projection ────────────────────────────────────────────────────────────
// Web Mercator, so the track keeps the shape a pilot recognises from a chart. Both
// axes carry the same scale; whatever is left over becomes a letterbox margin. A plain
// linear lat/lon stretch would squash north-south flights the further from the equator
// they happen.

namespace detail
{
constexpr double DEGREES_TO_RADIANS = 3.14159265358979323846 / 180.0;
constexpr double QUARTER_TURN_RAD   = 3.14159265358979323846 / 4.0;
// Mercator diverges at the poles; Web Mercator's usual cutoff keeps the maths finite.
constexpr double MERCATOR_LAT_LIMIT_DEG = 85.05112878;
// Good to ~0.1% — plenty for a scale bar that snaps to round distances anyway.
constexpr double KM_PER_DEGREE_LONGITUDE_AT_EQUATOR = 111.320;

inline double mercator_x(double lon_degrees) { return lon_degrees * DEGREES_TO_RADIANS; }

inline double mercator_y(double lat_degrees)
{
    const double clamped = std::clamp(lat_degrees, -MERCATOR_LAT_LIMIT_DEG, MERCATOR_LAT_LIMIT_DEG);
    return std::log(std::tan(QUARTER_TURN_RAD + clamped * DEGREES_TO_RADIANS / 2.0));
}
} // namespace detail

// A projected map fitted into a width x height box, centred with equal margins.
struct MapViewport
{
    double x_left     = 0; // Mercator x along the left edge of the fitted map
    double y_top      = 0; // Mercator y along its top edge
    double scale      = 1; // pixels per Mercator unit — the same on both axes
    double lon_centre = 0; // reference for unwrapping longitudes onto this map
    float  offset_x   = 0; // letterbox margins centring the map in its box
    float  offset_y   = 0;
};

inline MapViewport make_viewport(const GeoBounds &bounds, float width, float height)
{
    MapViewport viewport;
    viewport.lon_centre = (bounds.lon_min + bounds.lon_max) / 2.0;
    viewport.x_left     = detail::mercator_x(bounds.lon_min);
    viewport.y_top  = detail::mercator_y(bounds.lat_max);

    // Guard against a degenerate span; track_bounds always pads, so this only bites on
    // hand-built bounds.
    const double span_x = std::max(detail::mercator_x(bounds.lon_max) - viewport.x_left, 1e-12);
    const double span_y = std::max(viewport.y_top - detail::mercator_y(bounds.lat_min), 1e-12);

    viewport.scale    = std::min(width / span_x, height / span_y);
    viewport.offset_x = static_cast<float>((width - span_x * viewport.scale) / 2.0);
    viewport.offset_y = static_cast<float>((height - span_y * viewport.scale) / 2.0);
    return viewport;
}

// Ground distance one horizontal pixel covers, measured at the centre latitude —
// what a scale bar needs to label itself.
inline double km_per_pixel(const GeoBounds &bounds, const MapViewport &viewport)
{
    const double px_per_degree = detail::DEGREES_TO_RADIANS * viewport.scale;
    if (px_per_degree <= 0.0)
        return 0.0;
    const double mid_lat_rad = (bounds.lat_min + bounds.lat_max) / 2.0 * detail::DEGREES_TO_RADIANS;
    return detail::KM_PER_DEGREE_LONGITUDE_AT_EQUATOR * std::cos(mid_lat_rad) / px_per_degree;
}

// Drop points that would land on the pixel their predecessor already covers. Natural
// Earth outlines carry far more detail than a 1000 px map can show, and ImGui indexes
// vertices with 16 bits — a wide view once overflowed that limit and drew the map as
// garbage. Thinning removes the cause rather than capping the symptom.
inline constexpr float min_pixel_step = 1.5f;

// Map a position into the viewport, y growing downwards as pixels do.
inline void project_to_pixel(const MapViewport &viewport, double lat, double lon, float &out_x, float &out_y)
{
    // Unwrapping here fixes the overlays too: an American coastline at -120 lands at
    // +240 on a map centred past the date line, instead of vanishing off the left edge.
    const double unwrapped = GeoLongitude::unwrapped_near(lon, viewport.lon_centre);
    out_x = viewport.offset_x + static_cast<float>((detail::mercator_x(unwrapped) - viewport.x_left) * viewport.scale);
    out_y = viewport.offset_y + static_cast<float>((viewport.y_top - detail::mercator_y(lat)) * viewport.scale);
}

} // namespace FlightLoggerLogic
