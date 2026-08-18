#pragma once

#include <string>
#include <vector>

// Coastlines and lake outlines for the track map, from Natural Earth 1:50m (public
// domain), converted to a compact text format by tools/ and shipped in data/.
//
// Lakes are closed rings and get filled, so water reads as water. Coastlines are open
// lines: which side is sea cannot be derived from the line alone, so they are drawn in
// the same blue and read as "water is over there".

struct OutlinePoint
{
    double lat = 0, lon = 0;
};

struct GeoOutline
{
    bool                      is_lake = false; // false: coastline (open line)
    std::vector<OutlinePoint> points;
};

struct OutlineBounds
{
    double lat_min = 0, lat_max = 0, lon_min = 0, lon_max = 0;
};

namespace CoastlineData
{
// Outlines touching `bounds`. Filtering happens while reading; an unreadable file
// yields nothing and the map simply draws without them.
std::vector<GeoOutline> load_outlines(const std::string &coastlines_dat_path, const OutlineBounds &bounds);
} // namespace CoastlineData
