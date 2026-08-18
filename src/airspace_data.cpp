#include "airspace_data.hpp"

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
    // The conversion is checked: anything but all eight fields rejects the line, which
    // is what the strtol advice is meant to protect against.
    if (sscanf(line.c_str(), "DP %d:%d:%d %c %d:%d:%d %c", &lat_deg, &lat_min, &lat_sec, // NOLINT(bugprone-unchecked-string-to-number-conversion)
               &lat_hemisphere, &lon_deg, &lon_min, &lon_sec, &lon_hemisphere) != 8)
        return false;

    out.lat = lat_deg + lat_min / 60.0 + lat_sec / 3600.0;
    out.lon = lon_deg + lon_min / 60.0 + lon_sec / 3600.0;
    if (lat_hemisphere == 'S')
        out.lat = -out.lat;
    if (lon_hemisphere == 'W')
        out.lon = -out.lon;
    return true;
}

bool touches(const std::vector<AirspacePoint> &outline, const AirspaceBounds &bounds)
{
    for (const auto &point : outline)
    {
        if (point.lat >= bounds.lat_min && point.lat <= bounds.lat_max && point.lon >= bounds.lon_min &&
            point.lon <= bounds.lon_max)
            return true;
    }
    return false;
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
