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
#include <string>

// HTML building blocks shared by the flight report, the index and the summary report,
// so all of them keep one look. Internal to the report generators.
namespace HtmlCommon
{

std::string esc(const std::string &text);

// "1h 05m", or "42m" under an hour.
std::string fmt_dur(int minutes);

// Second-resolution counterpart to fmt_dur(), used where a pause has to add up exactly:
// "1h 23m" from an hour on, "8m 12s" below that, "50s" under a minute.
std::string fmt_dur_sec(int seconds);

// One figure in the row of stat tiles at the top of a report.
std::string stat_tile(const std::string &value, const std::string &label);

std::string rating_color(const std::string &rating);

// Shown on every printed report — sim hours are not real flight time.
inline constexpr const char *SIM_ONLY_NOTICE = "For flight simulation use only. Not a valid pilot logbook record.";

// The on-screen "Print / Save as PDF" button; hidden on paper by PRINT_CSS.
std::string print_toolbar_html();

// Footer repeated at the bottom of every printed page: what the report covers and the
// simulation-only notice. Invisible on screen.
std::string print_footer_html(const std::string &context);

inline constexpr const char *FL_CSS = R"(
<style>
body{background:#1a1a2e;color:#e0e0e0;font-family:sans-serif;margin:0;padding:16px}
h1{color:#00d4ff}h2{color:#aaa;font-size:1em;font-weight:normal;margin-top:0}
.stats{display:flex;flex-wrap:wrap;gap:12px;margin-bottom:20px}
.stat{background:#16213e;border-radius:8px;padding:10px 16px;min-width:120px}
.stat .val{font-size:1.6em;color:#00d4ff}.stat .lbl{font-size:.8em;color:#888}
#map{height:420px;border-radius:8px;margin-bottom:8px}
.legend{color:#888;font-size:.85em;margin:0 0 20px}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:7px;vertical-align:middle}
.dot-pause{background:#ffcc00}
canvas{background:#16213e;border-radius:8px}
.chart-box{position:relative;margin-bottom:20px}
.lcard{background:#16213e;border-radius:8px;padding:14px 18px;margin-bottom:12px;display:inline-block;min-width:300px}
.lcard .rat{font-size:1.4em;font-weight:bold;margin-bottom:8px}
.lcard table{border-collapse:collapse}.lcard td{padding:2px 10px 2px 0}
.lcard .rwy{width:560px;max-width:100%;height:auto;display:block;margin-top:10px}
a{color:#00d4ff}table{width:100%;border-collapse:collapse}
thead tr{color:#888;border-bottom:1px solid #333}
.dot-touchdown{background:#ff66cc}
.legs{margin-bottom:20px}.legs td,.legs th{padding:4px 12px 4px 0;text-align:left}
.badge-heli{background:#3a5;color:#fff;padding:2px 8px;border-radius:4px;font-size:.8em;margin-left:6px;vertical-align:middle}
</style>)";

// Added after FL_CSS by every printable report. Paper gets dark text on white: the screen
// palette would cost a full page of toner and loses contrast in print.
inline constexpr const char *PRINT_CSS = R"(
<style>
.toolbar{margin-bottom:16px}
.toolbar button{background:#00d4ff;color:#101020;border:0;border-radius:6px;padding:8px 16px;font-size:1em;cursor:pointer}
.print-footer{display:none}
@page{size:A4;margin:14mm 12mm 18mm}
@media print{
body{background:#fff;color:#000;padding:0;font-size:10pt}
h1{color:#000}h2,h3{color:#333}h3{break-after:avoid}
.toolbar{display:none}
.stat{background:#f2f2f2;break-inside:avoid;min-width:0;flex:1}.stat .val{color:#000}.stat .lbl{color:#555}
thead tr{color:#555;border-bottom:1px solid #999}
#map{height:95mm;break-inside:avoid}
.chart-box{break-inside:avoid}canvas{background:#fff}
.lcard{background:#f2f2f2;break-inside:avoid;display:block}
.lcard .rat,.lcard b,.rating{color:#000 !important}
.print-footer{display:block;position:fixed;bottom:0;left:0;right:0;font-size:8pt;color:#555;border-top:1px solid #999;padding-top:2mm}
}
</style>)";

} // namespace HtmlCommon
