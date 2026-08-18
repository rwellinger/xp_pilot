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

#include "ui_home.hpp"
#include "ui_flight_view.hpp"
#include "ui_theme.hpp"
#include "ui_widgets.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>

namespace
{

constexpr float STATUS_BAR_HEIGHT = 74.f;
constexpr float AIRCRAFT_FONT     = 20.f;

// Aircraft type, tail number and departure -> current position.
void draw_status_identity(const FlightLogger::LiveFlight &live)
{
    const FlightData &flight = live.flight;

    char aircraft[96];
    if (live.in_progress)
        snprintf(aircraft, sizeof(aircraft), ICON_FA_PLANE_DEP "  %s  %s",
                 flight.aircraft_icao.empty() ? "----" : flight.aircraft_icao.c_str(), flight.aircraft_tail.c_str());
    else
        snprintf(aircraft, sizeof(aircraft), ICON_FA_PLANE_DEP "  xp_pilot");

    ImGui::PushFont(nullptr, AIRCRAFT_FONT);
    ImGui::TextUnformatted(aircraft);
    ImGui::PopFont();

    if (live.in_progress)
    {
        char route[96];
        snprintf(route, sizeof(route), "%s  ->  %.3f, %.3f   HDG %03.0f",
                 flight.departure_icao.empty() ? "????" : flight.departure_icao.c_str(), live.latitude, live.longitude,
                 static_cast<double>(live.heading_true));
        Ui::text_dim(route);
    }
    else
    {
        Ui::text_dim("No flight in progress");
    }
}

void draw_status_metrics(const FlightLogger::LiveFlight &live, float width)
{
    const float  cell_w = width / 4.f;
    const ImVec2 row    = Ui::begin_metric_row();

    if (!live.in_progress)
    {
        Ui::metric_cell("ALTITUDE", "--", Theme::text_dim, cell_w);
        Ui::metric_cell("IAS", "--", Theme::text_dim, cell_w);
        Ui::metric_cell("V/S", "--", Theme::text_dim, cell_w);
        Ui::metric_cell("BLOCK", "--", Theme::text_dim, cell_w);
        Ui::end_metric_row(row);
        return;
    }

    char value[48];

    snprintf(value, sizeof(value), "%d ft", live.altitude_ft);
    Ui::metric_cell("ALTITUDE", value, Theme::text, cell_w);

    snprintf(value, sizeof(value), "%d kts", live.indicated_airspeed_kts);
    Ui::metric_cell("IAS", value, Theme::text, cell_w);

    snprintf(value, sizeof(value), "%+d fpm", live.vertical_speed_fpm);
    Ui::metric_cell("V/S", value, live.vertical_speed_fpm < -1000 ? Theme::warning : Theme::text, cell_w);

    Ui::metric_cell("BLOCK", FlightView::format_duration_sec(live.flight.block_time_sec).c_str(), Theme::text, cell_w);

    Ui::end_metric_row(row);
}

void draw_recording_badge(const FlightLogger::LiveFlight &live)
{
    if (live.in_progress)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::recording);
        ImGui::TextUnformatted(ICON_FA_CIRCLE "  REC");
        ImGui::PopStyleColor();
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::text_dim);
    ImGui::TextUnformatted(ICON_FA_CIRCLE "  IDLE");
    ImGui::PopStyleColor();
}

const char *live_subtitle(const FlightLogger::LiveFlight &live) { return live.in_progress ? "recording" : "no flight"; }

} // namespace

void Home::draw_status_bar(const FlightLogger::LiveFlight &live)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::surface);
    ImGui::BeginChild("status_bar", ImVec2(0.f, Theme::scaled(STATUS_BAR_HEIGHT)), ImGuiChildFlags_Borders);

    const float total_w    = ImGui::GetContentRegionAvail().x;
    const float identity_w = std::max(Theme::scaled(180.f), total_w * 0.32f);
    const float badge_w    = Theme::scaled(90.f);
    const float metrics_w  = std::max(Theme::scaled(240.f), total_w - identity_w - badge_w);

    const ImVec2 origin = ImGui::GetCursorPos();
    draw_status_identity(live);

    ImGui::SetCursorPos(ImVec2(origin.x + identity_w, origin.y));
    draw_status_metrics(live, metrics_w);

    ImGui::SetCursorPos(ImVec2(origin.x + identity_w + metrics_w, origin.y));
    draw_recording_badge(live);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void Home::draw_tiles(const FlightLogger::LiveFlight &live, const TileSummary &summary, Screen &screen)
{
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float  gap       = Theme::scaled(16.f);

    // Two columns, two rows, as square as the window allows. The grid has to fit the
    // panel exactly or it scrolls on every frame, so item spacing is pinned to the gap
    // and no extra spacers are emitted — the row break already provides the vertical gap.
    const float  tile_w = std::floor((available.x - gap) * 0.5f);
    const float  tile_h = std::floor(std::min(tile_w, (available.y - gap) * 0.5f));
    const ImVec2 tile{tile_w, std::max(Theme::scaled(90.f), tile_h)};

    char flights[32], archived[32];
    snprintf(flights, sizeof(flights), "%zu flight%s", summary.flight_count, summary.flight_count == 1 ? "" : "s");
    snprintf(archived, sizeof(archived), "%zu archived", summary.archive_count);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));

    if (Ui::icon_tile(ICON_FA_PLANE_DEP, "LIVE", live_subtitle(live), tile))
        screen = Screen::Live;
    ImGui::SameLine();
    if (Ui::icon_tile(ICON_FA_BOOK, "LOGBOOK", flights, tile))
        screen = Screen::Logbook;

    if (Ui::icon_tile(ICON_FA_ARCHIVE, "ARCHIVE", archived, tile))
        screen = Screen::Archive;
    ImGui::SameLine();
    if (Ui::icon_tile(ICON_FA_GEAR, "SETTINGS", summary.auto_qnh_on ? "Auto QNH on" : "Auto QNH off", tile))
        screen = Screen::Settings;

    ImGui::PopStyleVar();
}
