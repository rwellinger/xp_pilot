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

// User settings, persisted as JSON under X-Plane's Output dir. Each setting is owned
// by the module implementing its feature; this is only the binding to the file.
namespace Settings
{

// Apply settings.json over the module defaults. Missing keys and an unreadable or
// malformed file leave the defaults in place. Requires FlightLogger::init() to have
// run, because the path is derived from its output dir.
void load();

// Write every setting. Called after each change — the whole file is rewritten.
void save();

} // namespace Settings
