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

#include "logbook_ui.hpp"
#include "auto_qnh.hpp"
#include "flight_logger.hpp"
#include "html_report.hpp"
#include "settings.hpp"
#include "ui_flight_list.hpp"
#include "ui_flight_view.hpp"
#include "ui_home.hpp"
#include "ui_theme.hpp"
#include "ui_widgets.hpp"
#include <XPLM/XPLMDataAccess.h>
#include <XPLM/XPLMDisplay.h>
#include <XPLM/XPLMGraphics.h>
#include <XPLM/XPLMUtilities.h>
#include <backends/imgui_impl_opengl2.h>
#include <imgui.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <algorithm>
// Keep explicit: MSVC needs it; Clang often pulls it in transitively and flags it unused.
#include <cstdio> // snprintf
#include <string>

using Home::Screen;

// ════════════════════════════════════════════════════════════════
// State
// ════════════════════════════════════════════════════════════════

// The XPLM window is full-screen and invisible (decoration=None).
// It exists only to capture mouse/keyboard events and feed them to ImGui.
// ImGui draws its own window (with title bar, drag, resize) on top.
static XPLMWindowID  s_wnd          = nullptr;
static ImGuiContext *s_imgui_ctx    = nullptr;
static bool          s_logbook_open = false; // ImGui window open state
static bool          s_reset_window_layout = false; // recentre and resize on the next frame

static Screen s_screen = Screen::Home;

static FlightListScreen::FlightList make_list(const char *subdir, bool allow_archive)
{
    FlightListScreen::FlightList list;
    list.subdir        = subdir;
    list.allow_archive = allow_archive;
    return list;
}

static FlightListScreen::FlightList s_logbook = make_list("", true);
static FlightListScreen::FlightList s_archive = make_list("archived/", false);

// Shown on the archive tile. Counting files is cheap, parsing them is not — the
// archive itself stays lazily loaded until its screen is opened.
static size_t s_archive_count = 0;

// ── Time ──────────────────────────────────────────────────────────────────────

static double s_last_frame_time = 0.0;
static double get_xp_time()
{
    static XPLMDataRef dr = nullptr;
    if (!dr)
        dr = XPLMFindDataRef("sim/time/total_running_time_sec");
    return dr ? static_cast<double>(XPLMGetDataf(dr)) : 0.0;
}

// ════════════════════════════════════════════════════════════════
// Screens
// ════════════════════════════════════════════════════════════════

// Live view of the flight currently being recorded. The instantaneous figures live
// in the status bar; this screen adds what needs room: track, times and landings.
static void draw_live_screen(const FlightLogger::LiveFlight &live)
{
    if (!live.in_progress)
    {
        ImGui::Spacing();
        Ui::text_dim("No flight in progress.");
        Ui::text_dim("Recording starts on the takeoff roll and ends after engine shutdown.");
        return;
    }

    const FlightData &flight = live.flight;

    char header[160];
    snprintf(header, sizeof(header), "%s  off blocks %s UTC  |  %s  %s%s", flight.date.c_str(),
             flight.start_utc.empty() ? "?" : flight.start_utc.c_str(), flight.aircraft_icao.c_str(),
             flight.aircraft_tail.c_str(), flight.aircraft_category == "rotorcraft" ? "  [Heli]" : "");
    Ui::text_dim(header);

    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_MAP "  SkyVector"))
        FlightView::open_in_browser(FlightView::skyvector_url(live.latitude, live.longitude));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show this position on skyvector.com");
    ImGui::Separator();

    FlightView::draw_time_lines(flight);

    const float  cell_w = ImGui::GetContentRegionAvail().x / 4.f;
    const ImVec2 row    = Ui::begin_metric_row();
    Ui::metric_cell("AGL", (std::to_string(static_cast<int>(live.agl_ft)) + " ft").c_str(), Theme::text, cell_w);
    Ui::metric_cell("MAX ALT", (std::to_string(flight.max_altitude_ft) + " ft").c_str(), Theme::text, cell_w);
    Ui::metric_cell("MAX SPEED", (std::to_string(flight.max_speed_kts) + " kts").c_str(), Theme::text, cell_w);
    Ui::metric_cell("LANDINGS", std::to_string(flight.landings.size()).c_str(), Theme::text, cell_w);
    Ui::end_metric_row(row);
    ImGui::Separator();

    // The track is only sampled while log writing is on, so an empty map here usually
    // means the setting is off rather than that nothing has happened yet.
    if (!FlightLogger::write_enabled())
        Ui::text_dim("Track recording is off - enable \"Write flight logs\" in Settings.");
    else
        FlightView::draw_track_map(flight, ImGui::GetContentRegionAvail().x);
    ImGui::Separator();

    FlightView::draw_landings(flight);
}

