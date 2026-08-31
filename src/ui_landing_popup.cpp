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

#include "ui_landing_popup.hpp"
#include "flight_logger_logic.hpp"
#include "sim_clock.hpp"
#include "ui_theme.hpp"
#include "ui_widgets.hpp"
#include <XPLM/XPLMDisplay.h>
#include <cmath>
#include <cstdio> // snprintf
#include <imgui.h>

namespace
{

LandingData   s_landing;
bool          s_active   = false;
double        s_until    = 0;
bool          s_enabled  = true;
PopupPosition s_position = POPUP_POSITION_DEFAULT;

// Screen point the popup is pinned to, paired with the pivot below. The top row keeps
// the 12% inset the popup always had; the other edges use a flat margin.
ImVec2 anchor_point(PopupPosition position, float screen_w, float screen_h)
{
    constexpr float EDGE_MARGIN_PX = 40.f;
    const float     top_y          = screen_h * 0.12f;
    const float     bottom_y       = screen_h - EDGE_MARGIN_PX;

    switch (position)
    {
    case PopupPosition::TopLeft:
        return {EDGE_MARGIN_PX, top_y};
    case PopupPosition::TopRight:
        return {screen_w - EDGE_MARGIN_PX, top_y};
    case PopupPosition::Center:
        return {screen_w * 0.5f, screen_h * 0.5f};
    case PopupPosition::BottomLeft:
        return {EDGE_MARGIN_PX, bottom_y};
    case PopupPosition::BottomCenter:
        return {screen_w * 0.5f, bottom_y};
    case PopupPosition::BottomRight:
        return {screen_w - EDGE_MARGIN_PX, bottom_y};
    case PopupPosition::TopCenter:
        break;
    }
    return {screen_w * 0.5f, top_y};
}

// Which corner of the window sits on the anchor: 0 = left/top, 1 = right/bottom.
ImVec2 pivot_point(PopupPosition position)
{
    switch (position)
    {
    case PopupPosition::TopLeft:
        return {0.f, 0.f};
    case PopupPosition::TopRight:
        return {1.f, 0.f};
    case PopupPosition::Center:
        return {0.5f, 0.5f};
    case PopupPosition::BottomLeft:
        return {0.f, 1.f};
    case PopupPosition::BottomCenter:
        return {0.5f, 1.f};
    case PopupPosition::BottomRight:
        return {1.f, 1.f};
    case PopupPosition::TopCenter:
        break;
    }
    return {0.5f, 0.f};
}

// The rating headline on a tinted bar in its own colour.
void draw_rating_banner(const ImVec4 &col, float content_w)
{
    constexpr float BANNER_H = 40.f;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList  *dl     = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + content_w, origin.y + BANNER_H),
                      ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.16f)), 6.f);
    dl->AddRectFilled(origin, ImVec2(origin.x + 5.f, origin.y + BANNER_H), ImGui::GetColorU32(col), 6.f);

    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::SetWindowFontScale(1.45f);
    const float text_w = ImGui::CalcTextSize(s_landing.rating.c_str()).x;
    const float text_h = ImGui::GetTextLineHeight();
    ImGui::SetCursorScreenPos(
        ImVec2(origin.x + (content_w - text_w) * 0.5f, origin.y + (BANNER_H - text_h) * 0.5f));
    ImGui::TextUnformatted(s_landing.rating.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    // Claim the banner strip as a real item so the auto-sized window knows its height.
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(content_w, BANNER_H));
}

// Each metric is tinted by the grade it earns on its own, so the popup shows at a glance
// which criterion produced the verdict rather than just the verdict.
ImVec4 grade_color(int grade) { return Theme::rating_color(FlightLoggerLogic::rating_label(grade)); }

void draw_rotorcraft_metrics(float cell_w)
{
    using namespace FlightLoggerLogic;
    const ImVec4 white{0.92f, 0.94f, 0.98f, 1.f};
    char         value[64];

    const ImVec2 first_row = Ui::begin_metric_row();
    snprintf(value, sizeof(value), "%.0f fpm", s_landing.fpm);
    Ui::metric_cell("VERTICAL SPEED", value, Theme::rating_color(s_landing.rating), cell_w);
    snprintf(value, sizeof(value), "%.2f G", s_landing.g_force);
    Ui::metric_cell("G-FORCE", value, grade_color(grade_against(s_landing.g_force, ROTORCRAFT_G_FORCE_LIMITS)), cell_w);
    Ui::end_metric_row(first_row);

    // Above the run-on threshold the ground speed is deliberate travel, not drift, and
    // the rating ignores it — so the cell stays neutral instead of flagging it red.
    const bool   is_run_on = s_landing.ground_speed_kts >= ROTORCRAFT_RUN_ON_KTS;
    const ImVec4 drift_color =
        is_run_on ? white : grade_color(grade_against(s_landing.ground_speed_kts, ROTORCRAFT_DRIFT_LIMITS_KTS));

    const ImVec2 second_row = Ui::begin_metric_row();
    snprintf(value, sizeof(value), "%.0f kts", s_landing.ground_speed_kts);
    Ui::metric_cell(is_run_on ? "GROUND SPEED" : "DRIFT", value, drift_color, cell_w);
    snprintf(value, sizeof(value), "%.1f deg", std::abs(s_landing.bank_deg));
    Ui::metric_cell("BANK", value, grade_color(grade_against(std::abs(s_landing.bank_deg), ROTORCRAFT_BANK_LIMITS_DEG)),
                    cell_w);
    Ui::end_metric_row(second_row);

    const ImVec2 third_row = Ui::begin_metric_row();
    snprintf(value, sizeof(value), "%.1f deg/s", std::abs(s_landing.yaw_rate_deg_s));
    Ui::metric_cell("YAW RATE", value,
                    grade_color(grade_against(std::abs(s_landing.yaw_rate_deg_s), ROTORCRAFT_YAW_LIMITS_DEG_S)), cell_w);
    Ui::end_metric_row(third_row);
}

