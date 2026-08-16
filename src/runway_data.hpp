#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

// Runway geometry read from X-Plane's apt.dat. The XPLM SDK exposes airports but no
// runway layout, so the scenery database is the only source for thresholds and widths.

struct RunwayEnd
{
    std::string designator; // "14", "32L", ...
    double      lat = 0, lon = 0;
    float       displaced_threshold_m = 0;
};

struct Runway
{
    RunwayEnd ends[2];
    float     width_m = 0;
};

namespace RunwayData
{
// Stream apt.dat once and return the runways of every requested airport that was found.
// Airports without a land-runway record, and unreadable files, yield no entry.
std::map<std::string, std::vector<Runway>> load_runways(const std::string           &apt_dat_path,
                                                        const std::set<std::string> &icaos);
} // namespace RunwayData
