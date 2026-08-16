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

#include "html_report.hpp"
#include <algorithm>
#include <array>
#include <cmath>
// Keep explicit: MSVC needs it; Clang often pulls it in transitively and flags it unused.
#include <cstdio> // snprintf
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <sstream>

using json = nlohmann::json;

// ── Wind condition converters (single source of truth for JSON strings) ──────

WindCondition wind_condition_from_string(const std::string &s)
{
    if (s == "CALM")
        return WindCondition::Calm;
    if (s == "LIGHT")
        return WindCondition::Light;
    return WindCondition::Steady;
}

const char *wind_condition_to_string(WindCondition c)
{
    switch (c)
    {
    case WindCondition::Calm:
        return "CALM";
    case WindCondition::Light:
        return "LIGHT";
    case WindCondition::Steady:
        return "STEADY";
    }
    return "STEADY";
}

// ── Pause resolution ──────────────────────────────────────────────────────────

namespace
{
constexpr int TRACK_SAMPLE_SEC = 10; // update_track_sample() samples this often
constexpr int PAUSE_GAP_SEC    = 15; // wall-clock spacing that means "more than jitter"

// Track points are sampled every 10 seconds of *active* time, so a wider wall-clock gap
// between two of them means the sim stood still in between. That reconstructs the pauses
// of flights recorded before they were stored individually.
std::vector<PauseEvent> pauses_from_track_gaps(const std::vector<TrackPoint> &track)
{
    std::vector<PauseEvent> out;
    for (size_t i = 1; i < track.size(); ++i)
    {
        const time_t gap = track[i].t - track[i - 1].t;
        if (gap <= PAUSE_GAP_SEC)
            continue;
        PauseEvent p;
        p.t   = track[i - 1].t;
        p.sec = static_cast<int>(gap - TRACK_SAMPLE_SEC);
        p.lat = track[i - 1].lat;
        p.lon = track[i - 1].lon;
        out.push_back(p);
    }
    return out;
}
} // namespace

std::vector<PauseEvent> resolve_pauses(const FlightData &fd)
{
    if (!fd.pauses.empty())
        return fd.pauses;
    if (fd.paused_sec <= 0)
        return {};
    return pauses_from_track_gaps(fd.track);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string esc(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '"')
            out += "&quot;";
        else
            out += c;
    }
    return out;
}

static std::string fmt_dur(int min)
{
    char buf[32];
    int  h = min / 60, m = min % 60;
    if (h > 0)
        snprintf(buf, sizeof(buf), "%dh %02dm", h, m);
    else
        snprintf(buf, sizeof(buf), "%dm", m);
    return buf;
}

// Second-resolution counterpart to fmt_dur(), used where a pause has to add up exactly:
// "1h 23m" from an hour on, "8m 12s" below that, "50s" under a minute.
static std::string fmt_dur_sec(int sec)
{
    char buf[32];
    if (sec >= 3600)
        snprintf(buf, sizeof(buf), "%dh %02dm", sec / 3600, (sec % 3600) / 60);
    else if (sec >= 60)
        snprintf(buf, sizeof(buf), "%dm %02ds", sec / 60, sec % 60);
    else
        snprintf(buf, sizeof(buf), "%ds", sec);
    return buf;
}

static std::string stat_tile(const std::string &value, const std::string &label)
{
    return "<div class=\"stat\"><div class=\"val\">" + value + "</div><div class=\"lbl\">" + label + "</div></div>";
}

// Flights recorded with a sim pause show the full calculation — gross time, pause total,
// net block time — to the second, so Total minus Paused visibly adds up. Everything else
// keeps the single, minute-resolution Block Time tile it always had.
static std::string time_stat_tiles(const FlightData &fd)
{
    if (fd.paused_sec <= 0)
        return stat_tile(fmt_dur(fd.block_time_min), "Block Time");

    return stat_tile(fmt_dur_sec(fd.block_time_sec + fd.paused_sec), "Total") +
           stat_tile(fmt_dur_sec(fd.paused_sec), "Paused") +
           stat_tile(fmt_dur_sec(fd.block_time_sec), "Block Time");
}

