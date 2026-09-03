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

// auto_qnh — Automatic QNH setting for X-Plane 12.
// Port of "automatic set qnh.lua" by Ingo Alm / Carsten Lynker (2012, updated XP12).
//
// Behaviour:
//   Below transition altitude:
//     Auto mode silently syncs pilot+copilot barometer to actual sea-level pressure.
//     Skipped if pilot is on standard pressure (29.92).
//     Warning "CHECK ALTIMETER - QNH not set" when drift > threshold.
//   Above transition altitude:
//     No auto-correction (pilot should be on STD up here).
//     Warning "CHECK ALTIMETER - SET STD 29.92" until pilot switches to standard.
//   Manual:    XPLMCommandRef "xp_pilot/qnh/set_qnh"        — one-shot set QNH
//              XPLMCommandRef "xp_pilot/qnh/set_flightlevel" — set 29.92
//   Copilot mismatch warning: drawn when PF/PM disagree by >0.01 inHg.

namespace AutoQNH
{
void init();
void stop();
void draw();               // called from draw callback — renders warning text if needed

// True while draw() would put something on screen. The drawing callback is registered
// on the strength of this, so it must not disagree with what draw() actually does.
bool warning_visible();
bool enabled();            // current feature state
void set_enabled(bool on); // set feature state (used by settings persistence)

// Toggles whether on-screen QNH warning text is drawn. Independent of enabled():
// auto-correction can run silently while messages are suppressed.
void set_messages_enabled(bool on);
bool messages_enabled();

// Transition altitude in feet (indicated). USA fixed at 18000; Europe varies per
// airport (3000–18000). User-configurable in the settings UI; persisted to JSON.
void set_transition_altitude_ft(int ft);
int  transition_altitude_ft();
} // namespace AutoQNH
