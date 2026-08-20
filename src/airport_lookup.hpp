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
#include <vector>

// Airport identification and runway geometry lookup.
//
// Scanning apt.dat costs ~100 ms, far too much for the frame in which the aircraft
// touches down. The destination's runways are therefore fetched on a worker thread
// during the approach; the touchdown itself then only does arithmetic on cached data.
namespace AirportLookup
{

// apt_dat_path points at X-Plane's global airport database. Must be called before
// any preload is requested.
void init(const std::string &apt_dat_path);

// Joins a pending worker. Must be called before the plugin unloads — the thread
// touches this module's state and may not outlive it.
void stop();

// ICAO code of the airport nearest to the given position, or "" when there is none.
// Result is cached for 5 seconds; the lookup itself is expensive.
std::string nearest_airport_id(double latitude, double longitude);

// Owns the runway-analysis feature toggle, persisted via settings.json.
void set_analysis_enabled(bool on);
bool analysis_enabled();

// Start fetching an airport's runways in the background. A no-op while another
// load is still in flight — the next approach picks that airport up instead.
void request_runway_preload(const std::string &icao);

// Place a touchdown on its runway from the preloaded cache. Leaves the landing
// untouched when its airport was not cached; resolve_runways() fills those in.
void apply_runway_fix(LandingData &landing);

// Catch the touchdowns the approach preload didn't cover — a touch-and-go at a field
// the aircraft descended into too quickly, or a second airport later in the flight.
// Runs at shutdown, where a file scan costs nothing. Also joins a pending worker.
void resolve_runways(std::vector<LandingData> &landings);

} // namespace AirportLookup