static std::string rating_color(const std::string &r)
{
    if (r == "BUTTER!")
        return "#ffff00";
    if (r == "GREAT LANDING!")
        return "#40ff40";
    if (r == "ACCEPTABLE")
        return "#00cc00";
    if (r == "HARD LANDING!")
        return "#ff8000";
    if (r == "WASTED!")
        return "#ff2020";
    return "#ffffff";
}

static const char *FL_CSS = R"(
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
.badge-heli{background:#3a5;color:#fff;padding:2px 8px;border-radius:4px;font-size:.8em;margin-left:6px;vertical-align:middle}
</style>)";

// ── Landing card HTML ─────────────────────────────────────────────────────────

namespace
{
std::string pitch_label_for(float pitch_deg)
{
    float qa = std::abs(pitch_deg);
    if (qa <= 1.0f)
        return "good timing";
    if (qa <= 2.0f)
        return (pitch_deg < 0) ? "late" : "early";
    return (pitch_deg < 0) ? "too late" : "too early";
}

std::string centerline_label_for(float offset_m)
{
    char b[64];
    if (std::abs(offset_m) < 1.f)
        return "on centerline";
    snprintf(b, sizeof(b), "%.0f m %s", std::abs(offset_m), offset_m > 0 ? "right" : "left");
    return b;
}

// A scale drawing would render a 3 m deviation as a fraction of a pixel against a
// 1700 m runway, so the lateral axis is exaggerated and labelled as such.
std::string runway_diagram_svg(const LandingData &ld)
{
    if (ld.runway_length_m <= 0.f)
        return {};

    constexpr float WIDTH_PX      = 560.f;
    constexpr float HEIGHT_PX     = 90.f;
    constexpr float MARGIN_PX     = 20.f;
    constexpr float HALF_WIDTH_PX = 26.f; // half the drawn runway body
    constexpr float LATERAL_SPAN_M = 20.f; // deviation mapped onto the drawn half-width

    const float strip_px  = WIDTH_PX - 2 * MARGIN_PX;
    const float center_y  = HEIGHT_PX / 2.f;
    const float along_pct = std::min(1.f, std::max(0.f, ld.runway_distance_m / ld.runway_length_m));
    const float marker_x  = MARGIN_PX + along_pct * strip_px;

    const float offset_clamped = std::min(LATERAL_SPAN_M, std::max(-LATERAL_SPAN_M, ld.runway_offset_m));
    const float marker_y       = center_y + (offset_clamped / LATERAL_SPAN_M) * HALF_WIDTH_PX;

    const float touchdown_zone_px = std::min(strip_px, (300.f / ld.runway_length_m) * strip_px);

    char b[1600];
    snprintf(b, sizeof(b),
             "<svg class=\"rwy\" viewBox=\"0 0 %.0f %.0f\" width=\"100%%\" role=\"img\">"
             "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" fill=\"#3a3a3a\" rx=\"2\"/>"
             "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" fill=\"#4a4a3a\"/>"
             "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#ddd\" stroke-width=\"1.5\" "
             "stroke-dasharray=\"12 10\"/>"
             "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#fff\" stroke-width=\"3\"/>"
             "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"6\" fill=\"#e07a3c\" stroke=\"#fff\" stroke-width=\"1.5\"/>"
             "<text x=\"%.1f\" y=\"%.1f\" fill=\"#aaa\" font-size=\"11\">%s</text>"
             "<text x=\"%.1f\" y=\"%.1f\" fill=\"#aaa\" font-size=\"11\" text-anchor=\"end\">%.0f m</text>"
             "</svg>"
             "<div style=\"color:#666;font-size:.75em;margin-top:2px\">Touchdown point &mdash; lateral scale "
             "exaggerated</div>",
             WIDTH_PX, HEIGHT_PX,
             // runway body
             MARGIN_PX, center_y - HALF_WIDTH_PX, strip_px, 2 * HALF_WIDTH_PX,
             // touchdown zone shading over the first 300 m
             MARGIN_PX, center_y - HALF_WIDTH_PX, touchdown_zone_px, 2 * HALF_WIDTH_PX,
             // centerline
             MARGIN_PX, center_y, WIDTH_PX - MARGIN_PX, center_y,
             // threshold bar
             MARGIN_PX, center_y - HALF_WIDTH_PX, MARGIN_PX, center_y + HALF_WIDTH_PX,
             // touchdown marker
             marker_x, marker_y,
             // labels
             MARGIN_PX + 4, HEIGHT_PX - 4, esc(ld.runway_ident).c_str(), WIDTH_PX - MARGIN_PX, HEIGHT_PX - 4,
             ld.runway_length_m);
    return b;
}

struct WindDisplay
{
    std::string src;
    std::string hw;
    std::string xw;
};

WindDisplay format_wind_display(const LandingData &ld)
{
    int         xw_abs  = std::abs(ld.crosswind_kts);
    int         hw      = ld.headwind_kts;
    const char *xw_side = ld.crosswind_side.c_str();
    char        b[256];

    switch (wind_condition_from_string(ld.wind_status))
    {
    case WindCondition::Calm:
        return {"calm", "calm", "&mdash;"};

    case WindCondition::Light:
    {
        snprintf(b, sizeof(b), "%d kts (light/variable)", ld.wind_speed_kts);
        std::string src_hw = b;
        snprintf(b, sizeof(b), "%d kts %s", xw_abs, xw_side);
        return {src_hw, src_hw, b};
    }

    case WindCondition::Steady:
    {
        WindDisplay w;
        snprintf(b, sizeof(b), "%d kts from %d&deg; mag", ld.wind_speed_kts, ld.wind_dir_mag);
        w.src = b;
        // Rotorcraft don't align with a runway, so a tailwind isn't a "wrong RWY" warning.
        if (hw < -5 && !ld.is_rotorcraft)
            snprintf(b, sizeof(b), "<b style=\"color:#ff8000\">%d kts TAILWIND &mdash; WRONG RWY?</b>", std::abs(hw));
        else if (hw < 0)
            snprintf(b, sizeof(b), "%d kts tailwind", std::abs(hw));
        else
            snprintf(b, sizeof(b), "%d kts headwind", hw);
        w.hw = b;
        snprintf(b, sizeof(b), "%d kts %s", xw_abs, xw_side);
        w.xw = b;
        return w;
    }
    }
    return {};
}
} // namespace

