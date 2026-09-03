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

#include "draw_gate.hpp"
#include "auto_qnh.hpp"
#include "logbook_ui.hpp"
#include "ui_landing_popup.hpp"
#include "ui_overlay.hpp"
#include <XPLM/XPLMDisplay.h>
#include <XPLM/XPLMProcessing.h>

namespace
{

bool s_registered = false;

int DrawCallback(XPLMDrawingPhase, int, void *)
{
    AutoQNH::draw();
    Overlay::draw();
    LogbookUI::draw();
    return 1;
}

bool anything_on_screen()
{
    return LogbookUI::is_open() || LandingPopup::active() || Overlay::visible() || AutoQNH::warning_visible();
}

float GateCB(float, float, int, void *)
{
    DrawGate::refresh();
    return -1.f;
}

} // namespace

void DrawGate::refresh()
{
    const bool wanted = anything_on_screen();
    if (wanted == s_registered)
        return;

    if (wanted)
        XPLMRegisterDrawCallback(DrawCallback, xplm_Phase_Window, 1, nullptr);
    else
        XPLMUnregisterDrawCallback(DrawCallback, xplm_Phase_Window, 1, nullptr);
    s_registered = wanted;
}

void DrawGate::init() { XPLMRegisterFlightLoopCallback(GateCB, -1.f, nullptr); }

void DrawGate::stop()
{
    XPLMUnregisterFlightLoopCallback(GateCB, nullptr);
    if (s_registered)
    {
        XPLMUnregisterDrawCallback(DrawCallback, xplm_Phase_Window, 1, nullptr);
        s_registered = false;
    }
}
