#pragma once

#include "runway_data.hpp"

#include <cmath>
#include <string>
#include <vector>

// Where a touchdown happened relative to the runway it was made on.
// An empty runway_ident means no runway matched — grass strip, water, or an airport
// the scenery database doesn't describe.
struct RunwayFix
{
    std::string runway_ident;
    float       centerline_offset_m = 0; // + = right of centerline
    float       distance_from_thr_m = 0; // measured from the displaced threshold
    float       runway_length_m     = 0; // displaced threshold to far end
};

namespace RunwayGeometry
{
namespace detail
{
constexpr double EARTH_RADIUS_M = 6371008.8; // mean radius — the equatorial one overstates
                                             // runway lengths by ~0.1% at mid latitudes
constexpr double DEG_TO_RAD     = 3.14159265358979323846 / 180.0;
constexpr double RAD_TO_DEG     = 180.0 / 3.14159265358979323846;

// Match tolerances. A landing further out than this belongs to another runway — or to
// no runway at all.
constexpr float MAX_HEADING_DIFF_DEG = 45.f;
constexpr float MAX_OFFSET_M         = 75.f;
constexpr float MAX_UNDERSHOOT_M     = 100.f;

struct LocalPoint
{
    double east_m = 0, north_m = 0;
};

// Equirectangular projection around a reference point. Runway-scale distances stay well
// under a few kilometres, where the error against a geodesic solution is centimetres.
inline LocalPoint to_local_meters(double lat_ref, double lon_ref, double lat, double lon)
{
    const double mean_lat = (lat_ref + lat) * 0.5 * DEG_TO_RAD;
    LocalPoint   p;
    p.east_m  = (lon - lon_ref) * DEG_TO_RAD * EARTH_RADIUS_M * std::cos(mean_lat);
    p.north_m = (lat - lat_ref) * DEG_TO_RAD * EARTH_RADIUS_M;
    return p;
}

inline double normalize_deg(double deg)
{
    while (deg < 0.0)
        deg += 360.0;
    while (deg >= 360.0)
        deg -= 360.0;
    return deg;
}

inline double heading_difference_deg(double a, double b)
{
    const double diff = std::fabs(normalize_deg(a) - normalize_deg(b));
    return diff > 180.0 ? 360.0 - diff : diff;
}

// One landing direction of one runway, evaluated against the touchdown point.
struct Candidate
{
    bool        valid = false;
    std::string designator;
    float       offset_m = 0;
    float       along_m  = 0;
    float       length_m = 0;
};

inline Candidate evaluate(const Runway &runway, int landing_end, double td_lat, double td_lon,
                          double heading_true_deg)
{
    const RunwayEnd &near_end = runway.ends[landing_end];
    const RunwayEnd &far_end  = runway.ends[1 - landing_end];

    const LocalPoint far_local = to_local_meters(near_end.lat, near_end.lon, far_end.lat, far_end.lon);
    const double     axis_len  = std::hypot(far_local.east_m, far_local.north_m);
    if (axis_len < 1.0)
        return {};

    const double forward_east  = far_local.east_m / axis_len;
    const double forward_north = far_local.north_m / axis_len;

    const double course_deg = normalize_deg(std::atan2(forward_east, forward_north) * RAD_TO_DEG);
    if (heading_difference_deg(course_deg, heading_true_deg) > MAX_HEADING_DIFF_DEG)
        return {};

    const LocalPoint td = to_local_meters(near_end.lat, near_end.lon, td_lat, td_lon);

    // The usable landing surface starts at the displaced threshold, not at the pavement edge.
    const double threshold_east  = forward_east * near_end.displaced_threshold_m;
    const double threshold_north = forward_north * near_end.displaced_threshold_m;
    const double rel_east        = td.east_m - threshold_east;
    const double rel_north       = td.north_m - threshold_north;

    const double along = rel_east * forward_east + rel_north * forward_north;
    // Right of the direction of travel is the forward vector rotated 90° clockwise.
    const double offset   = rel_east * forward_north - rel_north * forward_east;
    const double length_m = axis_len - near_end.displaced_threshold_m;

    if (std::fabs(offset) > MAX_OFFSET_M)
        return {};
    if (along < -MAX_UNDERSHOOT_M || along > length_m)
        return {};

    Candidate c;
    c.valid      = true;
    c.designator = near_end.designator;
    c.offset_m   = static_cast<float>(offset);
    c.along_m    = static_cast<float>(along);
    c.length_m   = static_cast<float>(length_m);
    return c;
}
} // namespace detail

// Pick the runway direction the aircraft touched down on. Among the candidates that fit
// heading and pavement, the one closest to the centerline wins.
inline RunwayFix locate_touchdown(const std::vector<Runway> &runways, double lat, double lon,
                                  double heading_true_deg)
{
    detail::Candidate best;
    for (const Runway &runway : runways)
    {
        for (int end = 0; end < 2; ++end)
        {
            const detail::Candidate c = detail::evaluate(runway, end, lat, lon, heading_true_deg);
            if (!c.valid)
                continue;
            if (!best.valid || std::fabs(c.offset_m) < std::fabs(best.offset_m))
                best = c;
        }
    }

    RunwayFix fix;
    if (!best.valid)
        return fix;

    fix.runway_ident        = best.designator;
    fix.centerline_offset_m = best.offset_m;
    fix.distance_from_thr_m = best.along_m;
    fix.runway_length_m     = best.length_m;
    return fix;
}
} // namespace RunwayGeometry
