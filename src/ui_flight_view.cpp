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

#include "ui_flight_view.hpp"
#include "map_overlay_cache.hpp"
#include "flight_logger_logic.hpp"
#include "ui_theme.hpp"
#include "ui_widgets.hpp"
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace
{

// Blend two colours, t clamped to [0,1].
ImU32 lerp_color(ImU32 low, ImU32 high, float t)
{
    t                  = std::clamp(t, 0.f, 1.f);
    const auto channel = [&](int shift)
    {
        const float from = static_cast<float>((low >> shift) & 0xFF);
        const float to   = static_cast<float>((high >> shift) & 0xFF);
        return static_cast<ImU32>(from + (to - from) * t) << shift;
    };
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT) |
           (static_cast<ImU32>(0xFF) << IM_COL32_A_SHIFT);
}

// Largest round distance that still fits within the budget, for the scale bar.
double round_scale_distance_km(double budget_km)
{
    static constexpr double STEPS[] = {1, 2, 5, 10, 25, 50, 100, 250, 500, 1000, 2500};
    double                  chosen  = STEPS[0];
    for (double step : STEPS)
        if (step <= budget_km)
            chosen = step;
    return chosen;
}

// Restricted, prohibited and danger areas read red on aviation charts; controlled
// airspace reads blue. Anything else follows the controlled colour.
ImU32 airspace_color(const std::string &airspace_class)
{
    const bool restricted = airspace_class == "R" || airspace_class == "P" || airspace_class == "Q";
    return restricted ? Theme::map_airspace_restricted : Theme::map_airspace_controlled;
}

// Water first, so airspaces and the track sit on top of it. Lakes are closed rings and
// get filled — a filled shape reads as water without needing a legend. Coastlines are
// open lines; the shared teal is what marks them as water's edge.
void draw_water(ImDrawList *dl, const ImVec2 &origin, const std::vector<GeoOutline> &outlines,
                const FlightLoggerLogic::MapViewport &viewport)
{
    std::vector<ImVec2> projected;
    for (const auto &outline : outlines)
    {
        projected.clear();
        projected.reserve(outline.points.size());
        for (const auto &point : outline.points)
        {
            float x, y;
            FlightLoggerLogic::project_to_pixel(viewport, point.lat, point.lon, x, y);
            projected.push_back(ImVec2(origin.x + x, origin.y + y));
        }
        if (projected.size() < 2)
            continue;

        if (outline.is_lake)
        {
            // Lake outlines are concave often enough that the convex filler would
            // produce visible spikes.
            dl->AddConcavePolyFilled(projected.data(), static_cast<int>(projected.size()), Theme::map_water);
            dl->AddPolyline(projected.data(), static_cast<int>(projected.size()), Theme::map_coastline,
                            ImDrawFlags_Closed, 1.f);
        }
        else
        {
            dl->AddPolyline(projected.data(), static_cast<int>(projected.size()), Theme::map_coastline,
                            ImDrawFlags_None, 1.2f);
        }
    }
}

// Airspace outlines under the track. They come from X-Plane's own database, so this
// needs no network and no API key. Outlines reach past the map — an airspace only has
// to touch the bounds to be included — so the caller clips to the map rectangle.
void draw_airspaces(ImDrawList *dl, const ImVec2 &origin, const std::vector<Airspace> &airspaces,
                    const FlightLoggerLogic::MapViewport &viewport)
{
    std::vector<ImVec2> projected;
    for (const auto &airspace : airspaces)
    {
        projected.clear();
        projected.reserve(airspace.outline.size());
        for (const auto &point : airspace.outline)
        {
            float x, y;
            FlightLoggerLogic::project_to_pixel(viewport, point.lat, point.lon, x, y);
            projected.push_back(ImVec2(origin.x + x, origin.y + y));
        }
        if (projected.size() >= 3)
            dl->AddPolyline(projected.data(), static_cast<int>(projected.size()),
                            airspace_color(airspace.airspace_class), ImDrawFlags_Closed, 1.f);
    }
}