// The report map falls back to OpenFreeMap, which needs no key. A MapTiler key is
// optional and only swaps the styling.
static void draw_maptiler_key_field()
{
    static char key_buffer[128] = {};
    static bool buffer_filled   = false;
    if (!buffer_filled)
    {
        snprintf(key_buffer, sizeof(key_buffer), "%s", HtmlReport::maptiler_key().c_str());
        buffer_filled = true;
    }

    ImGui::SetNextItemWidth(Theme::scaled(240.f));
    if (ImGui::InputText("MapTiler API key", key_buffer, sizeof(key_buffer)))
    {
        HtmlReport::set_maptiler_key(key_buffer);
        Settings::save();
    }
    Ui::help_marker("Optional. Without a key the map uses OpenFreeMap.\n"
                    "A key only changes the map styling.\n"
                    "Reports already written keep their map until you press\n"
                    "\"Rebuild All Reports\" in the flight list.");
}

static void draw_settings_screen()
{
    Ui::section_header(ICON_FA_FILE_LINES, "Flight Log Writer");

    bool       value;
    const bool write_on = FlightLogger::write_enabled();

    value = write_on;
    if (ImGui::Checkbox("Write flight logs to disk (JSON)", &value))
    {
        FlightLogger::set_write_enabled(value);
        Settings::save();
    }

    ImGui::BeginDisabled(!write_on);
    ImGui::Indent();
    value = FlightLogger::html_report_enabled();
    if (ImGui::Checkbox("Also generate HTML report", &value))
    {
        FlightLogger::set_html_report_enabled(value);
        Settings::save();
    }
    draw_maptiler_key_field();
    ImGui::Unindent();
    ImGui::EndDisabled();

    value = FlightLogger::messages_enabled();
    if (ImGui::Checkbox("Show flight logger status messages on screen", &value))
    {
        FlightLogger::set_messages_enabled(value);
        Settings::save();
    }

    Ui::section_header(ICON_FA_PLANE_DEP, "Landing Rating");

    value = FlightLogger::landing_popup_enabled();
    if (ImGui::Checkbox("Show landing rating popup after touchdown", &value))
    {
        FlightLogger::set_landing_popup_enabled(value);
        Settings::save();
    }

    int position = static_cast<int>(FlightLogger::popup_position());
    ImGui::SetNextItemWidth(Theme::scaled(180.f));
    if (ImGui::Combo("Popup position", &position, popup_position_labels().data(),
                     static_cast<int>(popup_position_labels().size())))
    {
        FlightLogger::set_popup_position(static_cast<PopupPosition>(position));
        Settings::save();
    }

    if (ImGui::Button("Show last landing popup"))
        FlightLogger::replay_last_landing_popup();
    Ui::help_marker("Also available as the command \"xp_pilot/logbook/show_last_landing\",\n"
                    "which can be bound to a key in X-Plane's keyboard settings.");

    value = FlightLogger::runway_analysis_enabled();
    if (ImGui::Checkbox("Analyze touchdown point and centerline deviation", &value))
    {
        FlightLogger::set_runway_analysis_enabled(value);
        Settings::save();
    }
    Ui::help_marker("Locates each touchdown on the runway it was made on.\n"
                    "Reads X-Plane's global airport database once per flight,\n"
                    "after the engines are shut down.");

    Ui::section_header(ICON_FA_STOPWATCH, "Auto QNH");

    value = AutoQNH::enabled();
    if (ImGui::Checkbox("Enable Auto QNH (sync pilot/copilot altimeter)", &value))
    {
        AutoQNH::set_enabled(value);
        Settings::save();
    }

    value = AutoQNH::messages_enabled();
    if (ImGui::Checkbox("Show QNH warning messages on screen", &value))
    {
        AutoQNH::set_messages_enabled(value);
        Settings::save();
    }

    int transition_altitude = AutoQNH::transition_altitude_ft();
    ImGui::SetNextItemWidth(Theme::scaled(140.f));
    if (ImGui::InputInt("Transition altitude (ft)", &transition_altitude, 500, 1000))
    {
        AutoQNH::set_transition_altitude_ft(transition_altitude);
        Settings::save();
    }
    Ui::help_marker("Above this altitude, Auto QNH stops syncing and warns to set STD 29.92.\n"
                    "USA: 18000 (fixed). Europe: varies per airport (3000-18000),\n"
                    "see the approach chart for the destination.");

    Ui::section_header(ICON_FA_GEAR, "Appearance");

    // Stepped rather than dragged: the scale applies live, so a slider slides out from
    // under the cursor mid-drag and overshooting to the maximum is far too easy.
    const float scale       = Theme::ui_scale();
    const float step_button = Theme::scaled(30.f);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("UI scale");
    ImGui::SameLine();

    ImGui::BeginDisabled(scale <= Theme::ui_scale_min);
    if (ImGui::Button("-", ImVec2(step_button, 0.f)))
    {
        Theme::set_ui_scale(Theme::stepped_ui_scale(scale, -1));
        Settings::save();
    }
    ImGui::EndDisabled();

    // Padded to a fixed width so the buttons stay put between steps.
    char percent[16];
    snprintf(percent, sizeof(percent), "%4.0f%%", scale * 100.f);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", percent);
    ImGui::SameLine();

    ImGui::BeginDisabled(scale >= Theme::ui_scale_max);
    if (ImGui::Button("+", ImVec2(step_button, 0.f)))
    {
        Theme::set_ui_scale(Theme::stepped_ui_scale(scale, +1));
        Settings::save();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROTATE "  Reset"))
    {
        LogbookUI::reset_layout();
        Settings::save();
    }
    Ui::help_marker("Scales fonts and spacing. Useful on high-DPI displays.\n"
                    "Reset returns to 100% and recentres the window. It is also in\n"
                    "the Plugins menu under \"XP Pilot Suite\", which stays reachable\n"
                    "even if this window has been scaled out of view.");
}

