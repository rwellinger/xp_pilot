#include "logbook_ui.hpp"
#include "auto_qnh.hpp"
#include "flight_logger.hpp"
#include "html_report.hpp"
#include "settings.hpp"
#include <XPLM/XPLMDataAccess.h>
#include <XPLM/XPLMDisplay.h>
#include <XPLM/XPLMGraphics.h>
#include <XPLM/XPLMPlugin.h>
#include <XPLM/XPLMUtilities.h>
#include <backends/imgui_impl_opengl2.h>
#include <imgui.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ════════════════════════════════════════════════════════════════
// State
// ════════════════════════════════════════════════════════════════

// The XPLM window is full-screen and invisible (decoration=None).
// It exists only to capture mouse/keyboard events and feed them to ImGui.
// ImGui draws its own window (with title bar, drag, resize) on top.
static XPLMWindowID  s_wnd          = nullptr;
static ImGuiContext *s_imgui_ctx    = nullptr;
static bool          s_logbook_open = false; // ImGui window open state

// Active flight list (Logbook tab)
static std::vector<FlightData> s_entries;
static std::vector<bool>       s_active_checked; // multi-select state, parallel to s_entries
static int                     s_selected               = -1;
static bool                    s_confirm_del            = false; // single-flight delete in detail
static bool                    s_confirm_archive_single = false; // single-flight archive in detail
static FlightData              s_detail;
static bool                    s_detail_loaded = false;
static std::string             s_report_html;
static bool                    s_report_exists = false;

// Archived flight list (Archive tab)
static std::vector<FlightData> s_arch_entries;
static std::vector<bool>       s_arch_checked;
static int                     s_arch_selected    = -1;
static bool                    s_arch_confirm_del = false; // single-flight delete in detail
static FlightData              s_arch_detail;
static bool                    s_arch_detail_loaded = false;
static std::string             s_arch_report_html;
static bool                    s_arch_report_exists = false;
static bool                    s_arch_loaded        = false; // lazy-load on first Archive tab view

// Batch confirmation flags
static bool s_confirm_batch_archive         = false; // Active tab → batch archive
static bool s_confirm_batch_delete          = false; // Active tab → batch delete
static bool s_confirm_batch_delete_archived = false; // Archive tab → batch delete

// ── Time ──────────────────────────────────────────────────────────────────────

static double s_last_frame_time = 0.0;
static double get_xp_time()
{
    static XPLMDataRef dr = nullptr;
    if (!dr)
        dr = XPLMFindDataRef("sim/time/total_running_time_sec");
    return dr ? (double)XPLMGetDataf(dr) : 0.0;
}

// ════════════════════════════════════════════════════════════════
// Data loading
// ════════════════════════════════════════════════════════════════

// Read every *.json file in `dir` (non-recursive), parse it as a FlightData, drop the track
// samples to keep memory low, and return the list sorted newest-first by filename (filenames
// start with the date).
static std::vector<FlightData> read_flight_summaries(const std::string &dir)
{
    std::vector<FlightData> out;

    std::vector<std::string> fnames;
    std::error_code          ec;
    auto                     dit = std::filesystem::directory_iterator(dir, ec);
    if (ec)
        return out;
    for (auto &entry : dit)
    {
        if (entry.is_regular_file())
        {
            std::string n = entry.path().filename().string();
            if (n.size() > 5 && n.substr(n.size() - 5) == ".json")
                fnames.push_back(n);
        }
    }
    std::sort(fnames.begin(), fnames.end(), std::greater<std::string>());

    for (auto &fname : fnames)
    {
        std::ifstream f(dir + fname);
        if (!f.is_open())
            continue;
        std::string c((std::istreambuf_iterator<char>(f)), {});
        FlightData  fd = parse_flight_json(c, fname);
        fd.track.clear(); // summary only — keep memory low
        out.push_back(std::move(fd));
    }
    return out;
}

static FlightData read_flight_detail(const std::string &dir, const std::string &fname)
{
    std::ifstream f(dir + fname);
    if (!f.is_open())
        return {};
    std::string c((std::istreambuf_iterator<char>(f)), {});
    return parse_flight_json(c, fname);
}

static void load_entries()
{
    s_entries = read_flight_summaries(FlightLogger::data_dir() + "flights/");
    s_active_checked.assign(s_entries.size(), false);
    s_selected               = -1;
    s_confirm_del            = false;
    s_confirm_archive_single = false;
    s_confirm_batch_archive  = false;
    s_confirm_batch_delete   = false;
    s_detail_loaded          = false;
}

static void load_archived_entries()
{
    s_arch_entries = read_flight_summaries(FlightLogger::data_dir() + "flights/archived/");
    s_arch_checked.assign(s_arch_entries.size(), false);
    s_arch_selected                 = -1;
    s_arch_confirm_del              = false;
    s_confirm_batch_delete_archived = false;
    s_arch_detail_loaded            = false;
    s_arch_loaded                   = true;
}

