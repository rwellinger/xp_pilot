#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Coastlines and lake outlines for the track map, from Natural Earth 1:50m (public
// domain), converted to a compact text format by tools/ and shipped in data/.
//
// Lakes are closed rings and get filled, so water reads as water. Coastlines are open
// lines: which side is sea cannot be derived from the line alone, so they share the
// water colour and read as "the sea is over there". Country borders are open lines too
// and carry their own muted colour.

struct OutlinePoint
{
    double lat = 0, lon = 0;
};

enum class OutlineKind : std::uint8_t
{
    Coastline, // open line along the sea
    Lake,      // closed ring, filled
    Border,    // open line, country boundary
};

struct GeoOutline
{
    OutlineKind               kind = OutlineKind::Coastline;
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