// Screen chrome: back navigation and title, then the screen itself.
static void draw_current_screen(const FlightLogger::LiveFlight &live)
{
    switch (s_screen)
    {
    case Screen::Home:
        Ui::begin_content_panel("home_panel");
        Home::draw_tiles(live, {s_logbook.entries.size(), s_archive_count, AutoQNH::enabled()}, s_screen);
        Ui::end_content_panel();
        return;

    case Screen::Live:
        if (Ui::view_header(ICON_FA_PLANE_DEP, "LIVE FLIGHT"))
            s_screen = Screen::Home;
        ImGui::Separator();
        Ui::begin_content_panel("live_panel");
        draw_live_screen(live);
        Ui::end_content_panel();
        return;

    case Screen::Logbook:
        if (Ui::view_header(ICON_FA_BOOK, "LOGBOOK"))
            s_screen = Screen::Home;
        ImGui::Separator();
        if (FlightListScreen::draw(s_logbook))
        {
            s_archive.loaded = false; // reload on next visit
            s_archive_count  = FlightListScreen::count_on_disk("archived/");
        }
        return;

    case Screen::Archive:
        if (Ui::view_header(ICON_FA_ARCHIVE, "ARCHIVE"))
            s_screen = Screen::Home;
        ImGui::Separator();
        FlightListScreen::draw(s_archive);
        s_archive_count = s_archive.entries.size();
        return;

    case Screen::Settings:
        if (Ui::view_header(ICON_FA_GEAR, "SETTINGS"))
            s_screen = Screen::Home;
        ImGui::Separator();
        Ui::begin_content_panel("settings_panel");
        draw_settings_screen();
        Ui::end_content_panel();
        return;
    }
}