// A bar of round length, sized from the map's own scale so distances stay readable
// however far the flight ranged.
void draw_scale_bar(ImDrawList *dl, const ImVec2 &origin, const FlightLoggerLogic::GeoBounds &bounds,
                    const FlightLoggerLogic::MapViewport &viewport, float map_w, float map_h)
{
    const double km_per_px = FlightLoggerLogic::km_per_pixel(bounds, viewport);
    if (km_per_px <= 0.0)
        return;

    const double distance_km = round_scale_distance_km(map_w * 0.3f * km_per_px);
    const float  bar_px      = static_cast<float>(distance_km / km_per_px);
    if (bar_px < 1.f || bar_px > map_w)
        return;

    const float margin = Theme::scaled(10.f);
    const float x0     = origin.x + margin;
    const float y      = origin.y + map_h - margin;
    const float tick   = Theme::scaled(4.f);

    dl->AddLine(ImVec2(x0, y), ImVec2(x0 + bar_px, y), Theme::map_scale, 1.5f);
    dl->AddLine(ImVec2(x0, y - tick), ImVec2(x0, y + tick), Theme::map_scale, 1.5f);
    dl->AddLine(ImVec2(x0 + bar_px, y - tick), ImVec2(x0 + bar_px, y + tick), Theme::map_scale, 1.5f);

    char label[32];
    snprintf(label, sizeof(label), "%.0f km", distance_km);
    dl->AddText(ImVec2(x0, y - tick - ImGui::GetFontSize()), Theme::map_scale, label);
}

void draw_wind_status_line(const LandingData &landing)
{
    const int crosswind = std::abs(landing.crosswind_kts);
    char      line[128];

    switch (wind_condition_from_string(landing.wind_status))
    {
    case WindCondition::Calm:
        Ui::text_dim("  Wind: CALM");
        return;

    case WindCondition::Light:
        snprintf(line, sizeof(line), "  Wind: LIGHT  XW %d kts %s", crosswind, landing.crosswind_side.c_str());
        Ui::text_dim(line);
        return;

    case WindCondition::Steady:
        if (landing.headwind_kts < -5 && !landing.is_rotorcraft)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::warning);
            snprintf(line, sizeof(line), "  " ICON_FA_WARNING "  TAILWIND +%d kts  --  WRONG RWY?",
                     std::abs(landing.headwind_kts));
            ImGui::TextUnformatted(line);
            ImGui::PopStyleColor();
        }
        else
        {
            const int   headwind = std::abs(landing.headwind_kts);
            const char *label    = landing.headwind_kts < 0 ? "TW" : "HW";
            snprintf(line, sizeof(line), "  Wind: %d kts | %s %d kts | XW %d kts %s", landing.wind_speed_kts, label,
                     headwind, crosswind, landing.crosswind_side.c_str());
            Ui::text_dim(line);
        }
        return;
    }
}

} // namespace

std::string FlightView::format_duration(int minutes)
{
    char      buffer[32];
    const int hours = minutes / 60, remainder = minutes % 60;
    if (hours > 0)
        snprintf(buffer, sizeof(buffer), "%dh %02dm", hours, remainder);
    else
        snprintf(buffer, sizeof(buffer), "%dm", remainder);
    return buffer;
}

std::string FlightView::format_duration_sec(int seconds)
{
    char buffer[32];
    if (seconds >= 3600)
        snprintf(buffer, sizeof(buffer), "%dh %02dm", seconds / 3600, (seconds % 3600) / 60);
    else if (seconds >= 60)
        snprintf(buffer, sizeof(buffer), "%dm %02ds", seconds / 60, seconds % 60);
    else
        snprintf(buffer, sizeof(buffer), "%ds", seconds);
    return buffer;
}

void FlightView::open_in_browser(const std::string &target)
{
#if defined(__APPLE__)
    system(("open \"" + target + "\"").c_str()); // NOLINT(bugprone-command-processor)
#elif defined(_WIN32)
    system(("start \"\" \"" + target + "\"").c_str());
#else
    system(("xdg-open \"" + target + "\"").c_str()); // NOLINT(bugprone-command-processor)
#endif
}

// SkyVector centres its chart on the ll parameter; chart 301 is the world VFR layer.
std::string FlightView::skyvector_url(double latitude, double longitude)
{
    char url[128];
    snprintf(url, sizeof(url), "https://skyvector.com/?ll=%.5f,%.5f&chart=301&zoom=3", latitude, longitude);
    return url;
}

FlightView::TimeCells FlightView::time_cells(const FlightData &flight)
{
    TimeCells cells;
    cells.was_paused = flight.paused_sec > 0;
    cells.total      = format_duration_sec(flight.block_time_sec + flight.paused_sec);
    cells.paused     = cells.was_paused ? format_duration_sec(flight.paused_sec) : "--";
    cells.block      = format_duration_sec(flight.block_time_sec);
    return cells;
}

