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
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// The reports screen: totals for a chosen period and aircraft type, and the button that
// turns them into the printable summary report. Draws only — the caller loads the
// flights and writes the report.
namespace SummaryScreen
{

struct State
{
    std::vector<FlightSummary::LoggedFlight> flights;
    FlightSummary::Filter                    filter;
    FlightSummary::Summary                   summary;     // of `filter`, recomputed when it changes
    std::vector<std::string>                 route_cells; // parallel to summary.flight_list

    std::vector<FlightSummary::Period> periods;  // offered in the period combo
    std::vector<std::string>           aircraft; // types flown in the selected period

    std::string report_path; // last report written, shown below the toolbar
    bool        report_failed = false;
};

// Takes over a freshly read log and starts on the month `now` falls in.
void load(State &state, std::vector<FlightSummary::LoggedFlight> flights, time_t now);

enum class Action : std::uint8_t
{
    None,
    OpenReport
};

Action draw(State &state);

} // namespace SummaryScreen
