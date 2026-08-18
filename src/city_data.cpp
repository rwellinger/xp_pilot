#include "city_data.hpp"

#include "geo_longitude.hpp"

#include <cstdlib>
#include <fstream>

namespace
{

bool parse_double(const std::string &text, double &out)
{
    char       *end   = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0')
        return false;
    out = value;
    return true;
}

bool parse_long(const std::string &text, int &out)
{
    char      *end   = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || value <= 0)
        return false;
    out = static_cast<int>(value);
    return true;
}

} // namespace

std::vector<City> CityData::load_cities(const std::string &cities_dat_path, const CityBounds &bounds, size_t limit)
{
    std::vector<City> found;
    if (limit == 0)
        return found;

    std::ifstream file(cities_dat_path);
    if (!file.is_open())
        return found;

    std::string line;
    while (std::getline(file, line) && found.size() < limit)
    {
        if (line.empty() || line[0] == '#')
            continue;

        // "<population>|<lon>|<lat>|<name>", largest first.
        const size_t first  = line.find('|');
        const size_t second = line.find('|', first + 1);
        const size_t third  = line.find('|', second + 1);
        if (first == std::string::npos || second == std::string::npos || third == std::string::npos)
            continue;

        City city;
        // Checked conversions: a corrupt line must be skipped, not turned into a ghost
        // city off the coast of Africa at (0, 0).
        if (!parse_long(line.substr(0, first), city.population) ||
            !parse_double(line.substr(first + 1, second - first - 1), city.lon) ||
            !parse_double(line.substr(second + 1, third - second - 1), city.lat))
            continue;

        // Bounds may run past ±180 on a date-line crossing; compare in the same frame.
        const double lon = GeoLongitude::unwrapped_near(city.lon, (bounds.lon_min + bounds.lon_max) / 2.0);
        if (city.lat < bounds.lat_min || city.lat > bounds.lat_max || lon < bounds.lon_min || lon > bounds.lon_max)
            continue;

        city.name = line.substr(third + 1);
        found.push_back(std::move(city));
    }

    return found;
}
