#include "html_report.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
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
#map{height:420px;border-radius:8px;margin-bottom:20px}
canvas{background:#16213e;border-radius:8px;margin-bottom:20px}
.lcard{background:#16213e;border-radius:8px;padding:14px 18px;margin-bottom:12px;display:inline-block;min-width:300px}
.lcard .rat{font-size:1.4em;font-weight:bold;margin-bottom:8px}
.lcard table{border-collapse:collapse}.lcard td{padding:2px 10px 2px 0}
a{color:#00d4ff}table{width:100%;border-collapse:collapse}
thead tr{color:#888;border-bottom:1px solid #333}
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
        if (hw < -5)
            snprintf(b, sizeof(b), "<b style=\"color:#ff8000\">%d kts TAILWIND &mdash; WRONG RWY?</b>", std::abs(hw));
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
    auto        rc          = rating_color(ld.rating);
    std::string pitch_label = pitch_label_for(ld.pitch_deg);
    WindDisplay w           = format_wind_display(ld);

    char thresh[128];
    snprintf(thresh, sizeof(thresh), "Butter &gt;%d / Great &gt;%d / Acceptable &gt;%d / Hard &gt;%d", p[0], p[1], p[2],
             p[3]);

    char buf[2048];
    snprintf(buf, sizeof(buf),
             "<div class=\"lcard\">"
             "<div class=\"rat\" style=\"color:%s\">%s</div>"
             "<table>"
             "<tr><td>Vertical Speed</td><td><b style=\"color:%s\">%.0f fpm</b></td></tr>"
             "<tr><td>G-Force</td><td><b>%.2f G</b></td></tr>"
             "<tr><td>Float time</td><td><b>%.1f s</b></td></tr>"
             "<tr><td>AGL at 50ft gate</td><td><b>%.0f ft</b></td></tr>"
             "<tr><td>Pitch at TD</td><td><b>%.2f deg/s</b> &mdash; %s</td></tr>"
             "<tr><td>Pitch rate</td><td><b>%.2f</b></td></tr>"
             "<tr><td>Flare</td><td><b>%s</b></td></tr>"
             "<tr><td>Wind</td><td><b>%s</b></td></tr>"
             "<tr><td>Headwind</td><td>%s</td></tr>"
             "<tr><td>Crosswind</td><td><b>%s</b></td></tr>"
             "<tr><td style=\"color:#666;font-size:.85em\" colspan=\"2\">Profile: %s</td></tr>"
             "</table></div>\n",
             rc.c_str(), esc(ld.rating).c_str(), rc.c_str(), ld.fpm, ld.g_force, ld.float_time, ld.agl_ft, ld.pitch_deg,
             pitch_label.c_str(), ld.pitch_rate, esc(ld.flare).c_str(), w.src.c_str(), w.hw.c_str(), w.xw.c_str(),
             thresh);
    return buf;
}

// ── Report generation ─────────────────────────────────────────────────────────

std::string HtmlReport::generate(const FlightData &fd, const std::string &data_dir, const std::string &json_filename,
                                 const std::string &profile_name, const std::array<int, 4> &thresholds)
{
    // Build JS arrays for map + charts
    std::string js_lats, js_lons, js_alts, js_spds;
    double      clat = 0, clon = 0;
    if (!fd.track.empty())
    {
        for (size_t i = 0; i < fd.track.size(); ++i)
        {
            char b[32];
            if (i)
            {
                js_lats += ',';
                js_lons += ',';
                js_alts += ',';
                js_spds += ',';
            }
            snprintf(b, sizeof(b), "%.6f", fd.track[i].lat);
            js_lats += b;
            snprintf(b, sizeof(b), "%.6f", fd.track[i].lon);
            js_lons += b;
            snprintf(b, sizeof(b), "%d", fd.track[i].alt_ft);
            js_alts += b;
            snprintf(b, sizeof(b), "%d", fd.track[i].spd_kts);
            js_spds += b;
        }
        auto mid = fd.track[fd.track.size() / 2];
        clat     = mid.lat;
        clon     = mid.lon;
    }
    js_lats = "[" + js_lats + "]";
    js_lons = "[" + js_lons + "]";
    js_alts = "[" + js_alts + "]";
    js_spds = "[" + js_spds + "]";

    // Landing cards
    std::string lcards;
    if (fd.landings.empty())
    {
        lcards = "<p style='color:#888'>No landing recorded.</p>";
    }
    else
    {
        for (size_t i = 0; i < fd.landings.size(); ++i)
        {
            if (fd.landings.size() > 1)
            {
                char h[64];
                snprintf(h, sizeof(h), "<h3>Landing %zu</h3>\n", i + 1);
                lcards += h;
            }
            lcards += landing_card(fd.landings[i], profile_name, thresholds);
        }
    }

    char clat_s[32], clon_s[32];
    snprintf(clat_s, sizeof(clat_s), "%.6f", clat);
    snprintf(clon_s, sizeof(clon_s), "%.6f", clon);

    // HTML template
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>" << esc(fd.departure_icao) << " to "
         << esc(fd.arrival_icao) << "</title>"
         << "<link rel=\"stylesheet\" href=\"https://unpkg.com/leaflet@1.9.4/dist/leaflet.css\"/>"
         << "<script src=\"https://unpkg.com/leaflet@1.9.4/dist/leaflet.js\"></script>"
         << "<script src=\"https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js\"></script>" << FL_CSS
         << "</head><body>"
         << "<h1>" << esc(fd.departure_icao) << " &rarr; " << esc(fd.arrival_icao) << "</h1>"
         << "<h2>" << esc(fd.date) << " " << esc(fd.start_utc) << "&ndash;" << esc(fd.end_utc) << " UTC &bull; "
         << esc(fd.aircraft_icao) << " " << esc(fd.aircraft_tail) << "</h2>"
         << "<div class=\"stats\">"
         << "<div class=\"stat\"><div class=\"val\">" << fmt_dur(fd.block_time_min)
         << "</div><div class=\"lbl\">Block Time</div></div>"
         << "<div class=\"stat\"><div class=\"val\">" << fd.max_altitude_ft
         << " ft</div><div class=\"lbl\">Max Altitude</div></div>"
         << "<div class=\"stat\"><div class=\"val\">" << fd.max_speed_kts
         << " kts</div><div class=\"lbl\">Max Speed</div></div>"
         << "<div class=\"stat\"><div class=\"val\">" << fd.track.size()
         << "</div><div class=\"lbl\">Track Points</div></div>"
         << "</div>"
         << "<div id=\"map\"></div>"
         << "<canvas id=\"ac\" height=\"50\"></canvas>"
         << "<canvas id=\"sc\" height=\"40\"></canvas>"
         << "<h3>Landing" << (fd.landings.size() > 1 ? "s" : "") << "</h3>" << lcards
         << "<p style=\"color:#444;font-size:.8em\"><a href=\"../index.html\">&larr; All flights</a></p>"
         << "<script>"
         << "var lats=" << js_lats << ",lons=" << js_lons << ",alts=" << js_alts << ",spds=" << js_spds << ";"
         << "var map=L.map('map');"
         << "L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png',"
         << "{maxZoom:19,attribution:'&copy; OpenStreetMap contributors &copy; CARTO',subdomains:'abcd'}).addTo(map);"
         << "if(lats.length>0){"
         << "var coords=lats.map(function(la,i){return[la,lons[i]];});"
         << "var poly=L.polyline(coords,{color:'#00d4ff',weight:2}).addTo(map);"
         << "L.circleMarker(coords[0],{radius:6,color:'#00ff80',fillOpacity:1}).bindTooltip('Departure').addTo(map);"
         << "L.circleMarker(coords[coords.length-1],{radius:6,color:'#ff4040',fillOpacity:1}).bindTooltip('Arrival')."
            "addTo(map);"
         << "map.fitBounds(poly.getBounds(),{padding:[20,20]});"
         << "}else{map.setView([" << clat_s << "," << clon_s << "],8);}"
         << "var lb=alts.map(function(_,i){return i*10+'s';});"
         << "new Chart(document.getElementById('ac'),{type:'line',data:{labels:lb,datasets:[{label:'Altitude "
            "(ft)',data:alts,borderColor:'#00d4ff',backgroundColor:'rgba(0,212,255,.08)',tension:.3,pointRadius:0,fill:"
            "true}]},options:{plugins:{legend:{labels:{color:'#e0e0e0'}}},scales:{x:{ticks:{color:'#888',maxTicksLimit:"
            "12},grid:{color:'#333'}},y:{ticks:{color:'#888'},grid:{color:'#333'}}}}});"
         << "new Chart(document.getElementById('sc'),{type:'line',data:{labels:lb,datasets:[{label:'IAS "
            "(kts)',data:spds,borderColor:'#ff9900',backgroundColor:'rgba(255,153,0,.08)',tension:.3,pointRadius:0,"
            "fill:true}]},options:{plugins:{legend:{labels:{color:'#e0e0e0'}}},scales:{x:{ticks:{color:'#888',"
            "maxTicksLimit:12},grid:{color:'#333'}},y:{ticks:{color:'#888'},grid:{color:'#333'}}}}});"
         << "</script></body></html>";

    // Write to file
    std::string rdir  = data_dir + "reports/";
    std::string rname = json_filename.substr(0, json_filename.rfind('.')) + ".html";
    std::string rpath = rdir + rname;

    std::ofstream f(rpath);
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

FlightData parse_flight_json(const std::string &content, const std::string &filename)
{
    FlightData fd;
    fd.filename = filename;

    try
    {
        auto j             = json::parse(content);
        fd.date            = j.value("date", "?");
        fd.start_utc       = j.value("start_utc", "");
        fd.end_utc         = j.value("end_utc", "");
        fd.departure_icao  = j.value("departure_icao", "");
        fd.arrival_icao    = j.value("arrival_icao", "");
        fd.aircraft_icao   = j.value("aircraft_icao", "");
        fd.aircraft_tail   = j.value("aircraft_tail", "");
        fd.start_time      = j.value("start_time", (time_t)0);
        fd.end_time        = j.value("end_time", (time_t)0);
        fd.block_time_min  = j.value("block_time_min", 0);
        fd.max_altitude_ft = j.value("max_altitude_ft", 0);
        fd.max_speed_kts   = j.value("max_speed_kts", 0);

        if (j.contains("track") && j["track"].is_array())
        {
            for (auto &tp : j["track"])
            {
                TrackPoint p;
                p.t       = tp.value("t", (time_t)0);
                p.lat     = tp.value("lat", 0.0);
                p.lon     = tp.value("lon", 0.0);
                p.alt_ft  = tp.value("alt", 0);
                p.spd_kts = tp.value("spd", 0);
                p.vs_fpm  = tp.value("vs", 0);
                fd.track.push_back(p);
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
                ld.float_time     = lj.value("float_time", 0.0f);
                ld.time           = lj.value("time", (time_t)0);
                ld.wind_speed_kts = lj.value("wind_speed_kts", 0);
                ld.wind_dir_mag   = lj.value("wind_dir_mag", 0);
                ld.headwind_kts   = lj.value("headwind_kts", 0);
                ld.crosswind_kts  = lj.value("crosswind_kts", 0);
                ld.flare          = lj.value("flare", "");
                ld.rating         = lj.value("rating", "");
                ld.wind_status    = lj.value("wind_status", "STEADY");
                ld.crosswind_side = lj.value("crosswind_side", "");
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
