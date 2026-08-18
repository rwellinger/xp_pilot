#include "coastline_data.hpp"

#include <cstdio>
#include <fstream>

namespace
{

bool touches(const std::vector<OutlinePoint> &points, const OutlineBounds &bounds)
{
    for (const auto &point : points)
    {
        if (point.lat >= bounds.lat_min && point.lat <= bounds.lat_max && point.lon >= bounds.lon_min &&
            point.lon <= bounds.lon_max)
            return true;
    }
    return false;
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
        if (outline.points.size() >= minimum && touches(outline.points, bounds))
            found.push_back(std::move(outline));
    }

    return found;
}
