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

#include "ui_flight_list.hpp"
#include "flight_logger.hpp"
#include "ui_flight_view.hpp"
#include "ui_theme.hpp"
#include "ui_widgets.hpp"
#include <XPLM/XPLMUtilities.h>
#include <algorithm>
#include <cmath>
// Keep explicit: std::remove(const char*) comes from here, and <algorithm> declares
// an unrelated std::remove that would be picked instead.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <imgui.h>
#include <iterator>
#include <utility>

using FlightListScreen::FlightList;

namespace
{

std::string flights_dir(const std::string &subdir) { return FlightLogger::output_dir() + "flights/" + subdir; }

std::string reports_dir(const std::string &subdir) { return FlightLogger::output_dir() + "reports/" + subdir; }

std::string report_path_for(const std::string &subdir, const std::string &flight_filename)
{
    return reports_dir(subdir) + flight_filename.substr(0, flight_filename.rfind('.')) + ".html";
}

// Every *.json in `dir` (non-recursive), parsed as a FlightData without its track
// samples, newest first — the filenames start with the date.
std::vector<FlightData> read_flight_summaries(const std::string &dir)
{
    std::vector<FlightData> summaries;

    std::vector<std::string> filenames;
    std::error_code          ec;
    auto                     entries = std::filesystem::directory_iterator(dir, ec);
    if (ec)
        return summaries;
    for (const auto &entry : entries)
    {
        if (!entry.is_regular_file())
            continue;
        const std::string name = entry.path().filename().string();
        if (name.size() > 5 && name.substr(name.size() - 5) == ".json")
            filenames.push_back(name);
    }
    std::sort(filenames.begin(), filenames.end(), std::greater<std::string>());

    for (const auto &filename : filenames)
    {
        std::ifstream file(dir + filename);
        if (!file.is_open())
            continue;
        std::string content((std::istreambuf_iterator<char>(file)), {});
        FlightData  flight = parse_flight_json(content, filename);
        flight.track.clear(); // summary only — keep memory low
        summaries.push_back(std::move(flight));
    }
    return summaries;
}

FlightData read_flight_detail(const std::string &dir, const std::string &filename)
{
    std::ifstream file(dir + filename);
    if (!file.is_open())
        return {};
    std::string content((std::istreambuf_iterator<char>(file)), {});
    return parse_flight_json(content, filename);
}

// Move JSON + HTML from active to archived. Returns true if the JSON move succeeded;
// the report move is best-effort because older flights may not have one.
bool archive_flight(const std::string &filename)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::rename(flights_dir("") + filename, flights_dir("archived/") + filename, ec);
    if (ec)
    {
        XPLMDebugString(("[xp_pilot] archive: cannot move " + filename + ": " + ec.message() + "\n").c_str());
        return false;
    }
    ec.clear();
    fs::rename(report_path_for("", filename), report_path_for("archived/", filename), ec);
    return true;
}

void delete_flight_files(const std::string &subdir, const std::string &filename)
{
    std::remove((flights_dir(subdir) + filename).c_str());
    std::remove(report_path_for(subdir, filename).c_str());
}

int count_checked(const std::vector<bool> &flags)
{
    int checked = 0;
    for (bool flag : flags)
        if (flag)
            ++checked;
    return checked;
}

void clear_selection(FlightList &list)
{
    list.selected               = -1;
    list.detail_loaded          = false;
    list.confirm_delete_single  = false;
    list.confirm_archive_single = false;
}

void select_flight(FlightList &list, int index)
{
    if (list.selected == index)
        return;

    const std::string &filename = list.entries[index].filename;
    list.selected               = index;
    list.detail                 = read_flight_detail(flights_dir(list.subdir), filename);
    list.detail_loaded          = true;
    list.report_path            = report_path_for(list.subdir, filename);
    list.report_exists          = std::ifstream(list.report_path).good();
    list.confirm_delete_single  = false;
    list.confirm_archive_single = false;
}

// ── Toolbar ───────────────────────────────────────────────────────────────────

// Returns true when flights were archived.
bool draw_batch_actions(FlightList &list, int checked)
{
    bool archived = false;

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    char label[64];

    if (list.confirm_batch_archive)
    {
        snprintf(label, sizeof(label), "Archive %d flights?", checked);
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CHECK "  Yes, archive"))
        {
            for (size_t i = 0; i < list.entries.size(); ++i)
                if (list.checked[i])
                    archive_flight(list.entries[i].filename);
            HtmlReport::generate_index(FlightLogger::output_dir());
            FlightListScreen::reload(list);
            archived = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_XMARK "  Cancel"))
            list.confirm_batch_archive = false;
        return archived;
    }

    if (list.confirm_batch_delete)
    {
        snprintf(label, sizeof(label), "Delete %d flights (JSON + report)?", checked);
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CHECK "  Yes, delete"))
        {
            for (size_t i = 0; i < list.entries.size(); ++i)
                if (list.checked[i])
                    delete_flight_files(list.subdir, list.entries[i].filename);
            HtmlReport::generate_index(FlightLogger::output_dir());
            FlightListScreen::reload(list);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_XMARK "  Cancel"))
            list.confirm_batch_delete = false;
        return archived;
    }

    if (list.allow_archive)
    {
        snprintf(label, sizeof(label), ICON_FA_ARCHIVE "  Archive selected (%d)", checked);
        if (ImGui::Button(label))
        {
            list.confirm_batch_archive = true;
            list.confirm_batch_delete  = false;
        }
        ImGui::SameLine();
    }
    snprintf(label, sizeof(label), ICON_FA_TRASH "  Delete selected (%d)", checked);
    if (ImGui::Button(label))
    {
        list.confirm_batch_delete  = true;
        list.confirm_batch_archive = false;
    }
    return archived;
}

