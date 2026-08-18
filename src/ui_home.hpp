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
#include "flight_logger.hpp"
#include <cstddef>
#include <cstdint>

// The tablet home screen: a live status band that stays visible on every screen,
// and the four tiles that lead into the detail screens.
namespace Home
{

enum class Screen : std::uint8_t
{
    Home,
    Live,
    Logbook,
    Archive,
    Settings
};

// What the tiles show underneath their title. Gathered by the caller so this module
// never touches the filesystem.
struct TileSummary
{
    size_t flight_count  = 0;
    size_t archive_count = 0;
    bool   auto_qnh_on   = false;
};

// Fixed band at the top of the window: aircraft, route and the live figures.
// Draws the same layout with placeholder values when no flight is running.
void draw_status_bar(const FlightLogger::LiveFlight &live);

// 2x2 tile grid. Sets `screen` when the user picks one.
void draw_tiles(const FlightLogger::LiveFlight &live, const TileSummary &summary, Screen &screen);

} // namespace Home
