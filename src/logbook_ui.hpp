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

// logbook_ui — Dear ImGui logbook window inside a XPLMCreateWindowEx window.
// Opens/closes via menu or keyboard command.

namespace LogbookUI
{
void init(); // call after FlightLogger::init()
void stop();
void toggle();
void draw(); // call every frame from main draw callback

// Restore the default UI scale and recentre the window at its default size. Reachable
// from the plugin menu and a command, so an oversized window can always be recovered
// without editing the settings file.
void reset_layout();
} // namespace LogbookUI