static std::string landing_card(const LandingData &ld, const std::string &profile_name, const std::array<int, 4> &p)
{
    auto        rc = rating_color(ld.rating);
    WindDisplay w  = format_wind_display(ld);

    char b[512];
    std::string out = "<div class=\"lcard\">";
    snprintf(b, sizeof(b), "<div class=\"rat\" style=\"color:%s\">%s</div><table>", rc.c_str(), esc(ld.rating).c_str());
    out += b;

    snprintf(b, sizeof(b), "<tr><td>Vertical Speed</td><td><b style=\"color:%s\">%.0f fpm</b></td></tr>", rc.c_str(),
             ld.fpm);
    out += b;
    snprintf(b, sizeof(b), "<tr><td>G-Force</td><td><b>%.2f G</b></td></tr>", ld.g_force);
    out += b;

    if (ld.ias_kts > 0.f)
    {
        snprintf(b, sizeof(b), "<tr><td>Touchdown Speed</td><td><b>%.0f kts IAS</b> &mdash; %.0f kts GS</td></tr>",
                 ld.ias_kts, ld.ground_speed_kts);
        out += b;
    }

    // Pitch / flare / float / 50-ft-gate are flare-specific metrics — not meaningful for
    // a rotorcraft set-down, where descent rate alone defines the landing quality.
    if (!ld.is_rotorcraft)
    {
        std::string pitch_label = pitch_label_for(ld.pitch_deg);
        snprintf(b, sizeof(b), "<tr><td>Float time</td><td><b>%.1f s</b></td></tr>", ld.float_time);
        out += b;
        snprintf(b, sizeof(b), "<tr><td>AGL at 50ft gate</td><td><b>%.0f ft</b></td></tr>", ld.agl_ft);
        out += b;
        snprintf(b, sizeof(b), "<tr><td>Pitch at TD</td><td><b>%.2f deg/s</b> &mdash; %s</td></tr>", ld.pitch_deg,
                 pitch_label.c_str());
        out += b;
        snprintf(b, sizeof(b), "<tr><td>Pitch rate</td><td><b>%.2f</b></td></tr>", ld.pitch_rate);
        out += b;
        snprintf(b, sizeof(b), "<tr><td>Flare</td><td><b>%s</b></td></tr>", esc(ld.flare).c_str());
        out += b;
    }

    if (ld.bounce_count > 0)
    {
        snprintf(b, sizeof(b), "<tr><td>Bounces</td><td><b style=\"color:#e07a3c\">%d</b></td></tr>", ld.bounce_count);
        out += b;
    }

    if (!ld.runway_ident.empty())
    {
        snprintf(b, sizeof(b), "<tr><td>Runway</td><td><b>%s</b> &mdash; %.0f m usable</td></tr>",
                 esc(ld.runway_ident).c_str(), ld.runway_length_m);
        out += b;
        const float pct       = (ld.runway_length_m > 0.f) ? ld.runway_distance_m / ld.runway_length_m * 100.f : 0.f;
        const float remaining = ld.runway_length_m - ld.runway_distance_m;
        snprintf(b, sizeof(b), "<tr><td>Touchdown point</td><td><b>%.0f m</b> / %.0f ft past threshold &mdash; %.0f%%"
                               " of runway</td></tr>",
                 ld.runway_distance_m, ld.runway_distance_m * 3.28084f, pct);
        out += b;
        snprintf(b, sizeof(b), "<tr><td>Runway remaining</td><td><b>%.0f m</b> / %.0f ft</td></tr>", remaining,
                 remaining * 3.28084f);
        out += b;
        snprintf(b, sizeof(b), "<tr><td>Centerline</td><td><b>%s</b></td></tr>",
                 centerline_label_for(ld.runway_offset_m).c_str());
        out += b;
    }

    snprintf(b, sizeof(b), "<tr><td>Wind</td><td><b>%s</b></td></tr>", w.src.c_str());
    out += b;
    snprintf(b, sizeof(b), "<tr><td>Headwind</td><td>%s</td></tr>", w.hw.c_str());
    out += b;
    snprintf(b, sizeof(b), "<tr><td>Crosswind</td><td><b>%s</b></td></tr>", w.xw.c_str());
    out += b;

    char thresh[128];
    snprintf(thresh, sizeof(thresh), "Butter &gt;%d / Great &gt;%d / Acceptable &gt;%d / Hard &gt;%d", p[0], p[1], p[2],
             p[3]);
    snprintf(b, sizeof(b), "<tr><td style=\"color:#666;font-size:.85em\" colspan=\"2\">Profile: %s &mdash; %s</td></tr>",
             esc(profile_name).c_str(), thresh);
    out += b;

    out += "</table>";
    if (!ld.runway_ident.empty())
        out += runway_diagram_svg(ld);
    out += "</div>\n";
    return out;
}

