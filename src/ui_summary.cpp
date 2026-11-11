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

#include "ui_summary.hpp"
#include "flight_legs.hpp"
#include "html_common.hpp"
#include "ui_flight_view.hpp"
#include "ui_theme.hpp"
#include "ui_widgets.hpp"
#include <cstdio> // snprintf
#include <imgui.h>
#include <utility>

using FlightSummary::Period;
using SummaryScreen::State;

namespace
{

const char *ALL_AIRCRAFT_LABEL = "All aircraft types";

// The current month leads even when nothing was flown in it yet, so the screen opens on
// it as the issue asks; then every other month, every year and all time.
std::vector<Period> offered_periods(const std::vector<FlightSummary::LoggedFlight> &flights, const Period &current)
{
    std::vector<Period> periods{current};
    for (const Period &month : FlightSummary::available_months(flights))
        if (month != current)
            periods.push_back(month);
    for (const Period &year : FlightSummary::available_years(flights))
        periods.push_back(year);
    periods.push_back(Period::all_time());
    return periods;
}

bool contains(const std::vector<std::string> &values, const std::string &value)
{
    for (const std::string &candidate : values)
        if (candidate == value)
            return true;
    return false;
}

void recompute(State &state)
{
    state.aircraft = FlightSummary::available_aircraft(state.flights, state.filter.period);
    // A type picked for another period may not have flown in this one.
    if (!state.filter.aircraft_icao.empty() && !contains(state.aircraft, state.filter.aircraft_icao))
        state.filter.aircraft_icao.clear();

    state.summary = FlightSummary::summarize(state.flights, state.filter);
    state.route_cells.clear();
    state.route_cells.reserve(state.summary.flight_list.size());
    for (const FlightData &flight : state.summary.flight_list)
        state.route_cells.push_back(FlightLegs::route_text(FlightLegs::derive_route(flight), "-", false));
}

// ── Toolbar ───────────────────────────────────────────────────────────────────

bool draw_period_combo(State &state)
{
    bool changed = false;
    ImGui::SetNextItemWidth(Theme::scaled(180.f));
    if (!ImGui::BeginCombo("##period", state.filter.period.label().c_str()))
        return false;

    for (size_t i = 0; i < state.periods.size(); ++i)
    {
        const Period &period = state.periods[i];
        // Months, years and all time are three groups; a rule between them reads faster
        // than a heading.
        if (i > 0 && period.kind != state.periods[i - 1].kind)
            ImGui::Separator();
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(period.label().c_str(), period == state.filter.period))
        {
            state.filter.period = period;
            changed             = true;
        }
        ImGui::PopID();
    }
    ImGui::EndCombo();
    return changed;
}

bool draw_aircraft_combo(State &state)
{
    bool        changed = false;
    const char *current = state.filter.aircraft_icao.empty() ? ALL_AIRCRAFT_LABEL : state.filter.aircraft_icao.c_str();
    ImGui::SetNextItemWidth(Theme::scaled(180.f));
    if (!ImGui::BeginCombo("##aircraft", current))
        return false;

    if (ImGui::Selectable(ALL_AIRCRAFT_LABEL, state.filter.aircraft_icao.empty()))
    {
        state.filter.aircraft_icao.clear();
        changed = true;
    }
    for (const std::string &icao : state.aircraft)
    {
        if (ImGui::Selectable(icao.c_str(), icao == state.filter.aircraft_icao))
        {
            state.filter.aircraft_icao = icao;
            changed                    = true;
        }
    }
    ImGui::EndCombo();
    return changed;
}

SummaryScreen::Action draw_toolbar(State &state)
{
    bool changed = draw_period_combo(state);
    ImGui::SameLine();
    changed |= draw_aircraft_combo(state);
    if (changed)
        recompute(state);

    ImGui::SameLine();
    const bool open_report = ImGui::Button(ICON_FA_EXTERNAL "  Open Report");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Opens the summary in the browser - print it or save it as PDF from there");

    Ui::text_dim(HtmlCommon::SIM_ONLY_NOTICE);
    if (state.report_failed)
        ImGui::TextColored(Theme::warning, ICON_FA_WARNING "  The report could not be written.");
    else if (!state.report_path.empty())
        Ui::text_dim(("Report written to " + state.report_path).c_str());

    return open_report ? SummaryScreen::Action::OpenReport : SummaryScreen::Action::None;
}

// ── Sections ──────────────────────────────────────────────────────────────────

std::string format_number(const char *format, double value)
{
    char text[32];
    std::snprintf(text, sizeof(text), format, value);
    return text;
}

void draw_totals(const FlightSummary::Summary &summary)
{
    Ui::section_header(ICON_FA_BOOK, "Totals");
    const float  cell_w = ImGui::GetContentRegionAvail().x / 5.f;
    const ImVec2 row    = Ui::begin_metric_row();
    Ui::metric_cell("FLIGHTS", std::to_string(summary.flights).c_str(), Theme::text, cell_w);
    Ui::metric_cell("BLOCK TIME", FlightView::format_duration(summary.block_time_sec / 60).c_str(), Theme::text,
                    cell_w);
    Ui::metric_cell("LANDINGS", std::to_string(summary.landings).c_str(), Theme::text, cell_w);
    Ui::metric_cell("DISTANCE", format_number("%.0f nm", summary.distance_nm).c_str(), Theme::text, cell_w);
    Ui::metric_cell("AIRPORTS", std::to_string(summary.airports).c_str(), Theme::text, cell_w);
    Ui::end_metric_row(row);
}

