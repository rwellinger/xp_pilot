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

#include "summary_report.hpp"
#include "flight_legs.hpp"
#include "html_common.hpp"
#include <cctype>
#include <cstdio> // snprintf
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

using HtmlCommon::esc;
using HtmlCommon::fmt_dur;
using HtmlCommon::rating_color;
using HtmlCommon::stat_tile;

namespace
{

// Added on top of the shared report and print CSS.
const char *SUMMARY_CSS = R"(
<style>
.sim-only{background:#4a3200;color:#ffcc66;border:1px solid #8a6000;border-radius:6px;padding:8px 14px;margin-bottom:16px;font-weight:bold}
.panel{margin-bottom:24px;break-inside:avoid}
.cols{display:flex;flex-wrap:wrap;gap:24px}.cols .panel{flex:1;min-width:260px}
table.summary td,table.summary th{padding:4px 12px 4px 0;text-align:left}
table.summary tr{break-inside:avoid}
td.num,th.num{text-align:right}
table.ratings{width:auto}table.ratings td{padding-right:24px}
.bar-track{background:#16213e;border-radius:4px;height:12px;width:220px}
.bar{height:12px;border-radius:4px}
.empty{color:#888}
@media print{
.sim-only{background:#fff;color:#000;border:2px solid #000}
.bar-track{background:#e6e6e6}
}
</style>)";

std::string aircraft_label(const FlightSummary::Filter &filter)
{
    return filter.aircraft_icao.empty() ? "All aircraft types" : filter.aircraft_icao;
}

// Aircraft codes come from the sim and from hand-edited files; only plain characters
// make it into a file name.
std::string file_safe(const std::string &text)
{
    std::string safe;
    for (char c : text)
        safe += std::isalnum(static_cast<unsigned char>(c)) || c == '-' ? c : '_';
    return safe.empty() ? "unknown" : safe;
}

std::string utc_timestamp(time_t when)
{
    char             text[32] = "?";
    const struct tm *utc      = std::gmtime(&when);
    if (utc)
        std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M UTC", utc);
    return text;
}

std::string format_fpm(float fpm)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f fpm", fpm);
    return text;
}

std::string format_distance(double distance_nm)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f nm", distance_nm);
    return text;
}

std::string format_g(float g_force)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%.2f G", g_force);
    return text;
}

std::string header_html(const FlightSummary::Filter &filter, time_t generated_at)
{
    std::ostringstream html;
    html << "<div class=\"sim-only\">&#9888; " << HtmlCommon::SIM_ONLY_NOTICE << "</div>"
         << HtmlCommon::print_toolbar_html()
         << "<h1>Flight Summary</h1>"
         << "<h2>" << esc(filter.period.label()) << " &bull; " << esc(aircraft_label(filter)) << " &bull; generated "
         << utc_timestamp(generated_at) << "</h2>";
    return html.str();
}

std::string totals_html(const FlightSummary::Summary &summary)
{
    return "<div class=\"stats\">" + stat_tile(std::to_string(summary.flights), "Flights") +
           stat_tile(fmt_dur(summary.block_time_sec / 60), "Block Time") +
           stat_tile(std::to_string(summary.landings), "Landings") +
           stat_tile(format_distance(summary.distance_nm), "Distance") +
           stat_tile(std::to_string(summary.airports), "Airports") + "</div>";
}

std::string rating_rows_html(const FlightSummary::Summary &summary)
{
    std::ostringstream rows;
    for (const FlightSummary::Ranked &rating : summary.ratings)
    {
        const int percent = summary.landings > 0 ? rating.count * 100 / summary.landings : 0;
        rows << "<tr><td class=\"rating\" style=\"color:" << rating_color(rating.name) << "\">" << esc(rating.name)
             << "</td><td><div class=\"bar-track\"><div class=\"bar\" style=\"width:" << percent
             << "%;background:" << rating_color(rating.name) << "\"></div></div></td><td class=\"num\">"
             << rating.count << "</td><td class=\"num\">" << percent << "%</td></tr>";
    }
    return rows.str();
}

std::string landing_quality_html(const FlightSummary::Summary &summary)
{
    std::ostringstream html;
    html << "<div class=\"panel\"><h3>Landing Quality</h3>";
    if (summary.landings == 0)
        return html.str() + "<p class=\"empty\">No landings in this period.</p></div>";

    html << "<div class=\"stats\">" << stat_tile(format_fpm(summary.average_fpm), "Average")
         << stat_tile(format_fpm(summary.softest_fpm), "Softest") << stat_tile(format_fpm(summary.hardest_fpm), "Hardest")
         << stat_tile(format_g(summary.max_g), "Max G") << "</div>"
         << "<table class=\"summary ratings\"><tbody>" << rating_rows_html(summary) << "</tbody></table></div>";
    return html.str();
}

std::string aircraft_html(const FlightSummary::Summary &summary)
{
    std::ostringstream html;
    html << "<div class=\"panel\"><h3>Aircraft</h3><table class=\"summary\"><thead><tr><th>Type</th>"
            "<th class=\"num\">Flights</th><th class=\"num\">Block Time</th><th class=\"num\">Landings</th></tr>"
            "</thead><tbody>";
    for (const FlightSummary::AircraftTotals &aircraft : summary.aircraft)
        html << "<tr><td><b>" << esc(aircraft.icao) << "</b></td><td class=\"num\">" << aircraft.flights
             << "</td><td class=\"num\">" << fmt_dur(aircraft.block_time_sec / 60) << "</td><td class=\"num\">"
             << aircraft.landings << "</td></tr>";
    html << "</tbody></table></div>";
    return html.str();
}

std::string ranked_html(const char *title, const char *column, const std::vector<FlightSummary::Ranked> &ranked)
{
    std::ostringstream html;
    html << "<div class=\"panel\"><h3>" << title << "</h3><table class=\"summary\"><thead><tr><th>" << column
         << "</th><th class=\"num\">Flights</th></tr></thead><tbody>";
    for (const FlightSummary::Ranked &entry : ranked)
        html << "<tr><td>" << esc(entry.name) << "</td><td class=\"num\">" << entry.count << "</td></tr>";
    html << "</tbody></table></div>";
    return html.str();
}

std::string last_rating(const FlightData &flight)
{
    return flight.landings.empty() ? "" : flight.landings.back().rating;
}

std::string flight_row_html(const FlightData &flight)
{
    const std::string route  = FlightLegs::route_text(FlightLegs::derive_route(flight), " \xe2\x86\x92 ", false);
    const std::string rating = last_rating(flight);

    std::ostringstream row;
    row << "<tr><td>" << esc(flight.date) << "</td><td>" << esc(flight.start_utc) << "</td><td><b>" << esc(route)
        << "</b></td><td>" << esc(flight.aircraft_icao) << " " << esc(flight.aircraft_tail) << "</td><td class=\"num\">"
        << fmt_dur(flight.block_time_sec / 60) << "</td><td class=\"num\">" << flight.landings.size()
        << "</td><td class=\"rating\" style=\"color:" << rating_color(rating) << "\">" << esc(rating) << "</td></tr>";
    return row.str();
}

std::string flight_list_html(const FlightSummary::Summary &summary)
{
    std::ostringstream html;
    html << "<div><h3>Flights</h3><table class=\"summary\"><thead><tr><th>Date</th><th>UTC</th><th>Route</th>"
            "<th>Aircraft</th><th class=\"num\">Block Time</th><th class=\"num\">Landings</th><th>Last Landing</th>"
            "</tr></thead><tbody>";
    for (const FlightData &flight : summary.flight_list)
        html << flight_row_html(flight);
    html << "</tbody></table></div>";
    return html.str();
}

} // namespace

std::string SummaryReport::filename(const FlightSummary::Filter &filter)
{
    const std::string aircraft = filter.aircraft_icao.empty() ? "all" : file_safe(filter.aircraft_icao);
    return "summary_" + filter.period.key() + "_" + aircraft + ".html";
}

std::string SummaryReport::html(const FlightSummary::Summary &summary, const FlightSummary::Filter &filter,
                                time_t generated_at)
{
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Flight Summary &ndash; "
         << esc(filter.period.label()) << "</title>" << HtmlCommon::FL_CSS << HtmlCommon::PRINT_CSS << SUMMARY_CSS << "</head><body>"
         << header_html(filter, generated_at);

    if (summary.flights == 0)
        html << "<p class=\"empty\">No flights in this period.</p>";
    else
        html << totals_html(summary) << landing_quality_html(summary) << aircraft_html(summary)
             << "<div class=\"cols\">" << ranked_html("Top Airports", "Airport", summary.top_airports)
             << ranked_html("Top Routes", "Route", summary.top_routes) << "</div>" << flight_list_html(summary);

    html << HtmlCommon::print_footer_html(filter.period.label() + " - " + aircraft_label(filter)) << "</body></html>";
    return html.str();
}

std::string SummaryReport::generate(const FlightSummary::Summary &summary, const FlightSummary::Filter &filter,
                                    const std::string &output_dir)
{
    const std::string dir = output_dir + "summaries/";
    std::error_code   ec;
    std::filesystem::create_directories(dir, ec);

    std::string       path = dir + filename(filter);
    std::ofstream     file(path);
    if (!file.is_open())
        return "";
    file << html(summary, filter, std::time(nullptr));
    return path;
}
