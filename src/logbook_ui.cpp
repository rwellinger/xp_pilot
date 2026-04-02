#include "logbook_ui.hpp"
#include "flight_logger.hpp"
#include "html_report.hpp"
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

static std::vector<FlightData> s_entries;
static int                     s_selected    = -1;
static bool                    s_confirm_del = false;
static FlightData              s_detail;
static bool                    s_detail_loaded = false;
static std::string             s_report_html;
static bool                    s_report_exists = false;

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

static void load_entries()
{
    s_entries.clear();
    s_selected      = -1;
    s_confirm_del   = false;
    s_detail_loaded = false;

    const std::string        fdir = FlightLogger::data_dir() + "flights/";
    std::vector<std::string> fnames;
    std::error_code          ec;
    auto                     dit = std::filesystem::directory_iterator(fdir, ec);
    if (ec)
        return;
    for (auto &entry : dit)
    {
        if (entry.is_regular_file())
        {
            std::string n = entry.path().filename().string();
            if (n.size() > 5 && n.substr(n.size() - 5) == ".json")
                fnames.push_back(n);
        }
    }
    // Sort descending (newest first) — filenames start with date
    std::sort(fnames.begin(), fnames.end(), std::greater<std::string>());

    for (auto &fname : fnames)
    {
        std::string   path = fdir + fname;
        std::ifstream f(path);
        if (!f.is_open())
            continue;
        std::string c((std::istreambuf_iterator<char>(f)), {});
        FlightData  fd = parse_flight_json(c, fname);
        fd.track.clear(); // keep summary only — save memory
        s_entries.push_back(std::move(fd));
    }
}

