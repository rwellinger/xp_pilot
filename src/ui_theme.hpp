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

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <string>

// Icons from the embedded Font Awesome 6 Free Solid subset. Adding one here also
// means adding its codepoint to ICON_CODEPOINTS in tools/generate_fonts.sh and
// regenerating src/fonts/fa_solid.hpp.
#define ICON_FA_CHECK "\xef\x80\x8c"        // f00c
#define ICON_FA_XMARK "\xef\x80\x8d"        // f00d
#define ICON_FA_GEAR "\xef\x80\x93"         // f013
#define ICON_FA_ROTATE "\xef\x80\xa1"       // f021
#define ICON_FA_BOOK "\xef\x80\xad"         // f02d
#define ICON_FA_CHEVRON_LEFT "\xef\x81\x93" // f053
#define ICON_FA_ARROW_LEFT "\xef\x81\xa0"   // f060
#define ICON_FA_WARNING "\xef\x81\xaa"      // f06a
#define ICON_FA_SQUARE "\xef\x83\x88"       // f0c8
#define ICON_FA_CIRCLE "\xef\x84\x91"       // f111
#define ICON_FA_SQUARE_CHECK "\xef\x85\x8a" // f14a
#define ICON_FA_FILE_LINES "\xef\x85\x9c"   // f15c
#define ICON_FA_ARCHIVE "\xef\x86\x87"      // f187
#define ICON_FA_TRASH "\xef\x87\xb8"        // f1f8
#define ICON_FA_MAP "\xef\x89\xb9"          // f279
#define ICON_FA_STOPWATCH "\xef\x8b\xb2"    // f2f2
#define ICON_FA_EXTERNAL "\xef\x8d\x9d"     // f35d
#define ICON_FA_PLANE_DEP "\xef\x96\xb0"    // f5b0

namespace Theme
{

// ── Palette ───────────────────────────────────────────────────────────────────
// One dark EFB-style scheme. Every colour the plugin draws comes from here.
inline constexpr ImVec4 background{0.078f, 0.094f, 0.122f, 0.97f};
inline constexpr ImVec4 surface{0.110f, 0.133f, 0.173f, 1.00f};
inline constexpr ImVec4 surface_raised{0.145f, 0.176f, 0.224f, 1.00f};
inline constexpr ImVec4 surface_hover{0.196f, 0.239f, 0.302f, 1.00f};
inline constexpr ImVec4 border{0.220f, 0.263f, 0.325f, 1.00f};

inline constexpr ImVec4 text{0.894f, 0.914f, 0.949f, 1.00f};
inline constexpr ImVec4 text_dim{0.545f, 0.600f, 0.686f, 1.00f};

inline constexpr ImVec4 accent{0.239f, 0.545f, 0.992f, 1.00f};
inline constexpr ImVec4 success{0.208f, 0.769f, 0.420f, 1.00f};
inline constexpr ImVec4 warning{0.961f, 0.647f, 0.141f, 1.00f};
inline constexpr ImVec4 danger{0.957f, 0.282f, 0.243f, 1.00f};
inline constexpr ImVec4 recording{0.957f, 0.282f, 0.243f, 1.00f};

// Track map
inline constexpr ImU32 map_background = IM_COL32(24, 30, 40, 255);
// Track colour ramps with altitude, from ground level to the flight's ceiling.
inline constexpr ImU32 map_track_low  = IM_COL32(0, 110, 180, 255);
inline constexpr ImU32 map_track_high = IM_COL32(130, 235, 255, 255);
inline constexpr ImU32 map_pause      = IM_COL32(255, 204, 0, 255);
inline constexpr ImU32 map_departure  = IM_COL32(64, 255, 0, 255);
inline constexpr ImU32 map_arrival    = IM_COL32(80, 150, 255, 255);
inline constexpr ImU32 map_scale      = IM_COL32(139, 153, 175, 255); // text_dim, as ImDrawList wants ImU32

// Airspace outlines, drawn faintly beneath the track: controlled airspace in blue,
// restricted/prohibited/danger areas in red — the convention on aviation charts.
inline constexpr ImU32 map_airspace_controlled = IM_COL32(90, 140, 220, 110);
inline constexpr ImU32 map_airspace_restricted = IM_COL32(220, 90, 90, 110);

// Landing rating colours, shared by the logbook, the detail view and the popup.
ImVec4 rating_color(const std::string &rating);

// ── Fonts ─────────────────────────────────────────────────────────────────────
inline constexpr float font_size_base = 17.f;

// Loads the embedded Roboto + Font Awesome atlas and applies the style.
// Call once, before the renderer backend is initialised.
void init();

// Global UI scale, applied to fonts and to every layout constant that goes through
// scaled(). Persisted in settings.json. set_ui_scale() clamps to the range below, so
// no caller can drive the interface to an unusable size.
inline constexpr float ui_scale_min     = 0.8f;
inline constexpr float ui_scale_max     = 2.0f;
inline constexpr float ui_scale_default = 1.0f;
// 5% steps: fine enough to tune a TV-distance setup (0.85 is a real-world setting),
// coarse enough that a click is still a deliberate step rather than a drag.
inline constexpr float ui_scale_step    = 0.05f;

float ui_scale();
void  set_ui_scale(float scale);
void  reset_ui_scale();

// Next scale one step up (direction +1) or down (-1), snapped back onto the step grid
// and clamped. Stepping beats dragging here: the scale applies live, so a slider moves
// out from under the cursor while being dragged, which makes overshooting to the
// maximum almost unavoidable.
inline float stepped_ui_scale(float current, int direction)
{
    const float snapped = std::round(current / ui_scale_step) * ui_scale_step;
    return std::clamp(snapped + static_cast<float>(direction) * ui_scale_step, ui_scale_min, ui_scale_max);
}

// Layout constants are authored at scale 1.0 and passed through this.
inline float scaled(float value) { return value * ui_scale(); }

// ── Window fitting ────────────────────────────────────────────────────────────
// A window is never allowed to outgrow the screen. At a large UI scale its title bar
// and resize grip would otherwise sit off-screen, leaving no way to move or shrink it —
// including no way back to the scale setting responsible.
inline constexpr float window_screen_fraction = 0.95f;

struct WindowFit
{
    ImVec2 size;     // initial size, clamped to the screen
    ImVec2 pos;      // centred on the screen
    ImVec2 max_size; // ceiling for the user's own resizing
};

inline WindowFit fit_window_to_screen(float desired_w, float desired_h, float screen_w, float screen_h)
{
    WindowFit fit;
    fit.max_size = ImVec2(screen_w * window_screen_fraction, screen_h * window_screen_fraction);
    fit.size     = ImVec2(std::min(desired_w, fit.max_size.x), std::min(desired_h, fit.max_size.y));
    fit.pos      = ImVec2((screen_w - fit.size.x) * 0.5f, (screen_h - fit.size.y) * 0.5f);
    return fit;
}

} // namespace Theme
