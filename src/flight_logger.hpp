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
const std::string &data_dir();
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

} // namespace FlightLogger
