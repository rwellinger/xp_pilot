#pragma once

#include <string>
#include <vector>

// Populated places for the track map, from Natural Earth 1:10m (public domain),
// shipped in data/cities.dat by tools/build_map_data.py.
//
// A logbook map answers "where did I fly", and place names answer that far better than
// airspace outlines do — "south of Nancy, then across the Marne" reads instantly.

struct City
{
    std::string name;
    double      lat = 0, lon = 0;
    int         population = 0;
};

struct CityBounds
{
    double lat_min = 0, lat_max = 0, lon_min = 0, lon_max = 0;
};

namespace CityData
{
// The `limit` largest places inside `bounds`, largest first. The file is sorted by
// population, so reading stops as soon as enough have been found — which is what keeps
// the label count readable at every zoom without a threshold table: a local flight gets
// small towns, a continental one gets capitals.
std::vector<City> load_cities(const std::string &cities_dat_path, const CityBounds &bounds, size_t limit);
} // namespace CityData