// ════════════════════════════════════════════════════════════════
// XPLM window callbacks
// The XPLM window is full-screen, decoration=None.
// It acts as an invisible mouse/keyboard capture layer.
// ImGui draws on top with its own window chrome.
// ════════════════════════════════════════════════════════════════

static void close_window()
{
    s_logbook_open = false;
    if (s_wnd)
        XPLMSetWindowIsVisible(s_wnd, 0);
}

void LogbookUI::reset_layout()
{
    Theme::reset_ui_scale();
    s_reset_window_layout = true;
}

// Minimal XPLM window draw callback — input capture only, rendering is in LogbookUI::draw()
static void DrawCallback(XPLMWindowID, void *)
{
    if (FlightLogger::lb_needs_refresh())
    {
        FlightLogger::lb_needs_refresh() = false;
        FlightListScreen::reload(s_logbook);
    }
}

static void feed_mouse_position(XPLMWindowID wnd, int x, int y)
{
    int left, top, right, bottom;
    XPLMGetWindowGeometry(wnd, &left, &top, &right, &bottom);
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(x - left), static_cast<float>(top - y));
}

static int MouseCallback(XPLMWindowID wnd, int x, int y, XPLMMouseStatus status, void *)
{
    feed_mouse_position(wnd, x, y);
    if (status == xplm_MouseDown)
        ImGui::GetIO().AddMouseButtonEvent(0, true);
    if (status == xplm_MouseUp)
        ImGui::GetIO().AddMouseButtonEvent(0, false);
    return 1; // consume event
}

static int ScrollCallback(XPLMWindowID wnd, int x, int y, int, int clicks, void *)
{
    feed_mouse_position(wnd, x, y);
    ImGui::GetIO().AddMouseWheelEvent(0.f, static_cast<float>(clicks));
    return 1;
}

static int RightClickCallback(XPLMWindowID wnd, int x, int y, XPLMMouseStatus status, void *)
{
    feed_mouse_position(wnd, x, y);
    if (status == xplm_MouseDown)
        ImGui::GetIO().AddMouseButtonEvent(1, true);
    if (status == xplm_MouseUp)
        ImGui::GetIO().AddMouseButtonEvent(1, false);
    return 1;
}

static XPLMCursorStatus CursorCallback(XPLMWindowID wnd, int x, int y, void *)
{
    feed_mouse_position(wnd, x, y);
    return xplm_CursorDefault;
}

