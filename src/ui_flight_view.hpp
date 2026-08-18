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
#include <string>

// Presentation of a single FlightData — shared by the logbook, the archive and the
// live screen. Read-only: nothing here touches disk except open_in_browser().
namespace FlightView
{

std::string format_duration(int minutes);
std::string format_duration_sec(int seconds);

// Opens a URL or local file in the user's default browser.
void        open_in_browser(const std::string &target);
std::string skyvector_url(double latitude, double longitude);

// Gross/pause/net time when the flight was paused, plain block time otherwise.
void draw_time_lines(const FlightData &flight);

// Top-down view of the flown route. For a running flight the end marker is the
// aircraft's current position.
void draw_track_map(const FlightData &flight, float width);

// One block per recorded landing: rating, touchdown numbers, runway placement, wind.
void draw_landings(const FlightData &flight);

// Route, aircraft, times, stats, track map and landings — the full read-only detail
// panel. Action buttons are drawn by the caller so each screen keeps its own.
void draw_detail(const FlightData &flight, float width);

} // namespace FlightView
