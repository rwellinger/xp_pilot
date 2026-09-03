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

// draw_gate — owns the plugin's registration for X-Plane's 2-D drawing phase.
//
// X-Plane skips all OpenGL bookkeeping for a drawing phase no plugin has registered
// for. Once one has, every frame pays for GL state save/restore and, on the Metal
// backend, for synchronising textures across X-Plane's OpenGL bridge — whether or not
// the callback actually draws. A permanently registered callback therefore costs the
// whole session, so registration follows what is genuinely on screen.
namespace DrawGate
{
// Starts watching. Nothing is registered with X-Plane until something needs drawing.
void init();
void stop();

// Re-evaluates what is on screen and registers or unregisters accordingly. Runs every
// frame from a flight loop, and is called directly by whoever puts something on screen
// so the first frame is never missed while the sim is not stepping its flight loops.
//
// Never call this from inside the drawing callback itself: unregistering a callback
// X-Plane is currently iterating over is not safe. Switching off is therefore always
// left to the flight loop, where a frame of delay costs nothing.
void refresh();
} // namespace DrawGate
