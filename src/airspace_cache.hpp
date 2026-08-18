#pragma once

#include "airspace_data.hpp"

#include <string>
#include <vector>

// Keeps the airspaces for one map view around. Scanning X-Plane's 17 MB airspace file
// costs ~110 ms, far too much for the frame that draws the map, so the work happens on
// a worker thread and the map draws without airspaces until it lands.
namespace AirspaceCache
{
// Path to X-Plane's OpenAir database. Called once at plugin start; an empty path
// disables the feature.
void init(const std::string &airspace_txt_path);

// Joins any running load. Call before the plugin unloads.
void stop();

// Airspaces overlapping `bounds`. Returns what is cached right now — empty while a
// load is still running — and starts a load in the background when `bounds` differs
// from what the cache holds.
std::vector<Airspace> for_bounds(const AirspaceBounds &bounds);
} // namespace AirspaceCache