// The context the numbers don't carry: what the weather was and who was flying.
std::string fixed_wing_context_line()
{
    if (!s_landing.has_configuration)
        return "";

    std::string line = meteo_condition_to_string(s_landing.meteo);
    char        part[64];
    snprintf(part, sizeof(part), "Flaps %.0f%%", s_landing.flap_ratio * 100.f);
    if (!line.empty())
        line += "  \xc2\xb7  ";
    line += part;
    if (s_landing.gear_retractable && s_landing.gear_deploy_ratio < 0.99f)
        line += "  \xc2\xb7  GEAR NOT DOWN";
    if (s_landing.autopilot_engaged)
        line += "  \xc2\xb7  AUTOLAND";
    return line;
}

void draw_fixed_wing_metrics(float cell_w)
{
    using namespace FlightLoggerLogic;
    const ImVec4 white{0.92f, 0.94f, 0.98f, 1.f};
    char         value[64];

    const ImVec2 first_row = Ui::begin_metric_row();
    snprintf(value, sizeof(value), "%.0f fpm", s_landing.fpm);
    Ui::metric_cell("VERTICAL SPEED", value, Theme::rating_color(s_landing.rating), cell_w);
    snprintf(value, sizeof(value), "%.2f G", s_landing.g_force);
    Ui::metric_cell("G-FORCE", value, grade_color(grade_against(s_landing.g_force, FIXED_WING_G_FORCE_LIMITS)), cell_w);
    snprintf(value, sizeof(value), "%.1f s", s_landing.float_time);
    Ui::metric_cell("FLOAT", value, white, cell_w);
    Ui::end_metric_row(first_row);

    const ImVec2 second_row = Ui::begin_metric_row();
    if (s_landing.ias_kts > 0.f)
    {
        snprintf(value, sizeof(value), "%.0f kts", s_landing.ias_kts);
        Ui::metric_cell("TOUCHDOWN IAS", value, white, cell_w);

        snprintf(value, sizeof(value), "%.0f kts", s_landing.ground_speed_kts);
        Ui::metric_cell("GROUND SPEED", value, white, cell_w);
    }
    if (s_landing.bounce_count > 0)
    {
        snprintf(value, sizeof(value), "%d", s_landing.bounce_count);
        Ui::metric_cell("BOUNCES", value, ImVec4(1.f, 0.5f, 0.2f, 1.f), cell_w);
    }
    else
    {
        snprintf(value, sizeof(value), "%d kts %s", std::abs(s_landing.crosswind_kts),
                 s_landing.crosswind_side.c_str());
        Ui::metric_cell("CROSSWIND", value, white, cell_w);
    }
    Ui::end_metric_row(second_row);

    if (!s_landing.flare.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.80f, 0.88f, 1.f));
        ImGui::TextUnformatted(s_landing.flare.c_str());
        ImGui::PopStyleColor();
    }

    const std::string context = fixed_wing_context_line();
    if (!context.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.58f, 0.63f, 0.72f, 1.f));
        ImGui::TextUnformatted(context.c_str());
        ImGui::PopStyleColor();
    }
}

void draw_metrics(float content_w)
{
    const int columns = s_landing.is_rotorcraft ? 2 : 3;
    const float cell_w = content_w / static_cast<float>(columns);

    if (s_landing.is_rotorcraft)
        draw_rotorcraft_metrics(cell_w);
    else
        draw_fixed_wing_metrics(cell_w);
}

