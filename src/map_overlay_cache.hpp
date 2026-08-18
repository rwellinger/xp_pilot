#pragma once

#include "airspace_data.hpp"
#include "city_data.hpp"
#include "coastline_data.hpp"

#include <string>
#include <vector>

// Background loader for everything drawn beneath the flight track. Scanning X-Plane's
// 17 MB airspace file costs ~110 ms and the coastline set adds to that — far too much
// for the frame that draws the map — so one worker thread fetches both and the map
// draws without them until the load lands.
namespace MapOverlayCache
{
struct Overlay
{
    std::vector<Airspace>   airspaces; // empty on wide views, see below
    std::vector<GeoOutline> outlines;  // coastlines, lakes and country borders
    std::vector<City>       cities;
};

// Above this span, airspace outlines stop being information and become noise: a
// Zurich-Paris leg crosses 381 of them, almost all irrelevant at cruise level. Wide
// views therefore show geography — borders, water and place names — and leave the
// airspaces to the local flights where they actually say something.
inline constexpr double airspace_span_limit_deg = 3.0; // roughly 330 km north-south

// Candidates, not labels: the renderer drops any that would overlap another label or a
// departure/arrival marker, so it needs a surplus to fall back on. Cities cluster —
// a Pacific crossing finds ten of its fourteen largest places stacked over California
// while the ocean stays empty — and drawing from a deeper list fills the map evenly.
inline constexpr size_t city_limit = 60;

// Data file locations, resolved at plugin start. An empty path disables that layer.
void init(const std::string &airspace_txt_path, const std::string &coastlines_dat_path,
          const std::string &cities_dat_path);

// Joins any running load. Call before the plugin unloads — a worker outliving the
// process would take X-Plane down with it.
void stop();

// What is cached for `bounds` right now: empty while a load is still running. A change
// of bounds starts a load in the background.
Overlay for_bounds(double lat_min, double lat_max, double lon_min, double lon_max);
} // namespace MapOverlayCache