static FlightData load_detail(const std::string &fname)
{
    std::string   path = FlightLogger::data_dir() + "flights/" + fname;
    std::ifstream f(path);
    if (!f.is_open())
        return {};
    std::string c((std::istreambuf_iterator<char>(f)), {});
    return parse_flight_json(c, fname);
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

// ════════════════════════════════════════════════════════════════
// Logbook window content
// ════════════════════════════════════════════════════════════════

static void draw_logbook()
{
    // Toolbar
    if (ImGui::Button("Refresh"))
    {
        load_entries();
    }
    ImGui::SameLine();
    if (ImGui::Button("Rebuild All Reports"))
    {
        FlightLogger::regen_all_reports();
    }
    ImGui::Separator();

    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetContentRegionAvail().y;
    float panel_h = std::max(100.f, avail_h - 4.f);
    float left_w  = std::floor(avail_w * 0.30f);
    float right_w = std::max(100.f, avail_w - left_w - 10.f);

    // ── Left: flight list ──────────────────────────────────────────────────────
    ImGui::BeginChild("lb_list", ImVec2(left_w, panel_h), true);
    ImGui::TextUnformatted("Date        Route       Type  Dur");
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
            char  line[128];
            snprintf(line, sizeof(line), "%-11s %-9s %-5s %s", e.date.c_str(),
                     (e.departure_icao + "-" + e.arrival_icao).c_str(), e.aircraft_icao.c_str(),
                     fmt_dur(e.block_time_min).c_str());
            bool sel = (s_selected == i);
            if (ImGui::Selectable(line, sel))
            {
                if (s_selected != i)
                {
                    s_selected      = i;
                    s_confirm_del   = false;
                    s_detail        = load_detail(e.filename);
                    s_detail_loaded = true;
                    s_report_html =
                        FlightLogger::data_dir() + "reports/" + e.filename.substr(0, e.filename.rfind('.')) + ".html";
                    s_report_exists = std::ifstream(s_report_html).good();
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ── Right: detail ──────────────────────────────────────────────────────────
    ImGui::BeginChild("lb_detail", ImVec2(right_w, panel_h), true);

    if (!s_detail_loaded || s_selected < 0)
    {
        ImGui::TextUnformatted("Select a flight...");
    }
    else
    {
        auto &fd = s_detail;

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

        // Mini track map
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

        // Landings
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
                char stats[128];
                snprintf(stats, sizeof(stats), "  %.0f fpm  |  %.2f G  |  Float %.1f s", ld.fpm, ld.g_force,
                         ld.float_time);
                ImGui::TextUnformatted(stats);
                ImGui::TextUnformatted(("  Flare: " + ld.flare).c_str());

                int  xw = std::abs(ld.crosswind_kts);
                char wind[128];
                if (ld.wind_status == "CALM")
                {
                    ImGui::TextUnformatted("  Wind: CALM");
                }
                else if (ld.wind_status == "LIGHT")
                {
                    snprintf(wind, sizeof(wind), "  Wind: LIGHT  XW %d kts %s", xw, ld.crosswind_side.c_str());
                    ImGui::TextUnformatted(wind);
                }
                else if (ld.headwind_kts < -5)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.5f, 0, 1));
                    snprintf(wind, sizeof(wind), "  TAILWIND +%d kts  --  WRONG RWY?", std::abs(ld.headwind_kts));
                    ImGui::TextUnformatted(wind);
                    ImGui::PopStyleColor();
                }
                else
                {
                    snprintf(wind, sizeof(wind), "  Wind: %d kts | HW %d kts | XW %d kts %s", ld.wind_speed_kts,
                             ld.headwind_kts, xw, ld.crosswind_side.c_str());
                    ImGui::TextUnformatted(wind);
                }
                if (i + 1 < fd.landings.size())
                    ImGui::Separator();
            }
        }

        ImGui::Separator();

        // Open report button
        if (s_report_exists)
        {
            if (ImGui::Button("Open Report"))
            {
#if defined(__APPLE__)
                system(("open \"" + s_report_html + "\"").c_str()); // NOLINT(bugprone-command-processor)
#elif defined(_WIN32)
                system(("start \"\" \"" + s_report_html + "\"").c_str());
#endif
            }
            ImGui::SameLine();
        }

        // Delete
        if (!s_confirm_del)
        {
            if (ImGui::Button("Delete"))
                s_confirm_del = true;
        }
        else
        {
            ImGui::TextUnformatted("Really delete?");
            if (ImGui::Button("Yes, delete"))
            {
                if (s_selected >= 0 && s_selected < (int)s_entries.size())
                {
                    auto &fn = s_entries[s_selected].filename;
                    std::remove((FlightLogger::data_dir() + "flights/" + fn).c_str());
                    std::remove(
                        (FlightLogger::data_dir() + "reports/" + fn.substr(0, fn.rfind('.')) + ".html").c_str());
                    HtmlReport::generate_index(FlightLogger::data_dir());
                }
                load_entries();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                s_confirm_del = false;
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

static int MouseCallback(XPLMWindowID wnd, int x, int y, XPLMMouseStatus status, void *)
{
    int left, top, right, bottom;
    XPLMGetWindowGeometry(wnd, &left, &top, &right, &bottom);
    ImGuiIO &io = ImGui::GetIO();
    // XPLM: y increases upward from bottom. ImGui: y increases downward from top.
    io.MousePos = ImVec2((float)(x - left), (float)(top - y));
    if (status == xplm_MouseDown)
        io.MouseDown[0] = true;
    if (status == xplm_MouseUp)
        io.MouseDown[0] = false;
    return 1; // consume event
}

static int ScrollCallback(XPLMWindowID wnd, int x, int y, int, int clicks, void *)
{
    int left, top, right, bottom;
    XPLMGetWindowGeometry(wnd, &left, &top, &right, &bottom);
    ImGui::GetIO().MousePos = ImVec2((float)(x - left), (float)(top - y));
    ImGui::GetIO().MouseWheel += (float)clicks;
    return 1;
}

static XPLMCursorStatus CursorCallback(XPLMWindowID, int, int, void *) { return xplm_CursorDefault; }

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
    if (!s_logbook_open && !FlightLogger::lb_needs_refresh())
        return;

    if (FlightLogger::lb_needs_refresh())
    {
        FlightLogger::lb_needs_refresh() = false;
        load_entries();
    }

    if (!s_logbook_open)
        return;

    int sw = 0, sh = 0;
    XPLMGetScreenSize(&sw, &sh);
    if (sw <= 0 || sh <= 0)
        return;

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

    glViewport(0, 0, sw, sh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, sw, sh, 0, -1, 1); // top-left origin for ImGui
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    ImGuiIO &io       = ImGui::GetIO();
    double   now      = get_xp_time();
    io.DeltaTime      = (float)std::max(now - s_last_frame_time, 0.001);
    s_last_frame_time = now;
    io.DisplaySize    = ImVec2((float)sw, (float)sh);

    ImGui_ImplOpenGL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(1020, 700), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(600, 300), ImVec2(3840, 2160));

    bool open = s_logbook_open;
    if (ImGui::Begin("Logbook##xp_pilot", &open))
    {
        draw_logbook();
    }
    ImGui::End();
    s_logbook_open = open;

    if (!s_logbook_open && s_wnd)
        XPLMSetWindowIsVisible(s_wnd, 0);

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
        int sw = 0, sh = 0;
        XPLMGetScreenSize(&sw, &sh);
        XPLMCreateWindow_t p       = {};
        p.structSize               = sizeof(p);
        p.left                     = 0;
        p.bottom                   = 0;
        p.right                    = sw;
        p.top                      = sh;
        p.visible                  = 1;
        p.drawWindowFunc           = DrawCallback;
        p.handleMouseClickFunc     = MouseCallback;
        p.handleKeyFunc            = KeyCallback;
        p.handleCursorFunc         = CursorCallback;
        p.handleMouseWheelFunc     = ScrollCallback;
        p.refcon                   = nullptr;
        p.decorateAsFloatingWindow = xplm_WindowDecorationNone; // no chrome
        p.layer                    = xplm_WindowLayerFloatingWindows;
        p.handleRightClickFunc     = nullptr;
        s_wnd                      = XPLMCreateWindowEx(&p);
    }

    XPLMSetWindowIsVisible(s_wnd, 1);
    XPLMBringWindowToFront(s_wnd);
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