// ── Report generation ─────────────────────────────────────────────────────────

namespace
{
struct TrackJsArrays
{
    std::string lats = "[]";
    std::string lons = "[]";
    std::string alts = "[]";
    std::string spds = "[]";
    double      center_lat = 0;
    double      center_lon = 0;
};

TrackJsArrays track_js_arrays(const std::vector<TrackPoint> &track)
{
    TrackJsArrays out;
    if (track.empty())
        return out;

    std::string lats, lons, alts, spds;
    for (size_t i = 0; i < track.size(); ++i)
    {
        char b[32];
        if (i)
        {
            lats += ',';
            lons += ',';
            alts += ',';
            spds += ',';
        }
        snprintf(b, sizeof(b), "%.6f", track[i].lat);
        lats += b;
        snprintf(b, sizeof(b), "%.6f", track[i].lon);
        lons += b;
        snprintf(b, sizeof(b), "%d", track[i].alt_ft);
        alts += b;
        snprintf(b, sizeof(b), "%d", track[i].spd_kts);
        spds += b;
    }

    const TrackPoint &mid = track[track.size() / 2];
    out.lats              = "[" + lats + "]";
    out.lons              = "[" + lons + "]";
    out.alts              = "[" + alts + "]";
    out.spds              = "[" + spds + "]";
    out.center_lat        = mid.lat;
    out.center_lon        = mid.lon;
    return out;
}

std::string pauses_js_array(const std::vector<PauseEvent> &pauses)
{
    std::string js;
    for (const auto &p : pauses)
    {
        if (p.lat == 0.0 && p.lon == 0.0)
            continue; // no position recorded — nothing to mark on the map
        char b[96];
        snprintf(b, sizeof(b), "%s[%.6f,%.6f,\"%s\"]", js.empty() ? "" : ",", p.lat, p.lon, fmt_dur_sec(p.sec).c_str());
        js += b;
    }
    return "[" + js + "]";
}

std::string landing_cards_html(const FlightData &fd, const std::string &profile_name,
                               const std::array<int, 4> &thresholds)
{
    if (fd.landings.empty())
        return "<p style='color:#888'>No landing recorded.</p>";

    std::string out;
    for (size_t i = 0; i < fd.landings.size(); ++i)
    {
        if (fd.landings.size() > 1)
        {
            char h[64];
            snprintf(h, sizeof(h), "<h3>Landing %zu</h3>\n", i + 1);
            out += h;
        }
        out += landing_card(fd.landings[i], profile_name, thresholds);
    }
    return out;
}

constexpr const char *REPORT_CDN_HEAD =
    "<link rel=\"stylesheet\" href=\"https://unpkg.com/leaflet@1.9.4/dist/leaflet.css\"/>"
    "<script src=\"https://unpkg.com/leaflet@1.9.4/dist/leaflet.js\"></script>"
    "<script src=\"https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js\"></script>";

std::string line_chart_js(const char *canvas_id, const char *label, const char *data_var, const char *border_color,
                          const char *fill_color, const char *unit)
{
    std::ostringstream js;
    js << "new Chart(document.getElementById('" << canvas_id << "'),{type:'line',data:{labels:lb,datasets:[{label:'"
       << label << "',data:" << data_var << ",borderColor:'" << border_color << "',backgroundColor:'" << fill_color
       << "',tension:.3,pointRadius:0,fill:true}]},options:{maintainAspectRatio:false,plugins:{legend:{labels:{color:'"
          "#e0e0e0',font:{size:13}}}},scales:{x:{ticks:{color:'#aaa',font:{size:12},maxTicksLimit:8},grid:{color:'#"
          "333'}},y:{beginAtZero:true,ticks:{color:'#aaa',font:{size:12},callback:function(v){return v+' "
       << unit << "'}},grid:{color:'#333'}}}}});";
    return js.str();
}

std::string map_and_charts_script(const TrackJsArrays &track, const std::string &js_pauses, const char *time_fmt)
{
    char clat_s[32], clon_s[32];
    snprintf(clat_s, sizeof(clat_s), "%.6f", track.center_lat);
    snprintf(clon_s, sizeof(clon_s), "%.6f", track.center_lon);

    std::ostringstream js;
    js << "<script>"
       << "var lats=" << track.lats << ",lons=" << track.lons << ",alts=" << track.alts << ",spds=" << track.spds
       << ",pauses=" << js_pauses << ";"
       << "var map=L.map('map');"
       << "L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png',"
       << "{maxZoom:19,attribution:'&copy; OpenStreetMap contributors &copy; CARTO',subdomains:'abcd'}).addTo(map);"
       << "if(lats.length>0){"
       << "var coords=lats.map(function(la,i){return[la,lons[i]];});"
       << "var poly=L.polyline(coords,{color:'#00d4ff',weight:2}).addTo(map);"
       << "L.circleMarker(coords[0],{radius:6,color:'#00ff80',fillOpacity:1}).bindTooltip('Departure').addTo(map);"
       << "L.circleMarker(coords[coords.length-1],{radius:6,color:'#ff4040',fillOpacity:1}).bindTooltip('Arrival')."
          "addTo(map);"
       << "pauses.forEach(function(p){L.circleMarker([p[0],p[1]],{radius:6,color:'#ffcc00',fillColor:'#ffcc00',"
          "fillOpacity:.9}).bindTooltip('Pause '+p[2]).addTo(map);});"
       << "map.fitBounds(poly.getBounds(),{padding:[20,20]});"
       << "}else{map.setView([" << clat_s << "," << clon_s << "],8);}"
       << "var fmt='" << time_fmt << "';"
       << "var lb=alts.map(function(_,i){var s=i*10;"
          "if(fmt==='ms'){var m=Math.floor(s/60),ss=s%60;return m+':'+(ss<10?'0':'')+ss;}"
          "var h=Math.floor(s/3600),mm=Math.floor((s%3600)/60);return h+':'+(mm<10?'0':'')+mm;});"
       << line_chart_js("ac", "Altitude (ft)", "alts", "#00d4ff", "rgba(0,212,255,.08)", "ft")
       << line_chart_js("sc", "IAS (kts)", "spds", "#ff9900", "rgba(255,153,0,.08)", "kts")
       << "</script>";
    return js.str();
}

std::string flight_header_html(const FlightData &fd)
{
    std::ostringstream html;
    html << "<h1>" << esc(fd.departure_icao) << " &rarr; " << esc(fd.arrival_icao) << "</h1>"
         << "<h2>" << esc(fd.date) << " " << esc(fd.start_utc) << "&ndash;" << esc(fd.end_utc) << " UTC &bull; "
         << esc(fd.aircraft_icao) << " " << esc(fd.aircraft_tail)
         << (fd.aircraft_category == "rotorcraft" ? "<span class=\"badge-heli\">Helicopter</span>" : "") << "</h2>"
         << "<div class=\"stats\">" << time_stat_tiles(fd) << stat_tile(std::to_string(fd.max_altitude_ft) + " ft", "Max Altitude")
         << stat_tile(std::to_string(fd.max_speed_kts) + " kts", "Max Speed")
         << stat_tile(std::to_string(fd.track.size()), "Track Points") << "</div>";
    return html.str();
}
} // namespace

