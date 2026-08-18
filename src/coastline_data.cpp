#include "coastline_data.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace
{

bool contains(const std::vector<OutlinePoint> &ring, double lat, double lon)
{
    bool inside = false;
    for (size_t i = 0, previous = ring.size() - 1; i < ring.size(); previous = i++)
    {
        const OutlinePoint &a = ring[i];
        const OutlinePoint &b = ring[previous];
        if ((a.lat > lat) != (b.lat > lat) && lon < (b.lon - a.lon) * (lat - a.lat) / (b.lat - a.lat) + a.lon)
            inside = !inside;
    }
    return inside;
}

// Same trap as with airspaces: a flight staying over Lake Constance sits entirely
// within the lake ring, so no outline point falls inside the view and the water would
// vanish exactly where it matters.
bool touches(const std::vector<OutlinePoint> &points, const OutlineBounds &bounds, bool is_ring)
{
    double lat_min = points.front().lat, lat_max = lat_min;
    double lon_min = points.front().lon, lon_max = lon_min;
    for (const auto &point : points)
    {
        lat_min = std::min(lat_min, point.lat);
        lat_max = std::max(lat_max, point.lat);
        lon_min = std::min(lon_min, point.lon);
        lon_max = std::max(lon_max, point.lon);

        if (point.lat >= bounds.lat_min && point.lat <= bounds.lat_max && point.lon >= bounds.lon_min &&
            point.lon <= bounds.lon_max)
            return true;
    }

    // Only closed rings can enclose the view; an open coastline cannot.
    if (!is_ring || lat_max < bounds.lat_min || lat_min > bounds.lat_max || lon_max < bounds.lon_min ||
        lon_min > bounds.lon_max)
        return false;

    return contains(points, (bounds.lat_min + bounds.lat_max) / 2, (bounds.lon_min + bounds.lon_max) / 2);
}

} // namespace

std::vector<GeoOutline> CoastlineData::load_outlines(const std::string   &coastlines_dat_path,
                                                     const OutlineBounds &bounds)
{
    std::vector<GeoOutline> found;

    std::ifstream file(coastlines_dat_path);
    if (!file.is_open())
        return found;

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
            continue;

        // Part header: "<C|L> <point count>", followed by that many coordinate lines.
        char tag         = 0;
        int  point_count = 0;
        // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion) — result checked
        if (sscanf(line.c_str(), "%c %d", &tag, &point_count) != 2 || (tag != 'C' && tag != 'L') || point_count <= 0)
            continue;

        GeoOutline outline;
        outline.is_lake = (tag == 'L');
        outline.points.reserve(static_cast<size_t>(point_count));

        for (int i = 0; i < point_count && std::getline(file, line); ++i)
        {
            double lon = 0, lat = 0;
            // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion) — result checked
            if (sscanf(line.c_str(), "%lf %lf", &lon, &lat) == 2)
                outline.points.push_back({lat, lon});
        }

        // A lake needs a ring to fill; a coastline needs two points to be a line.
        const size_t minimum = outline.is_lake ? 3 : 2;
        if (outline.points.size() >= minimum && touches(outline.points, bounds, outline.is_lake))
            found.push_back(std::move(outline));
    }

    return found;
}
