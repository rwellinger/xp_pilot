#include "airspace_data.hpp"

#include "geo_longitude.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace
{

bool starts_with(const std::string &line, const char *prefix) { return line.rfind(prefix, 0) == 0; }

// "DP  46:54:52 N 007:29:52 E" — degrees, minutes and seconds with a hemisphere letter.
bool parse_point(const std::string &line, AirspacePoint &out)
{
    int  lat_deg, lat_min, lat_sec, lon_deg, lon_min, lon_sec;
    char lat_hemisphere, lon_hemisphere;
    // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion) — result checked
    if (sscanf(line.c_str(), "DP %d:%d:%d %c %d:%d:%d %c", &lat_deg, &lat_min, &lat_sec, &lat_hemisphere, &lon_deg,
               &lon_min, &lon_sec, &lon_hemisphere) != 8)
        return false;

    out.lat = lat_deg + lat_min / 60.0 + lat_sec / 3600.0;
    out.lon = lon_deg + lon_min / 60.0 + lon_sec / 3600.0;
    if (lat_hemisphere == 'S')
        out.lat = -out.lat;
    if (lon_hemisphere == 'W')
        out.lon = -out.lon;
    return true;
}

bool contains(const std::vector<AirspacePoint> &outline, double lat, double lon)
{
    // Ray casting: count crossings of a ray running east from the point.
    bool inside = false;
    for (size_t i = 0, previous = outline.size() - 1; i < outline.size(); previous = i++)
    {
        const AirspacePoint &a = outline[i];
        const AirspacePoint &b = outline[previous];
        if ((a.lat > lat) != (b.lat > lat) && lon < (b.lon - a.lon) * (lat - a.lat) / (b.lat - a.lat) + a.lon)
            inside = !inside;
    }
    return inside;
}

// The regression this prevents: testing only whether an outline *point* falls inside the
// view drops every airspace that surrounds it. A CTR around the airport you departed
// from has all its corners outside a short flight's bounds, so EDNY showed no airspaces
// at all while nearby LSZG — which sits closer to airspace edges — showed some.
bool touches(const std::vector<AirspacePoint> &outline, const AirspaceBounds &bounds)
{
    const double reference = (bounds.lon_min + bounds.lon_max) / 2.0;

    double lat_min = outline.front().lat, lat_max = lat_min;
    double lon_min = GeoLongitude::unwrapped_near(outline.front().lon, reference), lon_max = lon_min;
    for (const auto &point : outline)
    {
        const double lon = GeoLongitude::unwrapped_near(point.lon, reference);
        lat_min          = std::min(lat_min, point.lat);
        lat_max          = std::max(lat_max, point.lat);
        lon_min          = std::min(lon_min, lon);
        lon_max          = std::max(lon_max, lon);

        if (point.lat >= bounds.lat_min && point.lat <= bounds.lat_max && lon >= bounds.lon_min &&
            lon <= bounds.lon_max)
            return true; // an edge runs through the view
    }

    // Cheap reject before the ray cast. Without it, outlines spanning huge longitude
    // ranges (an oceanic CTA on the far side of the world) would be tested in full.
    if (lat_max < bounds.lat_min || lat_min > bounds.lat_max || lon_max < bounds.lon_min ||
        lon_min > bounds.lon_max)
        return false;

    // The view sits entirely inside the airspace — the surrounding-CTR case.
    return contains(outline, (bounds.lat_min + bounds.lat_max) / 2, (bounds.lon_min + bounds.lon_max) / 2);
}

std::string value_of(const std::string &line)
{
    return line.size() > 3 ? line.substr(3) : std::string();
}

} // namespace

std::vector<Airspace> AirspaceData::load_airspaces(const std::string &airspace_txt_path, const AirspaceBounds &bounds)
{
    std::vector<Airspace> found;

    std::ifstream file(airspace_txt_path);
    if (!file.is_open())
        return found;

    // An AC line opens the next airspace, so the one being built is only complete once
    // the following AC (or end of file) is reached.
    Airspace    pending;
    std::string line;

    const auto keep_if_relevant = [&]()
    {
        if (pending.outline.size() >= 3 && touches(pending.outline, bounds))
            found.push_back(pending);
        pending = Airspace();
    };

    while (std::getline(file, line))
    {
        if (starts_with(line, "AC "))
        {
            keep_if_relevant();
            pending.airspace_class = value_of(line);
        }
        else if (starts_with(line, "AN "))
            pending.name = value_of(line);
        else if (starts_with(line, "AL "))
            pending.lower_limit = value_of(line);
        else if (starts_with(line, "AH "))
            pending.upper_limit = value_of(line);
        else if (starts_with(line, "DP"))
        {
            AirspacePoint point;
            if (parse_point(line, point))
                pending.outline.push_back(point);
        }
    }
    keep_if_relevant();

    return found;
}