void FlightView::draw_time_lines(const FlightData &flight)
{
    const float     cell_w = ImGui::GetContentRegionAvail().x / 3.f;
    const TimeCells cells  = time_cells(flight);

    const ImVec2 row = Ui::begin_metric_row();
    Ui::metric_cell("TOTAL", cells.total.c_str(), Theme::text, cell_w);
    Ui::metric_cell("PAUSED", cells.paused.c_str(), cells.was_paused ? Theme::warning : Theme::text_dim, cell_w);
    Ui::metric_cell("BLOCK TIME", cells.block.c_str(), Theme::text, cell_w);
    Ui::end_metric_row(row);
}

void FlightView::draw_track_map(const FlightData &flight, float width)
{
    if (flight.track.size() < 2)
    {
        Ui::text_dim("(no track data)");
        return;
    }

    const float map_w = width - Theme::scaled(20.f);
    const float map_h = std::floor(map_w * 0.45f);

    const auto bounds   = FlightLoggerLogic::track_bounds(flight.track);
    const auto viewport = FlightLoggerLogic::make_viewport(bounds, map_w, map_h);
    auto       to_px    = [&](double lat, double lon, float &px, float &py)
    { FlightLoggerLogic::project_to_pixel(viewport, lat, lon, px, py); };

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(map_w, map_h));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + map_w, origin.y + map_h), Theme::map_background, Theme::scaled(8.f));

    // Airspaces sit underneath the track. Their outlines run past the map edge, so
    // everything from here on is clipped to the map rectangle.
    ImGui::PushClipRect(origin, ImVec2(origin.x + map_w, origin.y + map_h), true);

    const auto overlay = MapOverlayCache::for_bounds(bounds.lat_min, bounds.lat_max, bounds.lon_min, bounds.lon_max);
    draw_water(dl, origin, overlay.outlines, viewport);
    draw_airspaces(dl, origin, overlay.airspaces, viewport);

    int lowest_ft  = flight.track.front().alt_ft;
    int highest_ft = lowest_ft;
    for (const auto &point : flight.track)
    {
        lowest_ft  = std::min(lowest_ft, point.alt_ft);
        highest_ft = std::max(highest_ft, point.alt_ft);
    }
    const float altitude_span = static_cast<float>(std::max(highest_ft - lowest_ft, 1));

    for (size_t i = 1; i < flight.track.size(); ++i)
    {
        float x1, y1, x2, y2;
        to_px(flight.track[i - 1].lat, flight.track[i - 1].lon, x1, y1);
        to_px(flight.track[i].lat, flight.track[i].lon, x2, y2);
        const float share = static_cast<float>(flight.track[i].alt_ft - lowest_ft) / altitude_span;
        dl->AddLine(ImVec2(origin.x + x1, origin.y + y1), ImVec2(origin.x + x2, origin.y + y2),
                    lerp_color(Theme::map_track_low, Theme::map_track_high, share), 1.5f);
    }
    for (const auto &pause : resolve_pauses(flight))
    {
        if (pause.lat == 0.0 && pause.lon == 0.0)
            continue;
        float px, py;
        to_px(pause.lat, pause.lon, px, py);
        dl->AddCircleFilled(ImVec2(origin.x + px, origin.y + py), 4.f, Theme::map_pause);
    }

    float dep_x, dep_y, arr_x, arr_y;
    to_px(flight.track.front().lat, flight.track.front().lon, dep_x, dep_y);
    to_px(flight.track.back().lat, flight.track.back().lon, arr_x, arr_y);
    dl->AddCircleFilled(ImVec2(origin.x + dep_x, origin.y + dep_y), 5.f, Theme::map_departure);
    dl->AddCircleFilled(ImVec2(origin.x + arr_x, origin.y + arr_y), 5.f, Theme::map_arrival);

    const float label_gap = Theme::scaled(8.f);
    if (!flight.departure_icao.empty())
        dl->AddText(ImVec2(origin.x + dep_x + label_gap, origin.y + dep_y - label_gap), Theme::map_departure,
                    flight.departure_icao.c_str());
    if (!flight.arrival_icao.empty())
        dl->AddText(ImVec2(origin.x + arr_x + label_gap, origin.y + arr_y - label_gap), Theme::map_arrival,
                    flight.arrival_icao.c_str());

    draw_scale_bar(dl, origin, bounds, viewport, map_w, map_h);
    ImGui::PopClipRect();

    if (!overlay.airspaces.empty() || !overlay.outlines.empty())
        Ui::text_dim("Violet = controlled airspace, red = restricted, teal = water");

    if (flight.paused_sec > 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Pause");
        ImGui::SameLine(0.f, 6.f);
        Ui::text_dim("= sim paused here, not counted as block time");
    }
}