std::string HtmlReport::generate(const FlightData &fd, const std::string &data_dir, const std::string &json_filename,
                                 const std::string &profile_name, const std::array<int, 4> &thresholds)
{
    const TrackJsArrays           track   = track_js_arrays(fd.track);
    const std::vector<PauseEvent> pauses  = resolve_pauses(fd);
    const std::string             js_pauses = pauses_js_array(pauses);
    const std::string             lcards  = landing_cards_html(fd, profile_name, thresholds);

    const bool  is_short_flight = fd.block_time_min < 30;
    const int   ac_height       = is_short_flight ? 180 : 240;
    const int   sc_height       = is_short_flight ? 140 : 180;
    const char *time_fmt        = is_short_flight ? "ms" : "hm";

    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>" << esc(fd.departure_icao) << " to "
         << esc(fd.arrival_icao) << "</title>" << REPORT_CDN_HEAD << FL_CSS << "</head><body>"
         << flight_header_html(fd) << "<div id=\"map\"></div>"
         << (pauses.empty() ? ""
                            : "<p class=\"legend\"><span class=\"dot dot-pause\"></span>Pause &mdash; sim was "
                              "paused here; the time is not part of the block time</p>")
         << "<div class=\"chart-box\" style=\"height:" << ac_height << "px\"><canvas id=\"ac\"></canvas></div>"
         << "<div class=\"chart-box\" style=\"height:" << sc_height << "px\"><canvas id=\"sc\"></canvas></div>"
         << "<h3>Landing" << (fd.landings.size() > 1 ? "s" : "") << "</h3>" << lcards
         << "<p style=\"color:#444;font-size:.8em\"><a href=\"../index.html\">&larr; All flights</a></p>"
         << map_and_charts_script(track, js_pauses, time_fmt) << "</body></html>";

    std::string   rname = json_filename.substr(0, json_filename.rfind('.')) + ".html";
    std::ofstream f(data_dir + "reports/" + rname);
    if (!f.is_open())
        return "";
    f << html.str();
    return rname;
}

