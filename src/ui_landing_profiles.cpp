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


#include "ui_landing_profiles.hpp"
#include "flight_logger.hpp"
#include "flight_logger_logic.hpp"
#include "settings.hpp"
#include "ui_theme.hpp"
#include "ui_widgets.hpp"
#include <array>
#include <cstdio>
#include <imgui.h>
#include <string>
#include <vector>

using FlightLoggerLogic::ProfileOverride;

namespace
{

// The four rating steps in the order the thresholds are stored.
constexpr const char *THRESHOLD_LABELS[4] = {"Butter", "Great", "Acceptable", "Hard"};

// The assignment dropdown lists "Automatic" first and "Custom thresholds" last, with the
// bundled profiles in between.
constexpr int AUTOMATIC_CHOICE = 0;

// The threshold editor is only committed on Apply, so its values live here until then.
// They belong to one aircraft: switching aircraft or assignment reseeds them rather than
// carrying another type's numbers over.
std::string        s_editor_icao;
std::array<int, 4> s_editor_thresholds{};
bool               s_editor_open = false;

std::string thresholds_text(const std::array<int, 4> &thresholds)
{
    char text[64];
    snprintf(text, sizeof(text), "%d / %d / %d / %d", thresholds[0], thresholds[1], thresholds[2], thresholds[3]);
    return text;
}

const char *source_text(FlightLogger::ProfileSource source)
{
    switch (source)
    {
    case FlightLogger::ProfileSource::UserOverride:
        return "your assignment";
    case FlightLogger::ProfileSource::IcaoList:
        return "bundled aircraft list";
    case FlightLogger::ProfileSource::AirframeClass:
        return "airframe (mass, engines)";
    case FlightLogger::ProfileSource::Fallback:
        return "default, airframe unknown";
    }
    return "";
}

void open_editor(const std::string &aircraft_icao, const std::array<int, 4> &seed)
{
    s_editor_icao       = aircraft_icao;
    s_editor_thresholds = seed;
    s_editor_open       = true;
}

void close_editor()
{
    s_editor_icao.clear();
    s_editor_open = false;
}

// Which entry of the dropdown the current assignment corresponds to.
int selected_choice(const std::string &aircraft_icao, const std::vector<std::string> &profile_names)
{
    const auto *assigned = FlightLoggerLogic::find_profile_override(FlightLogger::profile_overrides(), aircraft_icao);
    if (!assigned)
        return s_editor_open && s_editor_icao == aircraft_icao ? static_cast<int>(profile_names.size()) + 1
                                                               : AUTOMATIC_CHOICE;
    if (assigned->is_custom)
        return static_cast<int>(profile_names.size()) + 1;

    for (int index = 0; index < static_cast<int>(profile_names.size()); ++index)
        if (profile_names[index] == assigned->profile_name)
            return index + 1;
    return AUTOMATIC_CHOICE;
}

void apply_choice(int choice, const FlightLogger::CurrentAircraft &aircraft,
                  const std::vector<std::string> &profile_names)
{
    if (choice == AUTOMATIC_CHOICE)
    {
        close_editor();
        FlightLogger::clear_profile_override(aircraft.icao);
        Settings::save();
        return;
    }

    if (choice == static_cast<int>(profile_names.size()) + 1)
    {
        // Seeded with what the aircraft is rated against right now, so the editor starts
        // from the values the user is reacting to rather than from nothing.
        open_editor(aircraft.icao, aircraft.profile.thresholds);
        return;
    }

    close_editor();
    ProfileOverride assignment;
    assignment.profile_name = profile_names[choice - 1];
    FlightLogger::set_profile_override(aircraft.icao, assignment);
    Settings::save();
}

void draw_assignment_dropdown(const FlightLogger::CurrentAircraft &aircraft,
                              const std::vector<std::string>      &profile_names)
{
    const int   choice      = selected_choice(aircraft.icao, profile_names);
    const int   custom_entry = static_cast<int>(profile_names.size()) + 1;
    const char *preview      = choice == AUTOMATIC_CHOICE ? "Automatic"
                               : choice == custom_entry   ? "Custom thresholds"
                                                          : profile_names[choice - 1].c_str();

    ImGui::SetNextItemWidth(Theme::scaled(200.f));
    if (ImGui::BeginCombo("Landing profile", preview))
    {
        if (ImGui::Selectable("Automatic", choice == AUTOMATIC_CHOICE))
            apply_choice(AUTOMATIC_CHOICE, aircraft, profile_names);

        for (int index = 0; index < static_cast<int>(profile_names.size()); ++index)
            if (ImGui::Selectable(profile_names[index].c_str(), choice == index + 1))
                apply_choice(index + 1, aircraft, profile_names);

        if (ImGui::Selectable("Custom thresholds", choice == custom_entry))
            apply_choice(custom_entry, aircraft, profile_names);

        ImGui::EndCombo();
    }
    ImGui::SameLine();
    Ui::help_marker("Applies to this aircraft type only, matched on its exact ICAO code.\n"
                    "Automatic uses the bundled aircraft list, or the airframe data\n"
                    "when the type is not listed.");
}

void draw_threshold_editor(const FlightLogger::CurrentAircraft &aircraft)
{
    // An aircraft that already carries custom thresholds — from an earlier session, or
    // from a hand-edited file — opens the editor on its stored values, so they can be
    // seen and adjusted without being re-entered from scratch.
    const auto *assigned = FlightLoggerLogic::find_profile_override(FlightLogger::profile_overrides(), aircraft.icao);
    if (assigned && assigned->is_custom && s_editor_icao != aircraft.icao)
        open_editor(aircraft.icao, assigned->thresholds);

    if (!s_editor_open || s_editor_icao != aircraft.icao)
        return;

    ImGui::Indent();
    ImGui::TextUnformatted("Descent rate at or below which a landing still earns each grade (fpm):");

    for (int index = 0; index < 4; ++index)
    {
        ImGui::SetNextItemWidth(Theme::scaled(140.f));
        ImGui::InputInt(THRESHOLD_LABELS[index], &s_editor_thresholds[index], 25, 100);
    }

    const bool valid = FlightLoggerLogic::are_valid_thresholds(s_editor_thresholds);
    if (!valid)
    {
        char message[160];
        snprintf(message, sizeof(message),
                 ICON_FA_WARNING "  Values must be negative, get harsher step by step, and stay between %d and %d fpm.",
                 FlightLoggerLogic::MAX_THRESHOLD_FPM, FlightLoggerLogic::MIN_THRESHOLD_FPM);
        ImGui::TextColored(Theme::warning, "%s", message);
    }

    ImGui::BeginDisabled(!valid);
    if (ImGui::Button(ICON_FA_CHECK "  Apply"))
    {
        ProfileOverride assignment;
        assignment.thresholds = s_editor_thresholds;
        assignment.is_custom  = true;
        FlightLogger::set_profile_override(aircraft.icao, assignment);
        Settings::save();
    }
    ImGui::EndDisabled();

    if (aircraft.is_rotorcraft)
    {
        ImGui::SameLine();
        Ui::help_marker("For a helicopter these values grade the descent rate only.\n"
                        "Drift, bank, yaw rate and G-force keep their fixed limits.");
    }

    ImGui::Unindent();
}

bool names_bundled_profile(const std::string &profile_name)
{
    for (const auto &name : FlightLogger::available_profile_names())
        if (name == profile_name)
            return true;
    return false;
}

void draw_assignment_list()
{
    const auto &assignments = FlightLogger::profile_overrides();
    if (assignments.empty())
        return;

    ImGui::Spacing();
    Ui::text_dim("Your assignments");

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH;
    if (!ImGui::BeginTable("landing_profile_assignments", 3, flags))
        return;

    ImGui::TableSetupColumn("Aircraft", ImGuiTableColumnFlags_WidthFixed, Theme::scaled(90.f));
    ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, Theme::scaled(34.f));
    ImGui::TableHeadersRow();