// Plan view of the runway with the touchdown marked. Same idea as the HTML report:
// along-track to scale, lateral deviation exaggerated so metres stay visible.
void draw_runway_diagram(float content_w)
{
    constexpr float STRIP_H        = 46.f;
    constexpr float LATERAL_SPAN_M = 20.f;
    constexpr float TOUCHDOWN_ZONE_M = 300.f;

    if (s_landing.runway_length_m <= 0.f)
        return;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.66f, 0.75f, 1.f));
    char header[96];
    snprintf(header, sizeof(header), "RUNWAY %s  --  %.0f m usable", s_landing.runway_ident.c_str(),
             s_landing.runway_length_m);
    ImGui::TextUnformatted(header);
    ImGui::PopStyleColor();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList  *dl     = ImGui::GetWindowDrawList();
    const float  mid_y  = origin.y + STRIP_H * 0.5f;
    const float  half_h = STRIP_H * 0.5f;

    dl->AddRectFilled(origin, ImVec2(origin.x + content_w, origin.y + STRIP_H), IM_COL32(48, 48, 52, 255), 3.f);

    const float zone_w =
        std::min(content_w, TOUCHDOWN_ZONE_M / s_landing.runway_length_m * content_w);
    dl->AddRectFilled(origin, ImVec2(origin.x + zone_w, origin.y + STRIP_H), IM_COL32(80, 78, 50, 255), 3.f);

    constexpr float DASH_PITCH_PX = 26.f;
    constexpr float DASH_LEN_PX   = 14.f;
    const float     dash_end_x    = origin.x + content_w - 12.f;
    const int       dash_count    = static_cast<int>((content_w - 24.f) / DASH_PITCH_PX);
    for (int i = 0; i < dash_count; ++i)
    {
        const float x = origin.x + 12.f + static_cast<float>(i) * DASH_PITCH_PX;
        dl->AddLine(ImVec2(x, mid_y), ImVec2(std::min(x + DASH_LEN_PX, dash_end_x), mid_y),
                    IM_COL32(215, 215, 215, 255), 1.5f);
    }

    dl->AddRectFilled(origin, ImVec2(origin.x + 4.f, origin.y + STRIP_H), IM_COL32(255, 255, 255, 255));

    const float along_pct =
        std::min(1.f, std::max(0.f, s_landing.runway_distance_m / s_landing.runway_length_m));
    const float offset_clamped =
        std::min(LATERAL_SPAN_M, std::max(-LATERAL_SPAN_M, s_landing.runway_offset_m));
    const ImVec2 marker(origin.x + along_pct * content_w, mid_y + (offset_clamped / LATERAL_SPAN_M) * half_h);

    dl->AddCircleFilled(marker, 6.f, IM_COL32(224, 122, 60, 255));
    dl->AddCircle(marker, 6.f, IM_COL32(255, 255, 255, 255), 0, 1.5f);

    ImGui::Dummy(ImVec2(content_w, STRIP_H + 2.f));

    char caption[128];
    snprintf(caption, sizeof(caption), "%.0f m past threshold  |  %.0f m %s of centerline",
             s_landing.runway_distance_m, std::abs(s_landing.runway_offset_m),
             s_landing.runway_offset_m > 0 ? "right" : "left");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.66f, 0.75f, 1.f));
    ImGui::TextUnformatted(caption);
    ImGui::PopStyleColor();
}

} // namespace

void LandingPopup::set_enabled(bool on) { s_enabled = on; }
bool LandingPopup::enabled() { return s_enabled; }
void          LandingPopup::set_position(PopupPosition position) { s_position = position; }
PopupPosition LandingPopup::position() { return s_position; }

bool LandingPopup::active()
{
    if (s_active && SimClock::seconds() > s_until)
        s_active = false;
    return s_active;
}

void LandingPopup::show(const LandingData &landing)
{
    s_landing = landing;
    s_active  = true;
    s_until   = SimClock::seconds() + 15.0;
}

// When the user turned the popup off the landing is still remembered, so the replay
// command can summon it on demand.
void LandingPopup::post(const LandingData &landing)
{
    if (!s_enabled)
    {
        s_landing = landing;
        return;
    }
    show(landing);
}

bool LandingPopup::replay_remembered()
{
    if (s_landing.rating.empty())
        return false;
    show(s_landing);
    return true;
}

void LandingPopup::draw()
{
    if (!active())
        return;

    int sw = 0, sh = 0;
    XPLMGetScreenSize(&sw, &sh);

    const float  popup_w = 470.f;
    const ImVec2 anchor  = anchor_point(s_position, static_cast<float>(sw), static_cast<float>(sh));
    const ImVec2 pivot   = pivot_point(s_position);
    // Positioning by pivot lets the bottom and centre placements work without knowing
    // the auto-sized window height in advance.
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowSize(ImVec2(popup_w, 0.f), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.11f, 0.18f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::rating_color(s_landing.rating));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.f, 14.f));

    ImGui::Begin("##landing_popup", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);

    const float content_w = ImGui::GetContentRegionAvail().x;

    draw_rating_banner(Theme::rating_color(s_landing.rating), content_w);
    ImGui::Spacing();
    draw_metrics(content_w);
    if (!s_landing.runway_ident.empty())
    {
        ImGui::Spacing();
        draw_runway_diagram(content_w);
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

