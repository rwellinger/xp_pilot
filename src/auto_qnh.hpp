#pragma once

// auto_qnh — Automatic QNH setting for X-Plane 12.
// Port of "automatic set qnh.lua" by Ingo Alm / Carsten Lynker (2012, updated XP12).
//
// Behaviour:
//   Auto mode: silently syncs pilot+copilot barometer to actual sea-level pressure.
//              Skipped if pilot is on standard pressure (29.92) = flight level.
//   Manual:    XPLMCommandRef "xp_pilot/qnh/set_qnh"        — one-shot set QNH
//              XPLMCommandRef "xp_pilot/qnh/set_flightlevel" — set 29.92
//   Warning:   "CHECK ALTIMETER - QNH not set" drawn on screen when drift > threshold.
//   Copilot mismatch warning: drawn when PF/PM disagree by >0.01 inHg.

namespace AutoQNH
{
void init();
void stop();
void draw();         // called from draw callback — renders warning text if needed
void toggle_auto();  // toggle auto-QNH mode
bool auto_enabled(); // current auto-QNH state
} // namespace AutoQNH