    // Removal happens after the loop: erasing from the map the loop walks would
    // invalidate it mid-frame.
    std::string aircraft_to_remove;
    for (const auto &[aircraft_icao, assignment] : assignments)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(aircraft_icao.c_str());

        ImGui::TableNextColumn();
        if (assignment.is_custom)
            ImGui::Text("custom  %s", thresholds_text(assignment.thresholds).c_str());
        else if (names_bundled_profile(assignment.profile_name))
            ImGui::Text("%s  %s", assignment.profile_name.c_str(),
                        thresholds_text(FlightLogger::profile_thresholds(assignment.profile_name)).c_str());
        else
            // Only reachable from a hand-edited settings file: the resolution ignores it,
            // and saying so beats printing thresholds it never applies.
            ImGui::TextColored(Theme::warning, "%s  unknown profile, ignored", assignment.profile_name.c_str());

        ImGui::TableNextColumn();
        ImGui::PushID(aircraft_icao.c_str());
        if (ImGui::SmallButton(ICON_FA_TRASH))
            aircraft_to_remove = aircraft_icao;
        ImGui::PopID();
    }
    ImGui::EndTable();

    if (!aircraft_to_remove.empty())
    {
        if (aircraft_to_remove == s_editor_icao)
            close_editor();
        FlightLogger::clear_profile_override(aircraft_to_remove);
        Settings::save();
    }
}

} // namespace

void LandingProfilesSection::draw()
{
    Ui::section_header(ICON_FA_PLANE_DEP, "Landing Profiles");

    const auto aircraft = FlightLogger::current_aircraft();
    if (aircraft.icao.empty())
    {
        // Every assignment is keyed on the ICAO code, so there is nothing to assign to.
        Ui::text_dim("The loaded aircraft reports no ICAO type code, so no profile can be assigned to it.");
        draw_assignment_list();
        return;
    }

    ImGui::Text("Aircraft:  %s%s%s", aircraft.icao.c_str(), aircraft.tail.empty() ? "" : "   ",
                aircraft.tail.c_str());
    ImGui::Text("Rated against:  %s   %s", aircraft.profile.name.c_str(),
                thresholds_text(aircraft.profile.thresholds).c_str());
    ImGui::Text("Source:  %s", source_text(aircraft.profile.source));

    ImGui::Spacing();
    const auto profile_names = FlightLogger::available_profile_names();
    draw_assignment_dropdown(aircraft, profile_names);
    draw_threshold_editor(aircraft);
    draw_assignment_list();
}