void draw_rating_bars(const FlightSummary::Summary &summary)
{
    if (!ImGui::BeginTable("ratings", 3, ImGuiTableFlags_SizingFixedFit))
        return;
    ImGui::TableSetupColumn("rating", ImGuiTableColumnFlags_WidthFixed, Theme::scaled(150.f));
    ImGui::TableSetupColumn("bar", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed, Theme::scaled(60.f));

    for (const FlightSummary::Ranked &rating : summary.ratings)
    {
        const ImVec4 color = Theme::rating_color(rating.name);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(color, "%s", rating.name.c_str());
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
        ImGui::ProgressBar(static_cast<float>(rating.count) / static_cast<float>(summary.landings),
                           ImVec2(-1.f, Theme::scaled(12.f)), "");
        ImGui::PopStyleColor();
        ImGui::TableNextColumn();
        ImGui::Text("%d", rating.count);
    }
    ImGui::EndTable();
}

void draw_landing_quality(const FlightSummary::Summary &summary)
{
    Ui::section_header(ICON_FA_PLANE_DEP, "Landing Quality");
    if (summary.landings == 0)
    {
        Ui::text_dim("No landings in this period.");
        return;
    }

    const float  cell_w = ImGui::GetContentRegionAvail().x / 4.f;
    const ImVec2 row    = Ui::begin_metric_row();
    Ui::metric_cell("AVERAGE", format_number("%.0f fpm", summary.average_fpm).c_str(), Theme::text, cell_w);
    Ui::metric_cell("SOFTEST", format_number("%.0f fpm", summary.softest_fpm).c_str(), Theme::text, cell_w);
    Ui::metric_cell("HARDEST", format_number("%.0f fpm", summary.hardest_fpm).c_str(), Theme::text, cell_w);
    Ui::metric_cell("MAX G", format_number("%.2f G", summary.max_g).c_str(), Theme::text, cell_w);
    Ui::end_metric_row(row);
    draw_rating_bars(summary);
}

void draw_aircraft_table(const FlightSummary::Summary &summary)
{
    if (!ImGui::BeginTable("aircraft", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame))
        return;
    ImGui::TableSetupColumn("Type");
    ImGui::TableSetupColumn("Flights");
    ImGui::TableSetupColumn("Time");
    ImGui::TableHeadersRow();
    for (const FlightSummary::AircraftTotals &aircraft : summary.aircraft)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(aircraft.icao.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d", aircraft.flights);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(FlightView::format_duration(aircraft.block_time_sec / 60).c_str());
    }
    ImGui::EndTable();
}

void draw_ranked_table(const char *id, const char *column, const std::vector<FlightSummary::Ranked> &ranked)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame))
        return;
    ImGui::TableSetupColumn(column);
    ImGui::TableSetupColumn("Flights");
    ImGui::TableHeadersRow();
    for (const FlightSummary::Ranked &entry : ranked)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(entry.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d", entry.count);
    }
    ImGui::EndTable();
}

void draw_breakdown(const FlightSummary::Summary &summary)
{
    Ui::section_header(ICON_FA_MAP, "Breakdown");
    if (!ImGui::BeginTable("breakdown", 3, ImGuiTableFlags_SizingStretchSame))
        return;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    draw_aircraft_table(summary);
    ImGui::TableNextColumn();
    draw_ranked_table("top_airports", "Top Airports", summary.top_airports);
    ImGui::TableNextColumn();
    draw_ranked_table("top_routes", "Top Routes", summary.top_routes);
    ImGui::EndTable();
}

void draw_flight_row(const FlightData &flight, const std::string &route)
{
    const std::string rating = flight.landings.empty() ? "" : flight.landings.back().rating;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(flight.date.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(route.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(flight.aircraft_icao.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(FlightView::format_duration(flight.block_time_sec / 60).c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%zu", flight.landings.size());
    ImGui::TableNextColumn();
    ImGui::TextColored(Theme::rating_color(rating), "%s", rating.c_str());
}

void draw_flight_list(const State &state)
{
    Ui::section_header(ICON_FA_FILE_LINES, "Flights");
    if (!ImGui::BeginTable("summary_flights", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame))
        return;
    ImGui::TableSetupColumn("Date");
    ImGui::TableSetupColumn("Route");
    ImGui::TableSetupColumn("Type");
    ImGui::TableSetupColumn("Time");
    ImGui::TableSetupColumn("Landings");
    ImGui::TableSetupColumn("Last Landing");
    ImGui::TableHeadersRow();

    // All time can run into thousands of rows; only the visible ones are drawn.
    const std::vector<FlightData> &flights = state.summary.flight_list;
    ImGuiListClipper               clipper;
    clipper.Begin(static_cast<int>(flights.size()));
    while (clipper.Step())
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            draw_flight_row(flights[i], state.route_cells[i]);
    ImGui::EndTable();
}

} // namespace

void SummaryScreen::load(State &state, std::vector<FlightSummary::LoggedFlight> flights, time_t now)
{
    const Period current = FlightSummary::current_month(now);
    state.flights        = std::move(flights);
    state.periods        = offered_periods(state.flights, current);
    state.filter         = {current, ""};
    state.report_path.clear();
    state.report_failed = false;
    recompute(state);
}

SummaryScreen::Action SummaryScreen::draw(State &state)
{
    const Action action = draw_toolbar(state);
    ImGui::Separator();

    Ui::begin_content_panel("summary_panel");
    if (state.summary.flights == 0)
        Ui::text_dim("No flights in this period.");
    else
    {
        draw_totals(state.summary);
        draw_landing_quality(state.summary);
        draw_breakdown(state.summary);
        draw_flight_list(state);
    }
    Ui::end_content_panel();
    return action;
}