// ── Index generation ──────────────────────────────────────────────────────────

void HtmlReport::generate_index(const std::string &data_dir)
{
    std::string fdir = data_dir + "flights/";

    std::vector<std::string> fnames;
    std::error_code          ec;
    auto                     it = std::filesystem::directory_iterator(fdir, ec);
    if (ec)
        return;
    for (auto &entry : it)
    {
        if (entry.is_regular_file())
        {
            std::string n = entry.path().filename().string();
            if (n.size() > 5 && n.substr(n.size() - 5) == ".json")
                fnames.push_back(n);
        }
    }
    std::sort(fnames.begin(), fnames.end());

    struct Row
    {
        std::string date, start_utc, dep, arr, ac, tail, link, rating;
        int         dur = 0, alt = 0;
    };
    std::vector<Row> rows;

    for (auto &fname : fnames)
    {
        std::string   path = fdir + fname;
        std::ifstream f(path);
        if (!f.is_open())
            continue;
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        FlightData  fd = parse_flight_json(content, fname);

        // Last rating
        std::string last_rating;
        for (auto &ld : fd.landings)
            last_rating = ld.rating;

        rows.push_back({fd.date, fd.start_utc, fd.departure_icao, fd.arrival_icao, fd.aircraft_icao, fd.aircraft_tail,
                        "reports/" + fname.substr(0, fname.rfind('.')) + ".html", last_rating, fd.block_time_min,
                        fd.max_altitude_ft});
    }

    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.date > b.date; });

    std::ostringstream tbody;
    for (auto &r : rows)
    {
        auto        c  = rating_color(r.rating);
        std::string ts = !r.start_utc.empty() ? (r.date + " " + r.start_utc + " UTC") : r.date;
        char        buf[1024];
        snprintf(buf, sizeof(buf),
                 "<tr><td>%s</td><td><b>%s &rarr; %s</b></td><td>%s %s</td><td>%s</td>"
                 "<td>%d ft</td><td style=\"color:%s\">%s</td><td><a href=\"%s\">Report</a></td></tr>\n",
                 esc(ts).c_str(), esc(r.dep).c_str(), esc(r.arr).c_str(), esc(r.ac).c_str(), esc(r.tail).c_str(),
                 fmt_dur(r.dur).c_str(), r.alt, c.c_str(), esc(r.rating).c_str(), esc(r.link).c_str());
        tbody << buf;
    }

    std::string idx = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Flight Log</title>" +
                      std::string(FL_CSS) +
                      "</head><body>"
                      "<h1>Flight Log</h1>"
                      "<p style=\"color:#888\">" +
                      std::to_string(rows.size()) +
                      " flights</p>"
                      "<table><thead><tr><th align=\"left\">Date</th><th align=\"left\">Route</th>"
                      "<th align=\"left\">Aircraft</th><th align=\"left\">Duration</th>"
                      "<th align=\"left\">Max Alt</th><th align=\"left\">Landing</th><th></th></tr></thead>"
                      "<tbody>" +
                      tbody.str() + "</tbody></table></body></html>";

    std::ofstream out(data_dir + "index.html");
    if (out.is_open())
        out << idx;
}

