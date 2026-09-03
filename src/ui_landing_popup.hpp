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
#include "flight_logger.hpp" // PopupPosition
#include "html_report.hpp"   // LandingData

// The landing rating popup: an ImGui window summoned after touchdown and dismissed
// on a timeout. Reads a landing it is handed; it never touches the flight log itself.
namespace LandingPopup
{

// Post a fresh touchdown. Shows the popup, or — while the feature is switched off —
// only remembers the landing so replay() can still summon it later.
void post(const LandingData &landing);

// Show a landing for the usual 15 seconds, regardless of the toggle. Used by the
// replay command, which is an explicit request.
void show(const LandingData &landing);

// Re-show the landing remembered from this session. False when there is none, in
// which case the caller falls back to the newest logged flight.
bool replay_remembered();

// True while the popup should be on screen; also expires it once its time is up.
bool active();

// Call from inside an ImGui frame.
void draw();

// Owns the popup's settings, persisted via the settings file.
void          set_enabled(bool on);
bool          enabled();
void          set_position(PopupPosition position);
PopupPosition position();

} // namespace LandingPopup