void FlightView::draw_landings(const FlightData &flight)
{
    if (flight.landings.empty())
    {
        Ui::text_dim("(no landing recorded)");
        return;
    }

    for (size_t i = 0; i < flight.landings.size(); ++i)
    {
        const auto &landing = flight.landings[i];
        if (flight.landings.size() > 1)
        {
            char heading[32];
            snprintf(heading, sizeof(heading), "-- Landing %zu --", i + 1);
            Ui::text_dim(heading);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, Theme::rating_color(landing.rating));
        ImGui::TextUnformatted(landing.rating.empty() ? "(no rating)" : landing.rating.c_str());
        ImGui::PopStyleColor();

        if (landing.time > 0)
        {
            char       stamp[64];
            struct tm *utc = gmtime(&landing.time);
            strftime(stamp, sizeof(stamp), "  Touchdown: %H:%M:%S UTC", utc);
            Ui::text_dim(stamp);
        }

        char stats[160];
        if (landing.is_rotorcraft)
        {
            if (landing.bounce_count > 0)
                snprintf(stats, sizeof(stats), "  %.0f fpm  |  %.2f G  |  %d bounce%s", landing.fpm, landing.g_force,
                         landing.bounce_count, landing.bounce_count == 1 ? "" : "s");
            else
                snprintf(stats, sizeof(stats), "  %.0f fpm  |  %.2f G", landing.fpm, landing.g_force);
        }
        else if (landing.bounce_count > 0)
            snprintf(stats, sizeof(stats), "  %.0f fpm  |  %.2f G  |  Float %.1f s  |  %d bounce%s", landing.fpm,
                     landing.g_force, landing.float_time, landing.bounce_count, landing.bounce_count == 1 ? "" : "s");
        else
            snprintf(stats, sizeof(stats), "  %.0f fpm  |  %.2f G  |  Float %.1f s", landing.fpm, landing.g_force,
                     landing.float_time);
        ImGui::TextUnformatted(stats);

        if (landing.ias_kts > 0.f)
        {
            char speed[64];
            snprintf(speed, sizeof(speed), "  Speed: %.0f kts IAS  |  %.0f kts GS", landing.ias_kts,
                     landing.ground_speed_kts);
            Ui::text_dim(speed);
        }
        if (!landing.runway_ident.empty())
        {
            char runway[128];
            snprintf(runway, sizeof(runway), "  RWY %s  --  %.0f m past threshold  |  %.0f m %s of centerline",
                     landing.runway_ident.c_str(), landing.runway_distance_m, std::abs(landing.runway_offset_m),
                     landing.runway_offset_m > 0 ? "right" : "left");
            Ui::text_dim(runway);
        }
        if (!landing.is_rotorcraft)
            Ui::text_dim(("  Flare: " + landing.flare).c_str());

        draw_wind_status_line(landing);
        if (i + 1 < flight.landings.size())
            ImGui::Separator();
    }
}

void FlightView::draw_detail(const FlightData &flight, float width)
{
    const std::string route = (flight.departure_icao.empty() ? "?" : flight.departure_icao) +
                              "   " ICON_FA_PLANE_DEP "   " + (flight.arrival_icao.empty() ? "?" : flight.arrival_icao);
    ImGui::PushFont(nullptr, 22.f);
    ImGui::TextUnformatted(route.c_str());
    ImGui::PopFont();

    char        info[256];
    const char *rotorcraft = flight.aircraft_category == "rotorcraft" ? "  [Heli]" : "";
    snprintf(info, sizeof(info), "%s  %s-%s UTC  |  %s  %s%s", flight.date.c_str(),
             flight.start_utc.empty() ? "?" : flight.start_utc.c_str(),
             flight.end_utc.empty() ? "?" : flight.end_utc.c_str(), flight.aircraft_icao.c_str(),
             flight.aircraft_tail.c_str(), rotorcraft);
    Ui::text_dim(info);
    ImGui::Separator();

    draw_time_lines(flight);

    const float  cell_w = ImGui::GetContentRegionAvail().x / 3.f;
    const ImVec2 row    = Ui::begin_metric_row();
    Ui::metric_cell("MAX ALT", (std::to_string(flight.max_altitude_ft) + " ft").c_str(), Theme::text, cell_w);
    Ui::metric_cell("MAX SPEED", (std::to_string(flight.max_speed_kts) + " kts").c_str(), Theme::text, cell_w);
    Ui::metric_cell("LANDINGS", std::to_string(flight.landings.size()).c_str(), Theme::text, cell_w);
    Ui::end_metric_row(row);
    ImGui::Separator();

    draw_track_map(flight, width);
    ImGui::Separator();

    draw_landings(flight);
    ImGui::Separator();
}
