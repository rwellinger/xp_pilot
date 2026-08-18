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

#include "ui_widgets.hpp"
#include "ui_theme.hpp"
#include <algorithm>

namespace
{

// Font sizes are given unscaled — ImGui applies the global UI scale on top.
constexpr float TILE_ICON_SIZE  = 46.f;
constexpr float TILE_TITLE_SIZE = 20.f;
constexpr float HEADER_SIZE     = 21.f;

// Drawn straight into the draw list rather than as a widget: the tile's layout
// footprint must stay exactly the InvisibleButton, so nothing here may emit items.
void draw_centered_text(ImDrawList *dl, const ImVec2 &box_min, float box_width, float y, const char *text,
                        const ImVec4 &color)
{
    const float text_width = ImGui::CalcTextSize(text).x;
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(box_min.x + (box_width - text_width) * 0.5f, y),
                ImGui::GetColorU32(color), text);
}

} // namespace

ImVec2 Ui::begin_metric_row() { return ImGui::GetCursorPos(); }

void Ui::end_metric_row(const ImVec2 &row_origin)
{
    ImGui::SetCursorPos(ImVec2(row_origin.x, row_origin.y + ImGui::GetTextLineHeightWithSpacing() * 2.f));
}

void Ui::metric_cell(const char *label, const char *value, const ImVec4 &value_color, float cell_width)
{
    const ImVec2 origin = ImGui::GetCursorPos();

    ImGui::PushStyleColor(ImGuiCol_Text, Theme::text_dim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::SetCursorPosX(origin.x);
    ImGui::PushStyleColor(ImGuiCol_Text, value_color);
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(ImVec2(origin.x + cell_width, origin.y));
}

bool Ui::icon_tile(const char *icon, const char *title, const char *subtitle, const ImVec2 &size)
{
    ImGui::PushID(title);
    const ImVec2 origin  = ImGui::GetCursorScreenPos();
    const bool   pressed = ImGui::InvisibleButton("##tile", size);
    const bool   hovered = ImGui::IsItemHovered();
    const bool   held    = ImGui::IsItemActive();

    // Raised against the panel behind it — the tile must not blend into its container.
    const ImVec4 pressed_fill{Theme::accent.x, Theme::accent.y, Theme::accent.z, 0.35f};
    const ImVec4 fill = held ? pressed_fill : (hovered ? Theme::surface_hover : Theme::surface_raised);
    ImDrawList  *dl   = ImGui::GetWindowDrawList();
    const ImVec2 max{origin.x + size.x, origin.y + size.y};

    dl->AddRectFilled(origin, max, ImGui::GetColorU32(fill), Theme::scaled(12.f));
    dl->AddRect(origin, max, ImGui::GetColorU32(hovered ? Theme::accent : Theme::border), Theme::scaled(12.f), 0,
                hovered ? 2.f : 1.f);

    // Vertical rhythm: icon on the upper third, title under it, subtitle at the foot.
    ImGui::PushFont(nullptr, TILE_ICON_SIZE);
    draw_centered_text(dl, origin, size.x, origin.y + size.y * 0.20f, icon, hovered ? Theme::accent : Theme::text);
    ImGui::PopFont();

    ImGui::PushFont(nullptr, TILE_TITLE_SIZE);
    draw_centered_text(dl, origin, size.x, origin.y + size.y * 0.60f, title, Theme::text);
    ImGui::PopFont();

    if (subtitle && subtitle[0])
        draw_centered_text(dl, origin, size.x, origin.y + size.y * 0.76f, subtitle, Theme::text_dim);

    ImGui::PopID();
    return pressed;
}

bool Ui::view_header(const char *icon, const char *title)
{
    const bool back = ImGui::Button(ICON_FA_CHEVRON_LEFT "  Home");

    ImGui::SameLine(0.f, Theme::scaled(16.f));
    ImGui::PushFont(nullptr, HEADER_SIZE);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::accent);
    ImGui::TextUnformatted(icon);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.f, Theme::scaled(9.f));
    ImGui::TextUnformatted(title);
    ImGui::PopFont();

    return back;
}

void Ui::section_header(const char *icon, const char *label)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::accent);
    ImGui::TextUnformatted(icon);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.f, Theme::scaled(8.f));
    ImGui::SeparatorText(label);
}

void Ui::begin_content_panel(const char *id)
{
    const float height = std::max(Theme::scaled(80.f), ImGui::GetContentRegionAvail().y - Theme::scaled(4.f));
    ImGui::BeginChild(id, ImVec2(0.f, height), ImGuiChildFlags_Borders);
}

void Ui::end_content_panel() { ImGui::EndChild(); }

void Ui::text_dim(const char *text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::text_dim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void Ui::help_marker(const char *tooltip)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
}
