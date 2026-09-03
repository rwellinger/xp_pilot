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
#include <string>

// Transient status text drawn over the sim by X-Plane's own text renderer, not ImGui.
// One message at a time; a new one replaces whatever is still showing.
namespace Overlay
{

// Show text for `seconds`. Silently does nothing while messages are disabled.
void show(const std::string &text, float seconds, float red = 1.f, float green = 1.f, float blue = 1.f);

// Call from a registered XPLM draw callback.
void draw();

// True while a message is still due on screen; expires a message whose time is up.
bool visible();

// Owns the "show status messages" toggle, persisted via the settings file. Gating happens
// in show(), so a disabled overlay stays silent no matter which module posts to it.
void set_enabled(bool on);
bool enabled();

} // namespace Overlay
