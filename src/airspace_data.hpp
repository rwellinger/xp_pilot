#pragma once

#include <string>
#include <vector>

// Airspace outlines read from X-Plane's own airspace database
// (Resources/default data/airspaces/airspace.txt), which is OpenAir format. Laminar
// resolves every shape to a point list, so no circle or arc records appear and the
// parser only has to understand DP.
//
// Local data on purpose: it needs no network, no API key and no third-party terms, and
// it is already installed with the simulator.

struct AirspacePoint
{
    double lat = 0, lon = 0;
};

struct Airspace
{
    std::string                 airspace_class; // "A".."G", "CTR", "R", "P", "Q", ...
    std::string                 name;
    std::string                 lower_limit; // "GND", "2500 MSL", "FL195" — verbatim
    std::string                 upper_limit;
    std::vector<AirspacePoint>  outline;
};

// The rectangle an outline must touch to be kept.
struct AirspaceBounds
{
    double lat_min = 0, lat_max = 0, lon_min = 0, lon_max = 0;
};

namespace AirspaceData
{
// Stream the OpenAir file once and return the airspaces overlapping `bounds`. The world
// file holds ~24,000 of them, so filtering happens while parsing rather than after.
// An unreadable file yields no entries — the map simply draws without airspaces.
std::vector<Airspace> load_airspaces(const std::string &airspace_txt_path, const AirspaceBounds &bounds);
} // namespace AirspaceData