static FlightData load_detail(const std::string &fname)
{
    return read_flight_detail(FlightLogger::data_dir() + "flights/", fname);
}

static FlightData load_archived_detail(const std::string &fname)
{
    return read_flight_detail(FlightLogger::data_dir() + "flights/archived/", fname);
}

// Move JSON + HTML from active to archived. Returns true if JSON move succeeded.
// HTML report move is best-effort (the report file may not exist for older flights).
static bool archive_flight(const std::string &fname)
{
    namespace fs            = std::filesystem;
    const std::string &dd   = FlightLogger::data_dir();
    const std::string  base = fname.substr(0, fname.rfind('.'));

    std::error_code ec;
    fs::rename(dd + "flights/" + fname, dd + "flights/archived/" + fname, ec);
    if (ec)
    {
        XPLMDebugString(("[xp_pilot] archive: cannot move " + fname + ": " + ec.message() + "\n").c_str());
        return false;
    }
    ec.clear();
    fs::rename(dd + "reports/" + base + ".html", dd + "reports/archived/" + base + ".html", ec);
    // ignore HTML move errors — report may not exist
    return true;
}

// Delete the JSON + HTML for one flight. `subdir` is either "" (active) or "archived/".
static void delete_flight_files(const std::string &fname, const std::string &subdir)
{
    const std::string &dd   = FlightLogger::data_dir();
    const std::string  base = fname.substr(0, fname.rfind('.'));
    std::remove((dd + "flights/" + subdir + fname).c_str());
    std::remove((dd + "reports/" + subdir + base + ".html").c_str());
}

static int count_checked(const std::vector<bool> &v)
{
    int n = 0;
    for (bool b : v)
        if (b)
            ++n;
    return n;
}

static void open_path_in_browser(const std::string &path)
{
#if defined(__APPLE__)
    system(("open \"" + path + "\"").c_str()); // NOLINT(bugprone-command-processor)
#elif defined(_WIN32)
    system(("start \"\" \"" + path + "\"").c_str());
#else
    system(("xdg-open \"" + path + "\"").c_str()); // NOLINT(bugprone-command-processor)
#endif
}

// ════════════════════════════════════════════════════════════════
// UI helpers
// ════════════════════════════════════════════════════════════════

static std::string fmt_dur(int min)
{
    char b[32];
    int  h = min / 60, m = min % 60;
    if (h > 0)
        snprintf(b, sizeof(b), "%dh %02dm", h, m);
    else
        snprintf(b, sizeof(b), "%dm", m);
    return b;
}

static ImVec4 rating_color(const std::string &r)
{
    if (r == "BUTTER!")
        return {1.0f, 1.0f, 0.0f, 1.0f};
    if (r == "GREAT LANDING!")
        return {0.25f, 1.0f, 0.25f, 1.0f};
    if (r == "ACCEPTABLE")
        return {0.0f, 0.8f, 0.0f, 1.0f};
    if (r == "HARD LANDING!")
        return {1.0f, 0.5f, 0.0f, 1.0f};
    if (r == "WASTED!")
        return {1.0f, 0.13f, 0.13f, 1.0f};
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

static void draw_wind_status_line(const LandingData &ld)
{
    const int xw = std::abs(ld.crosswind_kts);
    char      line[128];

    switch (wind_condition_from_string(ld.wind_status))
    {
    case WindCondition::Calm:
        ImGui::TextUnformatted("  Wind: CALM");
        return;

    case WindCondition::Light:
        snprintf(line, sizeof(line), "  Wind: LIGHT  XW %d kts %s", xw, ld.crosswind_side.c_str());
        ImGui::TextUnformatted(line);
        return;

    case WindCondition::Steady:
        if (ld.headwind_kts < -5)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.5f, 0, 1));
            snprintf(line, sizeof(line), "  TAILWIND +%d kts  --  WRONG RWY?", std::abs(ld.headwind_kts));
            ImGui::TextUnformatted(line);
            ImGui::PopStyleColor();
        }
        else
        {
            snprintf(line, sizeof(line), "  Wind: %d kts | HW %d kts | XW %d kts %s", ld.wind_speed_kts,
                     ld.headwind_kts, xw, ld.crosswind_side.c_str());
            ImGui::TextUnformatted(line);
        }
        return;
    }
}

// ════════════════════════════════════════════════════════════════
// Logbook window content
// ════════════════════════════════════════════════════════════════