bool draw_toolbar(FlightList &list)
{
    bool archived = false;

    if (ImGui::Button(ICON_FA_ROTATE "  Refresh"))
        FlightListScreen::reload(list);

    if (list.allow_archive)
    {
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FILE_LINES "  Rebuild All Reports"))
            FlightLogger::regen_all_reports();
    }

    if (!list.entries.empty())
    {
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SQUARE_CHECK "  Select all"))
            list.checked.assign(list.entries.size(), true);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SQUARE "  Clear"))
            list.checked.assign(list.entries.size(), false);
    }

    const int checked = count_checked(list.checked);
    if (checked > 0)
        archived = draw_batch_actions(list, checked);

    return archived;
}

// ── Panels ────────────────────────────────────────────────────────────────────

void draw_list_panel(FlightList &list, const ImVec2 &size)
{
    ImGui::BeginChild("flight_list", size, ImGuiChildFlags_Borders);

    if (list.entries.empty())
    {
        Ui::text_dim(list.allow_archive ? "No flights found." : "No archived flights.");
        ImGui::EndChild();
        return;
    }

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("flights", 5, flags))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##sel", ImGuiTableColumnFlags_WidthFixed, Theme::scaled(24.f));
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Route", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(list.entries.size()); ++i)
        {
            const FlightData &entry = list.entries[i];
            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableNextColumn();
            bool checked = list.checked[i];
            if (ImGui::Checkbox("##sel", &checked))
                list.checked[i] = checked;

            ImGui::TableNextColumn();
            if (ImGui::Selectable(entry.date.c_str(), list.selected == i, ImGuiSelectableFlags_SpanAllColumns))
                select_flight(list, i);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted((entry.departure_icao + "-" + entry.arrival_icao).c_str());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.aircraft_icao.c_str());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FlightView::format_duration(entry.block_time_min).c_str());

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

// Returns true when the selected flight was archived.
bool draw_detail_actions(FlightList &list)
{
    bool archived = false;

    if (list.report_exists)
    {
        if (ImGui::Button(ICON_FA_EXTERNAL "  Open Report"))
            FlightView::open_in_browser(list.report_path);
        ImGui::SameLine();
    }

    if (list.allow_archive)
    {
        if (list.confirm_archive_single)
        {
            ImGui::TextUnformatted("Archive this flight?");
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_CHECK "  Yes, archive"))
            {
                archive_flight(list.entries[list.selected].filename);
                HtmlReport::generate_index(FlightLogger::output_dir());
                FlightListScreen::reload(list);
                return true;
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_XMARK "  Cancel##archive"))
                list.confirm_archive_single = false;
        }
        else if (ImGui::Button(ICON_FA_ARCHIVE "  Archive"))
        {
            list.confirm_archive_single = true;
            list.confirm_delete_single  = false;
        }
        ImGui::SameLine();
    }

    if (list.confirm_delete_single)
    {
        ImGui::TextUnformatted("Really delete?");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CHECK "  Yes, delete"))
        {
            delete_flight_files(list.subdir, list.entries[list.selected].filename);
            HtmlReport::generate_index(FlightLogger::output_dir());
            FlightListScreen::reload(list);
            return archived;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_XMARK "  Cancel##delete"))
            list.confirm_delete_single = false;
    }
    else if (ImGui::Button(ICON_FA_TRASH "  Delete"))
    {
        list.confirm_delete_single  = true;
        list.confirm_archive_single = false;
    }

    return archived;
}

} // namespace

void FlightListScreen::reload(FlightList &list)
{
    list.entries = read_flight_summaries(flights_dir(list.subdir));
    list.checked.assign(list.entries.size(), false);
    list.confirm_batch_archive = false;
    list.confirm_batch_delete  = false;
    list.loaded                = true;
    clear_selection(list);
}

void FlightListScreen::ensure_loaded(FlightList &list)
{
    if (!list.loaded)
        reload(list);
}

size_t FlightListScreen::count_on_disk(const std::string &subdir)
{
    std::error_code ec;
    auto            entries = std::filesystem::directory_iterator(flights_dir(subdir), ec);
    if (ec)
        return 0;

    size_t count = 0;
    for (const auto &entry : entries)
    {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && name.size() > 5 && name.substr(name.size() - 5) == ".json")
            ++count;
    }
    return count;
}

bool FlightListScreen::draw(FlightList &list)
{
    ensure_loaded(list);

    bool archived = draw_toolbar(list);
    ImGui::Separator();

    const float available_w = ImGui::GetContentRegionAvail().x;
    const float available_h = ImGui::GetContentRegionAvail().y;
    const float panel_h     = std::max(Theme::scaled(100.f), available_h - Theme::scaled(4.f));
    const float list_w      = std::floor(available_w * 0.34f);
    const float detail_w    = std::max(Theme::scaled(100.f), available_w - list_w - Theme::scaled(10.f));

    draw_list_panel(list, ImVec2(list_w, panel_h));
    ImGui::SameLine();

    ImGui::BeginChild("flight_detail", ImVec2(detail_w, panel_h), ImGuiChildFlags_Borders);
    if (!list.detail_loaded || list.selected < 0 || list.selected >= static_cast<int>(list.entries.size()))
    {
        Ui::text_dim(list.allow_archive ? "Select a flight..." : "Select an archived flight...");
    }
    else
    {
        FlightView::draw_detail(list.detail, detail_w);
        archived |= draw_detail_actions(list);
    }
    ImGui::EndChild();

    return archived;
}
