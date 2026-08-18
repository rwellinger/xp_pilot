#pragma once

#include "airspace_data.hpp"
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
    std::vector<Airspace>   airspaces;
    std::vector<GeoOutline> outlines; // coastlines and lakes
};

// Data file locations, resolved at plugin start. An empty path disables that layer.
void init(const std::string &airspace_txt_path, const std::string &coastlines_dat_path);

// Joins any running load. Call before the plugin unloads — a worker outliving the
// process would take X-Plane down with it.
void stop();

// What is cached for `bounds` right now: empty while a load is still running. A change
// of bounds starts a load in the background.
Overlay for_bounds(double lat_min, double lat_max, double lon_min, double lon_max);
} // namespace MapOverlayCache