// ── JSON parsing ──────────────────────────────────────────────────────────────

// Up to schema version 3 the logger wrongly applied an m/s→kt conversion to
// sim/flightmodel/position/indicated_airspeed, which already reports knots. Older
// files therefore store IAS values inflated by that factor; scale them back on read
// so historic flights render correct speeds. Ground speed was never affected.
static float legacy_ias_scale(int schema_version)
{
    return (schema_version <= 3) ? 1.f / 1.94384f : 1.f;
}

FlightData parse_flight_json(const std::string &content, const std::string &filename)
{
    FlightData fd;
    fd.filename = filename;

    try
    {
        auto        j        = json::parse(content);
        const float ias_scale = legacy_ias_scale(j.value("version", 1));
        fd.date            = j.value("date", "?");
        fd.start_utc       = j.value("start_utc", "");
        fd.end_utc         = j.value("end_utc", "");
        fd.departure_icao  = j.value("departure_icao", "");
        fd.arrival_icao    = j.value("arrival_icao", "");
        fd.aircraft_icao     = j.value("aircraft_icao", "");
        fd.aircraft_tail     = j.value("aircraft_tail", "");
        fd.aircraft_category = j.value("aircraft_category", "fixed_wing");
        fd.start_time        = j.value("start_time", static_cast<time_t>(0));
        fd.end_time        = j.value("end_time", static_cast<time_t>(0));
        fd.block_time_min  = j.value("block_time_min", 0);
        fd.paused_sec      = j.value("paused_sec", 0);
        fd.block_time_sec  = j.value("block_time_sec", fd.block_time_min * 60);
        fd.max_altitude_ft = j.value("max_altitude_ft", 0);
        fd.max_speed_kts   = static_cast<int>(std::lround(j.value("max_speed_kts", 0) * ias_scale));

        if (j.contains("track") && j["track"].is_array())
        {
            for (auto &tp : j["track"])
            {
                TrackPoint p;
                p.t       = tp.value("t", static_cast<time_t>(0));
                p.lat     = tp.value("lat", 0.0);
                p.lon     = tp.value("lon", 0.0);
                p.alt_ft  = tp.value("alt", 0);
                p.spd_kts = static_cast<int>(std::lround(tp.value("spd", 0) * ias_scale));
                p.vs_fpm  = tp.value("vs", 0);
                fd.track.push_back(p);
            }
        }

        if (j.contains("pauses") && j["pauses"].is_array())
        {
            for (auto &pj : j["pauses"])
            {
                PauseEvent p;
                p.t   = pj.value("t", static_cast<time_t>(0));
                p.sec = pj.value("sec", 0);
                p.lat = pj.value("lat", 0.0);
                p.lon = pj.value("lon", 0.0);
                fd.pauses.push_back(p);
            }
        }

        if (j.contains("landings") && j["landings"].is_array())
        {
            for (auto &lj : j["landings"])
            {
                LandingData ld;
                ld.fpm            = lj.value("fpm", 0.0f);
                ld.g_force        = lj.value("g_force", 0.0f);
                ld.pitch_deg      = lj.value("pitch_deg", 0.0f);
                ld.pitch_rate     = lj.value("pitch_rate", 0.0f);
                ld.agl_ft         = lj.value("agl_ft", 0.0f);
                ld.float_time       = lj.value("float_time", 0.0f);
                ld.ias_kts          = lj.value("ias_kts", 0.0f) * ias_scale;
                ld.ground_speed_kts = lj.value("ground_speed_kts", 0.0f);
                ld.lat              = lj.value("lat", 0.0);
                ld.lon              = lj.value("lon", 0.0);
                ld.heading_true     = lj.value("heading_true", 0.0f);
                ld.time             = lj.value("time", static_cast<time_t>(0));
                ld.wind_speed_kts = lj.value("wind_speed_kts", 0);
                ld.wind_dir_mag   = lj.value("wind_dir_mag", 0);
                ld.headwind_kts   = lj.value("headwind_kts", 0);
                ld.crosswind_kts  = lj.value("crosswind_kts", 0);
                ld.bounce_count   = lj.value("bounce_count", 0);
                ld.is_rotorcraft  = lj.value("is_rotorcraft", false);
                ld.flare          = lj.value("flare", "");
                ld.rating         = lj.value("rating", "");
                ld.wind_status    = lj.value("wind_status", "STEADY");
                ld.crosswind_side = lj.value("crosswind_side", "");
                ld.airport_icao   = lj.value("airport_icao", "");

                ld.runway_ident      = lj.value("runway_ident", "");
                ld.runway_offset_m   = lj.value("runway_offset_m", 0.0f);
                ld.runway_distance_m = lj.value("runway_distance_m", 0.0f);
                ld.runway_length_m   = lj.value("runway_length_m", 0.0f);
                fd.landings.push_back(ld);
            }
        }
    }
    catch (...) // NOLINT(bugprone-empty-catch)
    {
        // Return partially-filled fd on parse error — malformed JSON must not crash the logbook scan
    }

    return fd;
}
