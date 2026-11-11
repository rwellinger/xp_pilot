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
#include <ctime>
#include <string>

// The printable summary report: one HTML page per period and aircraft filter, laid out
// for A4 so the browser's "Save as PDF" turns it into the PDF the user keeps.
namespace SummaryReport
{

// "summary_2026-11_all.html", "summary_all_DA42.html".
std::string filename(const FlightSummary::Filter &filter);

std::string html(const FlightSummary::Summary &summary, const FlightSummary::Filter &filter, time_t generated_at);

// Writes the report to <output_dir>/summaries/. Returns its full path, or "" when the
// file could not be written.
std::string generate(const FlightSummary::Summary &summary, const FlightSummary::Filter &filter,
                     const std::string &output_dir);

} // namespace SummaryReport