static void draw_settings()
{
    ImGui::Spacing();
    ImGui::TextUnformatted("Feature toggles (saved to settings.json):");
    ImGui::Spacing();

    bool v;

    ImGui::SeparatorText("Flight Log Writer");

    const bool write_on = FlightLogger::write_enabled();
    v                   = write_on;
    if (ImGui::Checkbox("Write flight logs to disk (JSON)", &v))
    {
        FlightLogger::set_write_enabled(v);
        Settings::save();
    }

    ImGui::BeginDisabled(!write_on);
    ImGui::Indent();
    v = FlightLogger::html_report_enabled();
    if (ImGui::Checkbox("Also generate HTML report", &v))
    {
        FlightLogger::set_html_report_enabled(v);
        Settings::save();
    }
    ImGui::Unindent();
    ImGui::EndDisabled();

    v = FlightLogger::messages_enabled();
    if (ImGui::Checkbox("Show flight logger status messages on screen", &v))
    {
        FlightLogger::set_messages_enabled(v);
        Settings::save();
    }

    ImGui::SeparatorText("Landing Rating");

    v = FlightLogger::landing_popup_enabled();
    if (ImGui::Checkbox("Show landing rating popup after touchdown", &v))
    {
        FlightLogger::set_landing_popup_enabled(v);
        Settings::save();
    }

    ImGui::SeparatorText("Auto QNH");

    v = AutoQNH::enabled();
    if (ImGui::Checkbox("Enable Auto QNH (sync pilot/copilot altimeter)", &v))
    {
        AutoQNH::set_enabled(v);
        Settings::save();
    }

    v = AutoQNH::messages_enabled();
    if (ImGui::Checkbox("Show QNH warning messages on screen", &v))
    {
        AutoQNH::set_messages_enabled(v);
        Settings::save();
    }

    int ta = AutoQNH::transition_altitude_ft();
    ImGui::SetNextItemWidth(140.f);
    if (ImGui::InputInt("Transition altitude (ft)", &ta, 500, 1000))
    {
        AutoQNH::set_transition_altitude_ft(ta);
        Settings::save();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Above this altitude, Auto QNH stops syncing and warns to set STD 29.92.\n"
                          "USA: 18000 (fixed). Europe: varies per airport (3000-18000),\n"
                          "see the approach chart for the destination.");
}