static void KeyCallback(XPLMWindowID, char key, XPLMKeyFlags flags, char vkey, void *, int losing_focus)
{
    if (losing_focus)
        return;
    if (!(flags & xplm_DownFlag))
        return;

    ImGuiIO &io = ImGui::GetIO();
    if (key >= 32 && key < 127)
        io.AddInputCharacter(static_cast<unsigned>(key));
    if (vkey == XPLM_VK_BACK)
        io.AddKeyEvent(ImGuiKey_Backspace, true);
    if (vkey == XPLM_VK_DELETE)
        io.AddKeyEvent(ImGuiKey_Delete, true);
    if (vkey == XPLM_VK_RETURN)
        io.AddKeyEvent(ImGuiKey_Enter, true);

    // Escape steps back to the home screen first, and only closes the window from there.
    if (vkey == XPLM_VK_ESCAPE)
    {
        if (s_screen != Screen::Home)
            s_screen = Screen::Home;
        else
            close_window();
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

    Theme::init();

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
        FlightListScreen::reload(s_logbook);
    }

    if (!s_logbook_open && !FlightLogger::popup_active())
        return;

    int screen_left, screen_top, screen_right, screen_bottom;
    XPLMGetScreenBoundsGlobal(&screen_left, &screen_top, &screen_right, &screen_bottom);
    const int screen_w = screen_right - screen_left;
    const int screen_h = screen_top - screen_bottom;
    if (screen_w <= 0 || screen_h <= 0)
        return;

    // Keep the invisible capture window sized to the current screen (only when logbook is open)
    if (s_logbook_open && s_wnd)
    {
        int left, top, right, bottom;
        XPLMGetWindowGeometry(s_wnd, &left, &top, &right, &bottom);
        if (left != screen_left || bottom != screen_bottom || right != screen_right || top != screen_top)
            XPLMSetWindowGeometry(s_wnd, screen_left, screen_top, screen_right, screen_bottom);
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
    const int framebuffer_w = prev_viewport[2];
    const int framebuffer_h = prev_viewport[3];

    glViewport(0, 0, framebuffer_w, framebuffer_h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, screen_w, screen_h, 0, -1, 1); // top-left origin for ImGui (logical coords)
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    ImGuiIO     &io            = ImGui::GetIO();
    const double now           = get_xp_time();
    io.DeltaTime               = static_cast<float>(std::max(now - s_last_frame_time, 0.001));
    s_last_frame_time          = now;
    io.DisplaySize             = ImVec2(static_cast<float>(screen_w), static_cast<float>(screen_h));
    io.DisplayFramebufferScale = ImVec2(static_cast<float>(framebuffer_w) / static_cast<float>(screen_w),
                                        static_cast<float>(framebuffer_h) / static_cast<float>(screen_h));

    ImGui_ImplOpenGL2_NewFrame();
    ImGui::NewFrame();

    FlightLogger::draw_popup();

    if (s_logbook_open)
    {
        const Theme::WindowFit fit = Theme::fit_window_to_screen(
            Theme::scaled(1060.f), Theme::scaled(720.f), static_cast<float>(screen_w), static_cast<float>(screen_h));

        // A scale change has to re-apply size and position, otherwise the window keeps
        // whatever the previous scale left behind and its content no longer fits.
        static float last_applied_scale = Theme::ui_scale();
        ImGuiCond    layout_cond        = ImGuiCond_FirstUseEver;
        if (s_reset_window_layout || last_applied_scale != Theme::ui_scale())
        {
            last_applied_scale    = Theme::ui_scale();
            s_reset_window_layout = false;
            layout_cond           = ImGuiCond_Always;
        }

        ImGui::SetNextWindowPos(fit.pos, layout_cond);
        ImGui::SetNextWindowSize(fit.size, layout_cond);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(std::min(Theme::scaled(760.f), fit.max_size.x), std::min(Theme::scaled(460.f), fit.max_size.y)),
            fit.max_size);

        bool open = true;
#ifdef XP_PILOT_VERSION
        static const std::string window_title = std::string("xp_pilot v") + XP_PILOT_VERSION + "##xp_pilot";
#else
        static const std::string window_title = "xp_pilot##xp_pilot";
#endif
        if (ImGui::Begin(window_title.c_str(), &open, ImGuiWindowFlags_NoCollapse))
        {
            const FlightLogger::LiveFlight live = FlightLogger::live_flight();
            Home::draw_status_bar(live);
            draw_current_screen(live);
        }
        ImGui::End();

        if (!open)
            close_window();
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
        int left, top, right, bottom;
        XPLMGetScreenBoundsGlobal(&left, &top, &right, &bottom);

        XPLMCreateWindow_t params       = {};
        params.structSize               = sizeof(params);
        params.left                     = left;
        params.bottom                   = bottom;
        params.right                    = right;
        params.top                      = top;
        params.visible                  = 1;
        params.drawWindowFunc           = DrawCallback;
        params.handleMouseClickFunc     = MouseCallback;
        params.handleKeyFunc            = KeyCallback;
        params.handleCursorFunc         = CursorCallback;
        params.handleMouseWheelFunc     = ScrollCallback;
        params.handleRightClickFunc     = RightClickCallback;
        params.refcon                   = nullptr;
        params.decorateAsFloatingWindow = xplm_WindowDecorationNone; // no chrome
        params.layer                    = xplm_WindowLayerFloatingWindows;
        s_wnd                           = XPLMCreateWindowEx(&params);
    }

    XPLMSetWindowIsVisible(s_wnd, 1);
    XPLMBringWindowToFront(s_wnd);

    s_screen = Screen::Home;
    FlightListScreen::reload(s_logbook);
    s_archive.loaded = false;
    s_archive_count  = FlightListScreen::count_on_disk("archived/");
}

void LogbookUI::toggle()
{
    if (s_logbook_open)
        close_window();
    else
        open();
}
