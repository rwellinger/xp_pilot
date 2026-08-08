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
#include "html_report.hpp"
#include <array>
#include <map>
#include <string>
#include <vector>

namespace FlightLogger
{

// ── Public lifecycle ──────────────────────────────────────────────────────────
void init();
void stop();

// ── Draw callbacks (call from registered XPLM draw callback) ─────────────────
void draw_overlay();
void draw_popup();
bool popup_active();

// ── Logbook access ────────────────────────────────────────────────────────────
// User data root (flights, reports, index, settings) under X-Plane's Output dir.
const std::string &output_dir();
bool              &lb_needs_refresh();
void               regen_all_reports();

// ── Settings ──────────────────────────────────────────────────────────────────
void set_write_enabled(bool on);
bool write_enabled();
void set_html_report_enabled(bool on);
bool html_report_enabled();
void set_messages_enabled(bool on);
bool messages_enabled();
void set_landing_popup_enabled(bool on);
bool landing_popup_enabled();

// ── Profile access (for HTML report generation) ───────────────────────────────
std::string        get_profile_name(const std::string &plane_icao);
std::array<int, 4> get_profile_thresholds(const std::string &profile_name);
std::string        get_profile_category(const std::string &profile_name); // "fixed_wing" | "rotorcraft"

} // namespace FlightLogger
