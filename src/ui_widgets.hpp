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

#pragma once
#include <imgui.h>

// Shared visual building blocks. Anything drawn in more than one place lives here
// so a design change stays a one-line edit.
namespace Ui
{

// A row of metrics: begin_metric_row() remembers where the row starts, each
// metric_cell() draws a dim label above its value and advances horizontally, and
// end_metric_row() drops the cursor below the whole row.
//
//     const ImVec2 row = Ui::begin_metric_row();
//     Ui::metric_cell("ALT", "5400 ft", Theme::text, cell_w);
//     Ui::end_metric_row(row);
ImVec2 begin_metric_row();
void   metric_cell(const char *label, const char *value, const ImVec4 &value_color, float cell_width);
void   end_metric_row(const ImVec2 &row_origin);

// Large square button for the home screen: centred icon, title, dim subtitle.
// Behaves like ImGui::Button() — returns true on click.
bool icon_tile(const char *icon, const char *title, const char *subtitle, const ImVec2 &size);

// Screen header: back arrow, icon and title. Returns true when the user wants to
// go back to the home screen.
bool view_header(const char *icon, const char *title);

// Section heading inside a screen — an icon, a bright label and a rule.
void section_header(const char *icon, const char *label);

// Bordered panel filling the rest of the window, so every screen ends on the same
// edge instead of letting its content trail off into empty space. Also gives the
// screen its own scrollbar when the content outgrows the window.
void begin_content_panel(const char *id);
void end_content_panel();

void text_dim(const char *text);

// The (?) marker with a hover tooltip used throughout the settings screen.
void help_marker(const char *tooltip);

} // namespace Ui
