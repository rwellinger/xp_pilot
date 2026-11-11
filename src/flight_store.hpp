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
#include "flight_summary.hpp"
#include "html_report.hpp"
#include <string>
#include <vector>

// Reading and writing the flight log on disk. Owns the JSON format — over 3000
// users have logs in it, so its field names and fallbacks are compatibility surface.
namespace FlightStore
{

// Write one flight to <output_dir>/flights/. Returns the bare filename, or an empty
// string when the file could not be opened.
std::string save(const FlightData &flight, const std::string &output_dir);

// Newest logged flight that actually contains a landing, so the popup can be replayed
// in a fresh X-Plane session. False when no logged flight has one.
bool load_last_landing(const std::string &output_dir, LandingData &out);

// Every flight in `flights_dir` (non-recursive), parsed without its track samples to
// keep memory low, newest first.
std::vector<FlightData> read_summaries(const std::string &flights_dir);

// Active and archived flights alike: archiving tidies the logbook, it does not unfly a
// flight. The track is reduced to the distance flown before it is dropped.
std::vector<FlightSummary::LoggedFlight> read_logged_flights(const std::string &output_dir);

} // namespace FlightStore