// Renders the read-only detail content (route, info, stats, track map, landings) for one flight.
// Action buttons are caller-rendered so each tab can show its own actions.
static void draw_flight_detail_block(const FlightData &fd, float right_w)
{
    std::string route = (fd.departure_icao.empty() ? "?" : fd.departure_icao) + "  ->  " +
                        (fd.arrival_icao.empty() ? "?" : fd.arrival_icao);
    ImGui::TextUnformatted(route.c_str());

    char info[256];
    snprintf(info, sizeof(info), "%s  %s-%s UTC  |  %s  %s", fd.date.c_str(),
             fd.start_utc.empty() ? "?" : fd.start_utc.c_str(), fd.end_utc.empty() ? "?" : fd.end_utc.c_str(),
             fd.aircraft_icao.c_str(), fd.aircraft_tail.c_str());
    ImGui::TextUnformatted(info);
    ImGui::Separator();

    ImGui::TextUnformatted(("Block Time:   " + fmt_dur(fd.block_time_min)).c_str());
    ImGui::TextUnformatted(("Max Alt:      " + std::to_string(fd.max_altitude_ft) + " ft").c_str());
    ImGui::TextUnformatted(("Max Speed:    " + std::to_string(fd.max_speed_kts) + " kts").c_str());
    ImGui::TextUnformatted(("Landings:     " + std::to_string(fd.landings.size())).c_str());
    ImGui::Separator();

    if (fd.track.size() >= 2)
    {
        float cw = right_w - 20.f;
        float ch = std::floor(cw * 0.27f);

        double lat_min = fd.track[0].lat, lat_max = fd.track[0].lat;
        double lon_min = fd.track[0].lon, lon_max = fd.track[0].lon;
        for (auto &p : fd.track)
        {
            lat_min = std::min(lat_min, p.lat);
            lat_max = std::max(lat_max, p.lat);
            lon_min = std::min(lon_min, p.lon);
            lon_max = std::max(lon_max, p.lon);
        }
        double dlat = (lat_max - lat_min) * 0.05 + 0.001;
        double dlon = (lon_max - lon_min) * 0.05 + 0.001;
        lat_min -= dlat;
        lat_max += dlat;
        lon_min -= dlon;
        lon_max += dlon;

        auto to_px = [&](double lat, double lon, float &px, float &py)
        {
            px = (float)((lon - lon_min) / (lon_max - lon_min) * cw);
            py = (float)((1.0 - (lat - lat_min) / (lat_max - lat_min)) * ch);
        };

        ImVec2 org = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(cw, ch));
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(org, ImVec2(org.x + cw, org.y + ch), IM_COL32(46, 26, 26, 255), 4.f);
        for (size_t i = 1; i < fd.track.size(); ++i)
        {
            float x1, y1, x2, y2;
            to_px(fd.track[i - 1].lat, fd.track[i - 1].lon, x1, y1);
            to_px(fd.track[i].lat, fd.track[i].lon, x2, y2);
            dl->AddLine(ImVec2(org.x + x1, org.y + y1), ImVec2(org.x + x2, org.y + y2), IM_COL32(212, 212, 0, 255),
                        1.5f);
        }
        float dx, dy, ax, ay;
        to_px(fd.track.front().lat, fd.track.front().lon, dx, dy);
        to_px(fd.track.back().lat, fd.track.back().lon, ax, ay);
        dl->AddCircleFilled(ImVec2(org.x + dx, org.y + dy), 5.f, IM_COL32(64, 255, 0, 255));
        dl->AddCircleFilled(ImVec2(org.x + ax, org.y + ay), 5.f, IM_COL32(0, 0, 255, 255));
    }
    else
    {
        ImGui::TextUnformatted("(no track data)");
    }
    ImGui::Separator();

    if (fd.landings.empty())
    {
        ImGui::TextUnformatted("(no landing recorded)");
    }
    else
    {
        for (size_t i = 0; i < fd.landings.size(); ++i)
        {
            auto &ld = fd.landings[i];
            if (fd.landings.size() > 1)
            {
                char h[32];
                snprintf(h, sizeof(h), "-- Landing %zu --", i + 1);
                ImGui::TextUnformatted(h);
            }
            ImGui::PushStyleColor(ImGuiCol_Text, rating_color(ld.rating));
            ImGui::TextUnformatted(ld.rating.empty() ? "(no rating)" : ld.rating.c_str());
            ImGui::PopStyleColor();

            if (ld.time > 0)
            {
                char       ts[64];
                struct tm *t = gmtime(&ld.time);
                strftime(ts, sizeof(ts), "  Touchdown: %H:%M:%S UTC", t);
                ImGui::TextUnformatted(ts);
            }
            char stats[160];
            if (ld.bounce_count > 0)
                snprintf(stats, sizeof(stats), "  %.0f fpm  |  %.2f G  |  Float %.1f s  |  %d bounce%s", ld.fpm,
                         ld.g_force, ld.float_time, ld.bounce_count, ld.bounce_count == 1 ? "" : "s");
            else
                snprintf(stats, sizeof(stats), "  %.0f fpm  |  %.2f G  |  Float %.1f s", ld.fpm, ld.g_force,
                         ld.float_time);
            ImGui::TextUnformatted(stats);
            ImGui::TextUnformatted(("  Flare: " + ld.flare).c_str());

            draw_wind_status_line(ld);
            if (i + 1 < fd.landings.size())
                ImGui::Separator();
        }
    }

    ImGui::Separator();
}

