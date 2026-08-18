/*
 * xp_pilot - Flight Logger and Auto QNH plugin for X-Plane 12
 * Copyright (C) 2026 thWelly
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "html_report.hpp"
#include <cstddef>
#include <string>
#include <vector>

// The flight list screen: a selectable table on the left, the flight's detail on the
// right. The logbook and the archive are the same screen with different sources —
// one instance of FlightList each.
namespace FlightListScreen
{

struct FlightList
{
    // Set once at construction.
    std::string subdir;                // "" for active flights, "archived/" for the archive
    bool        allow_archive = false; // archive actions and "rebuild reports" are logbook-only

    std::vector<FlightData> entries;
    std::vector<bool>       checked; // parallel to entries
    bool                    loaded = false;

    int         selected = -1;
    FlightData  detail;
    bool        detail_loaded = false;
    std::string report_path;
    bool        report_exists = false;

    bool confirm_delete_single  = false;
    bool confirm_archive_single = false;
    bool confirm_batch_delete   = false;
    bool confirm_batch_archive  = false;
};

// Re-reads the directory and drops any selection.
void reload(FlightList &list);

// Reads the directory only if it has not been read yet.
void ensure_loaded(FlightList &list);

// Number of flights on disk, without parsing them — for the home screen tiles.
size_t count_on_disk(const std::string &subdir);

// Draws the screen. Returns true when flights were moved into the archive, so the
// caller can invalidate its archive list.
bool draw(FlightList &list);

} // namespace FlightListScreen