static void draw_logbook()
{
    const int n_checked = count_checked(s_active_checked);

    // ── Toolbar ────────────────────────────────────────────────────────────────
    if (ImGui::Button("Refresh"))
    {
        load_entries();
    }
    ImGui::SameLine();
    if (ImGui::Button("Rebuild All Reports"))
    {
        FlightLogger::regen_all_reports();
    }

    if (!s_entries.empty())
    {
        ImGui::SameLine();
        if (ImGui::Button("Select all"))
            s_active_checked.assign(s_entries.size(), true);
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
            s_active_checked.assign(s_entries.size(), false);
    }

    if (n_checked > 0)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (!s_confirm_batch_archive && !s_confirm_batch_delete)
        {
            char btn[48];
            snprintf(btn, sizeof(btn), "Archive selected (%d)", n_checked);
            if (ImGui::Button(btn))
            {
                s_confirm_batch_archive = true;
                s_confirm_batch_delete  = false;
            }
            ImGui::SameLine();
            snprintf(btn, sizeof(btn), "Delete selected (%d)", n_checked);
            if (ImGui::Button(btn))
            {
                s_confirm_batch_delete  = true;
                s_confirm_batch_archive = false;
            }
        }
        else if (s_confirm_batch_archive)
        {
            char q[64];
            snprintf(q, sizeof(q), "Archive %d flights?", n_checked);
            ImGui::TextUnformatted(q);
            ImGui::SameLine();
            if (ImGui::Button("Yes, archive"))
            {
                for (int i = 0; i < (int)s_entries.size(); ++i)
                    if (s_active_checked[i])
                        archive_flight(s_entries[i].filename);
                HtmlReport::generate_index(FlightLogger::data_dir());
                load_entries();
                s_arch_loaded = false; // force reload when Archive tab is opened
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                s_confirm_batch_archive = false;
        }
        else // s_confirm_batch_delete
        {
            char q[64];
            snprintf(q, sizeof(q), "Delete %d flights (JSON + report)?", n_checked);
            ImGui::TextUnformatted(q);
            ImGui::SameLine();
            if (ImGui::Button("Yes, delete"))
            {
                for (int i = 0; i < (int)s_entries.size(); ++i)
                    if (s_active_checked[i])
                        delete_flight_files(s_entries[i].filename, "");
                HtmlReport::generate_index(FlightLogger::data_dir());
                load_entries();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                s_confirm_batch_delete = false;
        }
    }

    ImGui::Separator();

    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetContentRegionAvail().y;
    float panel_h = std::max(100.f, avail_h - 4.f);
    float left_w  = std::floor(avail_w * 0.30f);
    float right_w = std::max(100.f, avail_w - left_w - 10.f);

    // ── Left: flight list ──────────────────────────────────────────────────────
    ImGui::BeginChild("lb_list", ImVec2(left_w, panel_h), true);
    ImGui::TextUnformatted("    Date        Route       Type  Dur");
    ImGui::Separator();
    if (s_entries.empty())
    {
        ImGui::TextUnformatted("No flights found.");
    }
    else
    {
        for (int i = 0; i < (int)s_entries.size(); ++i)
        {
            auto &e = s_entries[i];
            ImGui::PushID(i);

            bool checked = s_active_checked[i];
            if (ImGui::Checkbox("##sel", &checked))
                s_active_checked[i] = checked;
            ImGui::SameLine();

            char line[128];
            snprintf(line, sizeof(line), "%-11s %-9s %-5s %s", e.date.c_str(),
                     (e.departure_icao + "-" + e.arrival_icao).c_str(), e.aircraft_icao.c_str(),
                     fmt_dur(e.block_time_min).c_str());
            bool sel = (s_selected == i);
            if (ImGui::Selectable(line, sel))
            {
                if (s_selected != i)
                {
                    s_selected               = i;
                    s_confirm_del            = false;
                    s_confirm_archive_single = false;
                    s_detail                 = load_detail(e.filename);
                    s_detail_loaded          = true;
                    s_report_html =
                        FlightLogger::data_dir() + "reports/" + e.filename.substr(0, e.filename.rfind('.')) + ".html";
                    s_report_exists = std::ifstream(s_report_html).good();
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ── Right: detail ──────────────────────────────────────────────────────────
    ImGui::BeginChild("lb_detail", ImVec2(right_w, panel_h), true);

    if (!s_detail_loaded || s_selected < 0 || s_selected >= (int)s_entries.size())
    {
        ImGui::TextUnformatted("Select a flight...");
    }
    else
    {
        draw_flight_detail_block(s_detail, right_w);

        if (s_report_exists)
        {
            if (ImGui::Button("Open Report"))
                open_path_in_browser(s_report_html);
            ImGui::SameLine();
        }

        // Single-flight Archive
        if (!s_confirm_archive_single)
        {
            if (ImGui::Button("Archive"))
            {
                s_confirm_archive_single = true;
                s_confirm_del            = false;
            }
        }
        else
        {
            ImGui::TextUnformatted("Archive this flight?");
            if (ImGui::Button("Yes, archive"))
            {
                auto &fn = s_entries[s_selected].filename;
                archive_flight(fn);
                HtmlReport::generate_index(FlightLogger::data_dir());
                load_entries();
                s_arch_loaded = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel##arch"))
                s_confirm_archive_single = false;
        }
        ImGui::SameLine();

        // Single-flight Delete
        if (!s_confirm_del)
        {
            if (ImGui::Button("Delete"))
            {
                s_confirm_del            = true;
                s_confirm_archive_single = false;
            }
        }
        else
        {
            ImGui::TextUnformatted("Really delete?");
            if (ImGui::Button("Yes, delete"))
            {
                auto &fn = s_entries[s_selected].filename;
                delete_flight_files(fn, "");
                HtmlReport::generate_index(FlightLogger::data_dir());
                load_entries();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel##del"))
                s_confirm_del = false;
        }
    }
    ImGui::EndChild();
}

static void draw_archive()
{
    if (!s_arch_loaded)
        load_archived_entries();

    const int n_checked = count_checked(s_arch_checked);

    // ── Toolbar ────────────────────────────────────────────────────────────────
    if (ImGui::Button("Refresh"))
        load_archived_entries();

    if (!s_arch_entries.empty())
    {
        ImGui::SameLine();
        if (ImGui::Button("Select all"))
            s_arch_checked.assign(s_arch_entries.size(), true);
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
            s_arch_checked.assign(s_arch_entries.size(), false);
    }

    if (n_checked > 0)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (!s_confirm_batch_delete_archived)
        {
            char btn[48];
            snprintf(btn, sizeof(btn), "Delete selected (%d)", n_checked);
            if (ImGui::Button(btn))
                s_confirm_batch_delete_archived = true;
        }
        else
        {
            char q[64];
            snprintf(q, sizeof(q), "Delete %d archived flights (JSON + report)?", n_checked);
            ImGui::TextUnformatted(q);
            ImGui::SameLine();
            if (ImGui::Button("Yes, delete"))
            {
                for (int i = 0; i < (int)s_arch_entries.size(); ++i)
                    if (s_arch_checked[i])
                        delete_flight_files(s_arch_entries[i].filename, "archived/");
                load_archived_entries();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                s_confirm_batch_delete_archived = false;
        }
    }

    ImGui::Separator();

    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetContentRegionAvail().y;
    float panel_h = std::max(100.f, avail_h - 4.f);
    float left_w  = std::floor(avail_w * 0.30f);
    float right_w = std::max(100.f, avail_w - left_w - 10.f);

    // ── Left: archived flight list ─────────────────────────────────────────────
    ImGui::BeginChild("arch_list", ImVec2(left_w, panel_h), true);
    ImGui::TextUnformatted("    Date        Route       Type  Dur");
    ImGui::Separator();
    if (s_arch_entries.empty())
    {
        ImGui::TextUnformatted("No archived flights.");
    }
    else
    {
        for (int i = 0; i < (int)s_arch_entries.size(); ++i)
        {
            auto &e = s_arch_entries[i];
            ImGui::PushID(i);

            bool checked = s_arch_checked[i];
            if (ImGui::Checkbox("##sel", &checked))
                s_arch_checked[i] = checked;
            ImGui::SameLine();

            char line[128];
            snprintf(line, sizeof(line), "%-11s %-9s %-5s %s", e.date.c_str(),
                     (e.departure_icao + "-" + e.arrival_icao).c_str(), e.aircraft_icao.c_str(),
                     fmt_dur(e.block_time_min).c_str());
            bool sel = (s_arch_selected == i);
            if (ImGui::Selectable(line, sel))
            {
                if (s_arch_selected != i)
                {
                    s_arch_selected      = i;
                    s_arch_confirm_del   = false;
                    s_arch_detail        = load_archived_detail(e.filename);
                    s_arch_detail_loaded = true;
                    s_arch_report_html   = FlightLogger::data_dir() + "reports/archived/" +
                                           e.filename.substr(0, e.filename.rfind('.')) + ".html";
                    s_arch_report_exists = std::ifstream(s_arch_report_html).good();
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ── Right: detail ──────────────────────────────────────────────────────────
    ImGui::BeginChild("arch_detail", ImVec2(right_w, panel_h), true);

    if (!s_arch_detail_loaded || s_arch_selected < 0 || s_arch_selected >= (int)s_arch_entries.size())
    {
        ImGui::TextUnformatted("Select an archived flight...");
    }
    else
    {
        draw_flight_detail_block(s_arch_detail, right_w);

        if (s_arch_report_exists)
        {
            if (ImGui::Button("Open Report"))
                open_path_in_browser(s_arch_report_html);
            ImGui::SameLine();
        }

        if (!s_arch_confirm_del)
        {
            if (ImGui::Button("Delete"))
                s_arch_confirm_del = true;
        }
        else
        {
            ImGui::TextUnformatted("Really delete?");
            if (ImGui::Button("Yes, delete"))
            {
                auto &fn = s_arch_entries[s_arch_selected].filename;
                delete_flight_files(fn, "archived/");
                load_archived_entries();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel##del"))
                s_arch_confirm_del = false;
        }
    }
    ImGui::EndChild();
}

// ════════════════════════════════════════════════════════════════
// XPLM window callbacks
// The XPLM window is full-screen, decoration=None.
// It acts as an invisible mouse/keyboard capture layer.
// ImGui draws on top with its own window chrome.
// ════════════════════════════════════════════════════════════════

// Minimal XPLM window draw callback — input capture only, rendering is in LogbookUI::draw()
static void DrawCallback(XPLMWindowID, void *)
{
    if (FlightLogger::lb_needs_refresh())
    {
        FlightLogger::lb_needs_refresh() = false;
        load_entries();
    }
}

static int  s_mouse_dbg_count = 0; // DEBUG: remove after Linux input is confirmed working
static bool s_fb_logged       = false;

static int MouseCallback(XPLMWindowID wnd, int x, int y, XPLMMouseStatus status, void *)
{
    int left, top, right, bottom;
    XPLMGetWindowGeometry(wnd, &left, &top, &right, &bottom);
    ImGuiIO &io = ImGui::GetIO();

    float mx = (float)(x - left);
    float my = (float)(top - y);

    // DEBUG: log first few mouse events so we can verify delivery on Linux
    if (s_mouse_dbg_count < 20)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "[xp_pilot] MouseCB: raw(%d,%d) wnd(%d,%d,%d,%d) imgui(%.0f,%.0f) status=%d\n", x, y,
                 left, top, right, bottom, mx, my, status);
        XPLMDebugString(buf);
        ++s_mouse_dbg_count;
    }

    io.AddMousePosEvent(mx, my);
    if (status == xplm_MouseDown)
        io.AddMouseButtonEvent(0, true);
    if (status == xplm_MouseUp)
        io.AddMouseButtonEvent(0, false);
    return 1; // consume event
}

static int ScrollCallback(XPLMWindowID wnd, int x, int y, int, int clicks, void *)
{
    int left, top, right, bottom;
    XPLMGetWindowGeometry(wnd, &left, &top, &right, &bottom);
    ImGui::GetIO().AddMousePosEvent((float)(x - left), (float)(top - y));
    ImGui::GetIO().AddMouseWheelEvent(0.f, (float)clicks);
    return 1;
}

static int RightClickCallback(XPLMWindowID wnd, int x, int y, XPLMMouseStatus status, void *)
{
    int left, top, right, bottom;
    XPLMGetWindowGeometry(wnd, &left, &top, &right, &bottom);
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((float)(x - left), (float)(top - y));
    if (status == xplm_MouseDown)
        io.AddMouseButtonEvent(1, true);
    if (status == xplm_MouseUp)
        io.AddMouseButtonEvent(1, false);
    return 1;
}

static XPLMCursorStatus CursorCallback(XPLMWindowID wnd, int x, int y, void *)
{
    int left, top, right, bottom;
    XPLMGetWindowGeometry(wnd, &left, &top, &right, &bottom);
    ImGui::GetIO().AddMousePosEvent((float)(x - left), (float)(top - y));
    return xplm_CursorDefault;
}

static void KeyCallback(XPLMWindowID, char key, XPLMKeyFlags flags, char vkey, void *, int losing_focus)
{
    if (losing_focus)
        return;
    ImGuiIO &io = ImGui::GetIO();
    // Printable ASCII
    if (!(flags & xplm_DownFlag))
        return;
    if (key >= 32 && key < 127)
        io.AddInputCharacter((unsigned)key);
    // Basic special keys
    if (vkey == XPLM_VK_BACK)
        io.AddKeyEvent(ImGuiKey_Backspace, true);
    if (vkey == XPLM_VK_DELETE)
        io.AddKeyEvent(ImGuiKey_Delete, true);
    if (vkey == XPLM_VK_RETURN)
        io.AddKeyEvent(ImGuiKey_Enter, true);
    if (vkey == XPLM_VK_ESCAPE)
    {
        s_logbook_open = false;
        if (s_wnd)
            XPLMSetWindowIsVisible(s_wnd, 0);
    }
}

// ════════════════════════════════════════════════════════════════
// Public lifecycle
// ════════════════════════════════════════════════════════════════

void LogbookUI::init()
{
    IMGUI_CHECKVERSION();
    s_imgui_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(s_imgui_ctx);

    ImGuiIO &io    = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImGui::StyleColorsDark();
    auto &style          = ImGui::GetStyle();
    style.WindowRounding = 6.f;
    style.FrameRounding  = 3.f;
    style.WindowPadding  = ImVec2(8, 6);

    ImGui_ImplOpenGL2_Init();
    s_last_frame_time = get_xp_time();
}

void LogbookUI::draw()
{
    // Called every frame from main draw callback (xplm_Phase_Window)
    if (!s_logbook_open && !FlightLogger::lb_needs_refresh() && !FlightLogger::popup_active())
        return;

    if (FlightLogger::lb_needs_refresh())
    {
        FlightLogger::lb_needs_refresh() = false;
        load_entries();
    }

    if (!s_logbook_open && !FlightLogger::popup_active())
        return;

    int gl, gt, gr, gb;
    XPLMGetScreenBoundsGlobal(&gl, &gt, &gr, &gb);
    int sw = gr - gl;
    int sh = gt - gb;
    if (sw <= 0 || sh <= 0)
        return;

    // Keep the invisible capture window sized to the current screen (only when logbook is open)
    if (s_logbook_open && s_wnd)
    {
        int wl, wt, wr, wb;
        XPLMGetWindowGeometry(s_wnd, &wl, &wt, &wr, &wb);
        if (wl != gl || wb != gb || wr != gr || wt != gt)
            XPLMSetWindowGeometry(s_wnd, gl, gt, gr, gb);
    }

    // Save GL state
    GLint prev_viewport[4];
    glGetIntegerv(GL_VIEWPORT, prev_viewport);
    glPushAttrib(GL_TRANSFORM_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_SCISSOR_BIT |
                 GL_TEXTURE_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Use X-Plane's actual viewport (framebuffer) dimensions — these may differ
    // from logical screen dimensions on Linux (Vulkan-to-OpenGL compat layer)
    int fb_w = prev_viewport[2];
    int fb_h = prev_viewport[3];

    if (!s_fb_logged)
    {
        char dbg[192];
        snprintf(dbg, sizeof(dbg), "[xp_pilot] Framebuffer: viewport(%d,%d) logical(%d,%d) scale(%.2f,%.2f)\n", fb_w,
                 fb_h, sw, sh, (float)fb_w / (float)sw, (float)fb_h / (float)sh);
        XPLMDebugString(dbg);
        s_fb_logged = true;
    }

    glViewport(0, 0, fb_w, fb_h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, sw, sh, 0, -1, 1); // top-left origin for ImGui (logical coords)
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    ImGuiIO &io                = ImGui::GetIO();
    double   now               = get_xp_time();
    io.DeltaTime               = (float)std::max(now - s_last_frame_time, 0.001);
    s_last_frame_time          = now;
    io.DisplaySize             = ImVec2((float)sw, (float)sh);
    io.DisplayFramebufferScale = ImVec2((float)fb_w / (float)sw, (float)fb_h / (float)sh);

    ImGui_ImplOpenGL2_NewFrame();
    ImGui::NewFrame();

    FlightLogger::draw_popup();

    if (s_logbook_open)
    {
        float win_w = 1020.f, win_h = 700.f;
        ImGui::SetNextWindowPos(ImVec2(((float)sw - win_w) * 0.5f, ((float)sh - win_h) * 0.5f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(win_w, win_h), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(600, 300), ImVec2(3840, 2160));

        bool open = true;
#ifdef XP_PILOT_VERSION
        static const std::string window_title = std::string("Logbook v") + XP_PILOT_VERSION + "##xp_pilot";
#else
        static const std::string window_title = "Logbook##xp_pilot";
#endif
        if (ImGui::Begin(window_title.c_str(), &open, ImGuiWindowFlags_NoCollapse))
        {
            if (ImGui::BeginTabBar("lb_tabs"))
            {
                if (ImGui::BeginTabItem("Logbook"))
                {
                    draw_logbook();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Archive"))
                {
                    draw_archive();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Settings"))
                {
                    draw_settings();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
        s_logbook_open = open;

        if (!s_logbook_open && s_wnd)
            XPLMSetWindowIsVisible(s_wnd, 0);
    }

    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

    // Restore GL state
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
}

void LogbookUI::stop()
{
    if (s_wnd)
    {
        XPLMDestroyWindow(s_wnd);
        s_wnd = nullptr;
    }
    ImGui_ImplOpenGL2_Shutdown();
    if (s_imgui_ctx)
    {
        ImGui::DestroyContext(s_imgui_ctx);
        s_imgui_ctx = nullptr;
    }
}

void LogbookUI::open()
{
    s_logbook_open = true;

    if (!s_wnd)
    {
        // Full-screen invisible capture window — mouse/keyboard only
        int gl, gt, gr, gb;
        XPLMGetScreenBoundsGlobal(&gl, &gt, &gr, &gb);
        char dbg[128];
        snprintf(dbg, sizeof(dbg), "[xp_pilot] Screen bounds: global(%d,%d,%d,%d) size(%dx%d)\n", gl, gt, gr, gb,
                 gr - gl, gt - gb);
        XPLMDebugString(dbg);
        XPLMCreateWindow_t p       = {};
        p.structSize               = sizeof(p);
        p.left                     = gl;
        p.bottom                   = gb;
        p.right                    = gr;
        p.top                      = gt;
        p.visible                  = 1;
        p.drawWindowFunc           = DrawCallback;
        p.handleMouseClickFunc     = MouseCallback;
        p.handleKeyFunc            = KeyCallback;
        p.handleCursorFunc         = CursorCallback;
        p.handleMouseWheelFunc     = ScrollCallback;
        p.refcon                   = nullptr;
        p.decorateAsFloatingWindow = xplm_WindowDecorationNone; // no chrome
        p.layer                    = xplm_WindowLayerFloatingWindows;
        p.handleRightClickFunc     = RightClickCallback;
        s_wnd                      = XPLMCreateWindowEx(&p);
    }

    XPLMSetWindowIsVisible(s_wnd, 1);
    XPLMBringWindowToFront(s_wnd);
    s_fb_logged       = false;
    s_mouse_dbg_count = 0;
    load_entries();
}

void LogbookUI::toggle()
{
    if (s_logbook_open)
    {
        s_logbook_open = false;
        if (s_wnd)
            XPLMSetWindowIsVisible(s_wnd, 0);
    }
    else
    {
        open();
    }
}
